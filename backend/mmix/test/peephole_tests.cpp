//
// MMIX selection fusions and the peephole pass: a comparison only a branch reads is a
// branch on cmp's sign (or on the value against zero), a pointer sum only a load or
// store reads is its address, a copy only one instruction reads goes, *p++ reads
// through p and then steps it, a loop's back edge is probable, and a float's rounding
// is not done twice.
//
#include "mmix_test.h"

#define EXPECT_OPT(name, expected, src)                \
    TEST_F(MmixTest, name)                             \
    {                                                  \
        EXPECT_EQ(expected, Code(CompileToMmix(src))); \
    }

EXPECT_OPT(CompareBranch,
           R"(cmp $248, $0, $1
bnn $248, L:1
setl $0, #1
pop 1, 0
setl $0, #2
pop 1, 0
)",
           "long f(long a, long b) { if (a < b) return 1; return 2; }")

EXPECT_OPT(CompareZeroBranch,
           R"(bnp $0, L:2
setl $0, #1
pop 1, 0
setl $0, #2
pop 1, 0
)",
           "long f(long a) { if (a > 0) return 1; return 2; }")

EXPECT_OPT(UnsignedCompareBranch,
           R"(cmpu $248, $0, $1
bnn $248, L:1
setl $0, #1
pop 1, 0
setl $0, #2
pop 1, 0
)",
           "long f(unsigned long a, unsigned long b) { if (a < b) return 1; return 2; }")

// An unordered pair gives 0: a < b false, so the branch past it is taken.
EXPECT_OPT(FloatCompareBranch,
           R"(fcmp $250, $0, $1
bnn $250, L:1
setl $0, #1
pop 1, 0
setl $0, #2
pop 1, 0
)",
           "long f(double a, double b) { if (a < b) return 1; return 2; }")

EXPECT_OPT(NotBranch,
           R"(bnz $0, L:0
setl $0, #1
pop 1, 0
setl $0, #2
pop 1, 0
)",
           "long f(long a) { if (!a) return 1; return 2; }")

EXPECT_OPT(IndexedLoad, R"(ldb $0, $0, $1
pop 1, 0
)", "char f(char *p, long i) { return p[i]; }")

EXPECT_OPT(OffsetLoad, R"(ldo $0, $0, 24
pop 1, 0
)", "long f(long *p) { return p[3]; }")

EXPECT_OPT(ConstantFirstOperand, R"(addu $0, $0, 5
pop 1, 0
)", "long f(long a) { return 5 + a; }")

// *d++ = *s++: each pointer read, then stepped; the loop entered at its test, the body
// empty, its back edge pbnz.
EXPECT_OPT(PostIncrement,
           R"(ldb $4, $1, 0
addu $1, $1, 1
stbu $4, $0, 0
addu $0, $0, 1
pbnz $4, L:2
pop 1, 0
)",
           "char *f(char *d, const char *s) { while ((*d++ = *s++)); return d; }")

EXPECT_OPT(LoopBackEdgeProbable,
           R"(setl $3, #0
bnp $0, L:L0
addu $3, $3, $0
subu $0, $0, 1
pbp $0, L:5
set $0, $3
pop 1, 0
)",
           "long f(long n) { long s = 0; while (n > 0) { s += n; n--; } return s; }")

// The product rounded once, by the stsf that also takes its bits for the result.
EXPECT_OPT(FloatRoundedOnce,
           R"(subu $254, $254, 8
sttu $0, $254, 0
ldsf $0, $254, 0
sttu $1, $254, 0
ldsf $1, $254, 0
fmul $0, $0, $1
stsf $0, $254, 0
ldt $0, $254, 0
addu $254, $254, 8
pop 1, 0
)",
           "float f(float x, float y) { return x * y; }")

// Run: every fused comparison both ways, against the host's answers, NaN included.
TEST_F(MmixTest, RunFusedComparisons)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        static long n;
        long lt(long a, long b) { if (a < b) return 1; return 0; }
        long le(long a, long b) { if (a <= b) return 1; return 0; }
        long gt(long a, long b) { if (a > b) return 1; return 0; }
        long ge(long a, long b) { if (a >= b) return 1; return 0; }
        long eq(long a, long b) { if (a == b) return 1; return 0; }
        long ne(long a, long b) { if (a != b) return 1; return 0; }
        long ult(unsigned long a, unsigned long b) { if (a < b) return 1; return 0; }
        long uge(unsigned a, unsigned b) { while (a >= b) { n++; return 1; } return 0; }
        long flt(double a, double b) { if (a < b) return 1; return 0; }
        long fgt(double a, double b) { if (!(a > b)) return 0; return 1; }
        long feq(double a, double b) { if (a == b) return 1; return 0; }
        long fne(double a, double b) { if (a != b) return 1; return 0; }
        long notz(long a) { if (!a) return 1; return 0; }
        int main(void)
        {
            double nan = 0.0 / 0.0;
            if (lt(-1, 1) != 1 || lt(1, -1) != 0 || lt(2, 2) != 0) return 1;
            if (le(2, 2) != 1 || le(3, 2) != 0) return 2;
            if (gt(3, 2) != 1 || gt(2, 2) != 0) return 3;
            if (ge(2, 2) != 1 || ge(-3, 2) != 0) return 4;
            if (eq(5, 5) != 1 || eq(5, 6) != 0 || ne(5, 6) != 1 || ne(5, 5) != 0) return 5;
            if (ult(1, -1L) != 1 || ult(-1L, 1) != 0) return 6;
            if (uge(4000000000u, 1) != 1 || uge(1, 4000000000u) != 0) return 7;
            if (flt(1.0, 2.0) != 1 || flt(nan, 2.0) != 0 || flt(2.0, nan) != 0) return 8;
            if (fgt(2.0, 1.0) != 1 || fgt(nan, 1.0) != 0) return 9;
            if (feq(1.5, 1.5) != 1 || feq(nan, nan) != 0) return 10;
            if (fne(nan, nan) != 1 || fne(1.5, 1.5) != 0) return 11;
            if (notz(0) != 1 || notz(7) != 0) return 12;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// A branch over one value is a conditional set on the inverse condition.
EXPECT_OPT(ConditionalSetMax,
           R"(cmp $248, $0, $1
csnp $0, $248, $1
pop 1, 0
)",
           "long f(long a, long b) { return a > b ? a : b; }")

// A choice of two values: the first set, the second a conditional set over it.
EXPECT_OPT(ConditionalSetChoice,
           R"(setl $2, #7
csz $2, $0, 0
set $0, $2
pop 1, 0
)",
           "long f(long c) { return c ? 7 : 0; }")

// A constant of -1..-255 added is subtracted, as an immediate.
EXPECT_OPT(NegativeConstant, R"(subu $0, $0, 5
pop 1, 0
)", "long f(long a) { return a + -5; }")

// Run: choices by conditional sets, against the plain answers.
TEST_F(MmixTest, RunConditionalSets)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        long mx(long a, long b) { return a > b ? a : b; }
        long mn(long a, long b) { return a < b ? a : b; }
        long sel(long c) { return c ? 7 : 0; }
        long sel2(long c, long x, long y) { long r; if (c >= 0) r = x; else r = y; return r; }
        long dec(long a) { return a + -200; }
        unsigned udec(unsigned a) { return a - -3; }
        int main(void)
        {
            if (mx(3, 9) != 9 || mx(9, 3) != 9 || mx(-1, -2) != -1) return 1;
            if (mn(3, 9) != 3 || mn(9, 3) != 3) return 2;
            if (sel(5) != 7 || sel(0) != 0) return 3;
            if (sel2(1, 10, 20) != 10 || sel2(-1, 10, 20) != 20) return 4;
            if (dec(5) != -195) return 5;
            if (udec(4294967294u) != 1) return 6;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// A call whose result is returned is a jmp, the arguments in $0..: with no call left, rJ
// is neither saved nor restored.
EXPECT_OPT(TailCall,
           R"(addu $5, $0, 1
set $0, $1
set $1, $5
jmp g
)",
           "long g(long, long); long f(long a, long b) { return g(b, a + 1); }")

// Not when the result types differ: an int result widened to long needs extending.
EXPECT_OPT(NoTailCallWidening,
           R"(get $1, rJ
pushj $2, narrow
slu $2, $2, 32
sr $2, $2, 32
set $0, $2
put rJ, $1
pop 1, 0
)",
           "int narrow(void); long f(void) { return narrow(); }")

// Run: tail calls, self-recursive and to GCC's code and back, a million deep, which
// the register stack would otherwise hold a frame for each.
TEST_F(MmixTest, RunTailCalls)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = R"(
long down_ours(long n, long acc);
long down_gcc(long n, long acc) { if (n == 0) return acc; return down_ours(n - 1, acc + 2); }
)";
    std::string ours = CompileToMmix(R"(
        long down_gcc(long n, long acc);
        long gcd(long a, long b) { if (b == 0) return a; return gcd(b, a % b); }
        long down_ours(long n, long acc) { if (n == 0) return acc; return down_gcc(n - 1, acc + 1); }
        long (*fp)(long, long) = gcd;
        long indirect(long a, long b) { return fp(a, b); }
        int main(void)
        {
            if (gcd(1071, 462) != 21) return 1;
            if (indirect(48, 18) != 6) return 2;
            if (down_ours(1000000, 0) != 1500000) return 3;
            return 0;
        }
    )");
    EXPECT_EQ("", Run(ours, "crt0.o", &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}
