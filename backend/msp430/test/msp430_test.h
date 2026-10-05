// Fixture for the MSP430 code generator tests: compile C in-process (the frontend lowered
// for msp430), take the assembly, and optionally run it on the mspsim simulator
// (qemu_test.h), main's result coming back as mspsim's exit status.  Programs are
// assembled and linked with the GNU MSP430 toolchain, GCC being the oracle; clang and
// ld.lld, when present, are a second toolchain.
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "qemu_test.h"

extern "C" {
#include "codegen.h"
}

// The MSP430 tools, from CMake; a missing one names a path that does not exist.
inline bool msp430_tools_available()
{
    return MSP430_TOOLS_FOUND && tool_available(MSP430_GCC) && tool_available(MSP430_LD) &&
           tool_available(MSPSIM);
}

inline bool msp430_clang_available()
{
    return msp430_tools_available() && MSP430_CLANG_FOUND && tool_available(MSP430_CLANG) &&
           tool_available(MSP430_LLD);
}

// Skip a run test when the GNU MSP430 toolchain or mspsim is absent.
#define SKIP_IF_NO_MSP430_TOOLS()                                                        \
    do {                                                                                 \
        if (!msp430_tools_available())                                                   \
            GTEST_SKIP() << "msp430-elf-gcc/ld or mspsim not found; skipping run test"; \
    } while (0)

// Skip a test that needs clang's MSP430 target and ld.lld as well.
#define SKIP_IF_NO_MSP430_CLANG()                                                  \
    do {                                                                           \
        if (!msp430_clang_available())                                             \
            GTEST_SKIP() << "MSP430 clang/ld.lld not found; skipping clang test"; \
    } while (0)

// The cycle limit of a run: about four seconds of mspsim, below the fixture's
// five-second wall-clock backstop.
#define MSP430_CYCLE_LIMIT "200000000"

// The run configurations.  The target flags are MSP430_TARGET_FLAGS of
// libc/msp430/CMakeLists.txt.  mspsim exits with main's result, which its
// "[Exit code N ...]" line confirms.
//
// GCC: msp430-elf-gcc assembles our output and compiles the C parts, msp430-elf-ld links,
// and libgcc.a follows our libc.a for the helpers only GCC's code calls.  Both links
// drop the sections nothing reaches: genmsp430 gives every function and variable one.
inline QemuConfig msp430_gcc_config()
{
    return { "msp430-tests",
             MSP430_GCC,
             { "-mcpu=msp430" },
             { "-ffreestanding", "-fno-builtin" },
             MSP430_LD,
             MSP430_LINK_SCRIPT,
             MSP430_LIB_DIR,
             { MSPSIM, "-n", MSP430_CYCLE_LIMIT },
             "",
             false,
             "",
             false,
             { "--gc-sections" },
             true,
             { MSP430_LIBGCC } };
}

// clang: its assembler and ld.lld, whose -n keeps the ELF header out of the peripheral
// area.
inline QemuConfig msp430_clang_config()
{
    return { "msp430-tests",
             MSP430_CLANG,
             { "--target=msp430" },
             { "-ffreestanding", "-fno-builtin" },
             MSP430_LLD,
             MSP430_LINK_SCRIPT,
             MSP430_LIB_DIR,
             { MSPSIM, "-n", MSP430_CYCLE_LIMIT },
             "",
             false,
             "",
             false,
             { "-n", "--gc-sections" },
             true };
}

class Msp430Test : public QemuTest {
protected:
    Msp430Test() : QemuTest("msp430", msp430_gcc_config()) {}

    // Assembly of every toplevel of the translation unit.
    std::string CompileToMsp430(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            msp430_codegen(all, t, f);
        tac_free_toplevel(all);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        if (msp430_clang_available())
            AssembleWithClang(s);
        return s;
    }

    // Our output is portable between the two assemblers: clang's must take it too.
    void AssembleWithClang(const std::string &asm_text)
    {
        std::string base = QemuScratchPath(".clang-as");
        {
            std::ofstream f(base + ".s");
            f << asm_text;
        }
        EXPECT_EQ(0, RunTool({ MSP430_CLANG, "--target=msp430", "-c", "-o", base + ".o",
                               base + ".s" },
                             base + ".log"))
            << "clang's assembler rejects our output:\n"
            << ReadFile(base + ".log");
    }

    // Instruction lines of `asm_text`, unindented and with one space after the
    // mnemonic, so a test can look for a sequence: Code(s).find("mov #2, r12\nret\n").
    static std::string Code(const std::string &asm_text)
    {
        std::string out;
        size_t pos = 0;
        while (pos < asm_text.size()) {
            size_t nl        = asm_text.find('\n', pos);
            std::string line = asm_text.substr(pos, nl - pos);
            pos              = nl == std::string::npos ? asm_text.size() : nl + 1;
            if (line.compare(0, 4, "    ") != 0 || line[4] == '.')
                continue;
            line      = line.substr(4);
            size_t sp = line.find(' ');
            if (sp != std::string::npos)
                line = line.substr(0, sp + 1) + line.substr(line.find_first_not_of(' ', sp));
            out += line + "\n";
        }
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunMsp430(const std::string &src)
    {
        return Run(CompileToMsp430(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToMsp430(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program written in C for GCC (-O1) on our runtime, with optional
    // hand-written or genmsp430 assembly.
    std::string GccRun(const std::string &src, const std::string &asm_text = "")
    {
        return Run(asm_text, "crt0.o", &src, { "-O1" }, ".gcc");
    }

    // The same with clang (-O1) and ld.lld.
    std::string ClangRun(const std::string &src, const std::string &asm_text = "")
    {
        return Run(msp430_clang_config(), asm_text, "crt0.o", &src, { "-O1" }, ".clang");
    }

    // Run a book program compiled by clang (-O0 by default) with the target headers.
    std::string ClangRunBook(const std::string &src, const char *opt = "-O0")
    {
        return Run(msp430_clang_config(), "", "crt0-status.o", &src,
                   { opt, "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I",
                     TEST_MODEL_INCLUDE_DIR, "-I", TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }

    // Run a program built wholly by GCC: msp430-elf-gcc -msim links newlib, its startup
    // and msp430-sim.ld, and mspsim serves newlib's host I/O.  `objs` are more objects
    // (ours, say) to link in.  Returns stdout, with exit()'s status in exit_status.
    std::string NewlibRun(const std::string &src, const std::vector<std::string> &flags,
                          const std::vector<std::string> &objs = {},
                          const std::string &extra_src = "", const char *tag = ".newlib")
    {
        exit_status          = -1;
        std::string base     = QemuScratchPath(tag);
        std::string c_path   = base + ".c";
        std::string x_path   = base + "-extra.c";
        std::string exe_path = base + ".elf";
        std::string out_path = base + ".out";
        std::string log_path = base + ".log";
        FlockGuard lock(c_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent msp430-tests run detected (" << c_path << ")";
            return "ERROR";
        }
        {
            std::ofstream c(c_path);
            c << src;
        }
        std::vector<std::string> cc = { MSP430_GCC, "-mcpu=msp430", "-msim" };
        cc.insert(cc.end(), flags.begin(), flags.end());
        cc.insert(cc.end(), { "-o", exe_path, c_path });
        if (!extra_src.empty()) {
            std::ofstream x(x_path);
            x << extra_src;
            cc.push_back(x_path);
        }
        cc.insert(cc.end(), objs.begin(), objs.end());
        cc.push_back("-lm");
        int rc = RunTool(cc, log_path);
        EXPECT_EQ(0, rc) << "msp430-elf-gcc failed on " << c_path << ":\n" << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        rc = RunWithTimeout({ MSPSIM, "-n", MSP430_CYCLE_LIMIT, exe_path }, out_path, log_path, 5);
        if (rc < 0 || ReadFile(log_path).find("[Exit code ") == std::string::npos) {
            ADD_FAILURE() << "mspsim did not finish " << exe_path << " (status " << rc << "):\n"
                          << ReadFile(log_path);
            return "ERROR";
        }
        exit_status = rc;
        return ReadFile(out_path);
    }

    // Run a program on our libc.a, and once more built by GCC (-O1 -fno-builtin) with
    // newlib: the two outputs and results must agree.  Returns ours, with main's result in
    // exit_status.
    std::string RunAgainstNewlib(const std::string &src)
    {
        std::string ours = CompileAndRunMsp430(src);
        int status       = exit_status;
        EXPECT_EQ(ours, NewlibRun(src, { "-O1", "-fno-builtin", "-w" })) << "newlib disagrees";
        EXPECT_EQ(status, exit_status) << "newlib's result disagrees";
        exit_status = status;
        return ours;
    }

    // Run a book program built by GCC (-O0) with newlib, its result printed as "%d\n" as
    // crt0-status does for ours: --wrap=main sends newlib's startup to a wrapper, which
    // calls the program's own main (so its implicit "return 0" holds).  An exit() call
    // prints nothing, as in ours.  putch is ours, not newlib's: a weak one stands in.
    std::string GccRunBook(const std::string &src)
    {
        static const char wrapper[] = R"(#include <stdio.h>
int __real_main(void);
__attribute__((weak)) int putch(int c)
{
    return putchar(c);
}
int __wrap_main(void)
{
    int status = __real_main();
    printf("%d\n", status);
    return status;
}
)";
        return NewlibRun(src, { "-O0", "-w", "-Wl,--wrap=main" }, {}, wrapper, ".gcc");
    }
};

// A golden test of the instruction lines of a translation unit's one function (each
// test compiles one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)                \
    TEST_F(Msp430Test, name)                            \
    {                                                   \
        EXPECT_EQ(expected, Code(CompileToMsp430(src))); \
    }
