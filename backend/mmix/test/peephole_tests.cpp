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
           "cmp $248,$0,$1\n"
           "bnn $248,L:1\n"
           "setl $0,#1\n"
           "pop 1,0\n"
           "setl $0,#2\n"
           "pop 1,0\n",
           "long f(long a, long b) { if (a < b) return 1; return 2; }")

EXPECT_OPT(CompareZeroBranch,
           "bnp $0,L:2\n"
           "setl $0,#1\n"
           "pop 1,0\n"
           "setl $0,#2\n"
           "pop 1,0\n",
           "long f(long a) { if (a > 0) return 1; return 2; }")

EXPECT_OPT(UnsignedCompareBranch,
           "cmpu $248,$0,$1\n"
           "bnn $248,L:1\n"
           "setl $0,#1\n"
           "pop 1,0\n"
           "setl $0,#2\n"
           "pop 1,0\n",
           "long f(unsigned long a, unsigned long b) { if (a < b) return 1; return 2; }")

// An unordered pair gives 0: a < b false, so the branch past it is taken.
EXPECT_OPT(FloatCompareBranch,
           "fcmp $250,$0,$1\n"
           "bnn $250,L:1\n"
           "setl $0,#1\n"
           "pop 1,0\n"
           "setl $0,#2\n"
           "pop 1,0\n",
           "long f(double a, double b) { if (a < b) return 1; return 2; }")

EXPECT_OPT(NotBranch,
           "bnz $0,L:1\n"
           "setl $0,#1\n"
           "pop 1,0\n"
           "setl $0,#2\n"
           "pop 1,0\n",
           "long f(long a) { if (!a) return 1; return 2; }")

EXPECT_OPT(IndexedLoad, "ldb $0,$0,$1\npop 1,0\n", "char f(char *p, long i) { return p[i]; }")

EXPECT_OPT(OffsetLoad, "ldo $0,$0,24\npop 1,0\n", "long f(long *p) { return p[3]; }")

EXPECT_OPT(ConstantFirstOperand, "addu $0,$0,5\npop 1,0\n", "long f(long a) { return 5 + a; }")

// *d++ = *s++: each pointer read, then stepped; the loop rotated, its back edge pbnz.
EXPECT_OPT(PostIncrement,
           "ldb $4,$1,0\n"
           "addu $1,$1,1\n"
           "stbu $4,$0,0\n"
           "addu $0,$0,1\n"
           "bz $4,L:L0\n"
           "ldb $4,$1,0\n"
           "addu $1,$1,1\n"
           "stbu $4,$0,0\n"
           "addu $0,$0,1\n"
           "pbnz $4,L:7\n"
           "pop 1,0\n",
           "char *f(char *d, const char *s) { while ((*d++ = *s++)); return d; }")

EXPECT_OPT(LoopBackEdgeProbable,
           "setl $3,#0\n"
           "bnp $0,L:L0\n"
           "addu $3,$3,$0\n"
           "subu $0,$0,1\n"
           "pbp $0,L:5\n"
           "set $0,$3\n"
           "pop 1,0\n",
           "long f(long n) { long s = 0; while (n > 0) { s += n; n--; } return s; }")

// The product rounded once, by the stsf that also takes its bits for the result.
EXPECT_OPT(FloatRoundedOnce,
           "subu $254,$254,8\n"
           "sttu $0,$254,0\n"
           "ldsf $0,$254,0\n"
           "sttu $1,$254,0\n"
           "ldsf $1,$254,0\n"
           "fmul $0,$0,$1\n"
           "stsf $0,$254,0\n"
           "ldt $0,$254,0\n"
           "addu $254,$254,8\n"
           "pop 1,0\n",
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
