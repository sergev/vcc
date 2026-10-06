//
// MMIX floating point, in hardware: binary64 instructions, a float held as its exact
// binary64 value and rounded by stsf, and conversions; runs against the host.
//
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

#include "mmix_test.h"

TEST_F(MmixTest, DoubleArithmetic)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        double f1(double a, double b) { return a + b; }
        double f2(double a, double b) { return a / b; }
        double f3(double a) { return -a; }
        double sqrt(double);
        double f4(double a) { return sqrt(a); }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(ldo $248, $254, 0
ldo $249, $254, 8
fadd $250, $248, $249
)")) << code;
    EXPECT_NE(std::string::npos, code.find("fdiv $250, $248, $249\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(seth $255, #8000
xor $248, $248, $255
)")) << code;
    EXPECT_NE(std::string::npos, code.find("fsqrt $248, 0, $248\n")) << code;
}

// A float is loaded with ldsf, computed in binary64, and rounded by stsf on its store.
TEST_F(MmixTest, FloatArithmetic)
{
    NaiveSelection();
    std::string code =
        Code(CompileToMmix("float f(float a, float b) { return a * b; }"));
    EXPECT_NE(std::string::npos, code.find(R"(ldsf $248, $254, 0
ldsf $249, $254, 4
fmul $250, $248, $249
stsf $250, )"))
        << code;
}

// <= and >= rule out an unordered pair with fun; == is feql, != its complement.
TEST_F(MmixTest, DoubleComparisons)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        int f1(double a, double b) { return a < b; }
        int f2(double a, double b) { return a <= b; }
        int f3(double a, double b) { return a == b; }
        int f4(double a, double b) { return a != b; }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(fcmp $250, $248, $249
zsn $250, $250, 1
)")) << code;
    EXPECT_NE(std::string::npos,
              code.find(R"(fcmp $250, $248, $249
fun $255, $248, $249
zsnp $250, $250, 1
csnz $250, $255, 0
)"))
        << code;
    EXPECT_NE(std::string::npos, code.find(R"(feql $250, $248, $249
sttu $250, )")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(feql $250, $248, $249
zsz $250, $250, 1
)")) << code;
}

TEST_F(MmixTest, FpConversions)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        long f1(double d) { return d; }
        unsigned long f2(double d) { return d; }
        double f3(long l) { return l; }
        double f4(unsigned long l) { return l; }
        float f5(long l) { return l; }
        float f6(unsigned l) { return l; }
    )"));
    EXPECT_NE(std::string::npos, code.find("fix $248, 1, $248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fixu $248, 1, $248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("flot $248, $248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("flotu $248, $248\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(sflot $248, $248
stsf $248, )")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(sflotu $248, $248
stsf $248, )")) << code;
}

// A truth test is any bit but the sign.
TEST_F(MmixTest, FpTruthTest)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("int f(double d) { if (d) return 1; return 2; }"));
    EXPECT_NE(std::string::npos, code.find(R"(ldo $248, $254, 0
slu $248, $248, 1
bz $248, )")) << code;
}

// A double constant as an exact C expression.
static std::string D(double d)
{
    if (std::isnan(d))
        return "(zero / zero)";
    if (std::isinf(d))
        return d > 0 ? "(1.0 / zero)" : "(-1.0 / zero)";
    char buf[64];
    snprintf(buf, sizeof buf, "%.17g", d);
    std::string s = buf;
    if (s.find_first_of(".en") == std::string::npos)
        s += ".0";
    return "(" + s + ")";
}

// A float constant: the shortest digits that round back to it, and the f suffix.
static std::string F(float f)
{
    if (std::isinf(f))
        return f > 0 ? "((float)(1.0 / zero))" : "((float)(-1.0 / zero))";
    if (std::isnan(f))
        return "((float)(zero / zero))";
    char buf[64];
    snprintf(buf, sizeof buf, "%.9g", f);
    std::string s = buf;
    if (s.find_first_of(".e") == std::string::npos)
        s += ".0";
    return "(" + s + "f)";
}

// Run: arithmetic, comparisons and conversions against the host, in binary64 and in
// binary32, NaN, -0, halfway and subnormal cases included.
TEST_F(MmixTest, RunFpTable)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::ostringstream src;
    src << R"(double sqrt(double);
int main(void)
{
    volatile double zero = 0.0;
)";
    int n            = 0;
    const double v[] = { 1.0, -2.5, 0.1, 3.0, 1e300, 1e-300, 4.9e-324, -0.0, 1.0 / 3.0 };
    for (double a : v)
        for (double b : v) {
            src << "    { volatile double a = " << D(a) << ", b = " << D(b) << ";\n";
            src << "      if (a + b != " << D(a + b) << ") return " << ++n << ";\n";
            src << "      if (a - b != " << D(a - b) << ") return " << ++n << ";\n";
            src << "      if (a * b != " << D(a * b) << ") return " << ++n << ";\n";
            if (b != 0)
                src << "      if (a / b != " << D(a / b) << ") return " << ++n << ";\n";
            src << "      if ((a < b) != " << (a < b) << ") return " << ++n << ";\n";
            src << "      if ((a <= b) != " << (a <= b) << ") return " << ++n << ";\n";
            src << "      if ((a == b) != " << (a == b) << ") return " << ++n << ";\n";
            src << "    }\n";
        }
    const float f[] = { 1.0f, 0.1f, 3.0f, 16777216.0f, 1e-40f, 3.4e38f, -1.5f };
    for (float a : f)
        for (float b : f) {
            src << "    { volatile float a = " << F(a) << ", b = " << F(b) << ";\n";
            src << "      if (a + b != " << F(a + b) << ") return " << ++n << ";\n";
            src << "      if (a * b != " << F(a * b) << ") return " << ++n << ";\n";
            src << "      if (a / b != " << F(a / b) << ") return " << ++n << ";\n";
            src << "    }\n";
        }
    // NaN: unordered, so every comparison but != is false; a NaN is true, -0 false.
    src << R"(    { volatile double q = zero / zero, one = 1.0;
      if (q < one || q <= one || q > one || q >= one || q == q) return 900;
      if (!(q != q)) return 901;
      if (!q) return 902;
      if (-zero) return 903;
      if (sqrt(2.0 * one) != )"
        << D(std::sqrt(2.0)) << ") return 904; }\n";
    // Conversions.  Through a double, 2^62 + 2^38 + 1 would round twice, to 2^62; once,
    // to binary32, it is 2^62 + 2^39.
    src << R"(    { volatile double d = -2.7, big = 1e19; volatile long l = (1L << 62) + (1L << 38) + 1;
      volatile unsigned long ul = 18446744073709551615UL; volatile unsigned u = 4294967295u;
      volatile float h = 16777217.0f;
      if ((long)d != -2) return 910;
      if ((int)d != -2) return 911;
      if ((unsigned long)big != 10000000000000000000UL) return 912;
      if ((float)l != 4611686568183201792.0f) return 913;
      if ((double)ul != 18446744073709551616.0) return 914;
      if ((float)u != 4294967296.0f) return 915;
      if ((double)l != 4611686293305294848.0) return 916;
      if (h != 16777216.0f) return 917;
      if ((double)(float)0.1 != )"
        << D((double)0.1f)
        << R"() return 918;
      if ((unsigned)(double)u != 4294967295u) return 919; }
)";
    src << R"(    return 0;
}
)";
    EXPECT_EQ("", CompileAndRunMmix(src.str()));
    EXPECT_EQ(0, exit_status) << "case " << exit_status << " of\n" << src.str();
}
