//
// Chapter 5 — Local Variables: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_5/invalid_parse +
// extra_credit).  Each program is lexically valid but cannot be parsed; the
// parser reports a fatal error.  Tests assert on the diagnostic text.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// a + = 1; — '+' and '=' are separate tokens, so '=' has no left operand.
TEST_F(ParserTest, Chapter5_CompoundInvalidOperator_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int a = 0;\n    a + = 1;\n    return a;\n}\n")),
        "expected an expression before '='");
}

// int return = 4; — 'return' is a keyword, not a declarator name.
TEST_F(ParserTest, Chapter5_DeclareKeywordAsVar_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int return = 4;\n    return return + 1;\n}\n")),
        "expected an identifier or '\\(' before 'return'");
}

// int foo bar = 3; — two identifiers where a declarator is expected.
TEST_F(ParserTest, Chapter5_InvalidSpecifier_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int foo bar = 3;\n    return bar;\n}\n")),
        "expected ';' before 'bar'");
}

// ints a = 1; — 'ints' is not a type, so it parses as an expression statement.
TEST_F(ParserTest, Chapter5_InvalidType_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    ints a = 1;\n    return a;\n}\n")),
                 "expected ';' before 'a'");
}

// int 10 = 0; — an integer constant cannot be a declarator name.
TEST_F(ParserTest, Chapter5_InvalidVariableName_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    int 10 = 0;\n    return 10;\n}\n")),
                 "expected an identifier or '\\(' before '10'");
}

// a =/ 1; — '=' then '/' has no right operand for the assignment.
TEST_F(ParserTest, Chapter5_MalformedCompoundAssignment_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int a = 10;\n    a =/ 1;\n    return a;\n}\n")),
        "expected an expression before '/'");
}

// a - -; — second '-' is unary minus with no operand before the ';'.
TEST_F(ParserTest, Chapter5_MalformedDecrement_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int a = 0;\n    a - -;\n    return a;\n}\n")),
        "expected an expression before ';'");
}

// a + +; — second '+' is unary plus with no operand before the ';'.
TEST_F(ParserTest, Chapter5_MalformedIncrement_Neg)
{
    EXPECT_DEATH(
        parse(CreateTempFile("int main(void) {\n    int a = 0;\n    a + +;\n    return a;\n}\n")),
        "expected an expression before ';'");
}

// return 1 < = 2; — '<' and '=' lex separately, so '=' has no left operand.
TEST_F(ParserTest, Chapter5_MalformedLessEqual_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return 1 < = 2;\n}\n")),
                 "expected an expression before '='");
}

// return 1 ! = 0; — '!' is unary and has no place after '1'.
TEST_F(ParserTest, Chapter5_MalformedNotEqual_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    return 1 ! = 0;\n}\n")),
                 "expected ';' before '!'");
}

// int a = 2  (missing ';' before the next statement).
TEST_F(ParserTest, Chapter5_MissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(
                     "int main(void) {\n    int a = 2\n    a = a + 4;\n    return a;\n}\n")),
                 "expected ';' before 'a'");
}

// int 10 = return 0; — integer constant as declarator name.
TEST_F(ParserTest, Chapter5_ReturnInAssignment_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void)\n{\n    int 10 = return 0;\n}\n")),
                 "expected an identifier or '\\(' before '10'");
}

// --- invalid_parse / extra_credit -------------------------------------------

// return a -- 1; — postfix '--' cannot be followed by another operand.
TEST_F(ParserTest, Chapter5_BinaryDecrement_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    int a = 0;\n    return a -- 1;\n}\n")),
                 "expected ';' before '1'");
}

// return a ++ 1; — postfix '++' cannot be followed by another operand.
TEST_F(ParserTest, Chapter5_BinaryIncrement_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    int a = 0;\n    return a ++ 1;\n}\n")),
                 "expected ';' before '1'");
}

// int a += 0; — a compound assignment is not a valid initializer.
TEST_F(ParserTest, Chapter5_CompoundInitializer_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    int a += 0;\n    return a;\n}\n")),
                 "expected ';' before '\\+='");
}

// int a++; — a declarator cannot carry a postfix '++'.
TEST_F(ParserTest, Chapter5_IncrementDeclaration_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main(void) {\n    int a++;\n    return 0;\n}\n")),
                 "expected ';' before '\\+\\+'");
}
