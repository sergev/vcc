//
// Chapter 1 — A Minimal Compiler: valid programs (compile and run on Dubna).
// Imported from "Writing a C Compiler" (tests/chapter_1/valid).
// Each program is `int main(void) { return N; }` with varied whitespace; the
// wrapper prints main()'s return value, which we compare against.
//
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "book_test.h"

// fatal_error() is defined once for the whole besm-tests binary in
// codegen_tests.cpp; the compiler libraries call it.

// return 100; — multi-digit constant.
TEST_F(BookTest, Chapter1_MultiDigit)
{
    EXPECT_EQ("100\n", CompileAndRunBook("int main(void) { return 100; }"));
}

// Tokens split across many newlines.
TEST_F(BookTest, Chapter1_Newlines)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int\nmain\n(\nvoid\n)\n{\nreturn\n0\n;\n}"));
}

// No whitespace at all between tokens.
TEST_F(BookTest, Chapter1_NoNewlines)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void){return 0;}"));
}

// return 0;
TEST_F(BookTest, Chapter1_Return0)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 0; }"));
}

// return 2;
TEST_F(BookTest, Chapter1_Return2)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return 2; }"));
}

// Extra spaces between tokens.
TEST_F(BookTest, Chapter1_Spaces)
{
    EXPECT_EQ("0\n", CompileAndRunBook("   int   main    (  void)  {   return  0 ; }"));
}

// Tabs between tokens.
TEST_F(BookTest, Chapter1_Tabs)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int\tmain\t(\tvoid)\t{\treturn\t0\t;\t}"));
}
