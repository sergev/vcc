//
// Chapter 8 — Loops: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_8/invalid_semantics +
// extra_credit).  Each program parses cleanly but fails type checking; the
// type checker reports a fatal error.  Tests assert on a substring of the text.
//
// Two reclassifications vs. the book:
//   * non_constant_case is a *parse* error for us (a case value parses as a
//     constant expression), so it lives in parser/chapter8_tests.cpp instead.
//   * Chapter 8's loop tests include programs that shadow an enclosing name in
//     a nested block / for-init / case block.  This compiler rejects shadowing
//     by a permanent design decision (no identifier shadowing — see CLAUDE.md),
//     so four programs the book lists as *valid* are rejected here with
//     "Duplicate variable declaration" (see the last section).
//
#include "typecheck_fixture.h"

// --- invalid_semantics ------------------------------------------------------

// 'break' appears in an if, with no enclosing loop or switch.
TEST_F(PipelineTest, Chapter8_BreakNotInLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    if (1)
        break;
}
)"),
                 "'break' statement not in a loop or switch");
}

// 'continue' in a bare block, with no enclosing loop.
TEST_F(PipelineTest, Chapter8_ContinueNotInLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    {
        int a;
        continue;
    }
    return 0;
}
)"),
                 "'continue' statement not in a loop");
}

// A variable declared in the do-body is out of scope in the controlling expr.
TEST_F(PipelineTest, Chapter8_OutOfScopeDoLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    do {
        int a = a + 1;
    } while (a < 100);
}
)"),
                 "use of undeclared identifier 'a'");
}

// The for header uses 'i' in its init without ever declaring it.
TEST_F(PipelineTest, Chapter8_OutOfScopeLoopVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    for (i = 0; i < 1; i = i + 1)
    {
        return 0;
    }
}
)"),
                 "use of undeclared identifier 'i'");
}

// --- invalid_semantics / extra_credit ---------------------------------------

// 'continue' under a case label, switch is not a loop.
TEST_F(PipelineTest, Chapter8_CaseContinue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    switch(a + 1) {
        case 0:
            continue;
        default: a = 1;
    }
    return a;
}
)"),
                 "'continue' statement not in a loop");
}

// A case label outside any switch (inside a for loop).
TEST_F(PipelineTest, Chapter8_CaseOutsideSwitch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    for (int i = 0; i < 10; i = i + 1) {
        case 0: return 1;
    }
    return 9;
}
)"),
                 "'case' label not in a switch statement");
}

// 'continue' under a default label, switch is not a loop.
TEST_F(PipelineTest, Chapter8_DefaultContinue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    switch(a + 1) {
        case 0:
            a = 1;
        default: continue;
    }
    return a;
}
)"),
                 "'continue' statement not in a loop");
}

// A default label outside any switch.
TEST_F(PipelineTest, Chapter8_DefaultOutsideSwitch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    {
        default: return 0;
    }
}
)"),
                 "'default' label not in a switch statement");
}

// Two case statements share the same scope, so 'int b' collides.
TEST_F(PipelineTest, Chapter8_DifferentCasesSameScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 1;
    switch (a) {
        case 1:;
            int b = 10;
            break;

        case 2:;
            int b = 11;
            break;

        default:
            break;
    }
    return 0;
}
)"),
                 "redefinition of 'b'");
}

// Duplicate 'case 1' across a label is still the same enclosing switch.
TEST_F(PipelineTest, Chapter8_DuplicateCaseInLabeledSwitch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 0;
label:
    switch (a) {
        case 1:
        case 1:
            break;
    }
    return 0;
}
)"),
                 "duplicate case value");
}

// Duplicate 'case 1' in a nested if is still the same enclosing switch.
TEST_F(PipelineTest, Chapter8_DuplicateCaseInNestedStatement_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 10;
    switch (a) {
        case 1: {
            if(1) {
                case 1:
                return 0;
            }
        }
    }
    return 0;
}
)"),
                 "duplicate case value");
}

// Two 'case 5' in the same switch.
TEST_F(PipelineTest, Chapter8_DuplicateCase_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    switch(4) {
        case 5: return 0;
        case 4: return 1;
        case 5: return 0;
        default: return 2;
    }
}
)"),
                 "duplicate case value");
}

// Two default labels reached through a nested loop/while.
TEST_F(PipelineTest, Chapter8_DuplicateDefaultInNestedStatement_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 10;
    switch (a) {
        case 1:
        for (int i = 0; i < 10; i = i + 1) {
            continue;
            while(1)
            default:;
        }
        case 2:
        return 0;
        default:;
    }
    return 0;
}
)"),
                 "multiple 'default' labels in one switch");
}

// Two default labels in the same switch.
TEST_F(PipelineTest, Chapter8_DuplicateDefault_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 0;
    switch(a) {
        case 0: return 0;
        default: return 1;
        case 2: return 2;
        default: return 2;
    }
}
)"),
                 "multiple 'default' labels in one switch");
}

// Duplicate 'label:' — one at function scope, one under a default label.
TEST_F(PipelineTest, Chapter8_DuplicateLabelInDefault_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
        int a = 1;
label:

    switch (a) {
        case 1:
            return 0;
        default:
        label:
            return 1;
    }
    return 0;
}
)"),
                 "redefinition of label");
}

// Duplicate 'lbl:' inside a do-while body.
TEST_F(PipelineTest, Chapter8_DuplicateLabelInLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    do {
    lbl:
        return 1;
    lbl:
        return 2;
    } while (1);
    return 0;
}
)"),
                 "redefinition of label");
}

// 'int b' redeclared in the same switch-body scope (one is unreachable).
TEST_F(PipelineTest, Chapter8_DuplicateVariableInSwitch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 1;
    switch (a) {
        int b = 2;
        case 0:
            a = 3;
            int b = 2;
    }
    return 0;
}
)"),
                 "redefinition of 'b'");
}

// A labeled 'break' with no enclosing loop or switch.
TEST_F(PipelineTest, Chapter8_LabeledBreakOutsideLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    label: break;
    return 0;
}
)"),
                 "'break' statement not in a loop or switch");
}

// 'continue' inside a switch that is not inside a loop.
TEST_F(PipelineTest, Chapter8_SwitchContinue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    switch(a + 1) {
        case 0:
            a = 4;
            continue;
        default: a = 1;
    }
    return a;
}
)"),
                 "'continue' statement not in a loop");
}

// The switch controlling expression references an undeclared 'a'.
TEST_F(PipelineTest, Chapter8_UndeclaredVarSwitchExpression_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    switch(a) {
        case 1: return 0;
        case 2: return 1;
    }
    return 0;
}
)"),
                 "use of undeclared identifier 'a'");
}

// A case body references an undeclared 'b'.
TEST_F(PipelineTest, Chapter8_UndeclaredVariableInCase_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 10;
    switch (a) {
        case 1:
            return b;
            break;

        default:
            break;
    }
    return 0;
}
)"),
                 "use of undeclared identifier 'b'");
}

// A default body references an undeclared 'b'.
TEST_F(PipelineTest, Chapter8_UndeclaredVariableInDefault_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 10;
    switch (a) {
        case 1:
            break;

        default:
            return b;
            break;
    }
    return 0;
}
)"),
                 "use of undeclared identifier 'b'");
}

// 'goto foo' inside a case targets a label that does not exist.
TEST_F(PipelineTest, Chapter8_UndefinedLabelInCase_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    switch (a) {
        case 1: goto foo;
        default: return 0;
    }
    return 0;
}
)"),
                 "use of undeclared label");
}

// --- shadowing rejected by design (book lists these as valid) ---------------

// for-init 'int shadow' shadows the outer 'int shadow'.
TEST_F(PipelineTest, Chapter8_ForShadow_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int shadow = 1;
    int acc = 0;
    for (int shadow = 0; shadow < 10; shadow = shadow + 1) {
        acc = acc + shadow;
    }
    return acc == 45 && shadow == 1;
}
)"),
                 "declaration of 'shadow' shadows an earlier one, which is not allowed");
}

// for-init 'int i' and a further inner 'int i' both shadow the outer 'i'.
TEST_F(PipelineTest, Chapter8_ForNestedShadow_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 0;
    int j = 0;
    int k = 1;
    for (int i = 100; i > 0; i = i - 1) {
        int i = 1;
        int j = i + k;
        k = j;
    }

    return k == 101 && i == 0 && j == 0;
}
)"),
                 "declaration of 'i' shadows an earlier one, which is not allowed");
}

// A case block's 'int a' shadows the outer 'int a'.
TEST_F(PipelineTest, Chapter8_CaseBlock_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 4;
    int b = 0;
    switch(2) {
        case 2: {
            int a = 8;
            b = a;
        }
    }
    return (a == 4 && b == 8);
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}

// A switch-body 'int a' shadows the outer 'int a'.
TEST_F(PipelineTest, Chapter8_SwitchDecl_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    int b = 0;
    switch(a) {
        int a = (b = 5);
    case 3:
        a = 4;
        b = b + a;
    }
    return a == 3 && b == 4;
}
)"),
                 "declaration of 'a' shadows an earlier one, which is not allowed");
}
