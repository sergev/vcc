//
// Chapter 1 — A Minimal Compiler: invalid lexical input.
// Imported from "Writing a C Compiler" (tests/chapter_1/invalid_lex).
// Each program contains a character or token the scanner must reject.
//
#include "scan_fixture.h"

// return 0@1; — '@' is not part of any C token outside a literal.
TEST(ScannerChapter1, AtSign_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 0@1;\n}\n"), "invalid character '@'");
}

// A lone backslash is not a valid token.
TEST(ScannerChapter1, Backslash_Neg)
{
    EXPECT_DEATH(LexToEnd("\\\n"), "invalid character");
}

// A backtick is not a valid token.
TEST(ScannerChapter1, Backtick_Neg)
{
    EXPECT_DEATH(LexToEnd("`\n"), "invalid character '`'");
}

// return @b; — stray '@' before an identifier.
TEST(ScannerChapter1, InvalidIdentifier2_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    return @b;\n}\n"), "invalid character '@'");
}

// return 1foo; — an identifier may not start with a digit; '1foo' is a single
// invalid numeric token, not '1f' followed by 'oo'.
TEST(ScannerChapter1, InvalidIdentifier_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 1foo;\n}\n"),
                 "invalid suffix on numeric constant");
}
