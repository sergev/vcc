// wasm32 fixture: compile C source to assembly in-process, assemble with clang, link with
// wasm-ld and run the module under node with libc/wasm32/run.mjs as the host
// (qemu_test.h), main's result its exit status.
#pragma once

#include <string>

#include "codegen.h"
#include "qemu_test.h"

// The wasm32 tools, from CMake; a missing one names a path that does not exist.
inline bool wasm32_tools_available()
{
    return WASM32_TOOLS_FOUND && command_available(WASM32_ASSEMBLER) && tool_available(WASM32_LD) &&
           tool_available(WASM32_NODE);
}

// Skip a run test when clang, wasm-ld or node is absent.
#define SKIP_IF_NO_WASM32_TOOLS()                                              \
    do {                                                                       \
        if (!wasm32_tools_available())                                         \
            GTEST_SKIP() << "clang/wasm-ld/node not found; skipping run test"; \
    } while (0)

// clang as the reference compiler: the one that assembles.
inline bool wasm32_clang_available()
{
    // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
    return wasm32_tools_available() && WASM32_CLANG_FOUND && tool_available(WASM32_CLANG);
}

// Skip a test that compiles C with clang.
#define SKIP_IF_NO_WASM32_CLANG()                                          \
    do {                                                                   \
        if (!wasm32_clang_available())                                     \
            GTEST_SKIP() << "wasm32 clang not found; skipping clang test"; \
    } while (0)

// The run configuration: node runs the module, whose clean exit run.mjs reports as
// "[exit N]" on stderr, so a trap (status 255) cannot pass for main returning 255.
inline QemuConfig wasm32_config()
{
    QemuConfig cfg   = cross_tools({ "wasm32-tests",
                                     WASM32_CLANG,
                                     split_words(WASM32_TARGET_FLAGS),
                                     { "-ffreestanding", "-fno-builtin" },
                                     WASM32_LD,
                                     "",
                                     WASM32_LIB_DIR,
                                     { WASM32_NODE, WASM32_RUNNER },
                                     "" },
                                   WASM32_ASSEMBLER, WASM32_LINK_FLAGS);
    cfg.image_option = "";
    cfg.exit_report  = "[exit ";
    return cfg;
}

class WasmTest : public QemuTest {
protected:
    WasmTest() : QemuTest("wasm32", wasm32_config()) { wasm_structure = true; }

    // Pin instruction selection itself: nothing so far to turn off.
    static void NaiveSelection() {}

    // Assembly of every toplevel of the translation unit.
    std::string CompileToWasm(const char *src)
    {
        Tac_TopLevel *all = CompileToTac(src);
        FILE *f           = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            wasm_codegen(all, t, f);
        tac_free_toplevel(all);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        return s;
    }

    // Instruction lines of `asm_text`: the tab-indented lines but for the directives,
    // unindented, with one space between the mnemonic and its immediate, so a test can
    // look for a sequence: Code(s).find("i32.const 2\nreturn\n").
    static std::string Code(const std::string &asm_text)
    {
        std::string out;
        size_t pos = 0;
        while (pos < asm_text.size()) {
            size_t nl        = asm_text.find('\n', pos);
            std::string line = asm_text.substr(pos, nl - pos);
            pos              = nl == std::string::npos ? asm_text.size() : nl + 1;
            if (line.size() < 2 || line[0] != '\t' || line[1] == '.')
                continue;
            line      = line.substr(1);
            size_t tb = line.find('\t');
            if (tb != std::string::npos)
                line[tb] = ' ';
            out += line + "\n";
        }
        return out;
    }

    // Run a program; returns its output, with main's result in exit_status.
    std::string CompileAndRunWasm(const std::string &src)
    {
        return Run(CompileToWasm(src.c_str()), "crt0.o");
    }

    // Run a book program: its output followed by main's result as "%d\n".
    std::string CompileAndRunBook(const std::string &src)
    {
        return Run(CompileToWasm(src.c_str()), "crt0-status.o");
    }

    // Run hand-written assembly, linked with `crt0`.
    std::string RunAssembly(const std::string &asm_text, const char *crt0 = "crt0.o")
    {
        return Run(asm_text, crt0);
    }

    // Run a program of two parts: `ours` compiled by us, `theirs` by clang -O1.
    std::string CompileAndRunWithClang(const std::string &ours, const std::string &theirs)
    {
        return Run(CompileToWasm(ours.c_str()), "crt0.o", &theirs, { "-O1" }, "");
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
#define EXPECT_CODE(name, expected, src)               \
    TEST_F(WasmTest, name)                             \
    {                                                  \
        NaiveSelection();                              \
        EXPECT_EQ(expected, Code(CompileToWasm(src))); \
    }
