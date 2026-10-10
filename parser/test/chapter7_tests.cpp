//
// Chapter 7 — Compound statements: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_7/invalid_parse).  Each
// program is lexically valid but cannot be parsed; the parser reports a fatal
// error.  Tests assert on the diagnostic text.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// A stray '}' closes main early, so 'return 2;' lands at file scope.
TEST_F(ParserTest, Chapter7_ExtraBrace_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    if(0){
        return 1;
    }}
    return 2;
}
)")),
                 "expected a type before 'return'");
}

// An unbalanced '{' leaves main's block open and parsing hits end of file.
TEST_F(ParserTest, Chapter7_MissingBrace_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    if(0){
        return 1;
    return 2;
}
)")),
                 "expected '\\}' at end of file");
}

// 'return a' with no terminating ';' before the closing brace.
TEST_F(ParserTest, Chapter7_MissingSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a = 4;
    {
        a = 5;
        return a
    }
}
)")),
                 "expected ';' before '\\}'");
}

// '{' cannot start an expression, so the ternary branch fails to parse.
TEST_F(ParserTest, Chapter7_TernaryBlocks_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int a;
    return 1 ? { a = 2 } : a = 4;
}
)")),
                 "expected an expression before '\\{'");
}
