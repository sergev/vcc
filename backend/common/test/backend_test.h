// Base GoogleTest fixture for backend tests: selects the target, runs C source
// in-process through parse → typecheck → translate (with the optimizer), and hands the
// whole translation unit's TAC to the backend fixture.  Each backend derives its own
// fixture adding code generation and its run harness.
#pragma once

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "parser.h"
#include "semantic.h"
#include "structtab.h"
#include "symtab.h"
#include "tac.h"
#include "target.h"
#include "test_preprocess.h"
#include "test_tools.h"
#include "translate.h"
#include "typetab.h"
#include "xalloc.h"

// A shared-suite test a target cannot run, with the reason.  Lists end with a NULL name.
struct SkippedTest {
    const char *name;
    const char *reason;
};

class BackendTest : public ::testing::Test {
    const char *target_name;
    FILE *input_file{};
    Program *program{};

protected:
    OptFlags opt_flags{};

    explicit BackendTest(const char *target) : target_name(target) {}

    void SetUp() override
    {
        target_config = target_lookup(target_name);
        ASSERT_NE(nullptr, target_config);
        opt_flags        = opt_flags_default();
        if (const char *n = getenv("VCC_OPT_MAX_ITER")) // for bisecting a failure
            opt_flags.max_iterations = atoi(n);
        translate_verify = 1;
        input_file       = tmpfile();
        ASSERT_NE(nullptr, input_file);
    }

    void TearDown() override
    {
        fclose(input_file);
        if (program)
            free_program(program);
        symtab_destroy();
        structtab_destroy();
        typetab_destroy();
        nametab_destroy();
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    // True when the fixture compiles for target `name`, for per-target expectations.
    bool IsTarget(const char *name) const { return strcmp(target_name, name) == 0; }

    // Disable optimization.
    void DisableOptimization() { opt_flags = {}; }

    // Parse C source, typecheck and translate every declaration, and return the whole
    // TAC chain (free it with tac_free_toplevel), or nullptr if preprocessing failed.
    // #include/#define directives are expanded by the system preprocessor first.
    Tac_TopLevel *CompileToTac(const char *src)
    {
        std::string source = preprocess_source(src);
        if (source.empty()) {
            ADD_FAILURE() << "C preprocessing failed for test source";
            return nullptr;
        }
        fwrite(source.data(), 1, source.size(), input_file);
        rewind(input_file);
        program = parse(input_file);
        EXPECT_NE(nullptr, program);

        Tac_TopLevel *all_tac = nullptr, **tac_tail = &all_tac;
        ExternalDecl *decls = program->decls;
        program->decls      = nullptr;
        int label_seq       = 0; // unit-wide temp/label counter (see translate.h)
        translate_unit_begin();
        while (decls) {
            ExternalDecl *next = decls->next;
            decls->next        = nullptr;
            typecheck_decl(decls, &label_seq);
            Tac_TopLevel *tac = translate(decls, opt_flags, &label_seq);
            free_external_decl(decls);
            if (tac) {
                Tac_TopLevel *t = tac;
                while (t->next)
                    t = t->next;
                *tac_tail = tac;
                tac_tail  = &t->next;
            }
            decls = next;
        }
        *tac_tail = translate_unit_end();

        Tac_Layout layout;
        tac_layout_of_target(&layout);
        EXPECT_EQ(0, tac_verify_program(all_tac, &layout, stderr));
        return all_tac;
    }

    // Start another translation unit in the same test: the last one's tables and source
    // are gone, as between two runs of the compiler.
    void NextUnit()
    {
        if (program)
            free_program(program);
        program = nullptr;
        symtab_destroy();
        structtab_destroy();
        typetab_destroy();
        nametab_destroy();
        fclose(input_file);
        input_file = tmpfile();
        ASSERT_NE(nullptr, input_file);
    }

    // Skip the current test if `list` names it (call from a fixture's SetUp).
    static void SkipIfListed(const SkippedTest *list)
    {
        const char *test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        for (; list && list->name; list++) {
            if (strcmp(list->name, test_name) == 0)
                GTEST_SKIP() << list->reason;
        }
    }

    // Scratch file of the current test in the build directory: TEST_DIR/<TestName><suffix>.
    static std::string ScratchPath(const std::string &suffix)
    {
        const char *test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        return std::string(TEST_DIR "/") + test_name + suffix;
    }
};
