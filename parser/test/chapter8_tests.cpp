//
// Chapter 8 — Loops: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_8/invalid_parse +
// extra_credit).  Each program is lexically valid but cannot be parsed; the
// parser reports a fatal error.  Tests assert on a substring of the diagnostic.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// A declaration cannot be the (single) body statement of a loop.
TEST_F(ParserTest, Chapter8_DeclAsLoopBody_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    while (1)
        int i = 0;
    return 0;
}
)")),
                 "expected an expression before 'int'");
}

// A stray ';' after the do-body block — 'while' must follow immediately.
TEST_F(ParserTest, Chapter8_DoExtraSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    do {
        int a;
    }; while(1);
    return 0;
}
)")),
                 "expected 'while' before ';'");
}

// do-while needs a terminating ';' after the condition.
TEST_F(ParserTest, Chapter8_DoMissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    do {
        4;
    } while(1)
    return 0;
}
)")),
                 "expected ';' before 'return'");
}

// 'while ()' — the controlling expression cannot be empty.
TEST_F(ParserTest, Chapter8_DoWhileEmptyParens_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    do
        1;
    while ();
    return 0;
}
)")),
                 "expected an expression before '\\)'");
}

// A for header has at most three clauses; the fourth ';' is rejected.
TEST_F(ParserTest, Chapter8_ExtraForHeaderClause_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (int i = 0; i < 10; i = i + 1; )
        ;
    return 0;
}
)")),
                 "expected '\\)' before ';'");
}

// A declaration is not allowed in the for header's *condition* clause.
TEST_F(ParserTest, Chapter8_InvalidForDeclaration_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (; int i = 0; i = i + 1)
        ;
    return 0;
}
)")),
                 "expected an expression before 'int'");
}

// for header is truncated after the init clause.
TEST_F(ParserTest, Chapter8_MissingForHeaderClause_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (int i = 0;)
        ;
    return 0;
}
)")),
                 "expected an expression before '\\)'");
}

// Unbalanced parentheses in the for header.
TEST_F(ParserTest, Chapter8_ParenMismatch_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (int i = 2; ))
        int a = 0;
}
)")),
                 "expected an expression before '\\)'");
}

// A declaration cannot appear as a while controlling expression.
TEST_F(ParserTest, Chapter8_StatementInCondition_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    while(int a) {
        2;
    }
}
)")),
                 "expected an expression before 'int'");
}

// 'while' must be followed by a parenthesized condition.
TEST_F(ParserTest, Chapter8_WhileMissingParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    while 1 {
        return 0;
    }
}
)")),
                 "expected '\\(' before '1'");
}

// --- invalid_parse / extra_credit -------------------------------------------

// The for-init clause may be a declaration, but not a compound assignment.
TEST_F(ParserTest, Chapter8_CompoundAssignmentInvalidDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (int i += 1; i < 10; i += 1) {
        return 0;
    }
}
)")),
                 "expected ';' before '\\+='");
}

// A label is not permitted inside a for header's condition clause.
TEST_F(ParserTest, Chapter8_LabelInLoopHeader_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    for (int i = 0; label: i < 10; i = i + 1) {
        ;
    }
    return 0;
}
)")),
                 "expected ';' before ':'");
}

// A label does not start a block, so 'do label: a; b;' is not a single body.
TEST_F(ParserTest, Chapter8_LabelIsNotBlock_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a = 0;
    int b = 0;
    do
    do_body:
        a = a + 1;
        b = b - 1;
    while (a < 10)
        ;
    return 0;
}
)")),
                 "expected 'while' before 'b'");
}

// A declaration cannot directly follow a case label (it is not a statement).
TEST_F(ParserTest, Chapter8_SwitchCaseDeclaration_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    switch(3) {
        case 3:
            int i = 0;
            return i;
    }
    return 0;
}
)")),
                 "expected an expression before 'int'");
}

// 'goto' requires an identifier target, not an integer constant.
TEST_F(ParserTest, Chapter8_SwitchGotoCase_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    goto 3;
    switch (3) {
        case 3: return 0;
    }
}
)")),
                 "expected identifier before '3'");
}

// 'case' requires a constant expression before the ':'.
TEST_F(ParserTest, Chapter8_SwitchMissingCaseValue_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    switch(0) {
        case: return 0;
    }
}
)")),
                 "expected an expression before ':'");
}

// 'switch' must be followed by a parenthesized controlling expression.
TEST_F(ParserTest, Chapter8_SwitchMissingParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    switch 3 {
        case 3: return 0;
    }
}
)")),
                 "expected '\\(' before '3'");
}

// 'switch' with no controlling expression at all.
TEST_F(ParserTest, Chapter8_SwitchNoCondition_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    switch {
        return 0;
    }
}
)")),
                 "expected '\\(' before '\\{'");
}

// 'case a:' — the book lists non_constant_case under invalid_semantics, but our
// parser parses a case label's value as a *constant* expression and rejects a
// non-constant one at parse time, so for us it is a parse error.
TEST_F(ParserTest, Chapter8_NonConstantCase_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a = 3;
    switch(a + 1) {
        case 0: return 0;
        case a: return 1;
        case 1: return 2;
    }
}
)")),
                 "expected a constant expression");
}
