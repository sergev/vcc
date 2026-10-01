// RISC-V fixture: compile C source to assembly in-process, and run it on bare-metal
// qemu `virt` (assemble with clang, link with ld.lld against crt0 and libc.a).
#pragma once

#include <fstream>
#include <string>
#include <vector>

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

    RiscvTest() : BackendTest("riscv64") 
    {
        riscv_regalloc = true;
        riscv_peephole      = true;
        riscv_frame_pointer = false;
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
        return Run("", "crt0-status.o", &src, { "-O0", "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I",
                     TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }

private:
    // Assemble `asm_text` (unless empty) and compile `clang_src` (if any) with
    // `clang_flags`, link and run under qemu.  Scratch files are named by the test and
    // `tag`.  Returns the UART output, or "ERROR".
    std::string Run(const std::string &asm_text, const char *crt0,
                    const std::string *clang_src = nullptr,
                    const std::vector<std::string> &clang_flags = {}, const char *tag = "")
    {
        exit_status          = -1;
        std::string base     = ScratchPath(tag);
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
        std::vector<std::string> objs;
        int rc;
        if (!asm_text.empty()) {
            {
                std::ofstream s(s_path);
                s << asm_text;
            }
            rc = RunTool({ RISCV_CLANG, "--target=riscv64", "-march=rv64imfd", "-mabi=lp64d",
                           "-c", "-o", o_path, s_path },
                         log_path);
            EXPECT_EQ(0, rc) << "assembler failed on " << s_path << ":\n" << ReadFile(log_path);
            if (rc != 0)
                return "ERROR";
            objs.push_back(o_path);
        }
        if (clang_src) {
            std::string c_path  = base + "-clang.c";
            std::string co_path = base + "-clang.o";
            {
                std::ofstream c(c_path);
                c << *clang_src;
            }
            std::vector<std::string> cc = { RISCV_CLANG,       "--target=riscv64", "-march=rv64imfd",
                                            "-mabi=lp64d",     "-mcmodel=medany",  "-ffreestanding",
                                            "-fno-builtin",    "-c",               "-o",
                                            co_path };
            cc.insert(cc.end(), clang_flags.begin(), clang_flags.end());
            cc.push_back(c_path);
            rc = RunTool(cc, log_path);
            EXPECT_EQ(0, rc) << "clang failed on " << c_path << ":\n" << ReadFile(log_path);
            if (rc != 0)
                return "ERROR";
            objs.push_back(co_path);
        }
        std::string lib = RISCV_LIB_DIR;
        std::vector<std::string> link = { RISCV_LD, "-T", RISCV_LINK_SCRIPT, "-o", exe_path,
                                          lib + "/" + crt0 };
        link.insert(link.end(), objs.begin(), objs.end());
        link.push_back(lib + "/libc.a");
        rc = RunTool(link, log_path);
        EXPECT_EQ(0, rc) << "ld.lld failed on " << exe_path << ":\n" << ReadFile(log_path);
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
