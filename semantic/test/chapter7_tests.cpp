//
// Chapter 7 — Compound statements: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_7/invalid_semantics +
// extra_credit).  Each program parses cleanly but fails type checking; the
// type checker reports a fatal error.  Tests assert on the diagnostic text.
//
// NOTE: Chapter 7 is about variable shadowing in nested blocks.  This compiler
// rejects shadowing by a permanent design decision (no identifier shadowing —
// see CLAUDE.md), so 10 programs the book lists as *valid* are rejected here
// with "Duplicate variable declaration".  Those tests live in this file under
// "shadowing rejected by design" rather than as run tests.
//
#include "typecheck_fixture.h"

// --- invalid_semantics ------------------------------------------------------

// int a declared twice in the same inner block.
TEST_F(PipelineTest, Chapter7_DoubleDefine_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    {
        int a;
        int a;
    }
}
)"),
                 "redefinition of 'a'");
}

// int a redeclared at function scope after an inner block used it.
TEST_F(PipelineTest, Chapter7_DoubleDefineAfterScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    {
        a = 5;
    }
    int a = 2;
    return a;
}
)"),
                 "redefinition of 'a'");
}

// 'a' used after its declaring block has been left.
TEST_F(PipelineTest, Chapter7_OutOfScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    {
        int a = 2;
    }
    return a;
}
)"),
                 "use of undeclared identifier 'a'");
}

// 'b' used before it is declared (in an enclosing scope).
TEST_F(PipelineTest, Chapter7_UseBeforeDeclare_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a;
    {
        b = 10;
    }
    int b;
    return b;
}
)"),
                 "use of undeclared identifier 'b'");
}

// --- invalid_semantics / extra_credit ---------------------------------------

// Labels do not introduce scopes, so the two 'int a' collide.
TEST_F(PipelineTest, Chapter7_DifferentLabelsSameScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
label1:;
    int a = 10;
label2:;
    int a = 11;
    return 1;
}
)"),
                 "redefinition of 'a'");
}

// Label names must be unique within a function, even across sibling blocks.
TEST_F(PipelineTest, Chapter7_DuplicateLabelsDifferentScopes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    if (x) {
        x = 5;
        goto l;
        return 0;
        l:
            return x;
    } else {
        goto l;
        return 0;
        l:
            return x;
    }
}
)"),
                 "redefinition of label");
}

// goto target's labeled statement uses 'y' before it is declared.
TEST_F(PipelineTest, Chapter7_GotoUseBeforeDeclare_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    if (x != 0) {
        return_y:
        return y;
    }
    int y = 4;
    goto return_y;
}
)"),
                 "use of undeclared identifier 'y'");
}

// --- shadowing rejected by design (book lists these as valid) ---------------

// int a = 3; { int a = a = 4; ... } — inner 'a' shadows the outer 'a'.
TEST_F(PipelineTest, Chapter7_AssignToSelf_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    {
        int a = a = 4;
        return a;
    }
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// Same shadow, with the inner block falling through to the outer return.
TEST_F(PipelineTest, Chapter7_AssignToSelf2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    {
        int a = a = 4;
    }
    return a;
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int a; { ...; int a = 7; ... } — inner 'a' shadows the outer 'a'.
TEST_F(PipelineTest, Chapter7_HiddenThenVisible_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 2;
    int b;
    {
        a = -4;
        int a = 7;
        b = a + 1;
    }
    return b == 8 && a == -4;
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int a = 2; { int a = 1; ... } — inner 'a' shadows the outer 'a'.
TEST_F(PipelineTest, Chapter7_HiddenVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 2;
    {
        int a = 1;
        return a;
    }
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int x = 4; { int x; } — inner 'x' shadows the outer 'x'.
TEST_F(PipelineTest, Chapter7_InnerUninitialized_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 4;
    {
        int x;
    }
    return x;
}
)"),
                 "declaration of 'x' shadows an earlier one, which is not allowed");
}

// Deeply nested blocks each redeclaring 'a' — every level shadows.
TEST_F(PipelineTest, Chapter7_SimilarVarNames_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a;
    int result;
    int a1 = 1;
    {
        int a = 2;
        int a1 = 2;
        {
            int a;
            {
                int a;
                {
                    int a = 20;
                    result = a;
                }
            }
        }
        result = result + a1;
    }
    return result + a1;
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int a = 5; if (...) { ...; int a = 5; ... } — inner 'a' shadows the outer.
TEST_F(PipelineTest, Chapter7_CompoundSubtractInBlock_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 5;
    if (a > 4) {
        a -= 4;
        int a = 5;
        if (a > 4) {
            a -= 4;
        }
    }
    return a;
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int a = 0; { ...; int a = 4; ... } — inner 'a' shadows the outer 'a'.
TEST_F(PipelineTest, Chapter7_GotoBeforeDeclaration_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 0;
    {
        if (a != 0)
            return_a:
                return a;
        int a = 4;
        goto return_a;
    }
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// int x = 5; { int x = 0; ... } — inner 'x' shadows the outer 'x'.
TEST_F(PipelineTest, Chapter7_GotoInnerScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 5;
    goto inner;
    {
        int x = 0;
        inner:
        x = 1;
        return x;
    }
}
)"),
                 "declaration of 'x' shadows an earlier one, which is not allowed");
}

// int a = 10; if (a) { int a = 1; ... } — inner 'a' shadows the outer 'a'.
TEST_F(PipelineTest, Chapter7_GotoOuterScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 10;
    int b = 0;
    if (a) {
        int a = 1;
        b = a;
        goto end;
    }
    a = 9;
end:
    return (a == 10 && b == 1);
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}
