#include "codegen_test.h"

// Integer width conversions (task #17).  Under the BESM-6 target short/int/long/pointer
// are all one 48-bit word, so the only width that differs from a full word is char (8
// bits).  TRUNCATE and ZERO_EXTEND therefore both reduce to masking the low 8 bits
// (aax =377); SIGN_EXTEND additionally reinterprets bit 7 as the sign via the branchless
// (x ^ 0x80) - 0x80 trick (aax =377 / aex =200 / a-x =200).  Operands are volatile in the
// run tests so the optimizer cannot fold the conversion away.

// --- Madlen shape -----------------------------------------------------------------

// int → char: keep the low 8 bits.
TEST_F(CodegenTest, TruncateToCharMadlen)
{
    std::string output = CompileToMadlen("extern char g; void foo(int a) { g = a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,aax, =377
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// unsigned char → unsigned: clear all but the low 8 bits (same sequence as truncate).
TEST_F(CodegenTest, ZeroExtendFromUCharMadlen)
{
    std::string output = CompileToMadlen("extern unsigned g; void foo(unsigned char a) { g = a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,aax, =377
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// signed char → int: mask, flip the sign bit, subtract 0x80.
TEST_F(CodegenTest, SignExtendFromSCharMadlen)
{
    std::string output = CompileToMadlen("extern int g; void foo(signed char a) { g = a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,aax, =377
             ,aex, =200
             ,a-x, =200
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// --- Runtime behaviour ------------------------------------------------------------

// Truncation keeps only the low 8 bits: 300 & 0xFF = 44 (bit 7 clear → positive).
TEST_F(CodegenTest, TruncateToChar)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile int a = 300;
            char c = a;
            printf("%d\n", c);
        }
    )");
    EXPECT_EQ("44\n", result);
}

// Sign extension of a byte with bit 7 set: (signed char)200 = -56.
TEST_F(CodegenTest, SignExtendNegativeByte)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile int a = 200;
            signed char c = a;
            int x = c;
            printf("%d\n", x);
        }
    )");
    EXPECT_EQ("-56\n", result);
}

// Sign extension round-trips a small negative value.
TEST_F(CodegenTest, SignExtendNegativeOne)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile signed char c = -5;
            int x = c;
            printf("%d\n", x);
        }
    )");
    EXPECT_EQ("-5\n", result);
}

// Zero extension never sets the high bits, even for a byte with bit 7 set.
TEST_F(CodegenTest, ZeroExtendHighByte)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile unsigned char c = 200;
            unsigned int x = c;
            printf("%o\n", x);
        }
    )");
    EXPECT_EQ("310\n", result); // 200 decimal in octal
}

// Truncate then zero-extend: 300 → 44, kept positive through the widening.
TEST_F(CodegenTest, TruncateThenZeroExtend)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile int a = 300;
            unsigned char c = a;
            unsigned int x = c;
            printf("%o\n", x);
        }
    )");
    EXPECT_EQ("54\n", result); // 44 decimal in octal
}

// Integer <-> floating-point conversions (task #18).  float == double on BESM-6, so the
// FLOAT_* forms mirror the DOUBLE_* ones and float<->double is a bit-pattern copy.  Signed
// int->FP is the inline INT-format-then-normalize sequence (aox =:64 / ntr 0 / a+x / ntr 7);
// unsigned->FP, FP->signed and FP->unsigned use the runtime helpers b/utod, b/dtoi, b/dtou.
// Operands are volatile in the run tests so the optimizer cannot fold the conversion away.

// --- Madlen shape -----------------------------------------------------------------

// int -> double: OR the INT-format exponent, then normalize (ntr 0 / a+x 0 / ntr 7).
TEST_F(CodegenTest, IntToDoubleMadlen)
{
    std::string output = CompileToMadlen("extern double g; void foo(int a) { g = a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,aox, =:64
             ,ntr, 0
             ,a+x,
             ,ntr, 7
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// unsigned -> double: the b/utod runtime helper (full 48-bit range).
TEST_F(CodegenTest, UintToDoubleMadlen)
{
    std::string output =
        CompileToMadlen("extern unsigned g; double foo(unsigned a) { return (double)a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,call, b/utod
             ,uj, b/ret
             ,end,
)",
              output);
}

// double -> int: the b/dtoi runtime helper (realign + mask, truncate toward zero).
TEST_F(CodegenTest, DoubleToIntMadlen)
{
    std::string output = CompileToMadlen("extern double d; int foo(void) { return (int)d; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        d:   ,subp,
             ,its, 13
             ,call, b/save0
             ,utc, d
             ,xta,
             ,call, b/dtoi
             ,uj, b/ret
             ,end,
)",
              output);
}

// double -> unsigned: the b/dtou runtime helper (full 48-bit extraction).
TEST_F(CodegenTest, DoubleToUintMadlen)
{
    std::string output =
        CompileToMadlen("extern double d; unsigned foo(void) { return (unsigned)d; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        d:   ,subp,
             ,its, 13
             ,call, b/save0
             ,utc, d
             ,xta,
             ,call, b/dtou
             ,uj, b/ret
             ,end,
)",
              output);
}

// float -> double: same 48-bit format, so just a load/store copy (no conversion code).
TEST_F(CodegenTest, FloatToDoubleMadlen)
{
    std::string output = CompileToMadlen("extern float f; double foo(void) { return f; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        f:   ,subp,
             ,its, 13
             ,call, b/save0
             ,utc, f
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// --- Runtime behaviour ------------------------------------------------------------

// int -> double yields the canonical BESM-6 FP word (checked as the raw octal bit pattern,
// matching the constants used in arith_tests.cpp).
TEST_F(CodegenTest, IntToDoubleBits)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void show(double x) { printf("%o\n", x); }
        void program() {
            volatile int one = 1, two = 2;
            show((double)one);   /* 1.0 */
            show((double)two);   /* 2.0 */
            show((double)0);     /* 0.0 */
        }
    )");
    EXPECT_EQ(
        "4050000000000000\n" /* 1.0 */
        "4110000000000000\n" /* 2.0 */
        "0\n" /* 0.0 prints as a single 0 in octal */,
        result);
}

// int -> double -> int round-trips for positive, negative, and zero values.
TEST_F(CodegenTest, IntDoubleRoundTrip)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void show(int x) { printf("%d\n", x); }
        void program() {
            volatile int i = 42, j = -7, k = 0, big = 1000000;
            show((int)(double)i);
            show((int)(double)j);
            show((int)(double)k);
            show((int)(double)big);
        }
    )");
    EXPECT_EQ("42\n-7\n0\n1000000\n", result);
}

// double -> int truncates toward zero (not floor): negatives round up toward 0.
TEST_F(CodegenTest, DoubleToIntTruncates)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void show(int x) { printf("%d\n", x); }
        void program() {
            volatile double a = 2.9, b = -2.9, c = -0.5, d = 7.0;
            show((int)a);   /*  2 */
            show((int)b);   /* -2 */
            show((int)c);   /*  0 */
            show((int)d);   /*  7 */
        }
    )");
    EXPECT_EQ("2\n-2\n0\n7\n", result);
}

// unsigned conversions over the full 48-bit range.  b/utod is checked bit-exactly against
// a literal double; the round-trip through b/dtou covers a value >= 2^40 where the signed
// path (b/dtoi, 41 bits) would lose the high bits.  Exact because each value has <= 40
// significant bits.
TEST_F(CodegenTest, UnsignedDoubleRoundTrip)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile unsigned a = 3, b = 1000000, c = 16777217 /* 2^24 + 1 */;
            volatile unsigned big = (1u << 44) | (1u << 10); /* >= 2^40, exact */
            printf("%d%d%d%d%d\n",
                (double)a == 3.0,
                (double)b == 1000000.0,
                (unsigned)(double)a == a,
                (unsigned)(double)c == c,
                (unsigned)(double)big == big);
        }
    )");
    EXPECT_EQ("11111\n", result);
}

// long double ≡ double on BESM-6 (one 48-bit native-FP word).  Exercise the newly enabled
// long-double paths: a long double constant (1.5L), int → long double → int round-trips,
// long double + long double, and conversions to/from double and unsigned.
TEST_F(CodegenTest, LongDoubleSameAsDouble)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            volatile int i = 7;
            volatile unsigned u = 1000000;
            long double a = 1.5L;        /* long double constant */
            long double b = (long double)i;  /* int -> long double */
            long double s = a + b;       /* long double arithmetic */
            double d = (double)s;        /* long double -> double */
            long double e = (long double)d;  /* double -> long double */
            printf("%d%d%d%d\n",
                (int)s == 8,             /* long double -> int (8.5 truncates to 8) */
                (int)e == 8,
                (long double)u == 1000000.0L,  /* unsigned -> long double */
                (unsigned)e == 8u);      /* long double -> unsigned */
        }
    )");
    EXPECT_EQ("1111\n", result);
}
