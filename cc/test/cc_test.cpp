//
// End-to-end tests for the vcc compiler driver.
//
// Each case writes a small source into a throwaway temp directory and runs the
// built driver on it.  The fixture points the driver at the in-tree passes
// through the VCC_* environment overrides (the build tree has no share/vcc, so
// the cases also pass -nostdinc -I... and, when linking, -nostdlib with the
// build's crt0.o and libc.a).  The StagedPrefix cases instead lay out a small
// installation -- bin/ with a copy of the driver and links to the passes,
// share/vcc/<target>/{include,lib} -- and run it with no overrides at all, which
// is what tests the lookup relative to the driver's own directory.
//
// Cases that assemble, link or run need the external tools (clang, ld.lld and
// qemu-system-riscv64; b6as for the BESM-6) and skip without them.
//
#include <gtest/gtest.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char **environ;

namespace fs = std::filesystem;

namespace {

// Read an entire file into a string ("" if it does not exist).
std::string ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// True if `name` is an executable on PATH (or, with a '/', at that path).
bool HaveTool(const std::string &name)
{
    if (name.empty())
        return false;
    if (name.find('/') != std::string::npos)
        return access(name.c_str(), X_OK) == 0;
    const char *path = getenv("PATH");
    std::stringstream dirs(path ? path : "");
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (access((dir + "/" + name).c_str(), X_OK) == 0)
            return true;
    }
    return false;
}

// The RISC-V toolchain and the build's runtime are all present.
bool HaveRiscvLink()
{
    return RISCV_TOOLS_FOUND && HaveTool(RISCV_CLANG) && HaveTool(RISCV_LD) &&
           access((std::string(RISCV_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

bool HaveRiscvRun()
{
    return HaveRiscvLink() && HaveTool(RISCV_QEMU);
}

// The same clang and ld.lld, with an AArch64 target, the AArch64 runtime and qemu.
bool HaveAarch64Run()
{
    return AARCH64_TOOLS_FOUND && HaveTool(RISCV_CLANG) && HaveTool(RISCV_LD) &&
           HaveTool(AARCH64_QEMU) &&
           access((std::string(AARCH64_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// Run argv and return its exit code; -1 on spawn failure or a signal, -2 on a
// timeout.  The child's stdout goes to `stdout_file` and its stderr to
// `stderr_file` when they are given (which is how the -v echo is captured).
int RunProcess(const std::vector<std::string> &argv, const std::string &stdout_file = {},
               const std::string &stderr_file = {}, int timeout_sec = 20)
{
    std::vector<char *> cargv;
    for (const auto &a : argv)
        // cppcheck-suppress useStlAlgorithm
        cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return -1;
    if (!stdout_file.empty())
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, stdout_file.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (!stderr_file.empty())
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, stderr_file.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600);

    pid_t pid;
    int spawned = posix_spawn(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
        return -1;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    int status;
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid)
            break;
        if (w < 0)
            return -1;
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

const char *const kOverrides[] = { "VCC_CPP", "VCC_PARSE", "VCC_LOWER", "VCC_GEN",
                                   "VCC_AS",  "VCC_LD" };

class CcDriver : public ::testing::Test {
protected:
    std::string dir;

    void SetUp() override
    {
        char tmpl[] = "/tmp/cc_testXXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr) << "mkdtemp failed";
        // Canonical, as the driver resolves its own path (/tmp is a symlink on
        // macOS).
        dir = fs::canonical(tmpl).string();

        // Pin our passes to the freshly built ones.  The code generator is set
        // per target by Vcc().
        ASSERT_EQ(setenv("VCC_CPP", VCC_CPP_PATH, 1), 0);
        ASSERT_EQ(setenv("VCC_PARSE", VCC_PARSE_PATH, 1), 0);
        ASSERT_EQ(setenv("VCC_LOWER", VCC_LOWER_PATH, 1), 0);
        unsetenv("VCC_AS");
        unsetenv("VCC_LD");
    }

    void TearDown() override
    {
        for (const char *name : kOverrides)
            unsetenv(name);
        if (!dir.empty()) {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    }

    void WriteSource(const std::string &name, const std::string &content)
    {
        std::ofstream out(dir + "/" + name, std::ios::binary | std::ios::trunc);
        out << content;
    }

    std::string Path(const std::string &name) const { return dir + "/" + name; }

    // Run the driver from `dir` with `args`, for the target named by a "-t"
    // among them (riscv64 otherwise), with the in-tree headers on the search
    // path.  stdout lands in out.log, stderr in err.log.
    int Vcc(std::vector<std::string> args, bool std_headers = true)
    {
        bool besm6 = false, aarch64 = false;
        for (size_t i = 0; i + 1 < args.size(); i++) {
            if (args[i] == "-t" && args[i + 1] == "besm6")
                besm6 = true;
            if (args[i] == "-t" && args[i + 1] == "aarch64")
                aarch64 = true;
        }
        setenv("VCC_GEN", besm6 ? VCC_GENBESM_PATH : aarch64 ? VCC_GENAARCH64_PATH : VCC_GENRISCV_PATH,
               1);

        std::vector<std::string> argv = { VCC_COMMAND };
        if (std_headers) {
            const char *inc = besm6 ? BESM6_INCLUDE_DIR : aarch64 ? AARCH64_INCLUDE_DIR : RISCV_INCLUDE_DIR;
            argv.insert(argv.end(), { "-nostdinc", std::string("-I") + inc });
            if (!besm6)
                argv.push_back(std::string("-I") + LP64_INCLUDE_DIR);
            argv.push_back(std::string("-I") + COMMON_INCLUDE_DIR);
        }
        argv.insert(argv.end(), args.begin(), args.end());

        // The driver derives output names relative to the current directory.
        std::string cwd = fs::current_path();
        fs::current_path(dir);
        int rc = RunProcess(argv, Path("out.log"), Path("err.log"));
        fs::current_path(cwd);
        return rc;
    }

    std::string Stdout() const { return ReadFile(Path("out.log")); }
    std::string Stderr() const { return ReadFile(Path("err.log")); }

    // Lay out a miniature installation under dir/prefix: bin/vcc is a copy of
    // the driver (a symlink would resolve back into the build tree), bin/v* link
    // to the in-tree passes, and share/vcc/<target>/include gathers the
    // target's headers.  The caller adds share/vcc/<target>/lib.  Clears the
    // overrides so nothing but the layout can steer the driver.
    std::string StagePrefix(const std::string &target)
    {
        std::string prefix = Path("prefix");
        fs::create_directories(prefix + "/bin");
        fs::copy_file(VCC_COMMAND, prefix + "/bin/vcc");
        fs::create_symlink(VCC_CPP_PATH, prefix + "/bin/vcpp");
        fs::create_symlink(VCC_PARSE_PATH, prefix + "/bin/vparse");
        fs::create_symlink(VCC_LOWER_PATH, prefix + "/bin/vlower");
        fs::create_symlink(VCC_GENBESM_PATH, prefix + "/bin/vgenbesm6");
        fs::create_symlink(VCC_GENRISCV_PATH, prefix + "/bin/vgenriscv64");
        fs::create_symlink(VCC_GENRISCV_PATH, prefix + "/bin/vgenriscv32");
        fs::create_symlink(VCC_GENAARCH64_PATH, prefix + "/bin/vgenaarch64");

        std::string share = prefix + "/share/vcc/" + target;
        fs::create_directories(share + "/include");
        fs::create_directories(share + "/lib");
        const char *target_inc = target == "besm6"     ? BESM6_INCLUDE_DIR
                                 : target == "riscv32" ? RISCV32_INCLUDE_DIR
                                 : target == "aarch64" ? AARCH64_INCLUDE_DIR
                                                       : RISCV_INCLUDE_DIR;
        const char *model_inc = target == "riscv64" || target == "aarch64" ? LP64_INCLUDE_DIR
                                : target == "riscv32"                         ? ILP32_INCLUDE_DIR
                                                                              : target_inc;
        for (const char *inc : { target_inc, model_inc, COMMON_INCLUDE_DIR }) {
            for (const auto &entry : fs::directory_iterator(inc)) {
                fs::path to = share + "/include/" + entry.path().filename().string();
                if (!fs::exists(to))
                    fs::create_symlink(entry.path(), to);
            }
        }
        for (const char *name : kOverrides)
            unsetenv(name);
        return prefix;
    }

    // Run the staged driver from `dir`; stdout/stderr as for Vcc().
    int StagedVcc(const std::string &prefix, const std::vector<std::string> &args)
    {
        std::vector<std::string> argv = { prefix + "/bin/vcc" };
        argv.insert(argv.end(), args.begin(), args.end());
        std::string cwd = fs::current_path();
        fs::current_path(dir);
        int rc = RunProcess(argv, Path("out.log"), Path("err.log"));
        fs::current_path(cwd);
        return rc;
    }

    // Run a linked RISC-V ELF under qemu `virt`; returns its UART output.
    std::string RunQemu(const std::string &elf, const char *qemu = RISCV_QEMU)
    {
        std::string out = Path("qemu.out");
        int rc = RunProcess({ qemu, "-M", "virt", "-bios", "none", "-display", "none",
                              "-serial", "stdio", "-monitor", "none", "-kernel", elf },
                            out, Path("qemu.err"), 10);
        EXPECT_GE(rc, 0) << "qemu failed:\n" << ReadFile(Path("qemu.err"));
        return ReadFile(out);
    }

    // Run a linked AArch64 ELF under qemu `virt`, main's result through semihosting;
    // returns its UART output, the exit status in *status.
    std::string RunQemuAarch64(const std::string &elf, int *status)
    {
        std::string out = Path("qemu.out");
        *status = RunProcess({ AARCH64_QEMU, "-M", "virt", "-cpu", "cortex-a57", "-display", "none",
                               "-serial", "stdio", "-monitor", "none", "-semihosting", "-kernel", elf },
                             out, Path("qemu.err"), 10);
        return ReadFile(out);
    }
};

const char kHello[] = "#include <stdio.h>\n"
                      "int main(void)\n"
                      "{\n"
                      "    printf(\"hello %d\\n\", 42);\n"
                      "    return 0;\n"
                      "}\n";

const char kTargetProbe[] = "#ifdef __riscv\n"
                            "RISCV __riscv_xlen\n"
                            "#endif\n"
                            "#ifdef besm6\n"
                            "BESM6\n"
                            "#endif\n";

//
// Preprocessing and target selection.
//
TEST_F(CcDriver, PreprocessDefaultTargetIsRiscv64)
{
    WriteSource("t.c", kTargetProbe);
    ASSERT_EQ(Vcc({ "-E", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.i"));
    EXPECT_NE(text.find("RISCV 64"), std::string::npos) << text;
    EXPECT_EQ(text.find("BESM6"), std::string::npos) << text;
}

TEST_F(CcDriver, PreprocessBesm6)
{
    WriteSource("t.c", kTargetProbe);
    ASSERT_EQ(Vcc({ "-t", "besm6", "-E", "-o", "out.i", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("out.i"));
    EXPECT_NE(text.find("BESM6"), std::string::npos) << text;
    EXPECT_EQ(text.find("RISCV"), std::string::npos) << text;
}

TEST_F(CcDriver, TargetOptionSpellings)
{
    WriteSource("t.c", kTargetProbe);
    for (const char *opt : { "-tbesm6", "--target=besm6" }) {
        ASSERT_EQ(Vcc({ opt, "-E", "-o", "out.i", "t.c" }), 0) << opt << ": " << Stderr();
        EXPECT_NE(ReadFile(Path("out.i")).find("BESM6"), std::string::npos) << opt;
    }
    ASSERT_EQ(Vcc({ "--target", "riscv64", "-E", "-o", "out.i", "t.c" }), 0) << Stderr();
    EXPECT_NE(ReadFile(Path("out.i")).find("RISCV 64"), std::string::npos);
}

TEST_F(CcDriver, PreprocessDefines)
{
    WriteSource("t.c", "A B\n");
    ASSERT_EQ(Vcc({ "-E", "-D", "A=alpha", "-DB", "t.c" }), 0) << Stderr();
    EXPECT_NE(ReadFile(Path("t.i")).find("alpha 1"), std::string::npos);
}

// A .S file is assembly that goes through the preprocessor first, with
// __ASSEMBLER__ defined.
TEST_F(CcDriver, PreprocessDotS)
{
    WriteSource("x.S", "#define REG 0\n"
                       "#ifdef __ASSEMBLER__\n"
                       "        xta REG\n"
                       "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "besm6", "-E", "-o", "x.i", "x.S" }), 0) << Stderr();
    std::string text = ReadFile(Path("x.i"));
    EXPECT_NE(text.find("xta 0"), std::string::npos) << text;
    EXPECT_EQ(text.find("#define"), std::string::npos) << text;
}

TEST_F(CcDriver, UnknownTargetFails)
{
    WriteSource("t.c", "int x;\n");
    EXPECT_NE(Vcc({ "-t", "pdp11", "-E", "t.c" }), 0);
    EXPECT_NE(Stderr().find("unknown target pdp11"), std::string::npos) << Stderr();
    EXPECT_NE(Stderr().find("riscv64"), std::string::npos) << Stderr();
}

//
// Compiling to assembly.
//
TEST_F(CcDriver, CompileToAssemblyRiscv64)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("printf"), std::string::npos) << text;
    EXPECT_NE(text.find("ret"), std::string::npos) << text;
}

TEST_F(CcDriver, CompileToAssemblyAarch64)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "aarch64", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("bl      printf"), std::string::npos) << text;
    EXPECT_NE(text.find("ret"), std::string::npos) << text;
}

// Separate compilation for aarch64, a .S among the sources, and the link of the build's
// runtime by hand.
TEST_F(CcDriver, LinkAndRunAarch64)
{
    if (!HaveAarch64Run())
        GTEST_SKIP() << "AArch64 clang/ld.lld/qemu not found";
    WriteSource("main.c", "#include <stdio.h>\n"
                          "int twice(int);\n"
                          "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S", "#ifdef __aarch64__\n"
                           "        .globl  twice\n"
                           "twice:  add     w0, w0, w0\n"
                           "        ret\n"
                           "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "aarch64", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4), "\x7f" "ELF");
    std::string lib = AARCH64_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "aarch64", "-nostdlib", "-T", AARCH64_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunQemuAarch64(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyBesm6)
{
    WriteSource("t.c", "int main(void)\n{\n    return 42;\n}\n");
    ASSERT_EQ(Vcc({ "-t", "besm6", "-S", "-o", "t.s", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    // Landmarks from the BESM-6 codegen for a trivial main() (#052 is octal 42).
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("b$save"), std::string::npos) << text;
    EXPECT_NE(text.find("#052"), std::string::npos) << text;
    EXPECT_NE(text.find("b$ret"), std::string::npos) << text;
}

// -Smadlen/-Sbemsh select the BESM-6 dialect, and without -o the output name
// takes the dialect's extension (the one vgenbesm6 itself uses).
TEST_F(CcDriver, Besm6Dialects)
{
    WriteSource("t.c", "int main(void)\n{\n    return 0;\n}\n");
    ASSERT_EQ(Vcc({ "-t", "besm6", "-Smadlen", "t.c" }), 0) << Stderr();
    ASSERT_EQ(Vcc({ "-t", "besm6", "-Sbemsh", "t.c" }), 0) << Stderr();
    ASSERT_EQ(Vcc({ "-t", "besm6", "-S", "t.c" }), 0) << Stderr();

    std::string madlen = ReadFile(Path("t.mad"));
    EXPECT_NE(madlen.find(",end,"), std::string::npos) << madlen;
    std::string bemsh = ReadFile(Path("t.bemsh"));
    // The Bemsh autocode uses Cyrillic keywords: "старт" (start).
    EXPECT_NE(bemsh.find("\xD1\x81\xD1\x82\xD0\xB0\xD1\x80\xD1\x82"), std::string::npos) << bemsh;
    std::string unix_asm = ReadFile(Path("t.s"));
    EXPECT_FALSE(unix_asm.empty());
    EXPECT_NE(madlen, unix_asm);
}

TEST_F(CcDriver, CompileErrorFails)
{
    WriteSource("t.c", "int main(void) { return x; }\n");
    EXPECT_NE(Vcc({ "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("exited with status"), std::string::npos) << Stderr();
}

//
// Usage errors.
//
TEST_F(CcDriver, RejectsUnknownDialect)
{
    WriteSource("t.c", "int x;\n");
    EXPECT_NE(Vcc({ "-t", "besm6", "-Sfoo", "t.c" }), 0);
}

TEST_F(CcDriver, RejectsDialectForRiscv64)
{
    WriteSource("t.c", "int x;\n");
    EXPECT_NE(Vcc({ "-Smadlen", "t.c" }), 0);
    EXPECT_NE(Stderr().find("-Smadlen needs -t besm6"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, RejectsLinkScriptForBesm6)
{
    WriteSource("t.o", "");
    EXPECT_NE(Vcc({ "-t", "besm6", "-T", "x.ld", "t.o" }), 0);
    EXPECT_NE(Stderr().find("-T is not supported for besm6"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, RejectsNoInputs)
{
    EXPECT_NE(Vcc({ "-c" }), 0);
    EXPECT_NE(Stderr().find("no input files"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, RejectsOutputWithMultipleInputs)
{
    WriteSource("a.c", "int a;\n");
    WriteSource("b.c", "int b;\n");
    EXPECT_NE(Vcc({ "-c", "-o", "x.o", "a.c", "b.c" }), 0);
}

TEST_F(CcDriver, RejectsUnknownSuffix)
{
    WriteSource("t.f", "");
    EXPECT_NE(Vcc({ "-c", "t.f" }), 0);
    EXPECT_NE(Stderr().find("don't know how to compile t.f"), std::string::npos) << Stderr();
}

//
// Assembling and linking for RISC-V.
//
TEST_F(CcDriver, CompileObjectRiscv64)
{
    if (!HaveRiscvLink())
        GTEST_SKIP() << "RISC-V clang/ld.lld not found";
    WriteSource("t.c", kHello);
    WriteSource("u.S", "#ifdef __riscv\n"
                       "        .globl  u\n"
                       "u:      ret\n"
                       "#endif\n");
    ASSERT_EQ(Vcc({ "-c", "t.c", "u.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("t.o")).substr(0, 4), "\x7f" "ELF");
    EXPECT_EQ(ReadFile(Path("u.o")).substr(0, 4), "\x7f" "ELF");
}

// The whole pipeline down to a qemu `virt` ELF, from the build tree: the
// standard library is named by hand, crt0.o first.
TEST_F(CcDriver, LinkAndRunRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V clang/ld.lld/qemu not found";
    WriteSource("t.c", kHello);
    std::string lib = RISCV_LIB_DIR;
    ASSERT_EQ(Vcc({ "-nostdlib", "-T", RISCV_LINK_SCRIPT, "-o", "t.elf", lib + "/crt0.o", "t.c",
                    lib + "/libc.a" }),
              0)
        << Stderr();
    EXPECT_EQ(RunQemu(Path("t.elf")), "hello 42\n");
}

// Separate compilation, then a link of the objects.
TEST_F(CcDriver, SeparateCompilationRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V clang/ld.lld/qemu not found";
    WriteSource("main.c", "#include <stdio.h>\n"
                          "int twice(int);\n"
                          "int main(void) { printf(\"%d\\n\", twice(21)); return 0; }\n");
    WriteSource("twice.c", "int twice(int x) { return 2 * x; }\n");
    ASSERT_EQ(Vcc({ "-c", "main.c", "twice.c" }), 0) << Stderr();
    std::string lib = RISCV_LIB_DIR;
    ASSERT_EQ(Vcc({ "-nostdlib", "-T", RISCV_LINK_SCRIPT, lib + "/crt0.o", "main.o", "twice.o",
                    lib + "/libc.a" }),
              0)
        << Stderr();
    EXPECT_EQ(RunQemu(Path("a.out")), "42\n");
}

// A miniature installation, with no overrides: the passes are found beside the
// driver, the headers and the runtime under ../share/vcc/riscv64.
TEST_F(CcDriver, StagedPrefixRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V clang/ld.lld/qemu not found";
    std::string prefix = StagePrefix("riscv64");
    std::string lib = prefix + "/share/vcc/riscv64/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(RISCV_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(RISCV_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", kHello);
    ASSERT_EQ(StagedVcc(prefix, { "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    EXPECT_EQ(RunQemu(Path("t.elf")), "hello 42\n");

    // Every stage came from the staged tree.
    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t riscv64 -nostdinc -I" + prefix +
                        "/share/vcc/riscv64/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenriscv64 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
}

// The same for riscv32: vgenriscv32 is the one code generator under another name.
TEST_F(CcDriver, StagedPrefixRiscv32)
{
    if (!HaveRiscvLink() || !HaveTool(RISCV32_QEMU) ||
        access((std::string(RISCV32_LIB_DIR) + "/libc.a").c_str(), R_OK) != 0)
        GTEST_SKIP() << "RISC-V clang/ld.lld/qemu-system-riscv32 not found";
    std::string prefix = StagePrefix("riscv32");
    std::string lib = prefix + "/share/vcc/riscv32/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(RISCV32_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(RISCV_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", "#include <stdio.h>\n"
                       "int main(void)\n"
                       "{\n"
                       "    long long x = 1LL << 40;\n"
                       "    printf(\"%d %d %lld\\n\", (int)sizeof(long), __riscv_xlen, x);\n"
                       "    return 0;\n"
                       "}\n");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "riscv32", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    EXPECT_EQ(RunQemu(Path("t.elf"), RISCV32_QEMU), "4 32 1099511627776\n");

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t riscv32 -nostdinc -I" + prefix +
                        "/share/vcc/riscv32/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenriscv32 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" --target=riscv32 -march=rv32imfd -mabi=ilp32d "), std::string::npos)
        << echo;
}

// The same for aarch64: clang without -march/-mabi, and the exit status through
// semihosting.
TEST_F(CcDriver, StagedPrefixAarch64)
{
    if (!HaveAarch64Run())
        GTEST_SKIP() << "AArch64 clang/ld.lld/qemu not found";
    std::string prefix = StagePrefix("aarch64");
    std::string lib = prefix + "/share/vcc/aarch64/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(AARCH64_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(AARCH64_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", "#include <stdio.h>\n"
                       "#include <limits.h>\n"
                       "#include <stddef.h>\n"
                       "int main(void)\n"
                       "{\n"
                       "    printf(\"%d %d %Lg\\n\", (int)sizeof(long), (int)sizeof(wchar_t), 2.5L);\n"
                       "    return CHAR_MAX == 255 ? 7 : 1;\n"
                       "}\n");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "aarch64", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    int status;
    EXPECT_EQ(RunQemuAarch64(Path("t.elf"), &status), "8 4 2.5\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t aarch64 -nostdinc -I" + prefix +
                        "/share/vcc/aarch64/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t aarch64 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenaarch64 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" --target=aarch64-none-elf -c "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
}

TEST_F(CcDriver, StagedPrefixMissingPass)
{
    std::string prefix = StagePrefix("riscv64");
    fs::remove(prefix + "/bin/vparse");
    WriteSource("t.c", "int x;\n");
    EXPECT_NE(StagedVcc(prefix, { "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("cannot find " + prefix + "/bin/vparse"), std::string::npos)
        << Stderr();
}

//
// Linking for the BESM-6.  b6ld is v7besm's, so the link line is checked with a
// stand-in linker: the -v echo shows the order, crt0.o first and -lc ahead of
// -lruntime (b6ld scans an archive once where it stands, and libc calls the b$*
// helpers, never the reverse).
//
TEST_F(CcDriver, Besm6LinkLine)
{
    std::string prefix = StagePrefix("besm6");
    std::string lib = prefix + "/share/vcc/besm6/lib";
    WriteSource(lib.substr(dir.size() + 1) + "/crt0.o", "");
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    ASSERT_EQ(StagedVcc(prefix, { "-t", "besm6", "-v", "-o", "t.b6", "t.o", "-lm" }), 0) << Stderr();
    EXPECT_EQ(Stdout(), "true -X -e _start -o t.b6 -L" + lib + " " + lib +
                            "/crt0.o t.o -lm -lc -lruntime \n");
}

TEST_F(CcDriver, Besm6LinkNostdlib)
{
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    ASSERT_EQ(Vcc({ "-t", "besm6", "-v", "-nostdlib", "t.o" }), 0) << Stderr();
    EXPECT_EQ(Stdout(), "true -X -e _start -o a.out t.o \n");
}

// The BESM-6 crt0.o and libc.a are v7besm's; without them the link says where
// they belong.
TEST_F(CcDriver, Besm6LinkWithoutCrt0)
{
    std::string prefix = StagePrefix("besm6");
    WriteSource("t.o", "");
    EXPECT_NE(StagedVcc(prefix, { "-t", "besm6", "t.o" }), 0);
    EXPECT_NE(Stderr().find("come from v7besm"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, CompileObjectBesm6)
{
    if (!HaveTool("b6as"))
        GTEST_SKIP() << "b6as not found on PATH";
    WriteSource("t.c", "int main(void)\n{\n    return 0;\n}\n");
    ASSERT_EQ(Vcc({ "-t", "besm6", "-c", "t.c" }), 0) << Stderr();
    EXPECT_FALSE(ReadFile(Path("t.o")).empty());
}

} // namespace
