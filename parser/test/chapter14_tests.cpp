//
// Chapter 14 — Pointers: invalid parser input.  Imported from "Writing a C
// Compiler" (tests/chapter_14/invalid_parse).  Each program contains a
// malformed (abstract) declarator that the parser rejects.  Tests assert on a
// substring of the diagnostic.
//
// Two of the book's invalid_parse programs (abstract_function_declarator,
// malformed_function_declarator) parse cleanly for us — the grammar permits the
// declarator — and are rejected by the type checker instead, so they live in
// semantic/chapter14_tests.cpp.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// return (int **a)(10); — can only cast to an abstract declarator, not a named one.
TEST_F(ParserTest, Chapter14_CastToDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void)
{
    return (int **a)(10);
}
)")),
                 "expected ')'");
}

// (int (*)*) 10; — a pointer declarator cannot follow a parenthesized declarator.
TEST_F(ParserTest, Chapter14_MalformedAbstractDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    (int (*)*) 10;
    return 0;
}
)")),
                 "expected ')'");
}

// int (*)* y; — same malformed declarator in a declaration.
TEST_F(ParserTest, Chapter14_MalformedDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int (*)* y;
    return 0;
}
)")),
                 "expected an identifier or '\\(' before '\\)'");
}

// int foo((void)); — a parameter list cannot be wrapped in extra parentheses.
TEST_F(ParserTest, Chapter14_MalformedFunctionDeclarator2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo((void));
)")),
                 "expected a type before '\\('");
}
