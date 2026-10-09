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
    bool rotate = false; // lower loops rotated, as lower does by default

    OptFlags Flags() const
    {
        OptFlags f{};
        f.loop_rotate = rotate;
        return f;
    }

    void SetUp() override
    {
        // The translator backend of record is BESM-6 (6-byte word); pin the target so
        // sizes/offsets/alignments in the expected YAML match that machine.
        target_config  = target_lookup("besm6");
        tac_yaml_types   = false; // expected output shows the instructions only
        translate_verify = 1;
        input_file       = tmpfile();
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
            Tac_TopLevel *tac = translate(decls, Flags(), &label_seq);
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

    // Translate a whole unit (no optimization) and return its TAC chain, externs last.
    // The caller frees it with tac_free_toplevel.
    Tac_TopLevel *CompileUnit(const char *src)
    {
        std::string source = preprocess_source(src);
        fwrite(source.data(), 1, source.size(), input_file);
        rewind(input_file);
        program = parse(input_file);
        EXPECT_NE(nullptr, program);

        Tac_TopLevel *head = nullptr, **tail = &head;
        ExternalDecl *decls = program->decls;
        program->decls      = nullptr;
        int label_seq       = 0;
        translate_unit_begin();
        while (decls) {
            ExternalDecl *next = decls->next;
            decls->next        = nullptr;
            typecheck_decl(decls, &label_seq);
            *tail = translate(decls, Flags(), &label_seq);
            free_external_decl(decls);
            while (*tail)
                tail = &(*tail)->next;
            decls = next;
        }
        *tail = translate_unit_end();
        return head;
    }

    // The type of frame-resident `name` in function `fn` of a CompileUnit chain.
    static const Tac_Type *SymbolType(const Tac_TopLevel *tac, const char *fn, const char *name)
    {
        for (; tac; tac = tac->next) {
            if (tac->kind != TAC_TOPLEVEL_FUNCTION || strcmp(tac->u.function.name, fn) != 0)
                continue;
            for (const Tac_Param *p = tac->u.function.params; p; p = p->next)
                if (strcmp(p->name, name) == 0)
                    return p->type;
            for (const Tac_Param *p = tac->u.function.locals; p; p = p->next)
                if (strcmp(p->name, name) == 0)
                    return p->type;
        }
        return nullptr;
    }

    // Members of a struct type as "name@offset:type ...".
    static std::string Members(const Tac_Type *t)
    {
        std::string out;
        if (!t || t->kind != TAC_TYPE_STRUCTURE)
            return "not a struct";
        for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
            char *ts = tac_type_str(m->type);
            out += std::string(out.empty() ? "" : " ") + (m->name ? m->name : "") + "@" + std::to_string(m->offset) +
                   ":" + ts;
            xfree(ts);
        }
        return out;
    }

    static std::string TypeStr(const Tac_Type *t)
    {
        char *ts = tac_type_str(t);
        std::string out(ts);
        xfree(ts);
        return out;
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

// Fixture for AVR: a 16-bit int, size_t and pointer, a 32-bit long, alignment 1.
class TranslateTestAvr : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("avr");
    }
};

// Fixture for MSP430: a 16-bit int, size_t and pointer, unsigned plain char, a binary64
// double, alignment 2 for everything wider than char, every struct returned in memory.
class TranslateTestMsp430 : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("msp430");
    }
};

// Fixture for MMIX: LP64, big-endian, signed plain char, long double = double, every
// struct returned by the backend through $251 rather than a hidden argument.
class TranslateTestMmix : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("mmix");
    }
};

// Fixture for wasm32: ILP32 with a signed plain char and a binary128 long double, every
// struct returned by the backend.
class TranslateTestWasm32 : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("wasm32");
    }
};

// Fixture for the byte-addressed LP64 target the RISC-V backend uses.
class TranslateTestRiscv : public TranslateTest {
protected:
    void SetUp() override
    {
        TranslateTest::SetUp();
        target_config = target_lookup("riscv64");
    }
};
