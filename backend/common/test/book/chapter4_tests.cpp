//
// Chapter 4 — Logical & Relational Operators: valid programs (compile and run on Dubna).
// Imported from "Writing a C Compiler" (tests/chapter_4/valid + extra_credit).
// Each program returns a logical/relational expression; the wrapper prints main()'s
// return value, which we compare against.  All results fit in 32 bits, so the host cc
// reference and our 41-bit BESM-6 int agree.  Short-circuit cases (e.g. 0 && (1/0))
// rely on the translator's && / || short-circuit lowering, so the divide is never run.
//
#include "book_test.h"

// --- valid: logical and (&&), short-circuit ---------------------------------

// return (10 && 0) + (0 && 4) + (0 && 0);
TEST_F(BookTest, Chapter4_AndFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return (10 && 0) + (0 && 4) + (0 && 0); }"));
}

// return 0 && (1 / 0); — short-circuits before the divide-by-zero: 0.
TEST_F(BookTest, Chapter4_AndShortCircuit)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 0 && (1 / 0); }"));
}

// return 1 && -1;
TEST_F(BookTest, Chapter4_AndTrue)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 1 && -1; }"));
}

// --- valid: relational operators --------------------------------------------

// return 5 >= 0 > 1 <= 0; — left-associative relational chain: ((5>=0)>1)<=0 = 1.
TEST_F(BookTest, Chapter4_Associativity)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 5 >= 0 > 1 <= 0; }"));
}

// return ~2 * -2 == 1 + 5; — (~2 * -2) = 6, 6 == 6 = 1.
TEST_F(BookTest, Chapter4_CompareArithmeticResults)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return ~2 * -2 == 1 + 5; }"));
}

// return 1 == 2;
TEST_F(BookTest, Chapter4_EqFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 1 == 2; }"));
}

// return 3 == 1 != 2; — left-associative: (3==1) != 2 = 0 != 2 = 1.
TEST_F(BookTest, Chapter4_EqPrecedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 3 == 1 != 2; }"));
}

// return 1 == 1;
TEST_F(BookTest, Chapter4_EqTrue)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 1 == 1; }"));
}

// return 1 >= 2;
TEST_F(BookTest, Chapter4_GeFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 1 >= 2; }"));
}

// return (1 >= 1) + (1 >= -4);
TEST_F(BookTest, Chapter4_GeTrue)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return (1 >= 1) + (1 >= -4); }"));
}

// return (1 > 2) + (1 > 1);
TEST_F(BookTest, Chapter4_GtFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return (1 > 2) + (1 > 1); }"));
}

// return 15 > 10;
TEST_F(BookTest, Chapter4_GtTrue)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 15 > 10; }"));
}

// return 1 <= -1;
TEST_F(BookTest, Chapter4_LeFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 1 <= -1; }"));
}

// return (0 <= 2) + (0 <= 0);
TEST_F(BookTest, Chapter4_LeTrue)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return (0 <= 2) + (0 <= 0); }"));
}

// return 2 < 1;
TEST_F(BookTest, Chapter4_LtFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 2 < 1; }"));
}

// return 1 < 2;
TEST_F(BookTest, Chapter4_LtTrue)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 1 < 2; }"));
}

// return 0 || 0 && (1 / 0); — '&&' binds tighter than '||': 0 || (0 && …) = 0.
TEST_F(BookTest, Chapter4_MultiShortCircuit)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 0 || 0 && (1 / 0); }"));
}

// return 0 != 0;
TEST_F(BookTest, Chapter4_NeFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 0 != 0; }"));
}

// return -1 != -2;
TEST_F(BookTest, Chapter4_NeTrue)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return -1 != -2; }"));
}

// --- valid: logical not (!) -------------------------------------------------

// return !-3; — !(−3) = 0.
TEST_F(BookTest, Chapter4_NestedOps)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return !-3; }"));
}

// return !(3 - 44); — !(−41) = 0.
TEST_F(BookTest, Chapter4_NotSum2)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return !(3 - 44); }"));
}

// return !(4-4); — !0 = 1.
TEST_F(BookTest, Chapter4_NotSum)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return !(4-4); }"));
}

// return !0;
TEST_F(BookTest, Chapter4_NotZero)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return !0; }"));
}

// return !5;
TEST_F(BookTest, Chapter4_Not)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return !5; }"));
}

// return ~(0 && 1) - -(4 || 3); — ~0 - -(1) = -1 + 1 = 0.
TEST_F(BookTest, Chapter4_OperateOnBooleans)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return ~(0 && 1) - -(4 || 3); }"));
}

// --- valid: logical or (||), short-circuit ----------------------------------

// return 0 || 0;
TEST_F(BookTest, Chapter4_OrFalse)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 0 || 0; }"));
}

// return 1 || (1 / 0); — short-circuits before the divide-by-zero: 1.
TEST_F(BookTest, Chapter4_OrShortCircuit)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 1 || (1 / 0); }"));
}

// return (4 || 0) + (0 || 3) + (5 || 5);
TEST_F(BookTest, Chapter4_OrTrue)
{
    EXPECT_EQ("3\n", CompileAndRunBook("int main(void) { return (4 || 0) + (0 || 3) + (5 || 5); }"));
}

// --- valid: precedence ------------------------------------------------------

// return (1 || 0) && 0; — 1 && 0 = 0.
TEST_F(BookTest, Chapter4_Precedence2)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return (1 || 0) && 0; }"));
}

// return 2 == 2 >= 0; — '>=' binds tighter than '==': 2 == (2>=0) = 2 == 1 = 0.
TEST_F(BookTest, Chapter4_Precedence3)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 2 == 2 >= 0; }"));
}

// return 2 == 2 || 0; — '==' binds tighter than '||': (2==2) || 0 = 1.
TEST_F(BookTest, Chapter4_Precedence4)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 2 == 2 || 0; }"));
}

// return (0 == 0 && 3 == 2 + 1 > 1) + 1; — (1 && (3 == ((2+1)>1))) + 1 = (1 && 0) + 1 = 1.
TEST_F(BookTest, Chapter4_Precedence5)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return (0 == 0 && 3 == 2 + 1 > 1) + 1; }"));
}

// return 1 || 0 && 2; — '&&' binds tighter than '||': 1 || (0 && 2) = 1.
TEST_F(BookTest, Chapter4_Precedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 1 || 0 && 2; }"));
}

// --- extra credit: bitwise vs. relational/equality precedence ---------------

// return 5 & 7 == 5; — '&' lower than '==': 5 & (7==5) = 5 & 0 = 0.
TEST_F(BookTest, Chapter4_BitwiseAndPrecedence)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { return 5 & 7 == 5; }"));
}

// return 5 | 7 != 5; — '|' lower than '!=': 5 | (7!=5) = 5 | 1 = 5.
TEST_F(BookTest, Chapter4_BitwiseOrPrecedence)
{
    EXPECT_EQ("5\n", CompileAndRunBook("int main(void) { return 5 | 7 != 5; }"));
}

// return 20 >> 4 <= 3 << 1; — shifts bind tighter than '<=': (20>>4) <= (3<<1) = 1 <= 6 = 1.
TEST_F(BookTest, Chapter4_BitwiseShiftPrecedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 20 >> 4 <= 3 << 1; }"));
}

// return 5 ^ 7 < 5; — '^' lower than '<': 5 ^ (7<5) = 5 ^ 0 = 5.
TEST_F(BookTest, Chapter4_BitwiseXorPrecedence)
{
    EXPECT_EQ("5\n", CompileAndRunBook("int main(void) { return 5 ^ 7 < 5; }"));
}
