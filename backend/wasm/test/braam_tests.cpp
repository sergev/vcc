//
// wasm32-braam (backend/wasm/Plan.md §7): programs built by the driver, `vcc -t
// wasm32-braam` with the in-tree passes, against the runtime staged in
// build/share/vcc/wasm32-braam, and run under node by its fake kernel, run.mjs, which
// checks the process ABI before it starts one.
//
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "test_tools.h"

namespace {

class BraamTest : public ::testing::Test {
protected:
    int status = -1;  // of the last run
    std::string log;  // its stderr

    static bool Available()
    {
        // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
        return WASM32_TOOLS_FOUND && tool_available(WASM32_NODE) &&
               std::ifstream(BRAAM_RUNNER).good();
    }

    void SetUp() override
    {
        if (!Available())
            GTEST_SKIP() << "clang/wasm-ld/node or the Braam runtime not found";
        setenv("VCC_CPP", VCC_CPP_PATH, 1);
        setenv("VCC_PARSE", VCC_PARSE_PATH, 1);
        setenv("VCC_LOWER", VCC_LOWER_PATH, 1);
        setenv("VCC_GEN", VCC_GENWASM_PATH, 1);
    }

    static std::string Scratch(const std::string &suffix)
    {
        const char *name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        return std::string(TEST_DIR "/braam-") + name + suffix;
    }

    // Build `src` into a process, with more driver options; "" on success, else the
    // driver's messages.
    std::string Build(const std::string &src, const std::vector<std::string> &options = {})
    {
        std::string c = Scratch(".c"), exe = Scratch(".wasm"), err = Scratch(".build");
        std::ofstream(c) << src;
        std::vector<std::string> argv = { VCC_COMMAND, "-t", "wasm32-braam" };
        argv.insert(argv.end(), options.begin(), options.end());
        argv.insert(argv.end(), { "-o", exe, c });
        if (RunTool(argv, err) != 0)
            return ReadFile(err).empty() ? "the driver failed" : ReadFile(err);
        return "";
    }

    // Run the last process built, with `args` and standard input `in`; its stdout.
    std::string Run(const std::vector<std::string> &args = {}, const std::string &in = "")
    {
        std::string in_path = Scratch(".in"), out = Scratch(".out"), err = Scratch(".err");
        std::ofstream(in_path) << in;
        std::vector<std::string> argv = { WASM32_NODE, BRAAM_RUNNER, Scratch(".wasm") };
        argv.insert(argv.end(), args.begin(), args.end());
        status = RunWithTimeout(argv, out, err, 10, "", in_path);
        log    = ReadFile(err);
        return ReadFile(out);
    }

    // Build and run; a build failure fails the test.
    std::string BuildAndRun(const std::string &src, const std::vector<std::string> &args = {},
                            const std::string &in = "")
    {
        std::string err = Build(src);
        EXPECT_EQ("", err);
        return err.empty() ? Run(args, in) : "ERROR";
    }
};

} // namespace

// printf into stdout's buffer, flushed by the runtime when main returns; argv from the
// host's blob; main's result is the exit status.
TEST_F(BraamTest, Hello)
{
    EXPECT_EQ("hello from braam-Hello.wasm: 3 args\n  one\n  two words\n",
              BuildAndRun(R"(
#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    printf("hello from %s: %d args\n", argv[0], argc);
    for (int i = 1; i < argc; i++)
        printf("  %s\n", argv[i]);
    return 0;
}
)",
                          { "one", "two words" }));
    EXPECT_EQ(0, status);
    EXPECT_NE(std::string::npos, log.find("[exit 0]")) << log;
}

// A status other than 0.
TEST_F(BraamTest, Status)
{
    BuildAndRun("#include <braam.h>\ncoro(braam_call *) int main(int c, char **v) { return 42; }");
    EXPECT_EQ(42, status);
    EXPECT_NE(std::string::npos, log.find("[exit 42]")) << log;
}

static const char *const cat_src = R"(
#include <unistd.h>
coro(braam_call *) int main(int argc, char **argv)
{
    char buf[512];
    for (;;) {
        ssize_t n = await read(0, buf, sizeof buf);
        if (n <= 0)
            return n < 0;
        if (await write(1, buf, n) != n)
            return 1;
    }
}
)";

// cat: standard input to standard output, a read and a write per chunk, as braam-apps'
// hand-written cat.s does.
TEST_F(BraamTest, Cat)
{
    std::string in;
    for (int i = 1; i <= 3000; i++)
        in += std::to_string(i) + (i % 7 ? " " : "\n");
    EXPECT_EQ(in, BuildAndRun(cat_src, {}, in));
    EXPECT_EQ(0, status);
    EXPECT_EQ("", BuildAndRun(cat_src, {}, ""));
    EXPECT_EQ(0, status);
}

static const char *const wc_src = R"(
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

coro(braam_call *) int count(int fd, long *lines, long *words, long *chars)
{
    char buf[100];
    int in_word = 0;
    ssize_t n;
    while ((n = await read(fd, buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            ++*chars;
            if (c == '\n')
                ++*lines;
            if (c == ' ' || c == '\n' || c == '\t')
                in_word = 0;
            else if (!in_word) {
                in_word = 1;
                ++*words;
            }
        }
    return n < 0 ? -1 : 0;
}

coro(braam_call *) int main(int argc, char **argv)
{
    int fd = argc > 1 ? await open(argv[1], O_RDONLY) : 0;
    if (fd < 0) {
        fprintf(stderr, "wc: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    long l = 0, w = 0, c = 0;
    if (await count(fd, &l, &w, &c) < 0)
        return 1;
    printf("%ld %ld %ld\n", l, w, c);
    if (fd)
        await close(fd);
    return 0;
}
)";

// wc: a coroutine of the program's own awaited, on stdin and on a file opened by name;
// a file that is not there is an error with errno set.
TEST_F(BraamTest, Wc)
{
    std::string src = std::string("#include <errno.h>\n#include <string.h>\n") + wc_src;
    std::string text = "one two  three\nfour\n\tfive six\n";
    EXPECT_EQ("3 6 30\n", BuildAndRun(src, {}, text));
    EXPECT_EQ(0, status);
    std::string file = Scratch(".txt");
    std::ofstream(file) << text << text;
    EXPECT_EQ("6 12 60\n", Run({ file }));
    EXPECT_EQ(0, status);
    EXPECT_EQ("", Run({ Scratch(".none") }));
    EXPECT_EQ(1, status);
    EXPECT_NE(std::string::npos, log.find("Not found")) << log;
}

// A sleep parks the process: the output written before it is out first.
TEST_F(BraamTest, Sleep)
{
    EXPECT_EQ("before\nafter 1\n", BuildAndRun(R"(
#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    unsigned t0 = braam_now();
    printf("before\n");
    await fflush(stdout);
    await sleep_ms(100);
    printf("after %d\n", braam_now() - t0 >= 90);
    return 0;
}
)"));
    EXPECT_EQ(0, status);
}

// exit() tells the kernel the status and traps: nothing unwinds, so what was buffered is
// lost, and the kernel reports a crash.
TEST_F(BraamTest, Exit)
{
    EXPECT_EQ("", BuildAndRun(R"(
#include <stdio.h>
#include <stdlib.h>
coro(braam_call *) int main(int argc, char **argv)
{
    printf("lost\n");
    exit(7);
}
)"));
    EXPECT_EQ(255, status);
    EXPECT_NE(std::string::npos, log.find("Sys::Exit said 7")) << log;
}

// The bytes of a little-endian u32 at `p`.
static unsigned U32(const std::string &s, size_t p)
{
    return (unsigned char)s[p] | (unsigned char)s[p + 1] << 8 | (unsigned char)s[p + 2] << 16 |
           (unsigned)(unsigned char)s[p + 3] << 24;
}

// The braam section: one, magic, PROC_ABI, flags, the initial pages of the link (the
// driver's --initial-pages) and 1600.  run.mjs checks the imports and exports too.
TEST_F(BraamTest, Section)
{
    const char *src = "#include <braam.h>\ncoro(braam_call *) int main(int c, char **v) { return 0; }";
    for (unsigned pages : { 4u, 9u }) {
        std::vector<std::string> options;
        if (pages != 4)
            options.push_back("--initial-pages=" + std::to_string(pages));
        ASSERT_EQ("", Build(src, options));
        std::ifstream f(Scratch(".wasm"), std::ios::binary);
        std::string bin((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t at = bin.find(std::string("\x05" "braam", 6));
        ASSERT_NE(std::string::npos, at);
        EXPECT_EQ(std::string::npos, bin.find(std::string("\x05" "braam", 6), at + 1));
        EXPECT_EQ(0, bin[at - 2]); // a custom section
        EXPECT_EQ(26, bin[at - 1]);
        EXPECT_EQ(0x6d617262u, U32(bin, at + 6));
        EXPECT_EQ(21u, U32(bin, at + 10));
        EXPECT_EQ(0u, U32(bin, at + 14));
        EXPECT_EQ(pages, U32(bin, at + 18));
        EXPECT_EQ(1600u, U32(bin, at + 22));
        Run();
        EXPECT_EQ(0, status) << log;
    }
}

// main is a coroutine on Braam: a plain one is a compile-time error.
TEST_F(BraamTest, PlainMain)
{
    std::string err = Build("int main(void) { return 0; }");
    EXPECT_NE(std::string::npos, err.find("on Braam, main is coro(braam_call *)")) << err;
}

// The allocator gives freed blocks back: many rounds of blocks of mixed sizes, freed in
// a mixed order and checked, leave the memory as large as one round did.
TEST_F(BraamTest, Malloc)
{
    EXPECT_EQ("ok 1\n", BuildAndRun(R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long __vcc_memory_size(void);
static int round_trip(void)
{
    char *p[64];
    for (int i = 0; i < 64; i++) {
        size_t n = 1 + (i * 37) % 3000;
        p[i]     = malloc(n);
        if (!p[i])
            return 0;
        memset(p[i], i, n);
    }
    for (int k = 0; k < 2; k++)
        for (int i = k; i < 64; i += 2) {
            size_t n = 1 + (i * 37) % 3000;
            for (size_t j = 0; j < n; j++)
                if (p[i][j] != (char)i)
                    return 0;
            free(p[i]);
        }
    return 1;
}
coro(braam_call *) int main(int argc, char **argv)
{
    if (!round_trip())
        return 1;
    unsigned long pages = __vcc_memory_size();
    for (int r = 0; r < 200; r++)
        if (!round_trip())
            return 2;
    char *big = realloc(malloc(10), 100000);
    if (!big)
        return 3;
    free(big);
    printf("ok %d\n", __vcc_memory_size() <= pages + 2);
    return 0;
}
)"));
    EXPECT_EQ(0, status);
}

// A read of standard input flushes standard output first: the prompt is out before the
// answer is read, so its write comes first.
TEST_F(BraamTest, PromptFlush)
{
    EXPECT_EQ("name? hello, ada\n", BuildAndRun(R"(
#include <stdio.h>
#include <unistd.h>
coro(braam_call *) int main(int argc, char **argv)
{
    char name[32];
    printf("name? ");
    ssize_t n = await read(0, name, sizeof name - 1);
    if (n > 0 && name[n - 1] == '\n')
        n--;
    name[n < 0 ? 0 : n] = 0;
    printf("hello, %s\n", name);
    return 0;
}
)",
                                       {}, "ada\n"));
    EXPECT_EQ(0, status);
}

// The root's block is 64 KiB unless the program defines __braam_task_bytes: a recursion
// whose frames do not fit in it traps (CO_TRAP_NO_SPACE goes through exit), and fits in
// a bigger one.
static const char *const deep_src = R"(
#include <stdio.h>
coro(braam_call *) int depth(int n)
{
    char pad[400];
    pad[n % 400] = (char)n;
    return n == 0 ? pad[0] : await depth(n - 1) + 1;
}
coro(braam_call *) int main(int argc, char **argv)
{
    printf("%d\n", await depth(300));
    return 0;
}
)";

TEST_F(BraamTest, TaskBytes)
{
    EXPECT_EQ("", BuildAndRun(deep_src));
    EXPECT_EQ(255, status);
    EXPECT_NE(std::string::npos, log.find("Sys::Exit said 255")) << log;
    EXPECT_EQ("300\n", BuildAndRun(std::string("const unsigned __braam_task_bytes = 256 * 1024;\n") +
                                   deep_src));
    EXPECT_EQ(0, status);
}

// The streams on files, and the file and directory calls (phase C7), inside a directory
// named by argv[1], so tests running side by side do not meet: fopen and fprintf,
// stat, fgets to the end, rewind and ungetc, fseek from the end, ftell, fread, fstat,
// append, mkdir, rename, rmdir refused on a full directory, chdir and getcwd, unlink.
TEST_F(BraamTest, Files)
{
    std::string dir = "braam-Files.d";
    std::string cmd = "rm -rf " + dir;
    ASSERT_EQ(0, system(cmd.c_str()));
    EXPECT_EQ("size 19 reg 1 dir 0\n"
              "1: one\n2: two\n3: three\n4: four\n"
              "eof 1 err 0\n"
              "first o again o\n"
              "tell 13\n"
              "read 5 '\nfour'\n"
              "fstat size 19\n"
              "appended 24\n"
              "old -1 new 0\n"
              "rmdir full -1 Not empty\n"
              "cwd ends /d\n"
              "unlink 0 rmdir 0 rmdir 0\n",
              BuildAndRun(R"(
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

coro(braam_call *) int main(int argc, char **argv)
{
    struct stat st;
    char line[64], cwd[256];
    if (await mkdir(argv[1], 0755) < 0 || await chdir(argv[1]) < 0)
        return 1;
    FILE *f = await fopen("t.txt", "w");
    if (!f)
        return 2;
    fprintf(f, "one\ntwo\nthree\n");
    fwrite("four\n", 1, 5, f);
    if (await fclose(f) != 0 || await stat("t.txt", &st) < 0)
        return 3;
    printf("size %d reg %d dir %d\n", (int)st.st_size, S_ISREG(st.st_mode), S_ISDIR(st.st_mode));
    f = await fopen("t.txt", "r");
    int n = 0;
    while (await fgets(line, sizeof line, f))
        printf("%d: %s", ++n, line);
    printf("eof %d err %d\n", feof(f) != 0, ferror(f) != 0);
    await rewind(f);
    int c = await fgetc(f);
    ungetc(c, f);
    printf("first %c again %c\n", c, await getc(f));
    await fseek(f, -6, SEEK_END);
    printf("tell %ld\n", await ftell(f));
    char buf[8] = { 0 };
    printf("read %d '%s'\n", (int)await fread(buf, 1, 5, f), buf);
    if (await fstat(fileno(f), &st) == 0)
        printf("fstat size %d\n", (int)st.st_size);
    await fclose(f);
    f = await fopen("t.txt", "a");
    fputs("five\n", f);
    await fclose(f);
    await stat("t.txt", &st);
    printf("appended %d\n", (int)st.st_size);
    await mkdir("d", 0755);
    await rename("t.txt", "d/u.txt");
    int old = await stat("t.txt", &st);
    printf("old %d new %d\n", old, await stat("d/u.txt", &st));
    int full = await rmdir("d");
    printf("rmdir full %d %s\n", full, strerror(errno));
    await chdir("d");
    if (await getcwd(cwd, sizeof cwd))
        printf("cwd ends %s\n", strrchr(cwd, '/'));
    int u = await unlink("u.txt");
    await chdir("..");
    int r = await rmdir("d");
    await chdir("..");
    printf("unlink %d rmdir %d rmdir %d\n", u, r, await rmdir(argv[1]));
    return 0;
}
)",
                          { dir }));
    EXPECT_EQ(0, status) << log;
}

// Errors: a missing file, a bad mode, perror on stderr, stat of nothing; stdio's own
// stdout is unaffected.
TEST_F(BraamTest, FileErrors)
{
    EXPECT_EQ("fopen 1 Not found\nmode 1 22\nstat -1 35\nfstat -1\n", BuildAndRun(R"(
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
coro(braam_call *) int main(int argc, char **argv)
{
    struct stat st;
    FILE *f = await fopen("braam-FileErrors.none", "r");
    printf("fopen %d %s\n", f == NULL, strerror(errno));
    perror("braam-FileErrors.none");
    f = await fopen("x", "q");
    printf("mode %d %d\n", f == NULL, errno == EINVAL ? 22 : errno);
    int s = await stat("braam-FileErrors.none", &st);
    printf("stat %d %d\n", s, errno);
    printf("fstat %d\n", await fstat(1, &st));
    return 0;
}
)"));
    EXPECT_EQ(0, status);
    EXPECT_NE(std::string::npos, log.find("braam-FileErrors.none: Not found\n")) << log;
}

// Standard input through the stream: fgets with a line longer than its buffer, getchar
// to the end across chunk boundaries (the input is several chunks), and the prompt out
// before the first read.
TEST_F(BraamTest, StdinStream)
{
    std::string in = "a long first line\n";
    std::string rest;
    for (int i = 0; i < 400; i++)
        rest += std::to_string(i % 10) + (i % 50 == 49 ? "\n" : "");
    in += rest;
    std::string expect = "? [a long f] [irst lin] [e\n] \n";
    for (char c : rest)
        expect += c == '\n' ? '|' : c;
    expect += "\n" + std::to_string(rest.size()) + " eof\n";
    EXPECT_EQ(expect, BuildAndRun(R"(
#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    char s[9];
    printf("? ");
    for (int i = 0; i < 3 && await fgets(s, sizeof s, stdin); i++)
        printf("[%s] ", s);
    printf("\n");
    int c, n = 0;
    while ((c = await getchar()) != EOF) {
        putchar(c == '\n' ? '|' : c);
        n++;
    }
    printf("\n%d %s\n", n, feof(stdin) ? "eof" : "?");
    return 0;
}
)",
                                  {}, in));
    EXPECT_EQ(0, status);
}

// fread of a file bigger than a chunk, and fwrite of it back, against a copy made
// byte by byte with fgetc and fputc.
TEST_F(BraamTest, CopyFile)
{
    std::string src = "braam-CopyFile.data";
    std::string data;
    for (int i = 0; i < 5000; i++)
        data += (char)('a' + i % 23);
    std::ofstream(src) << data;
    EXPECT_EQ("5000 5000 same\n", BuildAndRun(R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
coro(braam_call *) int main(int argc, char **argv)
{
    FILE *in = await fopen(argv[1], "rb");
    char *a = malloc(8000), *b = malloc(8000);
    size_t n = await fread(a, 1, 8000, in);
    await rewind(in);
    size_t m = 0;
    int c;
    while ((c = await fgetc(in)) != EOF)
        b[m++] = (char)c;
    await fclose(in);
    FILE *out = await fopen("braam-CopyFile.copy", "w");
    fwrite(a, 1, n, out);
    await fclose(out);
    printf("%d %d %s\n", (int)n, (int)m, n == m && !memcmp(a, b, n) ? "same" : "differ");
    return await remove(argv[1]);
}
)",
                                          { src }));
    EXPECT_EQ(0, status);
    EXPECT_EQ(data, ReadFile("braam-CopyFile.copy"));
}
