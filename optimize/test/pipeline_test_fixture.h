//
// Shared fixture for optimizer pipeline tests: compile C source, run the full
// typecheck + translate + optimize pipeline, and return the optimized TAC as
// YAML for string comparison.  Used by pipeline_tests.cpp and chapter19_tests.cpp.
//
// NOTE: the `fatal_error` definition the linked libraries require lives in
// pipeline_tests.cpp ONLY (a single definition per binary).  Files that include
// this header must not redefine it.
//
#ifndef OPTIMIZE_PIPELINE_TEST_FIXTURE_H
#define OPTIMIZE_PIPELINE_TEST_FIXTURE_H

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

#include "test_preprocess.h"

extern "C" {
#include "optimize.h"
#include "parser.h"
#include "semantic.h"
#include "structtab.h"
#include "symtab.h"
#include "tac.h"
#include "translate.h"
#include "typetab.h"
#include "xalloc.h"
}

// ---------------------------------------------------------------------------
// Test fixture: compile C source, optimize the function body, return YAML.
// ---------------------------------------------------------------------------

class PipelineTest : public ::testing::Test {
    FILE *input_file{};

protected:
    Program *program{};

    void SetUp() override
    {
        tac_yaml_types = false; // expected output shows the instructions only
        input_file     = tmpfile();
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

    static std::string capture_instructions(const Tac_Instruction *body)
    {
        FILE *f = tmpfile();
        EXPECT_NE(f, nullptr);
        tac_export_yaml_instruction_list(f, body, 0);
        long len = ftell(f);
        rewind(f);
        std::string yaml(static_cast<size_t>(len), '\0');
        EXPECT_TRUE(fread(&yaml[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return yaml;
    }

    // Compact fingerprint of the optimized instruction mix: counts of each
    // top-level (2-space-indented) "kind:" line, e.g.
    // "binary=0 copy=3 jump=1 label=1 return=2".  Value/const kinds are emitted
    // at deeper indent and are excluded.  Used in place of a full exact-YAML
    // golden for programs whose optimized TAC is too large to pin verbatim.
    static std::string KindHistogram(const std::string &yaml)
    {
        std::map<std::string, int> counts;
        std::istringstream in(yaml);
        std::string line;
        while (std::getline(in, line)) {
            // Match exactly "  kind: <word>" (two leading spaces); value/const
            // kinds are emitted at deeper indent and do not match.
            if (line.compare(0, 8, "  kind: ") == 0) {
                std::string kind = line.substr(8);
                if (!kind.empty() && kind.find(' ') == std::string::npos)
                    counts[kind]++;
            }
        }
        std::string out;
        for (const auto &kv : counts) {
            if (!out.empty())
                out += ' ';
            out += kv.first + "=" + std::to_string(kv.second);
        }
        return out;
    }

    std::string OptimizeYaml(const char *src, OptFlags flags = opt_flags_default())
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

        while (decls) {
            ExternalDecl *next = decls->next;
            decls->next        = nullptr;
            // This fixture dumps per-function TAC (no single assembly file is
            // produced), so it resets the label counter per function — the label
            // numbering is scoped to the function under test.  The TU-wide scope
            // that the single-file backends require lives in the compiler driver
            // and the codegen fixture (see translate.h).  The one counter feeds
            // both label_loops and translate.
            int label_seq = 0;
            typecheck_decl(decls, &label_seq);
            Tac_TopLevel *tac = translate(decls, flags, &label_seq);
            free_external_decl(decls);
            if (tac) {
                for (const Tac_TopLevel *t = tac; t; t = t->next) {
                    if (t->kind == TAC_TOPLEVEL_FUNCTION && t->u.function.body)
                        result += capture_instructions(t->u.function.body);
                }
                tac_free_toplevel(tac);
            }
            decls = next;
        }
        return result;
    }
};

#endif // OPTIMIZE_PIPELINE_TEST_FIXTURE_H
