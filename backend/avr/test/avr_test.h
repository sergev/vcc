// Fixture for the AVR code generator tests: compile C in-process (the frontend lowered
// for avr), take the assembly, and optionally run it on bare-metal qemu `arduino-mega`
// (qemu_test.h), main's result coming back on the second serial port.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "qemu_test.h"

extern "C" {
#include "codegen.h"
}

// The AVR tools, from CMake; a missing one names a path that does not exist.
inline bool avr_tools_available()
{
    return AVR_TOOLS_FOUND && command_available(AVR_ASSEMBLER) && tool_available(AVR_LLD) &&
           tool_available(AVR_QEMU);
}

// Skip a run test when the AVR toolchain or qemu is absent.
#define SKIP_IF_NO_AVR_TOOLS()                                                        \
    do {                                                                              \
        if (!avr_tools_available())                                                   \
            GTEST_SKIP() << "AVR assembler/linker/qemu not found; skipping run test"; \
    } while (0)

// clang, the reference compiler, when it has the target.
inline bool avr_clang_available()
{
    // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
    return avr_tools_available() && AVR_CLANG_FOUND && tool_available(AVR_CLANG);
}

// Skip a test that compiles C with clang.
#define SKIP_IF_NO_AVR_CLANG()                                          \
    do {                                                                \
        if (!avr_clang_available())                                     \
            GTEST_SKIP() << "AVR clang not found; skipping clang test"; \
    } while (0)

class AvrTest : public QemuTest {
protected:
    // The target flags are AVR_TARGET_FLAGS of libc/avr/CMakeLists.txt.  qemu has no
    // way to exit on AVR: main's result goes out on USART1, and the run ends with it.
    AvrTest()
        : QemuTest("avr", cross_tools({ "avr-tests",
                                        AVR_CLANG,
                                        { "--target=avr", "-mmcu=atmega1280" },
                                        { "-ffreestanding", "-fno-builtin" },
                                        AVR_LLD,
                                        AVR_LINK_SCRIPT,
                                        AVR_LIB_DIR,
                                        { AVR_QEMU, "-M", "arduino-mega", "-display", "none",
                                          "-monitor", "none", "-serial", "stdio" },
                                        "",
                                        false,
                                        "-bios",
                                        true },
                                      AVR_ASSEMBLER, AVR_LINK_FLAGS))
    {
        // The defaults; a test may change them.
        avr_regalloc = true;
        avr_peephole = true;
    }

    // Pin instruction selection itself: every variable in its slot, no peephole pass.
    static void NaiveSelection()
    {
        avr_regalloc = false;
        avr_peephole = false;
    }

    // Assembly of every toplevel of the translation unit.
    std::string CompileToAvr(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            avr_codegen(all, t, f);
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
    // mnemonic, so a test can look for a sequence: Code(s).find("ldi r24, 2\nret\n").
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

    // The instruction lines of Code(asm_text) without the prologue and the epilogue of
    // its one function (a test of the frame looks at Code).
    static std::string Body(const std::string &asm_text)
    {
        static const char *const frame[] = {
            "push r", "pop r", "in r28, ", "in r29, ", "sbiw r28, ", "adiw r28, ", "subi r28, ",
            "sbci r29, ", "in r0, __SREG__", "cli", "out __SP_", "out __SREG__", "ret", "rcall .",
        };
        auto is_frame = [](const std::string &line) {
            return std::any_of(std::begin(frame), std::end(frame), [&line](const char *f) {
                return line.compare(0, strlen(f), f) == 0;
            });
        };
        std::string code = Code(asm_text);
        std::vector<std::string> lines;
        for (size_t pos = 0, nl; pos < code.size(); pos = nl + 1) {
            nl = code.find('\n', pos);
            lines.push_back(code.substr(pos, nl - pos));
        }
        size_t first = 0, end = lines.size();
        while (first < end && is_frame(lines[first]))
            first++;
        while (end > first && is_frame(lines[end - 1]))
            end--;
        std::string out;
        for (size_t i = first; i < end; i++)
            out += lines[i] + "\n";
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunAvr(const std::string &src)
    {
        return Run(CompileToAvr(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToAvr(src.c_str()), "crt0-status.o");
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

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToAvr(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
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

// A golden test of the body of the one function of a translation unit (each test
// compiles one: the fixture's symbol table lives per test).
#define EXPECT_CODE(name, expected, src)              \
    TEST_F(AvrTest, name)                             \
    {                                                 \
        NaiveSelection();                             \
        EXPECT_EQ(expected, Body(CompileToAvr(src))); \
    }
