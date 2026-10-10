//
// Chapter 6 — Conditionals: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_6/invalid_parse +
// extra_credit).  Each program is lexically valid but cannot be parsed; the
// parser reports a fatal error.  Tests assert on the diagnostic text.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// if (5) int i = 0; — the body of an if must be a statement, not a declaration.
TEST_F(ParserTest, Chapter6_DeclarationAsStatement_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    if (5)
        int i = 0;
}
)")),
                 "expected an expression before 'int'");
}

// if (0) else return 0; — the then-branch is missing before 'else'.
TEST_F(ParserTest, Chapter6_EmptyIfBody_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    if (0) else return 0;
}
)")),
                 "expected an expression before 'else'");
}

// int a = if (flag) 2; else 3; — an if statement is not an expression.
TEST_F(ParserTest, Chapter6_IfAssignment_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int flag = 0;
    int a = if (flag)
                2;
            else
                3;
    return a;
}
)")),
                 "expected an expression before 'if'");
}

// if 0 return 1; — the controlling expression must be parenthesized.
TEST_F(ParserTest, Chapter6_IfNoParens_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    if 0 return 1;
}
)")),
                 "expected '\\('");
}

// return 1 ? 2; — the ternary is missing its ':' and false branch.
TEST_F(ParserTest, Chapter6_IncompleteTernary_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return 1 ? 2;
}
)")),
                 "expected ':'");
}

// return 1 ? 2 : 3 : 4; — a ternary cannot have a second ':' / third branch.
TEST_F(ParserTest, Chapter6_MalformedTernary_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return 1 ? 2 : 3 : 4;
}
)")),
                 "expected ';'");
}

// return 1 ? 2 ? 3 : 4; — the nested ternary in the true branch lacks its ':'.
TEST_F(ParserTest, Chapter6_MalformedTernary2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return 1 ? 2 ? 3 : 4;
}
)")),
                 "expected ':'");
}

// A second 'else' with no matching 'if'.
TEST_F(ParserTest, Chapter6_MismatchedNesting_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a = 0;
    if (1)
        return 1;
    else
        return 2;
    else
        return 3;
}
)")),
                 "expected an expression before 'else'");
}

// return x ? 1 = 2; — the ternary's second delimiter must be ':' not '='.
TEST_F(ParserTest, Chapter6_WrongTernaryDelimiter_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int x = 10;
    return x ? 1 = 2;
}
)")),
                 "expected ':'");
}

// --- invalid_parse/extra_credit ---------------------------------------------

// goto; — goto requires a label identifier.
TEST_F(ParserTest, Chapter6_GotoWithoutLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    goto;
lbl:
    return 0;
}
)")),
                 "expected identifier");
}

// return: return 0; — a keyword cannot be used as a label.
TEST_F(ParserTest, Chapter6_KeywordLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return: return 0;
}
)")),
                 "expected an expression before ':'");
}

// label: int a = 0; — in C17 a label cannot precede a declaration.
TEST_F(ParserTest, Chapter6_LabelDeclaration_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
label:
    int a = 0;
    return 0;
}
)")),
                 "expected an expression before 'int'");
}

// 1 && label: 2; — a label cannot appear in the middle of an expression.
TEST_F(ParserTest, Chapter6_LabelExpressionClause_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    1 && label: 2;
}
)")),
                 "expected ';'");
}

// label: int main(void) {...} — a label cannot appear at file scope.
TEST_F(ParserTest, Chapter6_LabelOutsideFunction_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(label:
int main(void) {
    return 0;
}
)")),
                 "expected a type before 'label'");
}

// foo: } — in C17 a label must be followed by a statement.
TEST_F(ParserTest, Chapter6_LabelWithoutStatement_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    foo:
}
)")),
                 "expected an expression before '\\}'");
}

// goto(a); — goto takes a bare identifier, not a parenthesized expression.
TEST_F(ParserTest, Chapter6_ParenthesizedLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    goto(a);
a:
    return 0;
}
)")),
                 "expected identifier");
}
