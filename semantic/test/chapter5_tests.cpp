//
// Chapter 5 — Local Variables: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_5/invalid_semantics +
// extra_credit).  Each program parses cleanly but fails type checking; the
// type checker reports a fatal error.  Tests assert on the diagnostic text.
//
// This is the first chapter that exercises the semantic phase: undeclared
// variables, duplicate declarations, and assignment / inc-dec to a non-lvalue.
//
#include "typecheck_fixture.h"

// --- invalid_semantics ------------------------------------------------------

// a = 1 + 2; before 'a' is declared.
TEST_F(PipelineTest, Chapter5_DeclaredAfterUse_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    a = 1 + 2;\n    int a;\n    return a;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// !a = 3; — the result of '!' is not an lvalue.
TEST_F(PipelineTest, Chapter5_InvalidLvalue2_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 2;\n    !a = 3;\n    return a;\n}\n"),
                 "expression is not assignable");
}

// a + 3 = 4; — the result of '+' is not an lvalue.
TEST_F(PipelineTest, Chapter5_InvalidLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 2;\n    a + 3 = 4;\n    return a;\n}\n"),
                 "expression is not assignable");
}

// a = 3 * b = a; — parses as a = ((3*b) = a); '3*b' is not an lvalue.
TEST_F(PipelineTest, Chapter5_MixedPrecedenceAssignment_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 1;\n    int b = 2;\n    a = 3 * b = a;\n}\n"),
                 "expression is not assignable");
}

// int a declared twice in the same scope.
TEST_F(PipelineTest, Chapter5_Redefine_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 1;\n    int a = 2;\n    return a;\n}\n"),
                 "Duplicate variable declaration");
}

// return 0 && a; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredVarAnd_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return 0 && a;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// return a < 5; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredVarCompare_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return a < 5;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// return -a; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredVarUnary_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return -a;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// return a; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredVar_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return a;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// int a declared again after a use (and a return).
TEST_F(PipelineTest, Chapter5_UseThenRedefine_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 0;\n    return a;\n    int a = 1;\n    return a;\n}\n"),
                 "Duplicate variable declaration");
}

// --- invalid_semantics / extra_credit ---------------------------------------

// (a += 1) -= 2; — the result of a compound assignment is not an lvalue.
TEST_F(PipelineTest, Chapter5_CompoundInvalidLvalue2_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 10;\n    (a += 1) -= 2;\n}\n"),
                 "expression is not assignable");
}

// -a += 1; — the result of unary '-' is not an lvalue.
TEST_F(PipelineTest, Chapter5_CompoundInvalidLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 0;\n    -a += 1;\n    return a;\n}\n"),
                 "expression is not assignable");
}

// a++--; — the result of postfix '++' is not an lvalue for the following '--'.
TEST_F(PipelineTest, Chapter5_PostfixDecrNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 10;\n    return a++--;\n}\n"),
                 "expression is not assignable");
}

// (a = 4)++; — the result of an assignment is not an lvalue.
TEST_F(PipelineTest, Chapter5_PostfixIncrNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 0;\n    (a = 4)++;\n}\n"),
                 "expression is not assignable");
}

// --3; — a constant is not an lvalue for prefix '--'.
TEST_F(PipelineTest, Chapter5_PrefixDecrNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return --3;\n}\n"),
                 "expression is not assignable");
}

// ++(a+1); — the result of '+' is not an lvalue for prefix '++'.
TEST_F(PipelineTest, Chapter5_PrefixIncrNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int a = 1;\n    ++(a+1);\n    return 0;\n}\n"),
                 "expression is not assignable");
}

// return a >> 2; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredBitwiseOp_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void){\n    return a >> 2;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// b *= a; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredCompoundAssignmentUse_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    int b = 10;\n    b *= a;\n    return 0;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// a += 1; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredCompoundAssignment_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    a += 1;\n    return 0;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// a--; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredPostfixDecr_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    a--;\n    return 0;\n}\n"),
                 "use of undeclared identifier 'a'");
}

// a++; — 'a' is undeclared.
TEST_F(PipelineTest, Chapter5_UndeclaredPrefixIncr_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    a++;\n    return 0;\n}\n"),
                 "use of undeclared identifier 'a'");
}
