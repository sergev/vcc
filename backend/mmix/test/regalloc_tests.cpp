//
// MMIX register allocation: GCC's fixed model ($0-$13 for values live across a call, $14
// rJ, $15 the hole, $16-$31 arguments and other values) and the compaction that moves
// $14-$31 down to just above the highest of $0-$13 in use.
//
#include "mmix_test.h"

// A leaf with its parameters where they arrive and its result in $0: no frame, no rJ.
TEST_F(MmixTest, RegallocLeaf)
{
    EXPECT_EQ("addu $0,$0,$1\npop 1,0\n",
              Code(CompileToMmix("long add(long a, long b) { return a + b; }")));
}

// GCC's shape: b, live across the call, stays in $1; a is passed in $4; so rJ goes in
// $2, the hole in $3, the arguments from $4.
TEST_F(MmixTest, RegallocCompaction)
{
    EXPECT_EQ("get $2,rJ\n"
              "set $4,$0\n"
              "pushj $3,g\n"
              "addu $0,$3,$1\n"
              "put rJ,$2\n"
              "pop 1,0\n",
              Code(CompileToMmix("long g(long); long f(long a, long b) { return g(a) + b; }")));
}

// With nothing kept across the call, rJ goes in $0 and the call is pushj $1; the result
// comes back in the hole, which the epilogue moves to $0 after rJ.
TEST_F(MmixTest, RegallocNothingKept)
{
    EXPECT_EQ("get $0,rJ\n"
              "pushj $1,g\n"
              "put rJ,$0\n"
              "set $0,$1\n"
              "pop 1,0\n",
              Code(CompileToMmix("long g(void); long f(void) { return g(); }")));
}

// An unsigned int result that wraps is extended again in its register; a signed one,
// whose overflow is undefined, is not, as GCC's is not.
TEST_F(MmixTest, RegallocReextend)
{
    EXPECT_EQ("addu $0,$0,1\nslu $0,$0,32\nsru $0,$0,32\npop 1,0\n",
              Code(CompileToMmix("unsigned inc(unsigned a) { return a + 1; }")));
}

TEST_F(MmixTest, RegallocSignedNotReextended)
{
    EXPECT_EQ("addu $0,$0,1\npop 1,0\n", Code(CompileToMmix("int inc(int a) { return a + 1; }")));
}

// Run: arithmetic wraps in its type in a register as it does in memory; a narrow result
// from a call is extended again.
TEST_F(MmixTest, RunRegallocWidths)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        int inc(int a) { return (int)((unsigned)a + 1); }
        unsigned uinc(unsigned a) { return a + 1; }
        signed char sc(int a) { return (signed char)(a * 3); }
        unsigned short us(int a) { return (unsigned short)-a; }
        int main(void)
        {
            if (inc(2147483647) != -2147483647 - 1) return 1;
            if (uinc(4294967295u) != 0) return 2;
            long x = sc(50);
            if (x != -106) return 3;
            long y = us(1);
            if (y != 65535) return 4;
            unsigned u = 3000000000u;
            int i = (int)u;
            if (i >= 0) return 5;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: a float in a register is rounded to binary32 after each operation, and goes to
// and from calls as its binary32 bits.
TEST_F(MmixTest, RunRegallocFloat)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        float third(float x) { return x / 3.0f; }
        float sum3(float a, float b, float c) { return a + b + c; }
        int main(void)
        {
            float t = third(1.0f);
            double d = t;
            if (d != (double)(1.0f / 3.0f)) return 1;
            float s = sum3(16777216.0f, 1.0f, 1.0f);
            if (s != 16777216.0f) return 2;
            float u = 0.1f;
            for (int i = 0; i < 9; i++)
                u = u + 0.1f;
            if (u != 1.0000001f) return 3;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: twenty values live across calls (more than $0-$13 hold: the rest spill to their
// slots), and twenty live across none (more than the seventeen other registers).
TEST_F(MmixTest, RunRegallocPressure)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string decl, use, mix;
    for (int i = 0; i < 20; i++) {
        std::string v = "v" + std::to_string(i);
        decl += "    long " + v + " = id(" + std::to_string(i * 7 + 1) + ");\n";
        use += " + " + v + " * " + std::to_string(i + 1);
        mix += "    long w" + std::to_string(i) + " = a * " + std::to_string(i + 3) + " + b;\n";
    }
    std::string wsum;
    for (int i = 0; i < 20; i++)
        wsum += " + w" + std::to_string(i) + " * w" + std::to_string((i + 7) % 20);
    long want = 0, wwant = 0;
    for (int i = 0; i < 20; i++)
        want += (i * 7 + 1L) * (i + 1);
    for (int i = 0; i < 20; i++)
        wwant += (5L * (i + 3) + 2) * (5L * ((i + 7) % 20 + 3) + 2);
    EXPECT_EQ("", CompileAndRunMmix("long id(long x) { return x; }\n"
                                    "long across(void)\n{\n" +
                                    decl + "    return 0" + use +
                                    ";\n}\n"
                                    "long leaf(long a, long b)\n{\n" +
                                    mix + "    return 0" + wsum +
                                    ";\n}\n"
                                    "int main(void)\n{\n"
                                    "    if (across() != " +
                                    std::to_string(want) +
                                    "L) return 1;\n"
                                    "    if (leaf(5, 2) != " +
                                    std::to_string(wwant) +
                                    "L) return 2;\n"
                                    "    return 0;\n}\n"));
    EXPECT_EQ(0, exit_status);
}

// Run: parameters that swap places on the way to a call (a parallel move with a
// cycle), and seventeen arguments with the last on the stack.
TEST_F(MmixTest, RunRegallocParallelMove)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        long sub3(long a, long b, long c) { return a * 100 + b * 10 + c; }
        long rot(long a, long b, long c) { return sub3(c, a, b); }
        long swap(long a, long b) { return sub3(b, a, 0); }
        long many(long a, long b, long c, long d, long e, long f, long g, long h, long i,
                  long j, long k, long l, long m, long n, long o, long p, long q)
        {
            return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 +
                   j * 10 + k * 11 + l * 12 + m * 13 + n * 14 + o * 15 + p * 16 + q * 17;
        }
        long shuffle(long a, long b, long c, long d, long e, long f, long g, long h,
                     long i, long j, long k, long l, long m, long n, long o, long p, long q)
        {
            return many(q, p, o, n, m, l, k, j, i, h, g, f, e, d, c, b, a);
        }
        int main(void)
        {
            if (rot(1, 2, 3) != 312) return 1;
            if (swap(4, 5) != 540) return 2;
            long want = 0;
            for (long k = 1; k <= 17; k++)
                want += (18 - k) * k;
            if (shuffle(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17) != want)
                return 3;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// A frameless leaf returns in place: each return is a pop, with no jump to an epilogue.
TEST_F(MmixTest, FramelessEarlyReturn)
{
    std::string code = Code(CompileToMmix("long f(long a) { if (a < 0) return -a; return a; }"));
    EXPECT_EQ(std::string::npos, code.find("jmp")) << code;
    EXPECT_EQ(std::string::npos, code.find("$254")) << code;
    EXPECT_EQ(std::string::npos, code.find("rJ")) << code;
    size_t first = code.find("pop 1,0\n");
    ASSERT_NE(std::string::npos, first) << code;
    EXPECT_NE(std::string::npos, code.find("pop 1,0\n", first + 1)) << code;
}

// Run: early returns in place, of a value and of none.
TEST_F(MmixTest, RunFramelessEarlyReturn)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        long g;
        long absval(long a) { if (a < 0) return -a; return a; }
        void setg(long v) { if (v == 0) return; g = v; }
        long sign(long a) { if (a < 0) return -1; if (a > 0) return 1; return 0; }
        int main(void)
        {
            if (absval(-5) != 5 || absval(7) != 7) return 1;
            setg(0);
            if (g != 0) return 2;
            setg(9);
            if (g != 9) return 3;
            if (sign(-3) != -1 || sign(4) != 1 || sign(0) != 0) return 4;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
