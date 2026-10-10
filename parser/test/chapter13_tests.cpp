//
// Chapter 13 — Floating-point: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_13/invalid_parse).  Each
// program is lexically valid but pairs incompatible type specifiers, which the
// parser's type-specifier fusion rejects.  Tests assert on a substring of the
// diagnostic.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// "double double d" — 'double' may not appear twice.
TEST_F(ParserTest, Chapter13_DoubleDouble_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    double double d = 10.0;
    return 0;
}
)")),
                 "cannot combine 'double' with 'double'");
}

// "unsigned double d" — 'unsigned' may not combine with a floating type.
TEST_F(ParserTest, Chapter13_UnsignedDouble_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    unsigned double d = 10.0;
    return 0;
}
)")),
                 "cannot combine 'signed', 'unsigned' or 'long' with 'float' or 'double'");
}
