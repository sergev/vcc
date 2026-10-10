// Fixture for the x86-64 code generator tests: compile C in-process (the frontend
// lowered for x86_64), take the assembly, and optionally run it on bare-metal qemu
// `microvm` (qemu_test.h), main's result coming back through the debug console.
#pragma once

#include <cstdio>
#include <string>

#include "qemu_test.h"

extern "C" {
#include "codegen.h"
}

// The x86-64 tools, from CMake; a missing one names a path that does not exist.
inline bool x86_tools_available()
{
    return X86_TOOLS_FOUND && command_available(X86_ASSEMBLER) && tool_available(X86_LD) &&
           tool_available(X86_QEMU);
}

// Skip a run test when the x86-64 toolchain or qemu is absent.
#define SKIP_IF_NO_X86_TOOLS()                                                           \
    do {                                                                                 \
        if (!x86_tools_available())                                                      \
            GTEST_SKIP() << "x86-64 assembler/linker/qemu not found; skipping run test"; \
    } while (0)

// clang, the reference compiler, when it has the target.
inline bool x86_clang_available()
{
    // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
    return x86_tools_available() && X86_CLANG_FOUND && tool_available(X86_CLANG);
}

// Skip a test that compiles C with clang.
#define SKIP_IF_NO_X86_CLANG()                                             \
    do {                                                                   \
        if (!x86_clang_available())                                        \
            GTEST_SKIP() << "x86-64 clang not found; skipping clang test"; \
    } while (0)

class X86Test : public QemuTest {
protected:
    // The target flags are X86_TARGET_FLAGS of libc/x86/CMakeLists.txt.
    X86Test()
        : QemuTest(
              "x86_64",
              cross_tools(
                  { "x86-tests",
                    X86_CLANG,
                    { "--target=x86_64-none-elf" },
                    { "-ffreestanding", "-fno-builtin", "-fno-asynchronous-unwind-tables" },
                    X86_LD,
                    X86_LINK_SCRIPT,
                    X86_LIB_DIR,
                    { X86_QEMU, "-M", "microvm", "-display", "none", "-serial", "stdio", "-monitor",
                      "none", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04" },
                    "",
                    true },
                  X86_ASSEMBLER, X86_LINK_FLAGS))
    {
        // The defaults; a test may change them.
        x86_regalloc      = true;
        x86_frame_pointer = false;
        x86_peephole      = true;
    }

    // Pin instruction selection itself: every variable in its slot, rbp the frame
    // pointer in every function that needs a frame, no peephole pass.
    static void NaiveSelection()
    {
        x86_regalloc      = false;
        x86_frame_pointer = true;
        x86_peephole      = false;
    }

    // Assembly of every toplevel of the translation unit.  When GNU as is the
    // assembler, it must accept the output even of a test that does not run it.
    std::string CompileToX86(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            x86_codegen(all, t, f);
        tac_free_toplevel(all);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        CheckGnuAs(s);
        return s;
    }

    // GNU as, when it is the assembler CMake found, must accept every output, the golden
    // ones included.
    void CheckGnuAs(const std::string &asm_text)
    {
        if (!X86_GNU || !command_available(X86_ASSEMBLER))
            return;
        std::string s_path = QemuScratchPath(".gas.s"), log_path = QemuScratchPath(".gas.log");
        FILE *f = fopen(s_path.c_str(), "w");
        ASSERT_NE(nullptr, f);
        fputs(asm_text.c_str(), f);
        fclose(f);
        std::vector<std::string> as = split_words(X86_ASSEMBLER);
        as.insert(as.end(), { "-o", "/dev/null", s_path });
        int rc = RunTool(as, log_path);
        EXPECT_EQ(0, rc) << "GNU as rejects the output:\n" << ReadFile(log_path);
    }

    // Instruction lines of `asm_text`, unindented and with one space after the
    // mnemonic, so a test can look for a sequence: Code(s).find("movl $2, %eax\nret\n").
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
                line.erase(sp + 1, line.find_first_not_of(' ', sp) - (sp + 1));
            out += line + "\n";
        }
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunX86(const std::string &src)
    {
        return Run(CompileToX86(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToX86(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToX86(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
    }

    // Run a book program compiled by clang -O0 with the target headers.
    std::string ClangRunBook(const std::string &src)
    {
        return Run("", "crt0-status.o", &src,
                   { "-O0", "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I",
                     TEST_MODEL_INCLUDE_DIR, "-I", TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }
};

// A golden test of the instruction lines of one translation unit, under naive
// selection (each test compiles one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)              \
    TEST_F(X86Test, name)                             \
    {                                                 \
        NaiveSelection();                             \
        EXPECT_EQ(expected, Code(CompileToX86(src))); \
    }
