// RISC-V fixture: compile C source to assembly in-process, and run it on bare-metal
// qemu `virt` (qemu_test.h).
//
// The width comes from RISCV_TEST_XLEN (64 by default): riscv32-tests is built from
// the same sources with 32, and with the riscv32 headers, runtime and qemu.
#pragma once

#include <string>

#include "codegen.h"
#include "qemu_test.h"

#ifndef RISCV_TEST_XLEN
#define RISCV_TEST_XLEN 64
#endif
#if RISCV_TEST_XLEN == 32
#define RISCV_TEST_TARGET "riscv32"
#define RISCV_TEST_MARCH  "-march=rv32imfd"
#define RISCV_TEST_MABI   "-mabi=ilp32d"
#else
#define RISCV_TEST_TARGET "riscv64"
#define RISCV_TEST_MARCH  "-march=rv64imfd"
#define RISCV_TEST_MABI   "-mabi=lp64d"
#endif

// The RISC-V tools, from CMake; a missing one names a path that does not exist.
inline bool riscv_tools_available()
{
    return RISCV_TOOLS_FOUND && tool_available(RISCV_CLANG) && tool_available(RISCV_LD) &&
           tool_available(RISCV_QEMU);
}

// Skip a run test when the RISC-V toolchain or qemu is absent.
#define SKIP_IF_NO_RISCV_TOOLS()                                                     \
    do {                                                                             \
        if (!riscv_tools_available())                                                \
            GTEST_SKIP() << "RISC-V clang/ld.lld/qemu not found; skipping run test"; \
    } while (0)

class RiscvTest : public QemuTest {
protected:
    RiscvTest()
        : QemuTest(RISCV_TEST_TARGET,
                   { "riscv-tests",
                     RISCV_CLANG,
                     { "--target=" RISCV_TEST_TARGET, RISCV_TEST_MARCH, RISCV_TEST_MABI },
                     { "-mcmodel=medany", "-ffreestanding", "-fno-builtin" },
                     RISCV_LD,
                     RISCV_LINK_SCRIPT,
                     RISCV_LIB_DIR,
                     { RISCV_QEMU, "-M", "virt", "-bios", "none", "-display", "none", "-serial",
                       "stdio", "-monitor", "none" },
                     RISCV_TEST_XLEN == 32 ? "-rv32" : "" })
    {
        riscv_regalloc      = true;
        riscv_peephole      = true;
        riscv_frame_pointer = false;
        riscv_xlen          = RISCV_TEST_XLEN / 8;
    }

    // Assembly of every toplevel of the translation unit.
    std::string CompileToRiscv(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            riscv_codegen(all, t, f);
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
    // mnemonic, so a test can look for a sequence: Code(s).find("lw a0, -20(s0)\nret\n").
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
    std::string CompileAndRunRiscv(const std::string &src)
    {
        return Run(CompileToRiscv(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToRiscv(src.c_str()), "crt0-status.o");
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToRiscv(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
    }

    // Run a book program compiled by clang -O0 with the target headers.
    std::string ClangRunBook(const std::string &src)
    {
        return Run("", "crt0-status.o", &src,
                   { "-O0", "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_INCLUDE_DIR,
#ifdef TEST_LP64_INCLUDE_DIR
                     "-I", TEST_LP64_INCLUDE_DIR,
#endif
                     "-I", TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }
};
