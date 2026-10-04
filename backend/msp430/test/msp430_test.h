// Fixture for the MSP430 code generator tests: compile C in-process (the frontend lowered
// for msp430), take the assembly, and optionally run it on the mspsim simulator
// (qemu_test.h), main's result coming back as mspsim's exit status.
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
    return MSP430_TOOLS_FOUND && tool_available(MSP430_CLANG) && tool_available(MSP430_LLD) &&
           tool_available(MSPSIM);
}

// Skip a run test when the MSP430 toolchain or mspsim is absent.
#define SKIP_IF_NO_MSP430_TOOLS()                                                       \
    do {                                                                                \
        if (!msp430_tools_available())                                                  \
            GTEST_SKIP() << "MSP430 clang/ld.lld/mspsim not found; skipping run test"; \
    } while (0)

// The cycle limit of a run: about four seconds of mspsim, below the fixture's
// five-second wall-clock backstop.
#define MSP430_CYCLE_LIMIT "200000000"

class Msp430Test : public QemuTest {
protected:
    // The target flags are MSP430_TARGET_FLAGS of libc/msp430/CMakeLists.txt.  mspsim
    // exits with main's result, which its "[Exit code N ...]" line confirms; ld.lld -n
    // keeps the ELF header out of the peripheral area.
    Msp430Test()
        : QemuTest("msp430", { "msp430-tests",
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
                               { "-n" },
                               true })
    {
    }

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
        return s;
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

    // Run a program written in C for clang (-O1), with optional hand-written assembly.
    std::string ClangRun(const std::string &src, const std::string &asm_text = "")
    {
        return Run(asm_text, "crt0.o", &src, { "-O1" }, ".clang");
    }

    // Run a book program compiled by clang (-O0 by default) with the target headers.
    std::string ClangRunBook(const std::string &src, const char *opt = "-O0")
    {
        return Run("", "crt0-status.o", &src,
                   { opt, "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I",
                     TEST_MODEL_INCLUDE_DIR, "-I", TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }
};

// A golden test of the instruction lines of a translation unit's one function (each
// test compiles one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)                \
    TEST_F(Msp430Test, name)                            \
    {                                                   \
        EXPECT_EQ(expected, Code(CompileToMsp430(src))); \
    }
