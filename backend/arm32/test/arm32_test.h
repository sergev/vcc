// ARM32 fixture: compile C source to assembly in-process, and run it on bare-metal
// qemu `virt` in SVC mode (qemu_test.h), its exit status through semihosting.
#pragma once

#include <string>

#include "codegen.h"
#include "qemu_test.h"

// The ARM32 tools, from CMake; a missing one names a path that does not exist.
inline bool arm32_tools_available()
{
    return ARM32_TOOLS_FOUND && command_available(ARM32_ASSEMBLER) && tool_available(ARM32_LD) &&
           tool_available(ARM32_QEMU);
}

// Skip a run test when the ARM32 toolchain or qemu is absent.
#define SKIP_IF_NO_ARM32_TOOLS()                                                        \
    do {                                                                                \
        if (!arm32_tools_available())                                                   \
            GTEST_SKIP() << "ARM32 assembler/linker/qemu not found; skipping run test"; \
    } while (0)

// clang, the reference compiler, when it has the target.
inline bool arm32_clang_available()
{
    // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
    return arm32_tools_available() && ARM32_CLANG_FOUND && tool_available(ARM32_CLANG);
}

// Skip a test that compiles C with clang.
#define SKIP_IF_NO_ARM32_CLANG()                                          \
    do {                                                                  \
        if (!arm32_clang_available())                                     \
            GTEST_SKIP() << "ARM32 clang not found; skipping clang test"; \
    } while (0)

class Arm32Test : public QemuTest {
protected:
    // The target flags are ARM32_TARGET_FLAGS of libc/arm32/CMakeLists.txt.
    Arm32Test()
        : QemuTest("arm32",
                   cross_tools(
                       { "arm32-tests",
                         ARM32_CLANG,
                         { "--target=armv7a-none-eabihf", "-mcpu=cortex-a15", "-mfpu=vfpv3-d16" },
                         { "-ffreestanding", "-fno-builtin" },
                         ARM32_LD,
                         ARM32_LINK_SCRIPT,
                         ARM32_LIB_DIR,
                         { ARM32_QEMU, "-M", "virt", "-cpu", "cortex-a15", "-display", "none",
                           "-serial", "stdio", "-monitor", "none", "-semihosting" },
                         "" },
                       ARM32_ASSEMBLER, ARM32_LINK_FLAGS))
    {
        // The defaults; a test may change them.
        arm32_regalloc      = true;
        arm32_frame_pointer = false;
        arm32_peephole      = true;
    }

    // Pin instruction selection itself: every variable in its slot, the frame addressed
    // from r11, no peephole pass.
    static void NaiveSelection()
    {
        arm32_regalloc      = false;
        arm32_frame_pointer = true;
        arm32_peephole      = false;
    }

    // Assembly of every toplevel of the translation unit.
    std::string CompileToArm32(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            arm32_codegen(all, t, f);
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
    // mnemonic, so a test can look for a sequence: Code(s).find("mov r0, #2\nbx lr\n").
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
                line.erase(sp + 1, line.find_first_not_of(' ', sp) - sp - 1);
            out += line + "\n";
        }
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunArm32(const std::string &src)
    {
        return Run(CompileToArm32(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToArm32(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToArm32(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
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

// A golden test of the instruction lines of one translation unit (each test compiles
// one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)                \
    TEST_F(Arm32Test, name)                             \
    {                                                   \
        NaiveSelection();                               \
        EXPECT_EQ(expected, Code(CompileToArm32(src))); \
    }
