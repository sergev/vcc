//
// Chapter 16 — Characters and strings: invalid lexical input.
// Imported from "Writing a C Compiler" (tests/chapter_16/invalid_lex).
// Each program contains a malformed character or string literal the scanner
// must reject: an unknown escape sequence, an unterminated literal, or a
// string/character constant broken across a newline.
//
// Two of the book's programs (char_bad_escape_sequence, string_bad_escape_
// sequence) required a scanner fix: scan_char/scan_string previously consumed
// any character after a backslash, silently accepting unknown escapes like
// '\y'.  Both now call lex_error("invalid escape sequence ...").
//
// unescaped_double_quote ("foo"bar") is in the book's invalid_lex set; under the
// scanner alone (no parser to stop at the stray identifier) it tokenizes "foo",
// `bar`, then opens a final string that runs to EOF, so it dies as an
// unterminated string here — a lexical error, kept with the other lex tests.
//
#include "scan_fixture.h"

// return '\y'; — '\y' is not a valid escape sequence.
TEST(ScannerChapter16, CharBadEscapeSequence_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    return '\\y';\n}\n"), "invalid escape sequence");
}

// char *str = "foo\ybar"; — '\y' is not a valid escape sequence.
TEST(ScannerChapter16, StringBadEscapeSequence_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    char *str = \"foo\\ybar\";\n    return 0;\n}\n"),
                 "invalid escape sequence");
}

// char *s = "hello<newline>world "; — a string literal may not span a newline.
TEST(ScannerChapter16, Newline_Neg)
{
    EXPECT_DEATH(LexToEnd("char *s = \"hello\n    world \";\n"), "unterminated string");
}

// char *ptr = "foo"bar"; — the stray '"' opens a string that runs to EOF.
TEST(ScannerChapter16, UnescapedDoubleQuote_Neg)
{
    EXPECT_DEATH(
        LexToEnd("int main(void)\n{\n    char *ptr = \"foo\"bar\";\n        return 0;\n}\n"),
        "unterminated string");
}

// char *ptr = "foo\"; — the escaped quote leaves the string unterminated.
TEST(ScannerChapter16, UnterminatedString_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    char *ptr = \"foo\\\";\n    return 0;\n}\n"),
                 "unterminated string");
}

// return '\'; — the escaped quote leaves the character literal unterminated.
TEST(ScannerChapter16, UnescapedBackslash_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    return '\\';\n}\n"),
                 "unterminated character literal");
}

// return 'x<newline>} — a character literal may not span a newline.
TEST(ScannerChapter16, UnterminatedCharConstant_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    return 'x\n}\n"),
                 "unterminated character literal");
}

// return '''; — '' is an empty character literal.
TEST(ScannerChapter16, UnescapedSingleQuote_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void)\n{\n    return ''';\n}\n"), "empty character literal");
}
