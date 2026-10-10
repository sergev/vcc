//
// Chapter 2 — Unary Operators: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_2/invalid_parse).
// Each program is lexically valid but cannot be parsed; the parser reports a
// fatal error.  Tests assert on the diagnostic text.
//
#include "fixture.h"

// return (3)); — an extra ')' after the parenthesized operand.
TEST_F(ParserTest, Chapter2_ExtraParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return (3));\n}\n")),
                 "expected ';' before '\\)'");
}

// return ~; — '~' has no operand.
TEST_F(ParserTest, Chapter2_MissingConst_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return ~;\n}\n")),
                 "expected an expression before ';'");
}

// return -5 — missing semicolon after the value.
TEST_F(ParserTest, Chapter2_MissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return -5\n}\n")),
                 "expected ';' before '\\}'");
}

// return -~; — the inner '~' has no operand.
TEST_F(ParserTest, Chapter2_NestedMissingConst_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return -~;\n}\n")),
                 "expected an expression before ';'");
}

// return (-)3; — '-' is parenthesized without its operand.
TEST_F(ParserTest, Chapter2_ParenthesizeOperand_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return (-)3;\n}\n")),
                 "expected an expression before '\\)'");
}

// return (1; — the '(' is never closed.
TEST_F(ParserTest, Chapter2_UnclosedParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return (1;\n}\n")),
                 "expected '\\)' before ';'");
}

// return 4-; — the binary '-' has no right operand.
TEST_F(ParserTest, Chapter2_WrongOrder_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    return 4-;\n}\n")),
                 "expected an expression before ';'");
}
