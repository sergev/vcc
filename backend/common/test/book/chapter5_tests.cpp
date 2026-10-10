//
// Chapter 5 — Local Variables: valid programs (compile and run on Dubna).
// Imported from "Writing a C Compiler" (tests/chapter_5/valid + extra_credit).
// Each program defines int main(void); the wrapper prints main()'s return
// value, compared against the host cc reference for the same source.  These
// exercise local-variable frame allocation, assignment, compound assignment,
// and increment/decrement — all BESM-6 backend supported.  All results fit in
// both 32-bit host int and 41-bit BESM-6 int.
//
// The book's host-only "#ifdef SUPPRESS_WARNINGS / #pragma" blocks are dropped
// here: they suppress host-compiler warnings and our frontend has no
// preprocessor.
//
#include "book_test.h"

// --- valid ------------------------------------------------------------------

// two locals, return their sum.
TEST_F(BookTest, Chapter5_AddVariables)
{
    EXPECT_EQ("3\n",
              CompileAndRunBook("int main(void) { int first_variable = 1; int second_variable = 2; "
                                "return first_variable + second_variable; }"));
}

// a = 2147483646; c = a/6 + !b; return c*2 == a-1431655762; -> 1.
TEST_F(BookTest, Chapter5_AllocateTempsAndVars)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 2147483646; int b = 0; int c = a "
                                       "/ 6 + !b; return c * 2 == a - 1431655762; }"));
}

// int a = a = 5; — assignment used as an initializer.
TEST_F(BookTest, Chapter5_AssignValInInitializer)
{
    EXPECT_EQ("5\n", CompileAndRunBook("int main(void) { int a = a = 5; return a; }"));
}

// declare then assign.
TEST_F(BookTest, Chapter5_Assign)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { int var0; var0 = 2; return var0; }"));
}

// int b = a = 0; — nested assignment in an initializer.
TEST_F(BookTest, Chapter5_AssignmentInInitializer)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { int a; int b = a = 0; return b; }"));
}

// a = 0 || 5; — assignment has the lowest precedence.
TEST_F(BookTest, Chapter5_AssignmentLowestPrecedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a; a = 0 || 5; return a; }"));
}

// int main(void) {} — main falls off the end and implicitly returns 0 (C11 §5.1.2.2.3).
TEST_F(BookTest, Chapter5_EmptyFunctionBody)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { }"));
}

// a = a % 3 with a negative dividend, then b = -a.
TEST_F(BookTest, Chapter5_ExpThenDeclaration)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { int a = -2593; a = a % 3; int b = -a; return b; }"));
}

// identifiers that start with keywords.
TEST_F(BookTest, Chapter5_KwVarNames)
{
    EXPECT_EQ(
        "5\n",
        CompileAndRunBook(
            "int main(void) { int return_val = 3; int void2 = 2; return return_val + void2; }"));
}

// no return after the body; main implicitly returns 0 (C11 §5.1.2.2.3).
TEST_F(BookTest, Chapter5_LocalVarMissingReturn)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { int a = 3; a = a + 5; }"));
}

// a = 3 * (b = a); return a + b.
TEST_F(BookTest, Chapter5_MixedPrecedenceAssignment)
{
    EXPECT_EQ("4\n",
              CompileAndRunBook(
                  "int main(void) { int a = 1; int b = 0; a = 3 * (b = a); return a + b; }"));
}

// 0 || (a = 1); — '||' does not short-circuit the assignment.
TEST_F(BookTest, Chapter5_NonShortCircuitOr)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 0; 0 || (a = 1); return a; }"));
}

// just a null statement; main falls off the end and implicitly returns 0 (C11 §5.1.2.2.3).
TEST_F(BookTest, Chapter5_NullStatement)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { ; }"));
}

// null statement then return 0.
TEST_F(BookTest, Chapter5_NullThenReturn)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { ; return 0; }"));
}

// return a local.
TEST_F(BookTest, Chapter5_ReturnVar)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { int a = 2; return a; }"));
}

// 0 && (a = 5); — short-circuits, so a stays 0.
TEST_F(BookTest, Chapter5_ShortCircuitAndFail)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { int a = 0; 0 && (a = 5); return a; }"));
}

// 1 || (a = 1); — short-circuits, so a stays 0.
TEST_F(BookTest, Chapter5_ShortCircuitOr)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { int a = 0; 1 || (a = 1); return a; }"));
}

// 2 + 2; as a discarded expression statement.
TEST_F(BookTest, Chapter5_UnusedExp)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { 2 + 2; return 0; }"));
}

// return a = b = 4; — chained assignment as the return value.
TEST_F(BookTest, Chapter5_UseAssignmentResult)
{
    EXPECT_EQ("4\n",
              CompileAndRunBook("int main(void) { int a = 1; int b = 2; return a = b = 4; }"));
}

// int a = 0 && a; — 'a' referenced in its own initializer.
TEST_F(BookTest, Chapter5_UseValInOwnInitializer)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { int a = 0 && a; return a; }"));
}

// --- valid / extra_credit ---------------------------------------------------

// int b = a ^ 5; return 1 | b;
TEST_F(BookTest, Chapter5_BitwiseInInitializer)
{
    EXPECT_EQ("11\n",
              CompileAndRunBook("int main(void) { int a = 15; int b = a ^ 5; return 1 | b; }"));
}

// a & b | c with variables.
TEST_F(BookTest, Chapter5_BitwiseOpsVars)
{
    EXPECT_EQ("9\n", CompileAndRunBook(
                         "int main(void) { int a = 3; int b = 5; int c = 8; return a & b | c; }"));
}

// x << 3 with a variable left operand.
TEST_F(BookTest, Chapter5_BitwiseShiftlVariable)
{
    EXPECT_EQ("24\n", CompileAndRunBook("int main(void) { int x = 3; return x << 3; }"));
}

// x = var_to_shift >> 4;
TEST_F(BookTest, Chapter5_BitwiseShiftrAssign)
{
    EXPECT_EQ("77\n", CompileAndRunBook("int main(void) { int var_to_shift = 1234; int x = 0; x = "
                                        "var_to_shift >> 4; return x; }"));
}

// chained compound assignment; right-associative, self-checking.
TEST_F(BookTest, Chapter5_CompoundAssignmentChained)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 250; int b = 200; int c = 100; "
                                       "int d = 75; int e = -25; int f = 0; int x = 0; "
                                       "x = a += b -= c *= d /= e %= f = -7; "
                                       "return a == 2250 && b == 2000 && c == -1800 && d == -18 && "
                                       "e == -4 && f == -7 && x == 2250; }"));
}

// compound assignment has the lowest precedence.
TEST_F(BookTest, Chapter5_CompoundAssignmentLowestPrecedence)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook(
                  "int main(void) { int a = 10; int b = 12; a += 0 || b; b *= a && 0; int c = 14; "
                  "c -= a || b; "
                  "int d = 16; d /= c || d; return (a == 11 && b == 0 && c == 13 && d == 16); }"));
}

// int y = x += 3; — compound assignment yields its result.
TEST_F(BookTest, Chapter5_CompoundAssignmentUseResult)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook(
                  "int main(void) { int x = 1; int y = x += 3; return (x == 4 && y == 4); }"));
}

// to_and &= 6;
TEST_F(BookTest, Chapter5_CompoundBitwiseAnd)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook("int main(void) { int to_and = 3; to_and &= 6; return to_and; }"));
}

// chained bitwise compound assignment, lowest precedence, self-checking.
TEST_F(BookTest, Chapter5_CompoundBitwiseAssignmentLowestPrecedence)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { int a = 11; int b = 12; a &= 0 || b; b ^= a || "
                                "1; int c = 14; c |= a || b; "
                                "int d = 16; d >>= c || d; int e = 18; e <<= c || d; "
                                "return (a == 1 && b == 13 && c == 15 && d == 8 && e == 36); }"));
}

// long chain of bitwise compound assignments, self-checking.
TEST_F(BookTest, Chapter5_CompoundBitwiseChained)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { int a = 250; int b = 200; int c = 100; int d = "
                                "75; int e = 50; int f = 25; "
                                "int g = 10; int h = 1; int j = 0; int x = 0; "
                                "x = a &= b *= c |= d = e ^= f += g >>= h <<= j = 1; "
                                "return (a == 40 && b == 21800 && c == 109 && d == 41 && e == 41 "
                                "&& f == 27 && g == 2 && h == 2 && j == 1 && x == 40); }"));
}

// to_or |= 30;
TEST_F(BookTest, Chapter5_CompoundBitwiseOr)
{
    EXPECT_EQ("31\n",
              CompileAndRunBook("int main(void) { int to_or = 1; to_or |= 30; return to_or; }"));
}

// to_shiftl <<= 4;
TEST_F(BookTest, Chapter5_CompoundBitwiseShiftl)
{
    EXPECT_EQ("48\n",
              CompileAndRunBook(
                  "int main(void) { int to_shiftl = 3; to_shiftl <<= 4; return to_shiftl; }"));
}

// to_shiftr >>= 4;
TEST_F(BookTest, Chapter5_CompoundBitwiseShiftr)
{
    EXPECT_EQ("23910\n",
              CompileAndRunBook(
                  "int main(void) { int to_shiftr = 382574; to_shiftr >>= 4; return to_shiftr; }"));
}

// to_xor ^= 5;
TEST_F(BookTest, Chapter5_CompoundBitwiseXor)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook("int main(void) { int to_xor = 7; to_xor ^= 5; return to_xor; }"));
}

// to_divide /= 4;
TEST_F(BookTest, Chapter5_CompoundDivide)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook(
                  "int main(void) { int to_divide = 8; to_divide /= 4; return to_divide; }"));
}

// to_subtract -= 8;
TEST_F(BookTest, Chapter5_CompoundMinus)
{
    EXPECT_EQ(
        "2\n",
        CompileAndRunBook(
            "int main(void) { int to_subtract = 10; to_subtract -= 8; return to_subtract; }"));
}

// to_mod %= 3;
TEST_F(BookTest, Chapter5_CompoundMod)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook("int main(void) { int to_mod = 5; to_mod %= 3; return to_mod; }"));
}

// to_multiply *= 3;
TEST_F(BookTest, Chapter5_CompoundMultiply)
{
    EXPECT_EQ("12\n",
              CompileAndRunBook(
                  "int main(void) { int to_multiply = 4; to_multiply *= 3; return to_multiply; }"));
}

// to_add += 4;
TEST_F(BookTest, Chapter5_CompoundPlus)
{
    EXPECT_EQ("4\n",
              CompileAndRunBook("int main(void) { int to_add = 0; to_add += 4; return to_add; }"));
}

// a++; ++a; ++a; b--; --b; self-checking.
TEST_F(BookTest, Chapter5_IncrExpressionStatement)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 0; int b = 0; a++; ++a; ++a; b--; "
                                       "--b; return (a == 3 && b == -2); }"));
}

// postfix/prefix inc inside binary expressions, self-checking.
TEST_F(BookTest, Chapter5_IncrInBinaryExpr)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 2; int b = 3 + a++; int c = 4 + "
                                       "++b; return (a == 3 && b == 6 && c == 10); }"));
}

// ++ and -- on parenthesized expressions, self-checking.
TEST_F(BookTest, Chapter5_IncrParenthesized)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { int a = 1; int b = 2; int c = -++(a); int d = "
                                "!(b)--; return (a == 2 && b == 1 && c == -2 && d == 0); }"));
}

// int c = a++; int d = b--; self-checking.
TEST_F(BookTest, Chapter5_PostfixIncrAndDecr)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 1; int b = 2; int c = a++; int d "
                                       "= b--; return (a == 2 && b == 1 && c == 1 && d == 2); }"));
}

// int b = !a++; — postfix binds tighter than prefix '!'.
TEST_F(BookTest, Chapter5_PostfixPrecedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { int a = 1; int b = !a++; return (a == 2 && b == 0); }"));
}

// int c = ++a; int d = --b; self-checking.
TEST_F(BookTest, Chapter5_PrefixIncrAndDecr)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 1; int b = 2; int c = ++a; int d "
                                       "= --b; return (a == 2 && b == 1 && c == 2 && d == 1); }"));
}
