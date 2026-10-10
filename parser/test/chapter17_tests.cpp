//
// Chapter 17 — Supporting types: void, sizeof, and dynamic allocation: invalid
// parser input.  Imported from "Writing a C Compiler" (tests/chapter_17/
// invalid_parse).  Each program combines `void` with another type specifier or
// applies `sizeof` to an unparenthesized type/cast.  Tests assert on a substring
// of the parser's diagnostic.
//
#include "fixture.h"

// void char *x; — void cannot combine with another type specifier.
TEST_F(ParserTest, Chapter17_BadSpecifier2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(void char *x;

int main(void) { return 0; }
)")),
                 "cannot combine 'char' with 'void'");
}

// unsigned void *v; — void cannot combine with modifiers.
TEST_F(ParserTest, Chapter17_BadSpecifier_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
  unsigned void *v;
  return 0;
}
)")),
                 "cannot combine 'void' or '_Bool' with another type specifier");
}

// sizeof(char) 1 — sizeof applied directly to a cast expression.
TEST_F(ParserTest, Chapter17_SizeofCast_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return sizeof(char) 1;
}
)")),
                 "expected ';'");
}

// sizeof int — a type name needs parentheses after sizeof.
TEST_F(ParserTest, Chapter17_SizeofTypeNoParens_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    return sizeof int;
}
)")),
                 "expected an expression before 'int'");
}
