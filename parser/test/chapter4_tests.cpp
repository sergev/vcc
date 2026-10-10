//
// Chapter 4 — Logical & Relational Operators: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_4/invalid_parse).
// Each program is lexically valid but cannot be parsed; the parser reports a
// fatal error.  Tests assert on the diagnostic text.
//
#include "fixture.h"

// return <= 2; — '<=' has no left operand.
TEST_F(ParserTest, Chapter4_MissingFirstOp_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return <= 2;\n}\n")),
                 "expected an expression before '<='");
}

// return 2 && ~; — unary '~' has no operand before the ';'.
TEST_F(ParserTest, Chapter4_MissingSecondOp_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 2 && ~;\n}\n")),
                 "expected an expression before ';'");
}

// return 1 < > 3; — '>' has no left operand.
TEST_F(ParserTest, Chapter4_MissingOperand_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 < > 3;\n}\n")),
                 "expected an expression before '>'");
}

// 10 <= !; — unary '!' has no operand (expression statement, not a return).
TEST_F(ParserTest, Chapter4_MissingConst_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    10 <= !;\n}\n")),
                 "expected an expression before ';'");
}

// return 1 || 2 — missing semicolon after the value.
TEST_F(ParserTest, Chapter4_MissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 1 || 2\n}\n")),
                 "expected ';' before '\\}'");
}

// return !10 — missing semicolon after the unary expression.
TEST_F(ParserTest, Chapter4_UnaryMissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return !10\n}\n")),
                 "expected ';' before '\\}'");
}
