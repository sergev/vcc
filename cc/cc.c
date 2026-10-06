//
// vcc: the C compiler driver.
//
// Turns C source into objects and executables for one target by driving the
// toolchain, one sub-tool per stage:
//
//     vcpp       preprocess      .c   -> .i
//     vparse     parse           .i   -> .ast
//     vlower     lower + opt     .ast -> .tac
//     vgen<T>    code gen        .tac -> .s
//     as         assemble        .s   -> .o     (b6as | clang | msp430-elf-as | mmix-...-as)
//     ld         link            .o   -> a.out  (b6ld | ld.lld | msp430-elf-ld | mmix-...-ld)
//
// The target is chosen with -t (riscv64 by default, like vcpp and vlower; or riscv32,
// aarch64, arm32, x86_64, avr, msp430, mmix, besm6).
// Input files are dispatched by suffix: .c runs the full pipeline, .S is
// preprocessed assembly (cpp -> as), .s is assembled directly, and .o is passed
// straight to the linker, as is a .a archive.
//
// Selection of the last stage to run is controlled by -E (stop after cpp),
// -S (stop after codegen, emit assembly) and -c (stop after as, emit object).
// With none of those, the objects are linked into an executable.  On the BESM-6,
// -Sbemsh and -Smadlen behave like -S but also select the assembly dialect.
//
// THE INSTALLATION IS RELOCATABLE.  Our own passes are looked up in the directory
// vcc itself runs from, and each target's headers and libraries under
// <that>/../share/vcc/<target>, so a copied tree keeps working.  The assembler and
// linker belong to other projects and are found on PATH.  Every sub-tool can be
// overridden with an environment variable (VCC_CPP, VCC_AS, ...); that is how the
// tests run the driver against the build tree.  VCC_AS and VCC_LD may carry
// arguments of their own, e.g. "clang --target=msp430 -c".
//
// Ported from the v7besm project's b6cc (cmd/cc), itself a modern rewrite of the
// Unix v7 cc(1) driver.
//
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef RISCV_CLANG
#define RISCV_CLANG ""
#endif
#ifndef RISCV_LD
#define RISCV_LD ""
#endif
#ifndef MSP430_AS
#define MSP430_AS ""
#endif
#ifndef MSP430_LD
#define MSP430_LD ""
#endif
#ifndef MSP430_LIBGCC
#define MSP430_LIBGCC ""
#endif
#ifndef MMIX_AS
#define MMIX_AS ""
#endif
#ifndef MMIX_LD
#define MMIX_LD ""
#endif
#ifndef MMIX_LIBGCC
#define MMIX_LIBGCC ""
#endif

static char *progname = "vcc"; // diagnostic prefix: basename of argv[0]

//
// A target: its code generator, and how to assemble and link for it.  The
// assembler and linker are given as an environment override, the path found when
// vcc was configured (may be empty), and the bare name to look up on PATH.  The
// targets other than the BESM-6 and the MSP430 are assembled by clang and linked by
// ld.lld with a linker script for qemu `virt` (`microvm` for x86-64, `arduino-mega`
// for AVR); the clang configured for RISC-V serves the other targets too.  The
// MSP430 is assembled and linked by the GNU MSP430 binutils, for mspsim, and its
// link drops the sections nothing reaches (vgenmsp430 gives every function and
// variable one) and ends with GCC's libgcc.a when it was found, so that objects
// compiled by GCC link too.  MMIX is the same with the GNU MMIX binutils, for Knuth's
// mmix: as GCC runs them, the assembler with -x (it expands an out-of-range branch and
// allocates the base registers) and the linker with no script, text from 0x100, its
// output Knuth's .mmo.
//
enum arch { ARCH_BESM6, ARCH_LLVM, ARCH_GNU };

struct target {
    const char *name;
    enum arch arch;
    const char *triple;       // clang --target
    const char *march, *mabi; // extra assembler flags (-march/-mabi, -mcpu/-mfpu), or NULL
    const char *codegen;      // our code generator, next to vcc
    const char *as_default;   // configure-time assembler path, or ""
    const char *as_name;      // assembler on PATH
    const char *ld_default;   // configure-time linker path, or ""
    const char *ld_name;      // linker on PATH
    const char *ld_flag;      // extra linker flag, or NULL
    const char *libgcc;       // configure-time libgcc.a, linked last when present, or NULL
    bool no_script;           // the linker's default script, unless -T names one
};

static const struct target targets[] = {
    { "besm6", ARCH_BESM6, NULL, NULL, NULL, "vgenbesm6", "", "b6as", "", "b6ld" },
    { "riscv64", ARCH_LLVM, "riscv64", "-march=rv64imfd", "-mabi=lp64d", "vgenriscv64", RISCV_CLANG,
      "clang", RISCV_LD, "ld.lld" },
    { "riscv32", ARCH_LLVM, "riscv32", "-march=rv32imfd", "-mabi=ilp32d", "vgenriscv32", RISCV_CLANG,
      "clang", RISCV_LD, "ld.lld" },
    { "aarch64", ARCH_LLVM, "aarch64-none-elf", NULL, NULL, "vgenaarch64", RISCV_CLANG, "clang",
      RISCV_LD, "ld.lld" },
    { "arm32", ARCH_LLVM, "armv7a-none-eabihf", "-mcpu=cortex-a15", "-mfpu=vfpv3-d16", "vgenarm32",
      RISCV_CLANG, "clang", RISCV_LD, "ld.lld" },
    { "x86_64", ARCH_LLVM, "x86_64-none-elf", NULL, NULL, "vgenx86", RISCV_CLANG, "clang", RISCV_LD,
      "ld.lld" },
    { "avr", ARCH_LLVM, "avr", "-mmcu=atmega1280", NULL, "vgenavr", RISCV_CLANG, "clang", RISCV_LD,
      "ld.lld" },
    { "msp430", ARCH_GNU, NULL, "-mcpu=msp430", NULL, "vgenmsp430", MSP430_AS, "msp430-elf-as",
      MSP430_LD, "msp430-elf-ld", "--gc-sections", MSP430_LIBGCC },
    { "mmix", ARCH_GNU, NULL, "-x", "-no-predefined-syms", "vgenmmix", MMIX_AS,
      "mmix-knuth-mmixware-as", MMIX_LD, "mmix-knuth-mmixware-ld",
      "--defsym=__.MMIX.start..text=0x100", MMIX_LIBGCC, true },
};

static const struct target *target = &targets[1]; // riscv64

//
// A growable vector of C strings, used for argument lists and file lists.
// The stored pointers are borrowed unless noted; the vector owns only its
// backing array, freed with vec_free().
//
struct vec {
    char **data;
    size_t len;
    size_t cap;
};

static void vec_push(struct vec *v, char *s)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, v->cap * sizeof(*v->data));
        if (!v->data) {
            fprintf(stderr, "%s: error: out of memory\n", progname);
            exit(1);
        }
    }
    v->data[v->len++] = s;
}

static void vec_free(struct vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}

//
// Parsed command-line state.
//
static bool opt_c;      // -c: compile/assemble only, no link
static bool opt_S;      // -S: compile to assembly only
static bool opt_E;      // -E: preprocess only
static bool opt_g;      // -g: request debug info (a no-op; README.md, "Reserved options")
static bool opt_O;      // -O: request optimization (a no-op; README.md, "Reserved options")
static bool opt_v;         // -v: echo each sub-command before running it
static bool opt_nostdlib;  // -nostdlib: skip the library dir, crt0.o and the implicit -l's
static bool opt_nostdinc;  // -nostdinc: skip the target's standard include dir
static char *outfile;      // -o NAME: explicit output name
static char *linkscript;   // -T FILE: linker script (not besm6), instead of the standard one
static char *codegen_dialect;  // -Sbemsh/-Smadlen: dialect flag for the BESM-6 codegen, or NULL

static struct vec sources;   // input .c/.s files to compile
static struct vec objects;   // .o (and produced) files to link
static struct vec cppflags;  // -D/-I/-U pass-throughs for the preprocessor
static struct vec ldflags;   // -L/-l pass-throughs for the linker
static struct vec tmpfiles;  // temp files to unlink on exit
static struct vec owned;     // heap-allocated file names to free on exit

static char *exe_dir;  // directory vcc runs from (owned), or NULL if unknown
static char *share_dir; // <exe_dir>/../share/vcc/<target> (owned)

static int errflag;  // set nonzero on any failure; becomes the exit status

//
// Take ownership of a heap-allocated string so it is freed at exit.  Every
// generated file name flows through here; the sources/objects/tmpfiles vectors
// only ever borrow these pointers.  Returns its argument for convenient
// chaining, e.g. own(replace_suffix(src, "o")).
//
static char *own(char *s)
{
    if (s)
        vec_push(&owned, s);
    return s;
}

//
// Return a newly allocated (and owned) concatenation of two strings.
//
static char *concat(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(b) + 1;
    char *s = malloc(n);
    if (!s) {
        fprintf(stderr, "%s: error: out of memory\n", progname);
        exit(1);
    }
    snprintf(s, n, "%s%s", a, b);
    return own(s);
}

//
// Report an error, printf-style, prefixed with "vcc: ".  Records the failure
// but does not exit; the driver decides whether to keep going.
//
static void error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%s: error: ", progname);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    errflag = 1;
}

//
// Remove every temp file we created.  Registered with atexit(), so it also
// runs on error paths that call exit().
//
static void cleanup(void)
{
    for (size_t i = 0; i < tmpfiles.len; i++)
        unlink(tmpfiles.data[i]);
    for (size_t i = 0; i < owned.len; i++)
        free(owned.data[i]);
    vec_free(&tmpfiles);
    vec_free(&owned);
}

//
// Return the file-name suffix character after the final '.', or 0 if the name
// has no extension.  E.g. suffix_of("foo/bar.c") == 'c'.
//
static char suffix_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strchr(dot, '/'))
        return 0;
    return dot[1] ? dot[1] : 0;
}

//
// True if `name` ends in ".a": an archive, which goes to the linker as is.
//
static bool is_archive(const char *name)
{
    size_t n = strlen(name);
    return n > 2 && strcmp(name + n - 2, ".a") == 0;
}

//
// Return a newly allocated copy of the base name of `name` (path stripped)
// with its suffix replaced by `.suf`.  Used to derive default output names,
// e.g. replace_suffix("src/foo.c", "o") == "foo.o".
//
static char *replace_suffix(const char *name, const char *suf)
{
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);

    char *out = malloc(stem + strlen(suf) + 2);
    if (!out) {
        error("out of memory");
        exit(1);
    }
    memcpy(out, base, stem);
    out[stem] = '.';
    strcpy(out + stem + 1, suf);
    return out;
}

//
// True if `a` and `b` name the same existing file (same device + inode).  Used
// to avoid clobbering a .S source on a case-insensitive filesystem, where the
// derived .s output resolves to the same file.  Returns false if either path
// cannot be stat()ed -- there is nothing to clobber yet.
//
static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

//
// Create a temporary file whose name ends in `.suf`, register it for cleanup,
// and return its (heap-allocated) path.  Uses mkstemps(3) rather than mkstemp(3)
// because the suffix is the point: every stage of the pipeline is a file whose
// extension says what is in it (and the assembler picks its input language by
// it).  The returned fd is closed immediately since the sub-tool reopens the
// path by name.
//
static char *make_temp(const char *suf)
{
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";

    size_t n = strlen(dir) + strlen(suf) + sizeof("/vccXXXXXX.");
    char *path = malloc(n);
    if (!path) {
        error("out of memory");
        exit(1);
    }
    snprintf(path, n, "%s/vccXXXXXX.%s", dir, suf);

    int fd = mkstemps(path, (int)strlen(suf) + 1);
    if (fd < 0) {
        error("cannot create temporary file: %s", strerror(errno));
        exit(1);
    }
    close(fd);
    vec_push(&tmpfiles, path);
    return own(path);
}

//
// Find the running executable's own path: the OS knows it on macOS and Linux;
// elsewhere fall back to argv[0], searched on PATH when it has no '/'.  Returns
// a pointer into `buf` (PATH_MAX bytes) or to argv0, or NULL.
//
static const char *exe_path(const char *argv0, char *buf)
{
#if defined(__APPLE__)
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath(buf, &size) == 0)
        return buf;
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", buf, PATH_MAX - 1);
    if (n > 0) {
        buf[n] = '\0';
        return buf;
    }
#endif
    if (strchr(argv0, '/'))
        return argv0;

    const char *path = getenv("PATH");
    while (path && *path) {
        const char *colon = strchr(path, ':');
        size_t len = colon ? (size_t)(colon - path) : strlen(path);
        snprintf(buf, PATH_MAX, "%.*s/%s", (int)len, len ? path : ".", argv0);
        if (access(buf, X_OK) == 0)
            return buf;
        path = colon ? colon + 1 : NULL;
    }
    return NULL;
}

//
// Set exe_dir to the directory holding the vcc executable, with symlinks
// resolved: a ~/bin/vcc linked to an installation still finds its passes.
// Left NULL when it cannot be determined; find_pass() reports that.
//
static void locate_self(const char *argv0)
{
    char buf[PATH_MAX], real[PATH_MAX];
    const char *path = exe_path(argv0, buf);

    if (!path || !realpath(path, real))
        return;
    char *slash = strrchr(real, '/');
    if (slash == real)
        slash[1] = '\0'; // "/vcc" lives in "/"
    else if (slash)
        *slash = '\0';
    exe_dir = own(strdup(real));
}

//
// Derive the target's data directory, <prefix>/share/vcc/<target>, where
// <prefix> is the parent of exe_dir.  Unknown exe_dir gives a relative
// "share/vcc/<target>" that will simply not be found.
//
static void locate_share(void)
{
    char prefix[PATH_MAX] = "";

    if (exe_dir) {
        snprintf(prefix, sizeof(prefix), "%s", exe_dir);
        char *slash = strrchr(prefix, '/');
        if (slash)
            *slash = '\0'; // /usr/local/bin -> /usr/local; /bin -> ""
    }
    size_t n = strlen(prefix) + strlen(target->name) + sizeof("/share/vcc/");
    share_dir = malloc(n);
    if (!share_dir) {
        error("out of memory");
        exit(1);
    }
    snprintf(share_dir, n, "%s%sshare/vcc/%s", prefix, exe_dir ? "/" : "", target->name);
    own(share_dir);
}

//
// Locate one of our own passes.  Resolution order:
//   1. the per-tool environment override, if set (e.g. VCC_CPP);
//   2. <exe_dir>/<name>.
// Returns an owned path, or NULL (with an error reported) if not found.
//
static char *find_pass(const char *envvar, const char *name)
{
    const char *override = getenv(envvar);
    if (override && *override)
        return own(strdup(override));

    if (!exe_dir) {
        error("cannot find %s: the directory %s runs from is unknown (set %s)", name,
              progname, envvar);
        return NULL;
    }
    char *path = concat(exe_dir, "/");
    path = concat(path, name);
    if (access(path, X_OK) != 0) {
        error("cannot find %s", path);
        return NULL;
    }
    return path;
}

//
// Locate a tool from another project (assembler, linker) and push it onto the
// argument vector `av`.  Resolution order:
//   1. the environment override, if set (VCC_AS, VCC_LD), split into words at
//      blanks, so that it may carry arguments ("ld.lld -n");
//   2. the path found when vcc was configured, if any;
//   3. the bare name, which run() looks up on PATH.
//
static void push_external(struct vec *av, const char *envvar, const char *configured,
                          const char *name)
{
    const char *override = getenv(envvar);
    if (override && strspn(override, " \t") < strlen(override)) {
        char *words = own(strdup(override));
        for (char *w = strtok(words, " \t"); w; w = strtok(NULL, " \t"))
            vec_push(av, w);
    } else if (configured && *configured) {
        vec_push(av, (char *)configured);
    } else {
        vec_push(av, (char *)name);
    }
}

//
// Spawn `tool` with argument vector `argv` (NULL-terminated) and wait for it.
// A tool name without a '/' is searched on PATH.  Returns the child's exit
// status: 0 on success, nonzero on failure.  With -v, the command line is echoed
// first.
//
static int run(const char *tool, char *const argv[])
{
    if (opt_v) {
        for (char *const *p = argv; *p; p++)
            printf("%s ", *p);
        putchar('\n');
    }
    // stdout is buffered: without the flush the child would inherit a copy of
    // whatever -v had put in the buffer.
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        error("cannot run %s: %s", tool, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execvp(tool, argv);
        // Only reached if the exec failed.  _exit(), not exit(): this is a copy of
        // the parent, and letting it flush the parent's buffers or run the parent's
        // atexit() handlers would unlink the very temp files still in use.
        fprintf(stderr, "%s: error: cannot run %s: %s\n", progname, tool, strerror(errno));
        fflush(stderr);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error("wait failed: %s", strerror(errno));
            return 1;
        }
    }
    if (WIFSIGNALED(status)) {
        error("%s killed by signal %d", tool, WTERMSIG(status));
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        error("%s exited with status %d", tool, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    return 0;
}

//
// Run the preprocessor:
//     vcpp -t <target> -nostdinc [-D__ASSEMBLER__] [cppflags] [-I<share>/include] in out
// vcpp's own compiled-in include directory is switched off and the one beside
// this vcc passed instead, which keeps a relocated installation self-consistent.
// Line markers are kept (vparse understands them and maps diagnostics back to
// the original source; both assemblers take them as comments or line markers).
// Returns 0 on success.
//
static int run_cpp(const char *in, const char *out, bool assembler)
{
    char *tool = find_pass("VCC_CPP", "vcpp");
    if (!tool)
        return 1;

    struct vec av = { 0 };
    vec_push(&av, tool);
    vec_push(&av, "-t");
    vec_push(&av, (char *)target->name);
    vec_push(&av, "-nostdinc");
    if (assembler)
        vec_push(&av, "-D__ASSEMBLER__");
    for (size_t i = 0; i < cppflags.len; i++)
        vec_push(&av, cppflags.data[i]);
    // vcpp wants the search directory glued to the flag: -Ipath, not -I path.
    // The standard directory comes after every user -I.
    if (!opt_nostdinc)
        vec_push(&av, concat(concat("-I", share_dir), "/include"));
    vec_push(&av, (char *)in);
    vec_push(&av, (char *)out);
    vec_push(&av, NULL);

    int rc = run(tool, av.data);
    vec_free(&av);
    return rc;
}

//
// Parse: vparse in out.  Returns 0 on success.
//
static int run_parse(const char *in, const char *out)
{
    char *tool = find_pass("VCC_PARSE", "vparse");
    if (!tool)
        return 1;
    char *av[] = { tool, (char *)in, (char *)out, NULL };
    return run(tool, av);
}

//
// Check and lower for the target: vlower -t <target> in out.  Returns 0 on
// success.
//
static int run_lower(const char *in, const char *out)
{
    char *tool = find_pass("VCC_LOWER", "vlower");
    if (!tool)
        return 1;
    char *av[] = { tool, "-t", (char *)target->name, (char *)in, (char *)out, NULL };
    return run(tool, av);
}

//
// Run the code generator: vgen<target> [--bemsh|--madlen] in out.  The dialect
// flag is selected by -Sbemsh/-Smadlen (BESM-6 only); for a plain -S it is
// omitted so the code generator uses its own default.  Returns 0 on success.
//
static int run_codegen(const char *in, const char *out)
{
    char *tool = find_pass("VCC_GEN", target->codegen);
    if (!tool)
        return 1;
    struct vec av = { 0 };
    vec_push(&av, tool);
    if (codegen_dialect)
        vec_push(&av, codegen_dialect);
    vec_push(&av, (char *)in);
    vec_push(&av, (char *)out);
    vec_push(&av, NULL);
    int rc = run(tool, av.data);
    vec_free(&av);
    return rc;
}

//
// Run the assembler, in -> out:
//     besm6:   b6as -X -o out in
//     riscv64: clang --target=riscv64 -march=rv64imfd -mabi=lp64d -c -o out in
//     riscv32: clang --target=riscv32 -march=rv32imfd -mabi=ilp32d -c -o out in
//     aarch64: clang --target=aarch64-none-elf -c -o out in
//     arm32:   clang --target=armv7a-none-eabihf -mcpu=cortex-a15 -mfpu=vfpv3-d16 -c -o out in
//     x86_64:  clang --target=x86_64-none-elf -c -o out in
//     avr:     clang --target=avr -mmcu=atmega1280 -c -o out in
//     msp430:  msp430-elf-as -mcpu=msp430 -o out in
//     mmix:    mmix-knuth-mmixware-as -x -no-predefined-syms -o out in
// Returns 0 on success.
//
static int run_as(const char *in, const char *out)
{
    struct vec av = { 0 };

    push_external(&av, "VCC_AS", target->as_default, target->as_name);
    switch (target->arch) {
    case ARCH_BESM6:
        vec_push(&av, "-X");
        break;
    case ARCH_LLVM:
        vec_push(&av, concat("--target=", target->triple));
        if (target->march)
            vec_push(&av, (char *)target->march);
        if (target->mabi)
            vec_push(&av, (char *)target->mabi);
        vec_push(&av, "-c");
        break;
    case ARCH_GNU:
        vec_push(&av, (char *)target->march);
        if (target->mabi)
            vec_push(&av, (char *)target->mabi);
        break;
    }
    vec_push(&av, "-o");
    vec_push(&av, (char *)out);
    vec_push(&av, (char *)in);
    vec_push(&av, NULL);
    int rc = run(av.data[0], av.data);
    vec_free(&av);
    return rc;
}

//
// Compile one source file through the pipeline up to the stage selected by the
// -E/-S/-c flags.  A .c file runs the full pipeline; a .S file is preprocessed
// assembly (cpp -> as); a .s file only needs assembling.  A produced object file
// is appended to `objects` so a later link step can pick it up.  Returns 0 on
// success.
//
static int compile_one(const char *src)
{
    char suf = suffix_of(src);

    // A .s file only needs assembling; a .o file is already an object.
    if (suf == 's') {
        if (opt_E || opt_S)
            return 0;
        char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
        int rc = run_as(src, obj);
        vec_push(&objects, obj);
        return rc;
    }

    // A .S file is assembly that must be preprocessed first: cpp -> as.  Both
    // assemblers accept the resulting "# line" markers, so run_cpp's output feeds
    // straight into the assembler.
    if (suf == 'S') {
        // Where the preprocessed assembly goes depends on the stop stage.
        const char *sfile;
        if (opt_E)
            sfile = own(outfile ? strdup(outfile) : replace_suffix(src, "i"));
        else if (opt_S)
            sfile = own(outfile ? strdup(outfile) : replace_suffix(src, "s"));
        else
            sfile = make_temp("s");

        // Guard against overwriting the source on a case-insensitive filesystem,
        // where replace_suffix("foo.S", "s") == "foo.s" names the same file.
        if ((opt_E || opt_S) && same_file(src, sfile)) {
            error("%s: refusing to overwrite input; use -o", src);
            return 1;
        }

        if (run_cpp(src, sfile, true) != 0)
            return 1;
        if (opt_E || opt_S)
            return 0;

        // Assemble the preprocessed output: .s -> .o
        char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
        int rc = run_as(sfile, obj);
        vec_push(&objects, obj);
        return rc;
    }

    if (suf != 'c') {
        error("don't know how to compile %s", src);
        return 1;
    }

    // Preprocess: .c -> .i
    const char *ifile;
    if (opt_E)
        ifile = own(outfile ? strdup(outfile) : replace_suffix(src, "i"));
    else
        ifile = make_temp("i");
    if (run_cpp(src, ifile, false) != 0)
        return 1;
    if (opt_E)
        return 0;

    // Parse and lower: .i -> .ast -> .tac
    const char *astfile = make_temp("ast");
    if (run_parse(ifile, astfile) != 0)
        return 1;
    const char *tacfile = make_temp("tac");
    if (run_lower(astfile, tacfile) != 0)
        return 1;

    // Code generation: .tac -> .s (or .mad/.bemsh for the -Smadlen/-Sbemsh
    // dialects, so the derived name reflects the assembly dialect emitted --
    // the same extensions vgenbesm6 itself picks).
    const char *asmsuf = !codegen_dialect                        ? "s"
                         : strcmp(codegen_dialect, "--madlen") == 0 ? "mad"
                                                                    : "bemsh";
    const char *asmfile =
        opt_S ? own(outfile ? strdup(outfile) : replace_suffix(src, asmsuf)) : make_temp("s");
    if (run_codegen(tacfile, asmfile) != 0)
        return 1;
    if (opt_S)
        return 0;

    // Assemble: .s -> .o
    char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
    int rc = run_as(asmfile, obj);
    vec_push(&objects, obj);
    return rc;
}

//
// Link all collected objects into an executable:
//     besm6:   b6ld -X -e _start -o out -L<lib> <lib>/crt0.o objs ldflags -lc -lruntime
//     msp430:  msp430-elf-ld --gc-sections -T <script> -o out -L<lib> <lib>/crt0.o objs
//              ldflags -lc [libgcc.a]
//     mmix:    mmix-knuth-mmixware-ld --defsym=__.MMIX.start..text=0x100 -o out -L<lib>
//              <lib>/crt0.o objs ldflags -lc [libgcc.a]
//     others: ld.lld -T <script> -o out -L<lib> <lib>/crt0.o objs ldflags -lc
// where <lib> is <share>/lib.  -nostdlib drops the -L, crt0.o and the implicit
// archives; the linker script (the qemu `virt` memory map) stays, unless
// -T names another.  MMIX takes the linker's default script.  A missing crt0.o is a fatal error.  See README.md,
// "Linking".  Returns 0 on success.
//
static int link_objects(void)
{
    char *libdir = concat(share_dir, "/lib");

    struct vec av = { 0 };
    push_external(&av, "VCC_LD", target->ld_default, target->ld_name);
    switch (target->arch) {
    case ARCH_BESM6:
        vec_push(&av, "-X");
        vec_push(&av, "-e");
        vec_push(&av, "_start");
        break;
    case ARCH_LLVM:
    case ARCH_GNU: {
        if (target->ld_flag)
            vec_push(&av, (char *)target->ld_flag);
        if (target->no_script && !linkscript)
            break;
        char *script = linkscript ? linkscript : concat(libdir, "/link.ld");
        if (access(script, R_OK) != 0) {
            error("linker script %s not found; use -T", script);
            vec_free(&av);
            return 1;
        }
        vec_push(&av, "-T");
        vec_push(&av, script);
        break;
    }
    }
    vec_push(&av, "-o");
    vec_push(&av, outfile ? outfile : (char *)"a.out");
    // The standard library dir and the crt0 startup object come before the
    // objects, crt0 first so its _start leads the text; -nostdlib skips both.
    if (!opt_nostdlib) {
        char *crt0 = concat(libdir, "/crt0.o");
        if (access(crt0, R_OK) != 0) {
            if (target->arch == ARCH_BESM6)
                error("%s not found: the BESM-6 crt0.o and libc.a come from v7besm and "
                      "must be installed into %s; or use -nostdlib",
                      crt0, libdir);
            else
                error("%s not found; or use -nostdlib", crt0);
            vec_free(&av);
            return 1;
        }
        vec_push(&av, concat("-L", libdir));
        vec_push(&av, crt0);
    }
    for (size_t i = 0; i < objects.len; i++)
        vec_push(&av, objects.data[i]);
    // User -L/-l flags follow the objects so user-named libraries resolve their
    // references (conventional link order).
    for (size_t i = 0; i < ldflags.len; i++)
        vec_push(&av, ldflags.data[i]);
    // The implicit C library, then (BESM-6) the compiler's helper archive.
    // libruntime.a is LAST, and the order is not cosmetic: b6ld scans an archive
    // once, in order, and libc calls the b$* helpers while no helper calls back
    // into libc.
    // On the MSP430 and MMIX, GCC's libgcc.a follows our libc.a for the helpers only GCC's
    // code calls; without it, objects of our own compiler still link.
    if (!opt_nostdlib) {
        vec_push(&av, "-lc");
        if (target->arch == ARCH_BESM6)
            vec_push(&av, "-lruntime");
        if (target->libgcc && *target->libgcc && access(target->libgcc, R_OK) == 0)
            vec_push(&av, (char *)target->libgcc);
    }
    vec_push(&av, NULL);

    int rc = run(av.data[0], av.data);
    vec_free(&av);
    return rc;
}

//
// Select the target by name; an unknown one is a usage error listing the valid
// names.
//
static void select_target(const char *name)
{
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        if (strcmp(name, targets[i].name) == 0) {
            target = &targets[i];
            return;
        }
    }
    error("unknown target %s", name);
    fprintf(stderr, "Known targets:");
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++)
        fprintf(stderr, " %s", targets[i].name);
    fprintf(stderr, "\n");
    exit(1);
}

static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [options] file...\n", progname);
    printf("Options:\n");
    printf("    -t, --target NAME  Target: riscv64 (default), riscv32, aarch64, arm32, x86_64,\n");
    printf("                       avr, msp430, mmix or besm6\n");
    printf("    -c              Compile and assemble, but do not link\n");
    printf("    -S              Compile only; emit assembly (.s)\n");
    printf("    -Sbemsh         Like -S, but emit Bemsh-dialect assembly (besm6)\n");
    printf("    -Smadlen        Like -S, but emit Madlen-dialect assembly (besm6)\n");
    printf("    -E              Preprocess only; write to output or .i\n");
    printf("    -o file         Set output file name\n");
    printf("    -O              Optimize (reserved; currently a no-op)\n");
    printf("    -g              Emit debug info (reserved; currently a no-op)\n");
    printf("    -v              Verbose: echo each sub-command\n");
    printf("    -Dname[=val]    Predefine a preprocessor macro\n");
    printf("    -Uname          Undefine a preprocessor macro\n");
    printf("    -Ipath          Add a header search directory\n");
    printf("    -Lpath          Add a library search directory (for the linker)\n");
    printf("    -lname          Link against library libname (for the linker)\n");
    printf("    -T file         Linker script instead of the standard one (not besm6)\n");
    printf("    -nostdlib       Do not use the standard library dir, crt0.o or the implicit libraries\n");
    printf("    -nostdinc       Do not add the standard include directory\n");
    printf("Inputs are dispatched by suffix: .c (compile), "
           ".S (preprocess + assemble), .s (assemble), .o and .a (link).\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    // Derive the diagnostic prefix from argv[0]'s basename (fallback "vcc").
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    atexit(cleanup);

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            vec_push(&sources, arg);
            continue;
        }
        // Multi-character options that the single-letter switch would misread.
        if (strcmp(arg, "-nostdlib") == 0) {
            opt_nostdlib = true;
            continue;
        }
        if (strcmp(arg, "-nostdinc") == 0) {
            opt_nostdinc = true;
            continue;
        }
        if (strncmp(arg, "--target", 8) == 0) {
            if (arg[8] == '=') {
                select_target(arg + 9);
            } else if (arg[8] == '\0' && i + 1 < argc) {
                select_target(argv[++i]);
            } else {
                error("unknown option %s", arg);
                usage();
            }
            continue;
        }
        switch (arg[1]) {
        case 'c':
            opt_c = true;
            break;
        case 'S':
            opt_S = true;
            if (arg[2] == '\0')
                break; // plain -S: codegen default dialect
            if (strcmp(arg + 2, "bemsh") == 0)
                codegen_dialect = "--bemsh";
            else if (strcmp(arg + 2, "madlen") == 0)
                codegen_dialect = "--madlen";
            else {
                error("unknown option %s", arg);
                usage();
            }
            break;
        case 'E':
            opt_E = true;
            break;
        case 'O':
            opt_O = true;
            break;
        case 'g':
            opt_g = true;
            break;
        case 'v':
            opt_v = true;
            break;
        case 'o':
        case 't':
        case 'T':
            // Options with a value, glued (-ofile) or separate (-o file).
            if (arg[2] == '\0' && i + 1 >= argc) {
                error("%s requires an argument", arg);
                usage();
            }
            {
                char *val = arg[2] ? arg + 2 : argv[++i];
                if (arg[1] == 'o')
                    outfile = val;
                else if (arg[1] == 't')
                    select_target(val);
                else
                    linkscript = val;
            }
            break;
        case 'D':
        case 'U':
        case 'I':
            // Pass through to the preprocessor.  vcpp requires the value glued
            // to the flag (-DNAME), so fold the separated form (-D NAME) into
            // one token.
            if (arg[2]) {
                vec_push(&cppflags, arg);
            } else if (i + 1 < argc) {
                const char flag[3] = { '-', arg[1], '\0' };
                vec_push(&cppflags, concat(flag, argv[++i]));
            } else {
                error("%s requires an argument", arg);
                usage();
            }
            break;
        case 'L':
        case 'l':
            // Pass through to the linker.  Fold the separated form (-L dir) into
            // one glued token, matching the -D/-I handling above.
            if (arg[2]) {
                vec_push(&ldflags, arg);
            } else if (i + 1 < argc) {
                const char flag[3] = { '-', arg[1], '\0' };
                vec_push(&ldflags, concat(flag, argv[++i]));
            } else {
                error("%s requires an argument", arg);
                usage();
            }
            break;
        default:
            error("unknown option %s", arg);
            usage();
        }
    }

    if (sources.len == 0) {
        error("no input files");
        usage();
    }
    if ((opt_E || opt_S || opt_c) && outfile && sources.len > 1) {
        error("cannot specify -o with -c, -S or -E and multiple input files");
        return 1;
    }
    if (codegen_dialect && target->arch != ARCH_BESM6) {
        error("-S%s needs -t besm6", codegen_dialect + 2);
        return 1;
    }
    if (linkscript && target->arch == ARCH_BESM6) {
        error("-T is not supported for besm6");
        return 1;
    }

    locate_self(argv[0]);
    locate_share();

    // Compile each source; a failure on one file skips the link step but does
    // not abort the remaining compilations.
    bool compiled_ok = true;
    for (size_t i = 0; i < sources.len; i++) {
        if (suffix_of(sources.data[i]) == 'o' || is_archive(sources.data[i])) {
            vec_push(&objects, sources.data[i]);
            continue;
        }
        if (compile_one(sources.data[i]) != 0)
            compiled_ok = false;
    }

    // Link unless we stopped early or a compile failed.
    if (compiled_ok && !opt_c && !opt_S && !opt_E && objects.len > 0)
        link_objects();

    vec_free(&sources);
    vec_free(&objects);
    vec_free(&cppflags);
    vec_free(&ldflags);
    return errflag ? 1 : 0;
}
