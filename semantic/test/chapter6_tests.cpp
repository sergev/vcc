//
// Chapter 6 — Conditionals: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_6/invalid_semantics +
// extra_credit).  Each program parses cleanly but fails semantic analysis; the
// analyzer reports a fatal error.  Tests assert on the diagnostic text.
//
// The extra-credit label/goto cases are validated by the resolve_labels pass
// (semantic/resolve_labels.c): duplicate labels and goto to an undefined label.
//
#include "typecheck_fixture.h"

// --- invalid_semantics ------------------------------------------------------

// return c; before 'c' is declared.
TEST_F(PipelineTest, Chapter6_InvalidVarInIf_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    if (1)
        return c;
    int c = 0;
}
)"),
                 "use of undeclared identifier 'c'");
}

// a > b ? a = 1 : a = 0; — parses as (a>b ? a=1 : a) = 0; the conditional is
// not an lvalue, so the trailing assignment is invalid.
TEST_F(PipelineTest, Chapter6_TernaryAssign_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 2;
    int b = 1;
    a > b ? a = 1 : a = 0;
    return a;
}
)"),
                 "expression is not assignable");
}

// return a > 0 ? 1 : 2; before 'a' is declared.
TEST_F(PipelineTest, Chapter6_UndeclaredVarInTernary_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    return a > 0 ? 1 : 2;
    int a = 5;
}
)"),
                 "use of undeclared identifier 'a'");
}

// --- invalid_semantics/extra_credit -----------------------------------------

// The same label name appears twice in one function.
TEST_F(PipelineTest, Chapter6_DuplicateLabels_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
label:
    x = 1;
label:
    return 2;
}
)"),
                 "redefinition of label");
}

// goto label; with no such label defined.
TEST_F(PipelineTest, Chapter6_GotoMissingLabel_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    goto label;
    return 0;
}
)"),
                 "use of undeclared label");
}

// goto a; where 'a' is a variable, not a label (separate namespaces).
TEST_F(PipelineTest, Chapter6_GotoVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a;
    goto a;
    return 0;
}
)"),
                 "use of undeclared label");
}

// lbl: return a; — 'a' is never declared.
TEST_F(PipelineTest, Chapter6_UndeclaredVarInLabeledStatement_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
lbl:
    return a;
    return 0;
}
)"),
                 "use of undeclared identifier 'a'");
}

// a: x = a; — 'a' is a label, not a value; using it as a variable is undeclared.
TEST_F(PipelineTest, Chapter6_UseLabelAsVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    a:
    x = a;
    return 0;
}
)"),
                 "use of undeclared identifier 'a'");
}
