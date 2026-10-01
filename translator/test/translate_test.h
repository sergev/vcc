#pragma once

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "parser.h"
#include "semantic.h"
#include "structtab.h"
#include "symtab.h"
#include "tac.h"
#include "target.h"
#include "test_preprocess.h"
#include "translate.h"
#include "typetab.h"
#include "xalloc.h"

class TranslateTest : public ::testing::Test {
    FILE *input_file{};

protected:
    Program *program{};

    void SetUp() override
    {
        // The translator backend of record is BESM-6 (6-byte word); pin the target so
        // sizes/offsets/alignments in the expected YAML match that machine.
        target_config  = target_lookup("besm6");
        tac_yaml_types = false; // expected output shows the instructions only
        input_file     = tmpfile();
        ASSERT_NE(nullptr, input_file);
    }

    void TearDown() override
    {
        fclose(input_file);
        if (program) {
            free_program(program);
        }
        symtab_destroy();
        structtab_destroy();
        typetab_destroy();
        nametab_destroy();
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    // Parse C source, run the full tacker pipeline on each declaration, and
    // return the concatenated YAML output for every translated toplevel.
    std::string CompileToYaml(const char *src)
    {
        std::string source = preprocess_source(src);
        if (source.empty()) {
            ADD_FAILURE() << "C preprocessing failed for test source";
            return {};
        }
        fwrite(source.data(), 1, source.size(), input_file);
        rewind(input_file);
        program = parse(input_file);
        EXPECT_NE(nullptr, program);

        std::string result;
        ExternalDecl *decls = program->decls;
        program->decls      = nullptr;
        if (whole_unit)
            translate_unit_begin();
        while (decls) {
            ExternalDecl *next = decls->next;
            decls->next        = nullptr;
            // Per-function label numbering: this fixture dumps TAC per function and
            // produces no single assembly file, so it resets the counter each
            // function (the TU-wide scope the single-file backends need lives in the
            // compiler driver and the codegen fixture — see translate.h).  The one
            // counter feeds both label_loops and translate.
            int label_seq = 0;
            typecheck_decl(decls, &label_seq);
            Tac_TopLevel *tac = translate(decls, OptFlags{}, &label_seq);
            free_external_decl(decls);
            if (tac) {
                result += Yaml(tac);
                tac_free_toplevel(tac);
            }
            decls = next;
        }
        if (whole_unit) {
            Tac_TopLevel *externs = translate_unit_end();
            result += Yaml(externs);
            tac_free_toplevel(externs);
        }
        return result;
    }

    // Like CompileToYaml, but with the type annotations shown and the unit's extern
    // list appended.
    std::string CompileUnitToTypedYaml(const char *src)
    {
        tac_yaml_types = true;
        whole_unit     = true;
        return CompileToYaml(src);
    }

private:
    bool whole_unit{};

    static std::string Yaml(const Tac_TopLevel *tac)
    {
        FILE *f = tmpfile();
        EXPECT_NE(nullptr, f);
        for (const Tac_TopLevel *t = tac; t; t = t->next)
            tac_export_yaml(f, t);
        long len = ftell(f);
        rewind(f);
        std::string yaml(static_cast<size_t>(len), '\0');
        if (len)
            EXPECT_TRUE(fread(&yaml[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return yaml;
    }
};

// Fixture for width-sensitive tests (casts, sizeof, alignof) whose expected output relies
// on x86_64's differing scalar sizes — on the BESM-6 default nearly every scalar is one
// 6-byte word, so int/long/short conversions would collapse to plain copies.  Pinning
// x86_64 keeps the truncate / sign-extend / zero-extend lowering under test.
class TranslateTestX86 : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("x86_64");
    }
};
