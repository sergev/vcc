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
    return X86_TOOLS_FOUND && tool_available(X86_CLANG) && tool_available(X86_LD) &&
           tool_available(X86_QEMU);
}

// Skip a run test when the x86-64 toolchain or qemu is absent.
#define SKIP_IF_NO_X86_TOOLS()                                                       \
    do {                                                                             \
        if (!x86_tools_available())                                                  \
            GTEST_SKIP() << "x86-64 clang/ld.lld/qemu not found; skipping run test"; \
    } while (0)

class X86Test : public QemuTest {
protected:
    // The target flags are X86_TARGET_FLAGS of libc/x86/CMakeLists.txt.
    X86Test()
        : QemuTest("x86_64", { "x86-tests",
                               X86_CLANG,
                               { "--target=x86_64-none-elf" },
                               { "-ffreestanding", "-fno-builtin",
                                 "-fno-asynchronous-unwind-tables" },
                               X86_LD,
                               X86_LINK_SCRIPT,
                               X86_LIB_DIR,
                               { X86_QEMU, "-M", "microvm", "-display", "none", "-serial",
                                 "stdio", "-monitor", "none", "-device",
                                 "isa-debug-exit,iobase=0xf4,iosize=0x04" },
                               "",
                               true })
    {
        x86_regalloc = true; // the default; a test may change it
    }

    // Pin instruction selection itself: every variable in its slot.
    static void NaiveSelection()
    {
        x86_regalloc = false;
    }

    // Assembly of every toplevel of the translation unit.  When GNU as is installed,
    // it must accept the output too, so that nothing comes to depend on clang's
    // assembler.
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

    void CheckGnuAs(const std::string &asm_text)
    {
        if (!tool_available(X86_GNU_AS))
            return;
        std::string s_path = QemuScratchPath(".gas.s"), log_path = QemuScratchPath(".gas.log");
        FILE *f            = fopen(s_path.c_str(), "w");
        ASSERT_NE(nullptr, f);
        fputs(asm_text.c_str(), f);
        fclose(f);
        int rc = RunTool({ X86_GNU_AS, "--64", "-o", "/dev/null", s_path }, log_path);
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
                line = line.substr(0, sp + 1) + line.substr(line.find_first_not_of(' ', sp));
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
