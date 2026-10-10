//
// Chapter 16 — Characters and strings: invalid parser input.  Imported from
// "Writing a C Compiler" (tests/chapter_16/invalid_parse and
// invalid_parse/extra_credit).  Each program parses a malformed type specifier,
// a constant used where a postfix operator / declarator name is expected, or a
// character/string constant used as a goto target or label.  Tests assert on a
// substring of the parser's diagnostic.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// int char x = 10; — char cannot combine with another type specifier (except
// signed/unsigned).
TEST_F(ParserTest, Chapter16_InvalidTypeSpecifier_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void)
{
    int char x = 10;
    return x;
}
)")),
                 "cannot combine 'char' with 'int'");
}

// char static long x = 0; — char cannot combine with long.
TEST_F(ParserTest, Chapter16_InvalidTypeSpecifier2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    char static long x = 0;
    return 0;
}
)")),
                 "cannot combine 'long' with 'char'");
}

// return a'1'; — a character constant is not a postfix operator.
TEST_F(ParserTest, Chapter16_MisplacedCharLiteral_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a = 3;
    return a'1';
}
)")),
                 "expected ';'");
}

// int "x" = 0; — a string literal is not a valid declarator name.
TEST_F(ParserTest, Chapter16_StringLiteralVarname_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int "x" = 0;
    return 0;
}
)")),
                 "expected an identifier or '\\(' before '\"x\"'");
}

// --- invalid_parse/extra_credit ---------------------------------------------

// goto 'x'; — a character constant is not a goto target.
TEST_F(ParserTest, Chapter16_CharacterConstGoto_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    goto 'x';
    'x';
    return 0;
}
)")),
                 "expected identifier");
}

// 'x': return 0; — a character constant is not a label.
TEST_F(ParserTest, Chapter16_CharacterConstLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    'x': return 0;
}
)")),
                 "expected ';'");
}

// goto "foo"; — a string literal is not a goto target.
TEST_F(ParserTest, Chapter16_StringLiteralGoto_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    goto "foo";
    return 0;
}
)")),
                 "expected identifier");
}

// "foo": return 0; — a string literal is not a label.
TEST_F(ParserTest, Chapter16_StringLiteralLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    "foo": return 0;
}
)")),
                 "expected ';'");
}
