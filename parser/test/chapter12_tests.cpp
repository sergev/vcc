//
// Chapter 12 — Unsigned integers: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_12/invalid_parse).  Each
// program is lexically valid but pairs incompatible signedness specifiers, which
// the parser's type-specifier fusion rejects.  Tests assert on a substring of
// the diagnostic.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// "(signed unsigned) i" — a type name may not combine 'signed' and 'unsigned'.
TEST_F(ParserTest, Chapter12_BadSpecifiers_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int i = 0;
    return (signed unsigned) i;
}
)")),
                 "cannot combine 'unsigned' with 'signed'");
}

// "unsigned long unsigned i" — the 'unsigned' specifier may not appear twice.
TEST_F(ParserTest, Chapter12_BadSpecifiers2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    unsigned long unsigned i = 0;
    return 0;
}
)")),
                 "duplicate 'unsigned'");
}
