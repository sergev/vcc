//
// Negative parser tests: inputs that must cause fatal_error() / exit(1).
// All tests use EXPECT_DEATH since the parser never returns nullptr on error.
//
#include "fixture.h"

// ---------------------------------------------------------------------------
// A. Type specifier conflicts
// ---------------------------------------------------------------------------

TEST_F(ParserTest, DuplicateInt_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("int int x;")), "");
}

TEST_F(ParserTest, TooManyLong_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("long long long x;")), "");
}

TEST_F(ParserTest, VoidCombine_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void int x;")), "");
}

TEST_F(ParserTest, BoolCombine_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("_Bool int x;")), "");
}

TEST_F(ParserTest, SignedFloat_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("signed float x;")), "");
}

TEST_F(ParserTest, SignedDouble_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("signed double x;")), "");
}

TEST_F(ParserTest, UnsignedFloat_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("unsigned float x;")), "");
}

// ---------------------------------------------------------------------------
// B. Declaration syntax errors
// ---------------------------------------------------------------------------

TEST_F(ParserTest, MissingSemicolon_negative)
{
    // int x with no terminating semicolon
    EXPECT_DEATH(parse(CreateTempFile("int x")), "");
}

TEST_F(ParserTest, UnclosedStruct_negative)
{
    // struct body never closed
    EXPECT_DEATH(parse(CreateTempFile("struct S { int x;")), "");
}

TEST_F(ParserTest, VariadicNoParams_negative)
{
    // ... requires at least one named parameter before it
    EXPECT_DEATH(parse(CreateTempFile("int f(...) {}")), "");
}

TEST_F(ParserTest, TrailingCommaParams_negative)
{
    // trailing comma leaves an empty parameter slot
    EXPECT_DEATH(parse(CreateTempFile("int f(int,) {}")), "");
}

TEST_F(ParserTest, MissingTypedefName_negative)
{
    // typedef with no type specifier at all
    EXPECT_DEATH(parse(CreateTempFile("typedef;")), "");
}

// ---------------------------------------------------------------------------
// C. Statement syntax errors
// ---------------------------------------------------------------------------

TEST_F(ParserTest, StmtMissingSemicolon_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void f() { int x = 1 }")), "");
}

TEST_F(ParserTest, IfUnclosedParen_negative)
{
    // condition paren never closed
    EXPECT_DEATH(parse(CreateTempFile("void f() { if (x {} }")), "");
}

TEST_F(ParserTest, WhileEmptyCondition_negative)
{
    // while () has no condition expression
    EXPECT_DEATH(parse(CreateTempFile("void f() { while () ; }")), "");
}

TEST_F(ParserTest, ForExtraSemicolon_negative)
{
    // for (;;;) has a third semicolon where ) is expected
    EXPECT_DEATH(parse(CreateTempFile("void f() { for (;;;) ; }")), "");
}

TEST_F(ParserTest, GotoMissingLabel_negative)
{
    // goto requires an identifier
    EXPECT_DEATH(parse(CreateTempFile("void f() { goto; }")), "");
}

// ---------------------------------------------------------------------------
// D. Expression syntax errors
// ---------------------------------------------------------------------------

TEST_F(ParserTest, MissingRightOperand_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void f() { int x = 1 + ; }")), "");
}

TEST_F(ParserTest, UnclosedParenExpr_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void f() { int x = (1 + 2; }")), "");
}

TEST_F(ParserTest, UnclosedSubscript_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void f() { int *p; int x = p[1; }")), "");
}

TEST_F(ParserTest, TernaryMissingColon_negative)
{
    // x ? y without the : else-branch
    EXPECT_DEATH(parse(CreateTempFile("void f() { int x = 1 ? 2; }")), "");
}

// ---------------------------------------------------------------------------
// E. C11 feature syntax errors
// ---------------------------------------------------------------------------

TEST_F(ParserTest, StaticAssertMissingComma_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("_Static_assert(1;")), "");
}

TEST_F(ParserTest, StaticAssertMissingParen_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("_Static_assert(1, \"msg\";")), "");
}

TEST_F(ParserTest, StaticAssertMissingSemi_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("_Static_assert(1, \"msg\")")), "");
}

// ---------------------------------------------------------------------------
// F. Comma operator where a constant expression is required (C11 6.6p3)
//
// These are the parse_constant_expression() sites: case labels, enumerators,
// bitfield widths, _Static_assert and _Alignas.  (An array dimension is *not*
// one — it parses as an assignment-expression and its constant-ness is settled
// later, in the semantic pass.)
// ---------------------------------------------------------------------------

TEST_F(ParserTest, CommaInCaseLabel_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("void f() { switch (x) { case (1, 2): ; } }")), "");
}

TEST_F(ParserTest, CommaInEnumerator_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("enum e { A = (1, 2) };")), "");
}

TEST_F(ParserTest, CommaInBitfieldWidth_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("struct S { int b : (1, 2); };")), "");
}

TEST_F(ParserTest, CommaInStaticAssert_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("_Static_assert((0, 1), \"msg\");")), "");
}

// ...but 6.6p3 exempts an unevaluated subexpression, so a comma inside sizeof
// still leaves a constant expression: is_constant_expression() answers true for
// EXPR_SIZEOF_EXPR without recursing into the operand.
TEST_F(ParserTest, CommaInsideSizeofIsConstant)
{
    Declaration *decl = GetDeclaration("_Static_assert(sizeof(1, 2) > 0, \"msg\");");
    EXPECT_EQ(DECL_STATIC_ASSERT, decl->kind);
}

TEST_F(ParserTest, FuncNameOutsideFunction_negative)
{
    EXPECT_DEATH(parse(CreateTempFile("const char *p = __func__;")), "");
}
