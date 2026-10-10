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
// Cases that assemble, link or run need the external tools (the target's GNU
// binutils or clang and ld.lld, and its simulator; b6as for the BESM-6) and skip
// without them.  The assembler commands they expect in the -v echo are those CMake
// found (scripts/CrossTools.cmake), so the driver's flags must agree with CMake's.
//
#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
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

// The assembler, a command with its flags, names an available tool.
bool HaveAssembler(const std::string &cmd)
{
    return HaveTool(cmd.substr(0, cmd.find(' ')));
}

// The RISC-V toolchain and the build's runtime are all present.
bool HaveRiscvLink()
{
    return RISCV_TOOLS_FOUND && HaveAssembler(RISCV_ASSEMBLER) && HaveTool(RISCV_LD) &&
           access((std::string(RISCV_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

bool HaveRiscvRun()
{
    return HaveRiscvLink() && HaveTool(RISCV_QEMU);
}

// The same for AArch64: its tools, runtime and qemu.
bool HaveAarch64Run()
{
    return AARCH64_TOOLS_FOUND && HaveAssembler(AARCH64_ASSEMBLER) && HaveTool(AARCH64_LD) &&
           HaveTool(AARCH64_QEMU) &&
           access((std::string(AARCH64_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The same for ARM32.
bool HaveArm32Run()
{
    return ARM32_TOOLS_FOUND && HaveAssembler(ARM32_ASSEMBLER) && HaveTool(ARM32_LD) &&
           HaveTool(ARM32_QEMU) &&
           access((std::string(ARM32_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The same for wasm32: clang, wasm-ld, the runtime, and node to run a module.
bool HaveWasm32Run()
{
    return WASM32_TOOLS_FOUND && HaveAssembler(WASM32_ASSEMBLER) && HaveTool(WASM32_LD) &&
           HaveTool(WASM32_NODE) &&
           access((std::string(WASM32_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The same for x86-64, run on qemu `microvm`.
bool HaveX86Run()
{
    return X86_TOOLS_FOUND && HaveAssembler(X86_ASSEMBLER) && HaveTool(X86_LD) &&
           HaveTool(X86_QEMU) && access((std::string(X86_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The same for AVR, run on qemu `arduino-mega`.
bool HaveAvrRun()
{
    return AVR_TOOLS_FOUND && HaveAssembler(AVR_ASSEMBLER) && HaveTool(AVR_LD) &&
           HaveTool(AVR_QEMU) && access((std::string(AVR_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The GNU MSP430 binutils, mspsim and the build's MSP430 runtime.
bool HaveMsp430Run()
{
    return MSP430_TOOLS_FOUND && MSP430_GNU && HaveAssembler(MSP430_ASSEMBLER) &&
           HaveTool(MSP430_LD) && HaveTool(MSPSIM) &&
           access((std::string(MSP430_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The GNU MMIX binutils, Knuth's mmix and the build's MMIX runtime.
bool HaveMmixRun()
{
    return MMIX_TOOLS_FOUND && HaveTool(MMIX_AS) && HaveTool(MMIX_LD) && HaveTool(MMIX_SIM) &&
           access((std::string(MMIX_LIB_DIR) + "/libc.a").c_str(), R_OK) == 0;
}

// The hosted target of this machine, as the driver decides it; "" where there is none,
// and the driver's default is riscv64.
#if defined(__linux__) && defined(__x86_64__)
#define HOST_TARGET "x86_64-linux"
#elif defined(__linux__) && defined(__aarch64__)
#define HOST_TARGET "aarch64-linux"
#elif defined(__APPLE__) && defined(__aarch64__)
#define HOST_TARGET "aarch64-darwin"
#else
#define HOST_TARGET ""
#endif

// macOS: no libvcc.a, a position-independent executable.
bool HostIsDarwin()
{
    return std::string(HOST_TARGET) == "aarch64-darwin";
}

std::string DefaultTarget()
{
    return *HOST_TARGET ? HOST_TARGET : "riscv64";
}

// The C compiler that assembles and links for the host target, and the build's libvcc.a.
std::string HostCc()
{
    if (HostIsDarwin())
        return AARCH64_DARWIN_CC;
    return std::string(HOST_TARGET) == "aarch64-linux" ? AARCH64_LINUX_CC : X86_64_LINUX_CC;
}

std::string HostLibDir()
{
    if (HostIsDarwin())
        return AARCH64_DARWIN_LIB_DIR;
    return std::string(HOST_TARGET) == "aarch64-linux" ? AARCH64_LINUX_LIB_DIR
                                                       : X86_64_LINUX_LIB_DIR;
}

bool HaveHostedRun()
{
    return *HOST_TARGET && HaveTool(HostCc()) &&
           access((HostLibDir() + "/libvcc.a").c_str(), R_OK) == 0;
}

// Link the build's libvcc.a into a staged installation for the host.
void StageLibvcc(const std::string &prefix)
{
    fs::create_symlink(HostLibDir() + "/libvcc.a",
                       prefix + "/share/vcc/" + HOST_TARGET + "/lib/libvcc.a");
}

// Run argv and return its exit code; -1 on spawn failure or a signal, -2 on a
// timeout.  The child's stdout goes to `stdout_file` and its stderr to
// `stderr_file` when they are given (which is how the -v echo is captured).  With a
// `done_file`, the child is stopped (and 0 returned) once that file is not empty:
// qemu has no way to exit on AVR.
int RunProcess(const std::vector<std::string> &argv, const std::string &stdout_file = {},
               const std::string &stderr_file = {}, int timeout_sec = 20,
               const std::string &done_file = {})
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
        struct stat st;
        if (!done_file.empty() && stat(done_file.c_str(), &st) == 0 && st.st_size > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return 0;
        }
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

const char *const kOverrides[] = { "VCC_CPP", "VCC_PARSE", "VCC_LOWER",
                                   "VCC_GEN", "VCC_AS",    "VCC_LD" };

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

    // The header directories of `target`, searched in this order.
    static std::vector<std::string> IncludeDirs(const std::string &target)
    {
        if (target == "besm6")
            return { BESM6_INCLUDE_DIR, COMMON_INCLUDE_DIR };
        if (target == "x86_64-linux")
            return { LINUX_X86_64_INCLUDE_DIR, LINUX_INCLUDE_DIR, X86_INCLUDE_DIR, LP64_INCLUDE_DIR,
                     COMMON_INCLUDE_DIR };
        if (target == "aarch64-linux")
            return { LINUX_AARCH64_INCLUDE_DIR, LINUX_INCLUDE_DIR, AARCH64_INCLUDE_DIR,
                     LP64_INCLUDE_DIR, COMMON_INCLUDE_DIR };
        if (target == "aarch64-darwin")
            return { DARWIN_INCLUDE_DIR, AARCH64_INCLUDE_DIR, LP64_INCLUDE_DIR,
                     COMMON_INCLUDE_DIR };
        const char *inc   = target == "riscv32"   ? RISCV32_INCLUDE_DIR
                            : target == "aarch64" ? AARCH64_INCLUDE_DIR
                            : target == "arm32"   ? ARM32_INCLUDE_DIR
                            : target == "x86_64"  ? X86_INCLUDE_DIR
                            : target == "avr"     ? AVR_INCLUDE_DIR
                            : target == "msp430"  ? MSP430_INCLUDE_DIR
                            : target == "mmix"    ? MMIX_INCLUDE_DIR
                            : target == "wasm32"  ? WASM32_INCLUDE_DIR
                                                  : RISCV_INCLUDE_DIR;
        const char *model = target == "avr" || target == "msp430" ? IP16_INCLUDE_DIR
                            : target == "riscv32" || target == "arm32" || target == "wasm32"
                                ? ILP32_INCLUDE_DIR
                                : LP64_INCLUDE_DIR;
        return { inc, model, COMMON_INCLUDE_DIR };
    }

    // Run the driver from `dir` with `args`, for the target named by a "-t"
    // among them (the driver's default otherwise), with the in-tree headers on the
    // search path.  stdout lands in out.log, stderr in err.log.
    int Vcc(std::vector<std::string> args, bool std_headers = true)
    {
        std::string target = DefaultTarget();
        for (size_t i = 0; i + 1 < args.size(); i++)
            if (args[i] == "-t")
                target = args[i + 1];
        setenv("VCC_GEN",
               target == "besm6" ? VCC_GENBESM_PATH
               : target == "aarch64" || target == "aarch64-linux" || target == "aarch64-darwin"
                   ? VCC_GENAARCH64_PATH
               : target == "arm32"                              ? VCC_GENARM32_PATH
               : target == "x86_64" || target == "x86_64-linux" ? VCC_GENX86_PATH
               : target == "avr"                                ? VCC_GENAVR_PATH
               : target == "msp430"                             ? VCC_GENMSP430_PATH
               : target == "mmix"                               ? VCC_GENMMIX_PATH
               : target == "wasm32"                             ? VCC_GENWASM_PATH
                                                                : VCC_GENRISCV_PATH,
               1);

        std::vector<std::string> argv = { VCC_COMMAND };
        if (std_headers) {
            argv.push_back("-nostdinc");
            for (const std::string &inc : IncludeDirs(target))
                // cppcheck-suppress useStlAlgorithm
                argv.push_back("-I" + inc);
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
        fs::create_symlink(VCC_GENARM32_PATH, prefix + "/bin/vgenarm32");
        fs::create_symlink(VCC_GENX86_PATH, prefix + "/bin/vgenx86");
        fs::create_symlink(VCC_GENAVR_PATH, prefix + "/bin/vgenavr");
        fs::create_symlink(VCC_GENMSP430_PATH, prefix + "/bin/vgenmsp430");
        fs::create_symlink(VCC_GENMMIX_PATH, prefix + "/bin/vgenmmix");
        fs::create_symlink(VCC_GENWASM_PATH, prefix + "/bin/vgenwasm");

        std::string share = prefix + "/share/vcc/" + target;
        fs::create_directories(share + "/include");
        fs::create_directories(share + "/lib");
        for (const std::string &inc : IncludeDirs(target)) {
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
        int rc = RunProcess({ qemu, "-M", "virt", "-bios", "none", "-display", "none", "-serial",
                              "stdio", "-monitor", "none", "-kernel", elf },
                            out, Path("qemu.err"), 10);
        EXPECT_GE(rc, 0) << "qemu failed:\n" << ReadFile(Path("qemu.err"));
        return ReadFile(out);
    }

    // Run a linked AArch64 ELF under qemu `virt`, main's result through semihosting;
    // returns its UART output, the exit status in *status.
    std::string RunQemuAarch64(const std::string &elf, int *status)
    {
        std::string out = Path("qemu.out");
        *status =
            RunProcess({ AARCH64_QEMU, "-M", "virt", "-cpu", "cortex-a57", "-display", "none",
                         "-serial", "stdio", "-monitor", "none", "-semihosting", "-kernel", elf },
                       out, Path("qemu.err"), 10);
        return ReadFile(out);
    }

    // Run a linked wasm32 module under node; returns its output, main's result in *status.
    std::string RunWasm32(const std::string &module, int *status)
    {
        std::string out = Path("node.out");
        *status = RunProcess({ WASM32_NODE, WASM32_RUNNER, module }, out, Path("node.err"), 10);
        return ReadFile(out);
    }

    // The same for an ARM32 ELF.
    std::string RunQemuArm32(const std::string &elf, int *status)
    {
        std::string out = Path("qemu.out");
        *status =
            RunProcess({ ARM32_QEMU, "-M", "virt", "-cpu", "cortex-a15", "-display", "none",
                         "-serial", "stdio", "-monitor", "none", "-semihosting", "-kernel", elf },
                       out, Path("qemu.err"), 10);
        return ReadFile(out);
    }

    // Run a linked x86-64 ELF under qemu `microvm`; returns its UART output, main's
    // result in *status.  The exit device makes qemu's own status (value << 1) | 1, so
    // the status is read from the debug console, where exit() writes it first.
    std::string RunQemuX86(const std::string &elf, int *status)
    {
        std::string out = Path("qemu.out"), con = Path("qemu.status");
        RunProcess({ X86_QEMU, "-M", "microvm", "-display", "none", "-serial", "stdio", "-monitor",
                     "none", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-debugcon",
                     "file:" + con, "-kernel", elf },
                   out, Path("qemu.err"), 10);
        std::string code = ReadFile(con);
        *status          = code.empty() ? -1 : (unsigned char)code[0];
        return ReadFile(out);
    }

    // Run a linked AVR ELF under qemu `arduino-mega`; returns its USART0 output, main's
    // result in *status, the byte the runtime writes to USART1, which ends the run.
    std::string RunQemuAvr(const std::string &elf, int *status)
    {
        std::string out = Path("qemu.out"), con = Path("qemu.status");
        RunProcess({ AVR_QEMU, "-M", "arduino-mega", "-display", "none", "-monitor", "none",
                     "-serial", "stdio", "-serial", "file:" + con, "-bios", elf },
                   out, Path("qemu.err"), 10, con);
        std::string code = ReadFile(con);
        *status          = code.empty() ? -1 : (unsigned char)code[0];
        return ReadFile(out);
    }

    // Run an MSP430 ELF or Intel HEX file under mspsim; returns its UART output, main's
    // result in *status.
    std::string RunMspsim(const std::string &firmware, int *status)
    {
        std::string out = Path("mspsim.out");
        *status =
            RunProcess({ MSPSIM, "-q", "-n", "100000000", firmware }, out, Path("mspsim.err"), 10);
        return ReadFile(out);
    }

    // Run an MMIX .mmo under Knuth's mmix; returns its output, main's result in *status.
    std::string RunMmix(const std::string &mmo, int *status)
    {
        std::string out = Path("mmix.out");
        *status         = RunProcess({ MMIX_SIM, "-q", mmo }, out, Path("mmix.err"), 10);
        return ReadFile(out);
    }
};

const char kHello[] =
    "#include <stdio.h>\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"hello %d\\n\", 42);\n"
    "    return 0;\n"
    "}\n";

const char kTargetProbe[] =
    "#ifdef __riscv\n"
    "RISCV __riscv_xlen\n"
    "#endif\n"
    "#ifdef besm6\n"
    "BESM6\n"
    "#endif\n"
    "#ifdef __x86_64__\n"
    "X86_64\n"
    "#endif\n"
    "#ifdef __aarch64__\n"
    "AARCH64\n"
    "#endif\n"
    "#ifdef __linux__\n"
    "LINUX\n"
    "#endif\n"
    "#ifdef __APPLE__\n"
    "APPLE\n"
    "#endif\n";

//
// Preprocessing and target selection.
//
// The default is the host, where it is one of the hosted targets, else riscv64.
TEST_F(CcDriver, PreprocessDefaultTarget)
{
    WriteSource("t.c", kTargetProbe);
    ASSERT_EQ(Vcc({ "-E", "t.c" }), 0) << Stderr();
    std::string text = Stdout();
    std::string host = HOST_TARGET;
    EXPECT_EQ(text.find("BESM6"), std::string::npos) << text;
    // cppcheck-suppress knownConditionTrueFalse ; depends on the host
    if (host.empty()) {
        EXPECT_NE(text.find("RISCV 64"), std::string::npos) << text;
        EXPECT_EQ(text.find("LINUX"), std::string::npos) << text;
    } else {
        EXPECT_NE(text.find(host == "x86_64-linux" ? "X86_64" : "AARCH64"), std::string::npos)
            << text;
        bool darwin = HostIsDarwin();
        EXPECT_NE(text.find(darwin ? "APPLE" : "LINUX"), std::string::npos) << text;
        EXPECT_EQ(text.find(darwin ? "LINUX" : "APPLE"), std::string::npos) << text;
        EXPECT_EQ(text.find("RISCV"), std::string::npos) << text;
    }
}

// The bare-metal x86_64 is not Linux; x86_64-linux is.
TEST_F(CcDriver, PreprocessHostedTargets)
{
    WriteSource("t.c", kTargetProbe);
    ASSERT_EQ(Vcc({ "-t", "x86_64", "-E", "-o", "bare.i", "t.c" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("bare.i")).find("LINUX"), std::string::npos);
    ASSERT_EQ(Vcc({ "-t", "x86_64-linux", "-E", "-o", "x.i", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("x.i"));
    EXPECT_NE(text.find("X86_64"), std::string::npos) << text;
    EXPECT_NE(text.find("LINUX"), std::string::npos) << text;
    ASSERT_EQ(Vcc({ "-t", "aarch64-linux", "-E", "-o", "a.i", "t.c" }), 0) << Stderr();
    text = ReadFile(Path("a.i"));
    EXPECT_NE(text.find("AARCH64"), std::string::npos) << text;
    EXPECT_NE(text.find("LINUX"), std::string::npos) << text;
    ASSERT_EQ(Vcc({ "-t", "aarch64-darwin", "-E", "-o", "d.i", "t.c" }), 0) << Stderr();
    text = ReadFile(Path("d.i"));
    EXPECT_NE(text.find("AARCH64"), std::string::npos) << text;
    EXPECT_NE(text.find("APPLE"), std::string::npos) << text;
    EXPECT_EQ(text.find("LINUX"), std::string::npos) << text;
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
    EXPECT_NE(Stdout().find("alpha 1"), std::string::npos);
}

// A .S file is assembly that goes through the preprocessor first, with
// __ASSEMBLER__ defined.
TEST_F(CcDriver, PreprocessDotS)
{
    WriteSource("x.S",
                "#define REG 0\n"
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
    EXPECT_NE(Stderr().find("unknown target 'pdp11'"), std::string::npos) << Stderr();
    EXPECT_NE(Stderr().find("riscv64"), std::string::npos) << Stderr();
}

//
// Compiling to assembly.
//
TEST_F(CcDriver, CompileToAssemblyRiscv64)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0) << Stderr();
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
        GTEST_SKIP() << "AArch64 assembler/linker/qemu not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S",
                "#ifdef __aarch64__\n"
                "        .globl  twice\n"
                "twice:  add     w0, w0, w0\n"
                "        ret\n"
                "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "aarch64", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = AARCH64_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "aarch64", "-nostdlib", "-T", AARCH64_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunQemuAarch64(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyArm32)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "arm32", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find(".syntax unified"), std::string::npos) << text;
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("bl      printf"), std::string::npos) << text;
}

// Separate compilation for arm32, a .S among the sources, and the link of the build's
// runtime by hand.
TEST_F(CcDriver, LinkAndRunArm32)
{
    if (!HaveArm32Run())
        GTEST_SKIP() << "ARM32 assembler/linker/qemu not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S",
                "#ifdef __arm__\n"
                "        .globl  twice\n"
                "twice:  add     r0, r0, r0\n"
                "        bx      lr\n"
                "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "arm32", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = ARM32_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "arm32", "-nostdlib", "-T", ARM32_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunQemuArm32(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyWasm32)
{
    WriteSource("t.c", "int main(void) { return 42; }\n");
    ASSERT_EQ(Vcc({ "-t", "wasm32", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("\t.functype\t__original_main () -> (i32)\n"), std::string::npos) << text;
    EXPECT_NE(text.find("__main_void = __original_main\n"), std::string::npos) << text;
    EXPECT_NE(text.find("\ti32.const\t42\n"), std::string::npos) << text;
}

// Separate compilation for wasm32, a .S among the sources, and the link of the build's
// runtime by hand.
TEST_F(CcDriver, LinkAndRunWasm32)
{
    if (!HaveWasm32Run())
        GTEST_SKIP() << "clang/wasm-ld/node not found";
    WriteSource("main.c", "int main(void) { return 3; }\n");
    WriteSource("seven.S",
                "#ifdef __wasm32__\n"
                "        .section .text.seven,\"\",@\n"
                "        .globl  seven\n"
                "        .type   seven,@function\n"
                "seven:\n"
                "        .functype seven () -> (i32)\n"
                "        i32.const 7\n"
                "        end_function\n"
                "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "wasm32", "-c", "main.c", "seven.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("seven.o")).substr(0, 4), std::string("\0asm", 4));
    std::string lib = WASM32_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "wasm32", "-nostdlib", "-o", "t.wasm", lib + "/crt0-status.o", "main.o",
                    "seven.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunWasm32(Path("t.wasm"), &status), "3\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyX86)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "x86_64", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("call    printf"), std::string::npos) << text;
}

// Separate compilation for x86_64, a .S among the sources, and the link of the build's
// runtime by hand.
TEST_F(CcDriver, LinkAndRunX86)
{
    if (!HaveX86Run())
        GTEST_SKIP() << "x86-64 assembler/linker/qemu not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S",
                "#ifdef __x86_64__\n"
                "        .globl  twice\n"
                "twice:  leal    (%rdi,%rdi), %eax\n"
                "        ret\n"
                "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "x86_64", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = X86_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "x86_64", "-nostdlib", "-T", X86_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunQemuX86(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyAvr)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "avr", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("call    printf"), std::string::npos) << text;
}

// Separate compilation for AVR, a .S among the sources, and the link of the build's
// runtime by hand.
TEST_F(CcDriver, LinkAndRunAvr)
{
    if (!HaveAvrRun())
        GTEST_SKIP() << "AVR assembler/linker/qemu not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S", R"(#ifdef __AVR__
        .globl  twice
twice:  lsl     r24
        rol     r25
        ret
#endif
)");
    ASSERT_EQ(Vcc({ "-t", "avr", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = AVR_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "avr", "-nostdlib", "-T", AVR_LINK_SCRIPT, "-o", "t.elf", lib + "/crt0.o",
                    "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunQemuAvr(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

TEST_F(CcDriver, CompileToAssemblyMsp430)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "msp430", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find(".section .text.main,"), std::string::npos) << text;
    EXPECT_NE(text.find("call    #printf"), std::string::npos) << text;
}

// Separate compilation for the MSP430, a .S among the sources, and the link of the
// build's runtime by hand: the ELF runs on mspsim, and so does its Intel HEX.
TEST_F(CcDriver, LinkAndRunMsp430)
{
    // cppcheck-suppress knownConditionTrueFalse ; MSP430_TOOLS_FOUND is per configuration
    if (!HaveMsp430Run())
        GTEST_SKIP() << "MSP430 binutils or mspsim not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S", R"(#ifdef __MSP430__
        .globl  twice
twice:  rla     r12
        ret
#endif
)");
    ASSERT_EQ(Vcc({ "-t", "msp430", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = MSP430_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "msp430", "-nostdlib", "-T", MSP430_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunMspsim(Path("t.elf"), &status), "42\n");
    EXPECT_EQ(status, 3);

    ASSERT_EQ(RunProcess({ MSP430_OBJCOPY, "-O", "ihex", Path("t.elf"), Path("t.hex") }), 0);
    EXPECT_EQ(RunMspsim(Path("t.hex"), &status), "42\n");
    EXPECT_EQ(status, 3);
}

// clang's assembler and ld.lld through the overrides, which carry their own flags.
TEST_F(CcDriver, LinkAndRunMsp430Clang)
{
    // cppcheck-suppress knownConditionTrueFalse ; MSP430_TOOLS_FOUND is per configuration
    if (!HaveMsp430Run() || !MSP430_CLANG_FOUND || !HaveTool(MSP430_CLANG) || !HaveTool(MSP430_LLD))
        GTEST_SKIP() << "MSP430 clang/ld.lld or mspsim not found";
    WriteSource("t.c", kHello);
    setenv("VCC_AS", (std::string(MSP430_CLANG) + " --target=msp430 -c").c_str(), 1);
    setenv("VCC_LD", (std::string(MSP430_LLD) + " -n").c_str(), 1);
    std::string lib = MSP430_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "msp430", "-v", "-nostdlib", "-T", MSP430_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "t.c", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunMspsim(Path("t.elf"), &status), "hello 42\n");
    EXPECT_EQ(status, 0);
    std::string echo = Stdout();
    EXPECT_NE(echo.find(std::string(MSP430_CLANG) + " --target=msp430 -c -o "), std::string::npos)
        << echo;
    EXPECT_NE(echo.find(std::string(MSP430_LLD) + " -n --gc-sections -T "), std::string::npos)
        << echo;
}

TEST_F(CcDriver, CompileToAssemblyMmix)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "mmix", "-S", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("t.s"));
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find(", printf\n"), std::string::npos) << text;
    EXPECT_NE(text.find("pushj   $"), std::string::npos) << text;
}

// Separate compilation for MMIX, a .S among the sources, and the link of the build's
// runtime by hand: the .mmo runs on mmix, whose status is main's result.
TEST_F(CcDriver, LinkAndRunMmix)
{
    // cppcheck-suppress knownConditionTrueFalse ; MMIX_TOOLS_FOUND is per configuration
    if (!HaveMmixRun())
        GTEST_SKIP() << "mmix-knuth-mmixware-as/ld or mmix not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "long twice(long);\n"
                "int main(void) { printf(\"%ld\\n\", twice(21)); return 3; }\n");
    WriteSource("twice.S", R"(#ifdef __MMIX__
        .text
        .global twice
twice   SLU     $0,$0,1
        POP     1,0
#endif
)");
    ASSERT_EQ(Vcc({ "-t", "mmix", "-c", "main.c", "twice.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("twice.o")).substr(0, 4),
              "\x7f"
              "ELF");
    std::string lib = MMIX_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "mmix", "-v", "-nostdlib", "-o", "t.mmo", lib + "/crt0.o", "main.o",
                    "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    int status;
    EXPECT_EQ(RunMmix(Path("t.mmo"), &status), "42\n");
    EXPECT_EQ(status, 3);
    std::string echo = Stdout();
    EXPECT_NE(echo.find("mmix-knuth-mmixware-ld --defsym=__.MMIX.start..text=0x100 -o t.mmo "),
              std::string::npos)
        << echo;
    EXPECT_EQ(echo.find(" -T "), std::string::npos) << echo;
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
    // The pass reports the error; the driver adds nothing of its own.
    EXPECT_NE(Stderr().find("t.c:1:25: error: "), std::string::npos) << Stderr();
    EXPECT_EQ(Stderr().find("exit status"), std::string::npos) << Stderr();
    EXPECT_EQ(Stderr().find("vcc: error"), std::string::npos) << Stderr();
}

// The diagnostics of each pass name the file, line and column.
TEST_F(CcDriver, LexicalErrorLocation)
{
    WriteSource("t.c", "int main(void)\n{\n    return 1x;\n}\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:3:12: error: invalid suffix"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, SyntaxErrorLocation)
{
    WriteSource("t.c", "int main(void)\n{\n    int a = 1\n    return a;\n}\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:4:5: error: expected ';' before 'return'"), std::string::npos)
        << Stderr();
}

TEST_F(CcDriver, TypeErrorLocation)
{
    WriteSource("t.c", "int main(void)\n{\n    int *p;\n    p = 3.5;\n    return 0;\n}\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:4:7: error: cannot convert 'double' to 'int *' when assigning"),
              std::string::npos)
        << Stderr();
}

// A redefinition or a wrong call is followed by a note at the earlier declaration.
TEST_F(CcDriver, NoteAtPreviousDeclaration)
{
    WriteSource("t.c", "int g;\ndouble g;\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:2:8: error: conflicting types for 'g'\n"
                            "t.c:1:5: note: previous declaration of 'g' is here\n"),
              std::string::npos)
        << Stderr();

    WriteSource("t.c", "int f(int);\nint main(void)\n{\n    return f(1, 2);\n}\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:4:13: error: too many arguments to function 'f' "
                            "(expected 1, have 2)\n"
                            "t.c:1:5: note: 'f' declared here\n"),
              std::string::npos)
        << Stderr();
}

// An undefined label is reported at the goto, not at the function.
TEST_F(CcDriver, UndefinedLabelAtGoto)
{
    WriteSource("t.c", "int main(void)\n{\n    goto out;\n}\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:3:5: error: use of undeclared label 'out'"), std::string::npos)
        << Stderr();
}

TEST_F(CcDriver, ErrorLocationInHeader)
{
    WriteSource("h.h", "\nint f(void) { return y; }\n");
    WriteSource("t.c", "int x;\n#include \"h.h\"\n");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("h.h:2:22: error: "), std::string::npos) << Stderr();
}

//
// Error recovery: a pass reports every error it finds, not just the first.
//
TEST_F(CcDriver, SeveralSyntaxErrors)
{
    WriteSource("t.c", R"(int f(int x)
{
    int a = x +;
    a = a * 2
    return a;
}
struct S { int p q; int r; };
int k = ;
int g(void) { return 1; }
int h(void) { return 2 }
)");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_EQ(Stderr(),
              "t.c:3:16: error: expected an expression before ';'\n"
              "t.c:5:5: error: expected ';' before 'return'\n"
              "t.c:7:18: error: expected ',' before 'q'\n"
              "t.c:8:9: error: expected an expression before ';'\n"
              "t.c:10:24: error: expected ';' before '}'\n");
    EXPECT_FALSE(fs::exists(Path("t.s")));
}

TEST_F(CcDriver, SeveralLexicalErrors)
{
    WriteSource("t.c", R"(int f(void)
{
    int a = 1foo;
    int b = 2 ` 3;
    return a + b;
}
int g(void) { return 1e+; }
)");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("t.c:3:13: error: invalid suffix on numeric constant '1foo'\n"
                            "t.c:4:15: error: invalid character '`'\n"),
              std::string::npos)
        << Stderr();
    EXPECT_NE(Stderr().find("t.c:7:22: error: missing exponent"), std::string::npos) << Stderr();
}

// An error in a nested block resumes in that block; an unterminated function ends it all.
TEST_F(CcDriver, SyntaxErrorRecoveryInBlocks)
{
    WriteSource("t.c", R"(int f(int x)
{
    if (x) {
        x = (x + ;
        x = x * ;
    } else
        x = ;
    return x;
}
int g(void) { return 1;
)");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_EQ(Stderr(),
              "t.c:4:18: error: expected an expression before ';'\n"
              "t.c:5:17: error: expected an expression before ';'\n"
              "t.c:7:13: error: expected an expression before ';'\n"
              "t.c:11:1: error: expected '}' at end of file\n");
}

// Type errors: each declaration is checked on its own, a block or a switch left
// half-checked does not disturb the next one, and a note follows its error.
TEST_F(CcDriver, SeveralTypeErrors)
{
    WriteSource("t.c", R"(int f(int x)
{
    switch (x) {
    case 1:
        return y;
    }
    return 0;
}
int g(void)
{
    int *p;
    {
        int q = 1;
        p = 3.5;
    }
    return 0;
}
int h(int a);
int k(void) { return h(1, 2); }
int m(void) { int q = 2; switch (q) { case 1: break; case 1: break; } return f(q) + g(); }
)");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_EQ(Stderr(),
              "t.c:5:16: error: use of undeclared identifier 'y'\n"
              "t.c:14:11: error: cannot convert 'double' to 'int *' when assigning\n"
              "t.c:19:23: error: too many arguments to function 'h' (expected 1, have 2)\n"
              "t.c:18:5: note: 'h' declared here\n"
              "t.c:20:54: error: duplicate case value 1\n");
    EXPECT_FALSE(fs::exists(Path("t.s")));
}

// A name whose declaration failed is no new error where it is used.
TEST_F(CcDriver, NoErrorsFollowingFailedDeclaration)
{
    WriteSource("t.c", R"(struct T t;
int k = 1.5 + "x";
int a(void) { return t.x + k; }
int b(void) { return w + 1; }
)");
    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_EQ(Stderr(),
              "t.c:1:10: error: variable 't' has incomplete type 'struct T'\n"
              "t.c:2:13: error: invalid operands to '+' ('double' and 'char *')\n"
              "t.c:4:22: error: use of undeclared identifier 'w'\n");
}

// -fmax-errors=N stops after N errors; 0 means no limit, the default is 20.
TEST_F(CcDriver, MaxErrors)
{
    std::string src;
    for (int i = 0; i < 25; i++)
        src += "int f" + std::to_string(i) + "(void) { return x; }\n";
    WriteSource("t.c", src);
    auto count = [&](const char *what) {
        size_t n = 0;
        for (size_t pos = 0; (pos = Stderr().find(what, pos)) != std::string::npos; pos++)
            n++;
        return n;
    };

    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "-fmax-errors=2", "t.c" }), 0);
    EXPECT_EQ(Stderr().find("t.c:1:23: error: use of undeclared identifier 'x'\n"
                            "t.c:2:23: error: use of undeclared identifier 'x'\n"),
              0u)
        << Stderr();
    EXPECT_EQ(count("undeclared identifier"), 2u) << Stderr();
    EXPECT_NE(Stderr().find(": error: too many errors, stopping\n"), std::string::npos) << Stderr();

    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_EQ(count("undeclared identifier"), 20u) << Stderr();
    EXPECT_EQ(count("too many errors"), 1u) << Stderr();

    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "-fmax-errors=0", "t.c" }), 0);
    EXPECT_EQ(count("undeclared identifier"), 25u) << Stderr();
    EXPECT_EQ(count("too many errors"), 0u) << Stderr();

    EXPECT_NE(Vcc({ "-t", "riscv64", "-S", "-fmax-errors=x", "t.c" }), 0);
    EXPECT_NE(Stderr().find("invalid value in '-fmax-errors=x'"), std::string::npos) << Stderr();
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
    EXPECT_NE(Vcc({ "-t", "riscv64", "-Smadlen", "t.c" }), 0);
    EXPECT_NE(Stderr().find("'-Smadlen' requires target 'besm6'"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, RejectsLinkScriptForBesm6)
{
    WriteSource("t.o", "");
    EXPECT_NE(Vcc({ "-t", "besm6", "-T", "x.ld", "t.o" }), 0);
    EXPECT_NE(Stderr().find("'-T' is not supported on target 'besm6'"), std::string::npos)
        << Stderr();
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
    EXPECT_NE(Stderr().find("'t.f': unknown file type"), std::string::npos) << Stderr();
}

TEST_F(CcDriver, HelpSucceeds)
{
    EXPECT_EQ(Vcc({ "--help" }), 0);
    EXPECT_EQ(Vcc({ "-h" }), 0);
}

// An option missing its value says so, and the usage text is not dumped after it.
TEST_F(CcDriver, MissingOptionArgument)
{
    EXPECT_NE(Vcc({ "-x" }), 0);
    EXPECT_NE(Stderr().find("error: missing argument to '-x'"), std::string::npos) << Stderr();
    EXPECT_NE(Vcc({ "--target" }), 0);
    EXPECT_NE(Stderr().find("error: missing argument to '--target'"), std::string::npos)
        << Stderr();
    EXPECT_NE(Vcc({ "--bogus" }), 0);
    EXPECT_NE(Stderr().find("error: unknown option '--bogus'"), std::string::npos) << Stderr();
}

//
// Assembling and linking for RISC-V.
//
TEST_F(CcDriver, CompileObjectRiscv64)
{
    if (!HaveRiscvLink())
        GTEST_SKIP() << "RISC-V assembler/linker not found";
    WriteSource("t.c", kHello);
    WriteSource("u.S",
                "#ifdef __riscv\n"
                "        .globl  u\n"
                "u:      ret\n"
                "#endif\n");
    ASSERT_EQ(Vcc({ "-t", "riscv64", "-c", "t.c", "u.S" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("t.o")).substr(0, 4),
              "\x7f"
              "ELF");
    EXPECT_EQ(ReadFile(Path("u.o")).substr(0, 4),
              "\x7f"
              "ELF");
}

// The whole pipeline down to a qemu `virt` ELF, from the build tree: the
// standard library is named by hand, crt0.o first.
TEST_F(CcDriver, LinkAndRunRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V assembler/linker/qemu not found";
    WriteSource("t.c", kHello);
    std::string lib = RISCV_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "riscv64", "-nostdlib", "-T", RISCV_LINK_SCRIPT, "-o", "t.elf",
                    lib + "/crt0.o", "t.c", lib + "/libc.a" }),
              0)
        << Stderr();
    EXPECT_NE(access(Path("t.o").c_str(), F_OK), 0) << "the object is a temporary";
    EXPECT_EQ(RunQemu(Path("t.elf")), "hello 42\n");
}

// Separate compilation, then a link of the objects.
TEST_F(CcDriver, SeparateCompilationRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V assembler/linker/qemu not found";
    WriteSource("main.c",
                "#include <stdio.h>\n"
                "int twice(int);\n"
                "int main(void) { printf(\"%d\\n\", twice(21)); return 0; }\n");
    WriteSource("twice.c", "int twice(int x) { return 2 * x; }\n");
    ASSERT_EQ(Vcc({ "-t", "riscv64", "-c", "main.c", "twice.c" }), 0) << Stderr();
    std::string lib = RISCV_LIB_DIR;
    ASSERT_EQ(Vcc({ "-t", "riscv64", "-nostdlib", "-T", RISCV_LINK_SCRIPT, lib + "/crt0.o",
                    "main.o", "twice.o", lib + "/libc.a" }),
              0)
        << Stderr();
    EXPECT_EQ(RunQemu(Path("a.out")), "42\n");
}

// A miniature installation, with no overrides: the passes are found beside the
// driver, the headers and the runtime under ../share/vcc/riscv64.
TEST_F(CcDriver, StagedPrefixRiscv64)
{
    if (!HaveRiscvRun())
        GTEST_SKIP() << "RISC-V assembler/linker/qemu not found";
    std::string prefix = StagePrefix("riscv64");
    std::string lib    = prefix + "/share/vcc/riscv64/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(RISCV_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(RISCV_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", kHello);
    ASSERT_EQ(StagedVcc(prefix, { "-t", "riscv64", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
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
    if (!RISCV32_TOOLS_FOUND || !HaveAssembler(RISCV32_ASSEMBLER) || !HaveTool(RISCV32_LD) ||
        !HaveTool(RISCV32_QEMU) ||
        access((std::string(RISCV32_LIB_DIR) + "/libc.a").c_str(), R_OK) != 0)
        GTEST_SKIP() << "RISC-V assembler/linker/qemu-system-riscv32 not found";
    std::string prefix = StagePrefix("riscv32");
    std::string lib    = prefix + "/share/vcc/riscv32/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(RISCV32_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(RISCV_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c",
                "#include <stdio.h>\n"
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
    EXPECT_NE(echo.find(std::string(RISCV32_ASSEMBLER) + " -o "), std::string::npos) << echo;
}

// The same for aarch64, with the exit status through semihosting.
TEST_F(CcDriver, StagedPrefixAarch64)
{
    if (!HaveAarch64Run())
        GTEST_SKIP() << "AArch64 assembler/linker/qemu not found";
    std::string prefix = StagePrefix("aarch64");
    std::string lib    = prefix + "/share/vcc/aarch64/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(AARCH64_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(AARCH64_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c",
                "#include <stdio.h>\n"
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
    EXPECT_NE(echo.find(std::string(AARCH64_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
}

// The same for arm32, with the ILP32 headers after ARM's own.
TEST_F(CcDriver, StagedPrefixArm32)
{
    if (!HaveArm32Run())
        GTEST_SKIP() << "ARM32 assembler/linker/qemu not found";
    std::string prefix = StagePrefix("arm32");
    std::string lib    = prefix + "/share/vcc/arm32/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(ARM32_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(ARM32_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", R"(#include <stdio.h>
#include <limits.h>
#include <stddef.h>
int main(void)
{
    long long x = 1LL << 40;
    printf("%d %d %lld %Lg\n", (int)sizeof(long), (int)sizeof(wchar_t), x, 2.5L);
    return CHAR_MAX == 255 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "arm32", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    int status;
    EXPECT_EQ(RunQemuArm32(Path("t.elf"), &status), "4 4 1099511627776 2.5\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t arm32 -nostdinc -I" + prefix +
                        "/share/vcc/arm32/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t arm32 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenarm32 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(ARM32_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
}

// The same for wasm32: no linker script, wasm-ld and its flags, a module run by node.
TEST_F(CcDriver, StagedPrefixWasm32)
{
    if (!HaveWasm32Run())
        GTEST_SKIP() << "clang/wasm-ld/node not found";
    std::string prefix = StagePrefix("wasm32");
    std::string lib    = prefix + "/share/vcc/wasm32/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(WASM32_LIB_DIR) + "/" + name, lib + "/" + name);

    WriteSource("t.c", R"(#include <limits.h>
int main(void)
{
    return CHAR_MAX == 127 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "wasm32", "-v", "-o", "t.wasm", "t.c" }), 0) << Stderr();
    int status;
    EXPECT_EQ(RunWasm32(Path("t.wasm"), &status), "");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t wasm32 -nostdinc -I" + prefix +
                        "/share/vcc/wasm32/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t wasm32 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenwasm "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(WASM32_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(WASM32_LD) + " --stack-first -z stack-size=1048576 -o "),
              std::string::npos)
        << echo;
    EXPECT_EQ(echo.find(" -T "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
}

// The same for x86_64: the LP64 headers with x86's own, and the x87 long double.
TEST_F(CcDriver, StagedPrefixX86)
{
    if (!HaveX86Run())
        GTEST_SKIP() << "x86-64 assembler/linker/qemu not found";
    std::string prefix = StagePrefix("x86_64");
    std::string lib    = prefix + "/share/vcc/x86_64/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(X86_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(X86_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", R"(#include <stdio.h>
#include <limits.h>
#include <float.h>
int main(void)
{
    long x = 1L << 40;
    printf("%d %d %ld %Lg\n", (int)sizeof(long), (int)sizeof(long double), x, 2.5L);
    return CHAR_MIN < 0 && LDBL_MANT_DIG == 64 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "x86_64", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    int status;
    EXPECT_EQ(RunQemuX86(Path("t.elf"), &status), "8 16 1099511627776 2.5\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t x86_64 -nostdinc -I" + prefix +
                        "/share/vcc/x86_64/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t x86_64 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenx86 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(X86_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
}

// The same for AVR: its own headers alone, the 16-bit int and the binary32 double.
TEST_F(CcDriver, StagedPrefixAvr)
{
    if (!HaveAvrRun())
        GTEST_SKIP() << "AVR assembler/linker/qemu not found";
    std::string prefix = StagePrefix("avr");
    std::string lib    = prefix + "/share/vcc/avr/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(AVR_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(AVR_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("t.c", R"(#include <stdio.h>
#include <limits.h>
#include <float.h>
int main(void)
{
    long x = 1L << 20;
    printf("%d %d %ld %g\n", (int)sizeof(int), (int)sizeof(double), x, 2.5);
    return INT_MAX == 32767 && DBL_MANT_DIG == 24 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "avr", "-v", "-o", "t.elf", "t.c" }), 0) << Stderr();
    int status;
    EXPECT_EQ(RunQemuAvr(Path("t.elf"), &status), "2 4 1048576 2.5\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(
        echo.find(prefix + "/bin/vcpp -t avr -nostdinc -I" + prefix + "/share/vcc/avr/include "),
        std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t avr "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenavr "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(AVR_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -T " + lib + "/link.ld "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
}

// The same for the MSP430: its own headers ahead of the 16-bit model's, the GNU
// binutils, the link with --gc-sections, and GCC's libgcc.a last, so that an object
// GCC compiled links too (__builtin_clz is __clzhi2, which only libgcc has).
TEST_F(CcDriver, StagedPrefixMsp430)
{
    // cppcheck-suppress knownConditionTrueFalse ; MSP430_TOOLS_FOUND is per configuration
    if (!HaveMsp430Run() || !HaveTool(MSP430_GCC) || access(MSP430_LIBGCC, R_OK) != 0)
        GTEST_SKIP() << "MSP430 binutils, libgcc.a or mspsim not found";
    std::string prefix = StagePrefix("msp430");
    std::string lib    = prefix + "/share/vcc/msp430/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(MSP430_LIB_DIR) + "/" + name, lib + "/" + name);
    fs::create_symlink(MSP430_LINK_SCRIPT, lib + "/link.ld");

    WriteSource("lz.c", "int lz(unsigned x) { return __builtin_clz(x); }\n");
    ASSERT_EQ(
        RunProcess({ MSP430_GCC, "-mcpu=msp430", "-O2", "-c", "-o", Path("lz.o"), Path("lz.c") }),
        0);
    WriteSource("t.c", R"(#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>
int lz(unsigned);
int main(void)
{
    long x = 1L << 20;
    printf("%d %d %ld %g %d\n", (int)sizeof(int), (int)sizeof(wchar_t), x, 2.5, lz(0x100));
    return INT_MAX == 32767 && DBL_MANT_DIG == 53 && CHAR_MAX == 255 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "msp430", "-v", "-o", "t.elf", "t.c", "lz.o" }), 0)
        << Stderr();
    int status;
    EXPECT_EQ(RunMspsim(Path("t.elf"), &status), "2 4 1048576 2.5 7\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t msp430 -nostdinc -I" + prefix +
                        "/share/vcc/msp430/include "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t msp430 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenmsp430 "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(MSP430_ASSEMBLER) + " -o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(std::string(MSP430_LD) + " --gc-sections -T " + lib + "/link.ld "),
              std::string::npos)
        << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" lz.o -lc " + std::string(MSP430_LIBGCC) + " \n"), std::string::npos)
        << echo;
}

// The same for MMIX: its own headers ahead of the LP64 model's, the GNU MMIX binutils,
// the link with the linker's own script, and GCC's libgcc.a last, so that an object GCC
// compiled links too (__builtin_clzl is __clzdi2, which only libgcc has).
TEST_F(CcDriver, StagedPrefixMmix)
{
    // cppcheck-suppress knownConditionTrueFalse ; MMIX_TOOLS_FOUND is per configuration
    if (!HaveMmixRun() || !HaveTool(MMIX_GCC) || access(MMIX_LIBGCC, R_OK) != 0)
        GTEST_SKIP() << "mmix-knuth-mmixware-gcc/as/ld, libgcc.a or mmix not found";
    std::string prefix = StagePrefix("mmix");
    std::string lib    = prefix + "/share/vcc/mmix/lib";
    for (const char *name : { "crt0.o", "libc.a" })
        fs::create_symlink(std::string(MMIX_LIB_DIR) + "/" + name, lib + "/" + name);

    WriteSource("lz.c", "int lz(unsigned long x) { return __builtin_clzl(x); }\n");
    ASSERT_EQ(RunProcess({ MMIX_GCC, "-O2", "-c", "-o", Path("lz.o"), Path("lz.c") }), 0);
    WriteSource("t.c", R"(#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>
int lz(unsigned long);
int main(void)
{
    union { long l; char c[8]; } u = { 1 };
    printf("%d %d %d %g %d\n", (int)sizeof(long), (int)sizeof(wchar_t), u.c[7], 2.5, lz(0x100));
    return LONG_MAX == 9223372036854775807L && DBL_MANT_DIG == 53 && CHAR_MIN < 0 ? 7 : 1;
}
)");
    ASSERT_EQ(StagedVcc(prefix, { "-t", "mmix", "-v", "-o", "t.mmo", "t.c", "lz.o" }), 0)
        << Stderr();
    int status;
    EXPECT_EQ(RunMmix(Path("t.mmo"), &status), "8 4 1 2.5 55\n");
    EXPECT_EQ(status, 7);

    std::string echo = Stdout();
    EXPECT_NE(
        echo.find(prefix + "/bin/vcpp -t mmix -nostdinc -I" + prefix + "/share/vcc/mmix/include "),
        std::string::npos)
        << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vlower -t mmix "), std::string::npos) << echo;
    EXPECT_NE(echo.find(prefix + "/bin/vgenmmix "), std::string::npos) << echo;
    EXPECT_NE(echo.find("mmix-knuth-mmixware-as -x -no-predefined-syms -o "), std::string::npos)
        << echo;
    EXPECT_NE(echo.find(" -L" + lib + " " + lib + "/crt0.o "), std::string::npos) << echo;
    EXPECT_NE(echo.find(" lz.o -lc " + std::string(MMIX_LIBGCC) + " \n"), std::string::npos)
        << echo;
}

TEST_F(CcDriver, StagedPrefixMissingPass)
{
    std::string prefix = StagePrefix("riscv64");
    fs::remove(prefix + "/bin/vparse");
    WriteSource("t.c", "int x;\n");
    EXPECT_NE(StagedVcc(prefix, { "-t", "riscv64", "-S", "t.c" }), 0);
    EXPECT_NE(Stderr().find("cannot find '" + prefix + "/bin/vparse'"), std::string::npos)
        << Stderr();
}

//
// Hosted targets: assembled and linked by the system C compiler against glibc.
//
// The code generator marks the stack non-executable for Linux, and only there.
TEST_F(CcDriver, CompileToAssemblyHosted)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "x86_64-linux", "-S", "-o", "x.s", "t.c" }), 0) << Stderr();
    EXPECT_NE(ReadFile(Path("x.s")).find(".note.GNU-stack"), std::string::npos);
    ASSERT_EQ(Vcc({ "-t", "aarch64-linux", "-S", "-o", "a.s", "t.c" }), 0) << Stderr();
    EXPECT_NE(ReadFile(Path("a.s")).find(".note.GNU-stack"), std::string::npos);
    ASSERT_EQ(Vcc({ "-t", "x86_64", "-S", "-o", "bare.s", "t.c" }), 0) << Stderr();
    EXPECT_EQ(ReadFile(Path("bare.s")).find(".note.GNU-stack"), std::string::npos);
}

// macOS: Mach-O, its C names with a `_`, no ELF directives.
TEST_F(CcDriver, CompileToAssemblyDarwin)
{
    WriteSource("t.c", kHello);
    ASSERT_EQ(Vcc({ "-t", "aarch64-darwin", "-S", "-o", "d.s", "t.c" }), 0) << Stderr();
    std::string text = ReadFile(Path("d.s"));
    EXPECT_NE(text.find("\n_main:\n"), std::string::npos) << text;
    EXPECT_NE(text.find(" _printf\n"), std::string::npos) << text;
    EXPECT_NE(text.find("@PAGEOFF"), std::string::npos) << text;
    EXPECT_EQ(text.find(".note.GNU-stack"), std::string::npos) << text;
    EXPECT_EQ(text.find(".type"), std::string::npos) << text;
}

// The link line, with a stand-in linker: the C compiler gets no startup file, no
// script and no -lc, only libvcc.a after the user's libraries.
TEST_F(CcDriver, HostedLinkLine)
{
    std::string prefix = StagePrefix("x86_64-linux");
    std::string lib    = prefix + "/share/vcc/x86_64-linux/lib";
    WriteSource(lib.substr(dir.size() + 1) + "/libvcc.a", "");
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    ASSERT_EQ(StagedVcc(prefix, { "-t", "x86_64-linux", "-v", "-o", "t", "t.o", "-lm" }), 0)
        << Stderr();
    EXPECT_EQ(Stdout(), "true -no-pie -o t t.o -lm -L" + lib + " -lvcc \n");

    ASSERT_EQ(StagedVcc(prefix, { "-t", "x86_64-linux", "-v", "-nostdlib", "t.o" }), 0) << Stderr();
    EXPECT_EQ(Stdout(), "true -no-pie -nostdlib -o a.out t.o \n");
}

// macOS: a position-independent executable.
TEST_F(CcDriver, DarwinLinkLine)
{
    std::string prefix = StagePrefix("aarch64-darwin");
    std::string lib    = prefix + "/share/vcc/aarch64-darwin/lib";
    WriteSource(lib.substr(dir.size() + 1) + "/libvcc.a", "");
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    ASSERT_EQ(StagedVcc(prefix, { "-t", "aarch64-darwin", "-v", "-o", "t", "t.o", "-lm" }), 0)
        << Stderr();
    EXPECT_EQ(Stdout(), "true -o t t.o -lm -L" + lib + " -lvcc \n");

    ASSERT_EQ(StagedVcc(prefix, { "-t", "aarch64-darwin", "-v", "-nostdlib", "t.o" }), 0)
        << Stderr();
    EXPECT_EQ(Stdout(), "true -nostdlib -o a.out t.o \n");
}

TEST_F(CcDriver, HostedLinkWithoutLibvcc)
{
    std::string prefix = StagePrefix("x86_64-linux");
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    EXPECT_NE(StagedVcc(prefix, { "-t", "x86_64-linux", "t.o" }), 0);
    EXPECT_NE(Stderr().find("libvcc.a' not found"), std::string::npos) << Stderr();
}

// A staged installation for the host, with no -t: a program built by the default
// target runs natively, calling glibc through every corner of the ABI.
TEST_F(CcDriver, StagedPrefixHost)
{
    if (!HaveHostedRun())
        GTEST_SKIP() << "no hosted target for this machine, or no C compiler for it";
    std::string target = HOST_TARGET;
    std::string prefix = StagePrefix(target);
    std::string lib    = prefix + "/share/vcc/" + target + "/lib";
    StageLibvcc(prefix);

    WriteSource("main.c",
                "#include <alloca.h>\n"
                "#include <errno.h>\n"
                "#include <math.h>\n"
                "#include <setjmp.h>\n"
                "#include <stdarg.h>\n"
                "#include <stdio.h>\n"
                "#include <stdlib.h>\n"
                "#include <string.h>\n"
                "int twice(int);\n"
                "static void say(const char *fmt, ...)\n"
                "{\n"
                "    va_list ap;\n"
                "    va_start(ap, fmt);\n"
                "    vprintf(fmt, ap);\n"
                "    va_end(ap);\n"
                "}\n"
                "static int cmp(const void *a, const void *b)\n"
                "{\n"
                "    return *(const int *)a - *(const int *)b;\n"
                "}\n"
                "static jmp_buf env;\n"
                "int main(void)\n"
                "{\n"
                "    say(\"%d %s %.2f %ld\\n\", twice(21), \"str\", 2.5, 1L << 40);\n"
                "    errno = 0;\n"
                "    strtol(\"99999999999999999999\", NULL, 10);\n"
                "    printf(\"%d\\n\", errno == ERANGE);\n"
                "    int r = setjmp(env);\n"
                "    if (r == 0)\n"
                "        longjmp(env, 7);\n"
                "    int a[] = { 3, 1, 2 };\n"
                "    qsort(a, 3, sizeof a[0], cmp);\n"
                "    printf(\"%d %d%d%d\\n\", r, a[0], a[1], a[2]);\n"
                "    int (*p)(const char *) = puts;\n"
                "    p(\"puts\");\n"
                "    char *s = alloca(6);\n"
                "    strcpy(s, \"stack\");\n"
                "    p(s);\n"
                "    ldiv_t d = ldiv(-17L, 5L);\n"
                "    printf(\"%ld %ld %.4Lf %.4f\\n\", d.quot, d.rem, 1.0L / 3, sqrt(2.0));\n"
                "    return 3;\n"
                "}\n");
    WriteSource("twice.c", "int twice(int x) { return 2 * x; }\n");
    ASSERT_EQ(StagedVcc(prefix, { "-c", "twice.c" }), 0) << Stderr();
    ASSERT_EQ(StagedVcc(prefix, { "-v", "-o", "t", "main.c", "twice.o", "-lm" }), 0) << Stderr();
    EXPECT_EQ(RunProcess({ Path("t") }, Path("t.out"), Path("t.err")), 3);
    EXPECT_EQ(ReadFile(Path("t.out")),
              "42 str 2.50 1099511627776\n"
              "1\n"
              "7 123\n"
              "puts\n"
              "stack\n"
              "-3 -2 0.3333 1.4142\n");

    std::string echo = Stdout();
    EXPECT_NE(echo.find(prefix + "/bin/vcpp -t " + target + " -nostdinc -I" + prefix +
                        "/share/vcc/" + target + "/include "),
              std::string::npos)
        << echo;
    if (HostIsDarwin()) {
        EXPECT_NE(echo.find(" --darwin "), std::string::npos) << echo;
        EXPECT_EQ(echo.find(" -no-pie "), std::string::npos) << echo;
    } else {
        EXPECT_NE(echo.find(" --linux "), std::string::npos) << echo;
        EXPECT_NE(echo.find(" -no-pie -o t "), std::string::npos) << echo;
    }
    EXPECT_NE(echo.find(" twice.o -lm -L" + lib + " -lvcc \n"), std::string::npos) << echo;
}

// The hosted headers agree with the system's on what the ABI fixes: the layouts and the
// values a program hands to glibc.  The same probe is built by vcc and by the system
// compiler, and run.
TEST_F(CcDriver, HostedHeadersAgreeWithSystem)
{
    if (!HaveHostedRun())
        GTEST_SKIP() << "no hosted target for this machine, or no C compiler for it";
    std::string target = HOST_TARGET;
    std::string prefix = StagePrefix(target);
    StageLibvcc(prefix);
    WriteSource("probe.c",
                "#include <errno.h>\n"
                "#include <fenv.h>\n"
                "#include <float.h>\n"
                "#include <limits.h>\n"
                "#include <locale.h>\n"
                "#include <math.h>\n"
                "#include <setjmp.h>\n"
                "#include <signal.h>\n"
                "#include <stddef.h>\n"
                "#include <stdint.h>\n"
                "#include <stdio.h>\n"
                "#include <stdlib.h>\n"
                "#include <time.h>\n"
                "#include <wchar.h>\n"
                "#define P(x) printf(#x \" %ld\\n\", (long)(x))\n"
                "int main(void)\n"
                "{\n"
                "    P(sizeof(jmp_buf)); P(sizeof(struct tm)); P(sizeof(struct timespec));\n"
                "    P(offsetof(struct tm, tm_isdst)); P(sizeof(struct lconv));\n"
                "    P(offsetof(struct lconv, int_frac_digits));\n"
                "    P(offsetof(struct lconv, int_n_sign_posn));\n"
                "    P(sizeof(fpos_t)); P(sizeof(fenv_t)); P(sizeof(fexcept_t));\n"
                "    P(sizeof(mbstate_t)); P(sizeof(wchar_t)); P(sizeof(wint_t));\n"
                "    P(sizeof(div_t)); P(sizeof(ldiv_t)); P(sizeof(lldiv_t));\n"
                "    P(sizeof(max_align_t)); P(_Alignof(max_align_t));\n"
                "    P(sizeof(long double)); P(LDBL_MANT_DIG); P(sizeof(time_t));\n"
                "    P(sizeof(clock_t)); P(sizeof(sig_atomic_t));\n"
                "    P(EDOM); P(ERANGE); P(EILSEQ); P(EINVAL); P(ENOMEM); P(ENOENT);\n"
                "    P(EAGAIN); P(EINTR);\n"
                "    P(LC_ALL); P(LC_CTYPE); P(LC_NUMERIC); P(LC_TIME); P(LC_COLLATE);\n"
                "    P(LC_MONETARY);\n"
                "    P(EOF); P(BUFSIZ); P(FOPEN_MAX); P(FILENAME_MAX); P(L_tmpnam);\n"
                "    P(TMP_MAX); P(SEEK_SET); P(SEEK_CUR); P(SEEK_END); P(_IOFBF);\n"
                "    P(_IOLBF); P(_IONBF);\n"
                "    P(RAND_MAX); P(EXIT_FAILURE); P(MB_CUR_MAX); P(CLOCKS_PER_SEC);\n"
                "    P(TIME_UTC);\n"
                "    P(SIGINT); P(SIGILL); P(SIGABRT); P(SIGFPE); P(SIGSEGV); P(SIGTERM);\n"
                "    P(FE_INVALID); P(FE_DIVBYZERO); P(FE_OVERFLOW); P(FE_UNDERFLOW);\n"
                "    P(FE_INEXACT); P(FE_ALL_EXCEPT); P(FE_TONEAREST); P(FE_DOWNWARD);\n"
                "    P(FE_UPWARD); P(FE_TOWARDZERO);\n"
                "    P(FP_NAN); P(FP_INFINITE); P(FP_ZERO); P(FP_SUBNORMAL); P(FP_NORMAL);\n"
                "    P(math_errhandling); P(fpclassify(1.0)); P(fpclassify(0.0f));\n"
                "    P(isnan(NAN)); P(isinf(-INFINITY)); P(!!signbit(-0.0)); P(isfinite(1.0L));\n"
                "    P(CHAR_MIN); P(WCHAR_MIN); P(WCHAR_MAX); P(MB_LEN_MAX);\n"
                "    return 0;\n"
                "}\n");
    ASSERT_EQ(StagedVcc(prefix, { "-o", "ours", "probe.c", "-lm" }), 0) << Stderr();
    ASSERT_EQ(RunProcess({ HostCc(), "-std=c11", "-o", Path("theirs"), Path("probe.c"), "-lm" },
                         Path("cc.out"), Path("cc.err")),
              0)
        << ReadFile(Path("cc.err"));
    ASSERT_EQ(RunProcess({ Path("ours") }, Path("ours.out")), 0);
    ASSERT_EQ(RunProcess({ Path("theirs") }, Path("theirs.out")), 0);
    std::string ours = ReadFile(Path("ours.out"));
    EXPECT_FALSE(ours.empty());
    EXPECT_EQ(ours, ReadFile(Path("theirs.out")));
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
    std::string lib    = prefix + "/share/vcc/besm6/lib";
    WriteSource(lib.substr(dir.size() + 1) + "/crt0.o", "");
    WriteSource("t.o", "");
    setenv("VCC_LD", "true", 1);
    ASSERT_EQ(StagedVcc(prefix, { "-t", "besm6", "-v", "-o", "t.b6", "t.o", "-lm" }), 0)
        << Stderr();
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
