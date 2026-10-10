//
// Chapter 13 — Floating-point: invalid lexical input.
// Imported from "Writing a C Compiler" (tests/chapter_13/invalid_lex).
// Each program contains a malformed floating-point constant the scanner must
// reject: a constant may not run straight into an identifier character or a
// second '.', and an exponent marker must be followed by at least one digit.
//
#include "scan_fixture.h"

// return 1.ex; — "1.ex" is a pp-number whose 'e' exponent has no digits.
TEST(ScannerChapter13, AnotherBadConstant_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 1.ex;\n}\n"), "missing exponent");
}

// double foo = 1E2x; — "1E2x" ends in a stray letter.
TEST(ScannerChapter13, BadExponentSuffix_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    double foo = 1E2x;\n}\n"),
                 "invalid suffix on numeric constant");
}

// return 2._; — "2._" runs into an underscore.
TEST(ScannerChapter13, MalformedConst_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 2._;\n}\n"),
                 "invalid suffix on numeric constant");
}

// double d = 1.0e10.0; — one malformed pp-number, not "1.0e10" '.' '0'.
TEST(ScannerChapter13, MalformedExponent_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    double d = 1.0e10.0;\n    return 0;\n}\n"),
                 "invalid suffix on numeric constant");
}

// return 1.e-10x; — a valid exponent followed by a stray letter.
TEST(ScannerChapter13, YetAnotherBadConstant_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 1.e-10x;\n}\n"),
                 "invalid suffix on numeric constant");
}

// double foo = 30.e; — the exponent marker has no digits.
TEST(ScannerChapter13, MissingExponent_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    double foo = 30.e;\n    return 4;\n}\n"),
                 "missing exponent");
}

// double foo = 24e-; — the exponent is just a sign, no digits.
TEST(ScannerChapter13, MissingNegativeExponent_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    double foo = 24e-;\n}\n"), "missing exponent");
}
