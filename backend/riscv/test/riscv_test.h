// RISC-V fixture: compile C source to assembly in-process, and run it on bare-metal
// qemu `virt` (assemble with clang, link with ld.lld against crt0 and libc.a).
#pragma once

#include <fstream>
#include <string>

#include "backend_test.h"
#include "codegen.h"

// The RISC-V tools, from CMake; a missing one names a path that does not exist.
inline bool riscv_tools_available()
{
    return RISCV_TOOLS_FOUND && tool_available(RISCV_CLANG) && tool_available(RISCV_LD) &&
           tool_available(RISCV_QEMU);
}

// Skip a run test when the RISC-V toolchain or qemu is absent.
#define SKIP_IF_NO_RISCV_TOOLS()                                                       \
    do {                                                                               \
        if (!riscv_tools_available())                                                  \
            GTEST_SKIP() << "RISC-V clang/ld.lld/qemu not found; skipping run test";   \
    } while (0)

class RiscvTest : public BackendTest {
protected:
    int exit_status = -1; // of the last run: main's result, modulo 256

    RiscvTest() : BackendTest("riscv64") {}

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

    // Instruction lines of `asm_text` without the leading tabs, one per line, so a
    // test can look for a sequence: Code(s).find("lw\ta0, -20(s0)\nret\n").
    static std::string Code(const std::string &asm_text)
    {
        std::string out, line;
        for (size_t i = 0; i < asm_text.size(); i++) {
            if (asm_text[i] != '\n') {
                line += asm_text[i];
                continue;
            }
            if (line.size() > 1 && line[0] == '\t' && line[1] != '.')
                out += line.substr(1) + "\n";
            line.clear();
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

private:
    // Assemble, link and run under qemu.  Returns the UART output, or "ERROR".
    std::string Run(const std::string &asm_text, const char *crt0)
    {
        exit_status          = -1;
        std::string base     = ScratchPath("");
        std::string s_path   = base + ".s";
        std::string o_path   = base + ".o";
        std::string exe_path = base + ".elf";
        std::string out_path = base + ".out";
        std::string log_path = base + ".log";

        // Guards the scratch files against a concurrent run of the same test.
        FlockGuard lock(s_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent riscv-tests run detected (" << s_path << ")";
            return "ERROR";
        }
        {
            std::ofstream s(s_path);
            s << asm_text;
        }
        int rc = RunTool({ RISCV_CLANG, "--target=riscv64", "-march=rv64imfd", "-mabi=lp64d",
                           "-c", "-o", o_path, s_path },
                         log_path);
        EXPECT_EQ(0, rc) << "assembler failed on " << s_path << ":\n" << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        std::string lib = RISCV_LIB_DIR;
        rc = RunTool({ RISCV_LD, "-T", RISCV_LINK_SCRIPT, "-o", exe_path, lib + "/" + crt0, o_path,
                       lib + "/libc.a" },
                     log_path);
        EXPECT_EQ(0, rc) << "ld.lld failed on " << o_path << ":\n" << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        rc = RunWithTimeout({ RISCV_QEMU, "-M", "virt", "-bios", "none", "-display", "none",
                              "-serial", "stdio", "-monitor", "none", "-kernel", exe_path },
                            out_path, log_path, 5);
        if (rc < 0) {
            ADD_FAILURE() << (rc == -2 ? "qemu timed out" : "qemu failed") << " on " << exe_path
                          << ":\n" << ReadFile(log_path);
            return "ERROR";
        }
        exit_status = rc;
        return ReadFile(out_path);
    }
};
