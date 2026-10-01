//
// Chapter 2 — Unary Operators: valid programs (compile and run on Dubna).
// Imported from "Writing a C Compiler" (tests/chapter_2/valid).
// Each program returns a unary expression (negate '-' and complement '~'); the
// wrapper prints main()'s return value, which we compare against.
//
#include "book_test.h"

// return ~12;
TEST_F(BookTest, Chapter2_Bitwise)
{
    EXPECT_EQ("-13\n", CompileAndRunBook("int main(void) { return ~12; }"));
}

// return ~-2147483647; — complement of the smallest int we can construct.
TEST_F(BookTest, Chapter2_BitwiseIntMin)
{
    EXPECT_EQ("2147483646\n", CompileAndRunBook("int main(void) { return ~-2147483647; }"));
}

// return ~0;
TEST_F(BookTest, Chapter2_BitwiseZero)
{
    EXPECT_EQ("-1\n", CompileAndRunBook("int main(void) { return ~0; }"));
}

// return -5;
TEST_F(BookTest, Chapter2_Neg)
{
    EXPECT_EQ("-5\n", CompileAndRunBook("int main(void) { return -5; }"));
}

// return -0;
TEST_F(BookTest, Chapter2_NegZero)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return -0; }"));
}

// return -2147483647; — the largest int negated.
TEST_F(BookTest, Chapter2_NegateIntMax)
{
    EXPECT_EQ("-2147483647\n", CompileAndRunBook("int main(void) { return -2147483647; }"));
}

// return ~-3;
TEST_F(BookTest, Chapter2_NestedOps)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return ~-3; }"));
}

// return -~0;
TEST_F(BookTest, Chapter2_NestedOps2)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return -~0; }"));
}

// return (-2);
TEST_F(BookTest, Chapter2_Parens)
{
    EXPECT_EQ("-2\n", CompileAndRunBook("int main(void) { return (-2); }"));
}

// return ~(2);
TEST_F(BookTest, Chapter2_Parens2)
{
    EXPECT_EQ("-3\n", CompileAndRunBook("int main(void) { return ~(2); }"));
}

// return -(-4);
TEST_F(BookTest, Chapter2_Parens3)
{
    EXPECT_EQ("4\n", CompileAndRunBook("int main(void) { return -(-4); }"));
}

// return -((((10))));
TEST_F(BookTest, Chapter2_RedundantParens)
{
    EXPECT_EQ("-10\n", CompileAndRunBook("int main(void) { return -((((10)))); }"));
}
