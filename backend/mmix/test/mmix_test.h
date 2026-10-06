// Fixture for the MMIX code generator tests: compile C in-process (the frontend lowered
// for mmix), take the assembly, and optionally run it on Knuth's simulator mmix
// (qemu_test.h), main's result coming back as mmix's exit status.  Programs are
// assembled and linked with the GNU MMIX toolchain; GCC with newlib is the oracle.
#pragma once

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "qemu_test.h"

extern "C" {
#include "codegen.h"
}

// The MMIX tools, from CMake; a missing one names a path that does not exist.
inline bool mmix_tools_available()
{
    return MMIX_TOOLS_FOUND && tool_available(MMIX_AS) && tool_available(MMIX_LD) &&
           tool_available(MMIX_GCC) && tool_available(MMIX_SIM);
}

// Skip a run test when the GNU MMIX toolchain or mmix is absent.
#define SKIP_IF_NO_MMIX_TOOLS()                                                           \
    do {                                                                                  \
        if (!mmix_tools_available())                                                      \
            GTEST_SKIP() << "mmix-knuth-mmixware-as/ld/gcc or mmix not found; skipping"; \
    } while (0)

// The wall-clock limit of a run, mmix's only one: about 350 M instructions at the 14 M
// per second measured here.  Chapter 13's DoubleAndIntParamsRecursive runs 88 M (7 s,
// GCC's build as long), which a loaded machine stretches past 10 s.
#define MMIX_TIMEOUT 25

// The run configuration: GNU as with the flags GCC passes it assembles our output, GCC
// compiles the C parts, and ld links from 0x100 (MMIX_LD_FLAGS of
// libc/mmix/CMakeLists.txt) into a .mmo with its default script.  mmix exits with $255
// of the final trap, which crt0 sets to main's result; the "[exit N]" report of our
// exit tells that from a jump into zeroed memory.
inline QemuConfig mmix_config()
{
    QemuConfig c = { "mmix-tests",
                     MMIX_GCC,
                     {},
                     { "-ffreestanding", "-fno-builtin" },
                     MMIX_LD,
                     "",
                     MMIX_LIB_DIR,
                     { MMIX_SIM, "-q" },
                     "" };
    c.image_option = "";
    c.link_flags   = { "--defsym", "__.MMIX.start..text=0x100" };
    c.exit_report  = "[exit ";
    c.assembler    = { MMIX_AS, "-x", "-no-predefined-syms" };
    c.timeout      = MMIX_TIMEOUT;
    return c;
}

class MmixTest : public QemuTest {
protected:
    MmixTest() : QemuTest("mmix", mmix_config()) {}

    // Assembly of every toplevel of the translation unit.  When GNU as is installed, it
    // must take the output too.
    std::string CompileToMmix(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            mmix_codegen(all, t, f);
        tac_free_toplevel(all);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        if (mmix_tools_available())
            Assemble(s);
        return s;
    }

    void Assemble(const std::string &asm_text)
    {
        std::string base = QemuScratchPath(".as");
        {
            std::ofstream f(base + ".s");
            f << asm_text;
        }
        EXPECT_EQ(0, RunTool({ MMIX_AS, "-x", "-no-predefined-syms", "-o", base + ".o",
                               base + ".s" },
                             base + ".log"))
            << "GNU as rejects the output:\n"
            << ReadFile(base + ".log");
    }

    // Instruction lines of `asm_text`, without the leading tab and with one space after
    // the mnemonic, so a test can look for a sequence: Code(s).find("setl $0,#c8\n").
    static std::string Code(const std::string &asm_text)
    {
        std::string out;
        size_t pos = 0;
        while (pos < asm_text.size()) {
            size_t nl        = asm_text.find('\n', pos);
            std::string line = asm_text.substr(pos, nl - pos);
            pos              = nl == std::string::npos ? asm_text.size() : nl + 1;
            if (line.size() < 2 || line[0] != '\t' || line[1] == '.')
                continue;
            line      = line.substr(1);
            size_t tab = line.find('\t');
            if (tab != std::string::npos)
                line[tab] = ' ';
            out += line + "\n";
        }
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunMmix(const std::string &src)
    {
        return Run(CompileToMmix(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToMmix(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program built wholly by GCC: mmix-knuth-mmixware-gcc links newlib and its
    // startup, and mmix serves newlib's simulator calls.  `extra_src` is more C to link
    // in.  Returns stdout, with exit()'s status in exit_status.  newlib prints no exit
    // report, so the run is judged on its output and status alone.
    std::string NewlibRun(const std::string &src, const std::vector<std::string> &flags,
                          const std::string &extra_src = "", const char *tag = ".newlib")
    {
        exit_status          = -1;
        std::string base     = QemuScratchPath(tag);
        std::string c_path   = base + ".c";
        std::string exe_path = base + ".mmo";
        std::string out_path = base + ".out";
        std::string log_path = base + ".log";
        FlockGuard lock(c_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent mmix-tests run detected (" << c_path << ")";
            return "ERROR";
        }
        {
            std::ofstream c(c_path);
            c << src;
        }
        std::vector<std::string> cc = { MMIX_GCC };
        cc.insert(cc.end(), flags.begin(), flags.end());
        cc.insert(cc.end(), { "-o", exe_path, c_path });
        if (!extra_src.empty()) {
            std::string x_path = base + "-extra.c";
            std::ofstream x(x_path);
            x << extra_src;
            cc.push_back(x_path);
        }
        cc.push_back("-lm");
        int rc = RunTool(cc, log_path);
        EXPECT_EQ(0, rc) << "mmix-knuth-mmixware-gcc failed on " << c_path << ":\n"
                         << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        rc = RunWithTimeout({ MMIX_SIM, "-q", exe_path }, out_path, log_path, MMIX_TIMEOUT);
        if (rc < 0) {
            ADD_FAILURE() << "mmix did not finish " << exe_path << " (status " << rc << "):\n"
                          << ReadFile(log_path);
            return "ERROR";
        }
        exit_status = rc;
        return ReadFile(out_path);
    }

    // Run C compiled by GCC on our runtime: our crt0.o and libc.a, then GCC's libgcc.a.
    // `asm_text`, ours, is linked in too unless empty.
    std::string GccOnOurRuntime(const std::string &gcc_src, const std::string &asm_text = "",
                                const std::vector<std::string> &flags = { "-O2" })
    {
        QemuConfig cfg = mmix_config();
        cfg.extra_libs = { MMIX_LIBGCC };
        return Run(cfg, asm_text, "crt0.o", &gcc_src, flags, ".gcc");
    }

    // Run our code under newlib: `asm_text` assembled, then linked by
    // mmix-knuth-mmixware-gcc with newlib, its startup and `gcc_src`.
    std::string NewlibRunOurs(const std::string &asm_text, const std::string &gcc_src = "")
    {
        std::string base = QemuScratchPath(".ours");
        {
            std::ofstream f(base + ".s");
            f << asm_text;
        }
        int rc = RunTool({ MMIX_AS, "-x", "-no-predefined-syms", "-o", base + ".o", base + ".s" },
                         base + ".log");
        EXPECT_EQ(0, rc) << "GNU as rejects the output:\n" << ReadFile(base + ".log");
        if (rc != 0)
            return "ERROR";
        return NewlibRun(gcc_src, { "-O2", base + ".o" });
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
        return NewlibRun(src, { "-O0", "-w", "-Wl,--wrap=main" }, wrapper, ".gcc");
    }
};

// A golden test of the instruction lines of a translation unit's one function (each
// test compiles one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)              \
    TEST_F(MmixTest, name)                            \
    {                                                 \
        EXPECT_EQ(expected, Code(CompileToMmix(src))); \
    }
