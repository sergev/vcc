// AArch64 fixture: compile C source to assembly in-process, and run it on bare-metal
// qemu `virt` at EL1 (qemu_test.h), its exit status through semihosting.
#pragma once

#include <string>

#include "codegen.h"
#include "qemu_test.h"

// The AArch64 tools, from CMake; a missing one names a path that does not exist.
inline bool aarch64_tools_available()
{
    return AARCH64_TOOLS_FOUND && tool_available(AARCH64_CLANG) && tool_available(AARCH64_LD) &&
           tool_available(AARCH64_QEMU);
}

// Skip a run test when the AArch64 toolchain or qemu is absent.
#define SKIP_IF_NO_AARCH64_TOOLS()                                                    \
    do {                                                                              \
        if (!aarch64_tools_available())                                               \
            GTEST_SKIP() << "AArch64 clang/ld.lld/qemu not found; skipping run test"; \
    } while (0)

class Aarch64Test : public QemuTest {
protected:
    Aarch64Test()
        : QemuTest("aarch64", { "aarch64-tests",
                                AARCH64_CLANG,
                                { "--target=aarch64-none-elf" },
                                { "-ffreestanding", "-fno-builtin" },
                                AARCH64_LD,
                                AARCH64_LINK_SCRIPT,
                                AARCH64_LIB_DIR,
                                { AARCH64_QEMU, "-M", "virt", "-cpu", "cortex-a57", "-display",
                                  "none", "-serial", "stdio", "-monitor", "none", "-semihosting" },
                                "" })
    {
    }

    // Assembly of every toplevel of the translation unit.
    std::string CompileToAarch64(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            aarch64_codegen(all, t, f);
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
    // mnemonic, so a test can look for a sequence: Code(s).find("mov w0, #2\nret\n").
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
    std::string CompileAndRunAarch64(const std::string &src)
    {
        return Run(CompileToAarch64(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToAarch64(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToAarch64(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
    }

    // Run a book program compiled by clang -O0 with the target headers.
    std::string ClangRunBook(const std::string &src)
    {
        return Run("", "crt0-status.o", &src,
                   { "-O0", "-w", "-Wno-parentheses", "-nostdinc", "-I", TEST_TARGET_INCLUDE_DIR,
                     "-I", TEST_INCLUDE_DIR, "-I", TEST_COMMON_INCLUDE_DIR },
                   ".clang");
    }
};
