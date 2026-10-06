// The AArch64 fixture on macOS (aarch64-darwin-tests, built with AARCH64_DARWIN): C
// source compiled in-process with --darwin (Mach-O, the GOT, Apple's arm64 calling
// convention), assembled and linked by the system C compiler against libSystem, and run
// natively.  The interface is that of the bare-metal fixture in aarch64_test.h, so its
// run, interop and book tests build unchanged; clang, the reference compiler, is that
// same system compiler.  The runs need a Mac on Apple silicon, and skip elsewhere.
#pragma once

#include <string>

#include "codegen.h"
#include "qemu_test.h"

// The system C compiler, when this is a Mac on Apple silicon.
inline bool aarch64_tools_available()
{
    return *AARCH64_DARWIN_CC && tool_available(AARCH64_DARWIN_CC);
}

#define SKIP_IF_NO_AARCH64_TOOLS()                                                        \
    do {                                                                                  \
        if (!aarch64_tools_available())                                                   \
            GTEST_SKIP() << "not a Mac on Apple silicon, or no C compiler; skipping run"; \
    } while (0)

// A test of our own runtime's behavior, beyond what C fixes: not libSystem's.
#define SKIP_IF_HOSTED() GTEST_SKIP() << "tests our bare-metal runtime, not libSystem"

inline bool aarch64_clang_available()
{
    return aarch64_tools_available();
}

#define SKIP_IF_NO_AARCH64_CLANG() SKIP_IF_NO_AARCH64_TOOLS()

class Aarch64Test : public QemuTest {
    // A program whose result is printed after its output: the entry is
    // vcc_status_main (darwin_status.c), which calls main.
    QemuConfig status_config;

    static QemuConfig native_config(std::vector<std::string> link_flags)
    {
        QemuConfig cfg   = { "aarch64-darwin-tests",
                             AARCH64_DARWIN_CC,
                             { "-arch", "arm64" },
                             {},
                             AARCH64_DARWIN_CC,
                             "",
                             TEST_DIR,
                             {},
                             ".darwin" };
        cfg.image_option = "";
        cfg.native       = true;
        cfg.link_flags   = { "-arch", "arm64" };
        cfg.link_flags.insert(cfg.link_flags.end(), link_flags.begin(), link_flags.end());
        return cfg;
    }

protected:
    Aarch64Test()
        : QemuTest("aarch64-darwin", native_config({})),
          status_config(native_config({ "-Wl,-e,_vcc_status_main" }))
    {
        // The defaults; a test may change them.
        aarch64_regalloc      = true;
        aarch64_peephole      = true;
        aarch64_frame_pointer = false;
        aarch64_darwin        = true;
    }

    static void NaiveSelection()
    {
        aarch64_regalloc      = false;
        aarch64_peephole      = false;
        aarch64_frame_pointer = true;
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
    // mnemonic, as in aarch64_test.h.
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
    std::string CompileAndRunAarch64(const std::string &src)
    {
        return Run(CompileToAarch64(src.c_str()), "");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(status_config, CompileToAarch64(src.c_str()), "darwin-status.o");
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1, with the
    // system's headers (ours spell the same ABI, but clang cannot expand our va_start).
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToAarch64(ours.c_str()), "", &theirs, { "-O1" }, "");
    }

    // Run a book program compiled by clang -O0, with the system's headers.
    std::string ClangRunBook(const std::string &src)
    {
        return Run(status_config, "", "darwin-status.o", &src, { "-O0", "-w", "-Wno-parentheses" },
                   ".clang");
    }
};
