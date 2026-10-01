//
// Chapter 3 — Binary Operators: valid programs (compile and run on Dubna).
// Imported from "Writing a C Compiler" (tests/chapter_3/valid + extra_credit).
// Each program returns a binary expression; the wrapper prints main()'s return
// value, which we compare against.  All results fit in 32 bits, so the host cc
// reference and our 41-bit BESM-6 int agree.
//
#include "book_test.h"

// --- valid: arithmetic, grouping, precedence, associativity ----------------

// return 1 + 2;
TEST_F(BookTest, Chapter3_Add)
{
    EXPECT_EQ("3\n", CompileAndRunBook("int main(void) { return 1 + 2; }"));
}

// return 1 - 2 - 3; — left-associative: (1-2)-3 = -4.
TEST_F(BookTest, Chapter3_Associativity)
{
    EXPECT_EQ("-4\n", CompileAndRunBook("int main(void) { return 1 - 2 - 3; }"));
}

// return 6 / 3 / 2; — left-associative: (6/3)/2 = 1.
TEST_F(BookTest, Chapter3_Associativity2)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 6 / 3 / 2; }"));
}

// return (3 / 2 * 4) + (5 - 4 + 3); — 4 + 4 = 8.
TEST_F(BookTest, Chapter3_Associativity3)
{
    EXPECT_EQ("8\n", CompileAndRunBook("int main(void) { return (3 / 2 * 4) + (5 - 4 + 3); }"));
}

// return 5 * 4 / 2 - 3 % (2 + 1); — 10 - 0 = 10.
TEST_F(BookTest, Chapter3_AssociativityAndPrecedence)
{
    EXPECT_EQ("10\n", CompileAndRunBook("int main(void) { return 5 * 4 / 2 - 3 % (2 + 1); }"));
}

// return 4 / 2;
TEST_F(BookTest, Chapter3_Div)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return 4 / 2; }"));
}

// return (-12) / 5; — signed divide truncates toward zero: -2.
TEST_F(BookTest, Chapter3_DivNeg)
{
    EXPECT_EQ("-2\n", CompileAndRunBook("int main(void) { return (-12) / 5; }"));
}

// return 4 % 2;
TEST_F(BookTest, Chapter3_Mod)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 4 % 2; }"));
}

// return 2 * 3;
TEST_F(BookTest, Chapter3_Mult)
{
    EXPECT_EQ("6\n", CompileAndRunBook("int main(void) { return 2 * 3; }"));
}

// return 2 * (3 + 4); — parentheses override precedence: 2 * 7 = 14.
TEST_F(BookTest, Chapter3_Parens)
{
    EXPECT_EQ("14\n", CompileAndRunBook("int main(void) { return 2 * (3 + 4); }"));
}

// return 2 + 3 * 4; — '*' binds tighter than '+': 2 + 12 = 14.
TEST_F(BookTest, Chapter3_Precedence)
{
    EXPECT_EQ("14\n", CompileAndRunBook("int main(void) { return 2 + 3 * 4; }"));
}

// return 1 - 2;
TEST_F(BookTest, Chapter3_Sub)
{
    EXPECT_EQ("-1\n", CompileAndRunBook("int main(void) { return 1 - 2; }"));
}

// return 2- -1; — binary minus of a negated literal: 2 - (-1) = 3.
TEST_F(BookTest, Chapter3_SubNeg)
{
    EXPECT_EQ("3\n", CompileAndRunBook("int main(void) { return 2- -1; }"));
}

// return ~2 + 3; — unary '~' binds tighter than '+': -3 + 3 = 0.
TEST_F(BookTest, Chapter3_UnopAdd)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return ~2 + 3; }"));
}

// return ~(1 + 1); — complement of 2 = -3.
TEST_F(BookTest, Chapter3_UnopParens)
{
    EXPECT_EQ("-3\n", CompileAndRunBook("int main(void) { return ~(1 + 1); }"));
}

// --- extra credit: bitwise and shift operators -----------------------------

// return 3 & 5;
TEST_F(BookTest, Chapter3_BitwiseAnd)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 3 & 5; }"));
}

// return 1 | 2;
TEST_F(BookTest, Chapter3_BitwiseOr)
{
    EXPECT_EQ("3\n", CompileAndRunBook("int main(void) { return 1 | 2; }"));
}

// return 7 ^ 1;
TEST_F(BookTest, Chapter3_BitwiseXor)
{
    EXPECT_EQ("6\n", CompileAndRunBook("int main(void) { return 7 ^ 1; }"));
}

// return 35 << 2; — left shift by 2: 140.
TEST_F(BookTest, Chapter3_BitwiseShiftl)
{
    EXPECT_EQ("140\n", CompileAndRunBook("int main(void) { return 35 << 2; }"));
}

// return 1000 >> 4; — logical right shift of a positive value: 62.
TEST_F(BookTest, Chapter3_BitwiseShiftr)
{
    EXPECT_EQ("62\n", CompileAndRunBook("int main(void) { return 1000 >> 4; }"));
}

// return 33 << 4 >> 2; — left-associative: (33<<4)>>2 = 132.
TEST_F(BookTest, Chapter3_BitwiseShiftAssociativity)
{
    EXPECT_EQ("132\n", CompileAndRunBook("int main(void) { return 33 << 4 >> 2; }"));
}

// return 33 >> 2 << 1; — left-associative: (33>>2)<<1 = 16.
TEST_F(BookTest, Chapter3_BitwiseShiftAssociativity2)
{
    EXPECT_EQ("16\n", CompileAndRunBook("int main(void) { return 33 >> 2 << 1; }"));
}

// return 40 << 4 + 12 >> 1; — '+' binds tighter than shifts: 40<<16>>1 = 1310720.
TEST_F(BookTest, Chapter3_BitwiseShiftPrecedence)
{
    EXPECT_EQ("1310720\n", CompileAndRunBook("int main(void) { return 40 << 4 + 12 >> 1; }"));
}

// return 80 >> 2 | 1 ^ 5 & 7 << 1; — shift > & > ^ > |: 20 | (1 ^ (5 & 14)) = 21.
TEST_F(BookTest, Chapter3_BitwisePrecedence)
{
    EXPECT_EQ("21\n", CompileAndRunBook("int main(void) { return 80 >> 2 | 1 ^ 5 & 7 << 1; }"));
}

// return (4 << (2 * 2)) + (100 >> (1 + 2)); — variable shift counts: 64 + 12 = 76.
TEST_F(BookTest, Chapter3_BitwiseVariableShiftCount)
{
    EXPECT_EQ("76\n",
              CompileAndRunBook("int main(void) { return (4 << (2 * 2)) + (100 >> (1 + 2)); }"));
}

// return -5 >> 30; — on BESM-6 a signed right shift is logical (the shift unit does no
// sign extension), so the optimizer folds it to match the backend at runtime: the 41-bit
// pattern of -5 (2^41 - 5) shifted right by 30 is 2047, not the arithmetic -1.
TEST_F(BookTest, Chapter3_BitwiseShiftrNegative)
{
    EXPECT_EQ("2047\n", CompileAndRunBook("int main(void) { return -5 >> 30; }"));
}
