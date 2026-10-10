//
// Chapter 3 — Binary Operators: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_3/invalid_parse).
// Each program is lexically valid but cannot be parsed; the parser reports a
// fatal error.  Tests assert on the diagnostic text.
//
// Note: the book's invalid_parse/malformed_paren.c ("return 2 (- 3);") is NOT a
// parse error for us — our parser accepts a call on any postfix expression and
// defers "called object is not a function" to semantic analysis, so it lives in
// semantic/chapter3_tests.cpp instead.
//
#include "fixture.h"

// return 1 * / 2; — '/' has no left operand for the second operator.
TEST_F(ParserTest, Chapter3_DoubleOperation_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 * / 2;\n}\n")),
                 "expected an expression before '/'");
}

// return 1 + (2; — the '(' is never closed.
TEST_F(ParserTest, Chapter3_ImbalancedParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 + (2;\n}\n")),
                 "expected '\\)' before ';'");
}

// return 1 + (2;) — ';' appears before the ')' that closes the group.
TEST_F(ParserTest, Chapter3_MisplacedSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 + (2;)\n}\n")),
                 "expected '\\)' before ';'");
}

// return /3; — '/' has no left operand.
TEST_F(ParserTest, Chapter3_MissingFirstOp_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return /3;\n}\n")),
                 "expected an expression before '/'");
}

// return 1 + 2); — an extra ')' with no matching '('.
TEST_F(ParserTest, Chapter3_MissingOpenParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 + 2);\n}\n")),
                 "expected ';' before '\\)'");
}

// return 1 + ; — the binary '+' has no right operand.
TEST_F(ParserTest, Chapter3_MissingSecondOp_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 + ;\n}\n")),
                 "expected an expression before ';'");
}

// return 2*2 — missing semicolon after the value.
TEST_F(ParserTest, Chapter3_NoSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 2*2\n}\n")),
                 "expected ';' before '\\}'");
}

// return 1 | | 2; — two '|' tokens, not a single '||' (lexed as separate operators).
TEST_F(ParserTest, Chapter3_BitwiseDoubleOperator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 | | 2;\n}\n")),
                 "expected an expression before '\\|'");
}
