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
    EXPECT_NE(std::string::npos, code.find("ldo $248,$254,0\nldo $249,$254,8\nfadd $250,$248,$249\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fdiv $250,$248,$249\n")) << code;
    EXPECT_NE(std::string::npos, code.find("seth $255,#8000\nxor $248,$248,$255\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fsqrt $248,0,$248\n")) << code;
}

// A float is loaded with ldsf, computed in binary64, and rounded by stsf on its store.
TEST_F(MmixTest, FloatArithmetic)
{
    NaiveSelection();
    std::string code =
        Code(CompileToMmix("float f(float a, float b) { return a * b; }"));
    EXPECT_NE(std::string::npos, code.find("ldsf $248,$254,0\nldsf $249,$254,4\nfmul $250,$248,$249\nstsf $250,"))
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
    EXPECT_NE(std::string::npos, code.find("fcmp $250,$248,$249\nzsn $250,$250,1\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find("fcmp $250,$248,$249\nfun $255,$248,$249\nzsnp $250,$250,1\ncsnz $250,$255,0\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("feql $250,$248,$249\nsttu $250,")) << code;
    EXPECT_NE(std::string::npos, code.find("feql $250,$248,$249\nzsz $250,$250,1\n")) << code;
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
    EXPECT_NE(std::string::npos, code.find("fix $248,1,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fixu $248,1,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("flot $248,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("flotu $248,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sflot $248,$248\nstsf $248,")) << code;
    EXPECT_NE(std::string::npos, code.find("sflotu $248,$248\nstsf $248,")) << code;
}

// A truth test is any bit but the sign.
TEST_F(MmixTest, FpTruthTest)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("int f(double d) { if (d) return 1; return 2; }"));
    EXPECT_NE(std::string::npos, code.find("ldo $248,$254,0\nslu $248,$248,1\nbz $248,")) << code;
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
    src << "double sqrt(double);\n"
           "int main(void)\n{\n"
           "    volatile double zero = 0.0;\n";
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
    src << "    { volatile double q = zero / zero, one = 1.0;\n"
           "      if (q < one || q <= one || q > one || q >= one || q == q) return 900;\n"
           "      if (!(q != q)) return 901;\n"
           "      if (!q) return 902;\n"
           "      if (-zero) return 903;\n"
           "      if (sqrt(2.0 * one) != "
        << D(std::sqrt(2.0)) << ") return 904; }\n";
    // Conversions.  Through a double, 2^62 + 2^38 + 1 would round twice, to 2^62; once,
    // to binary32, it is 2^62 + 2^39.
    src << "    { volatile double d = -2.7, big = 1e19; volatile long l = (1L << 62) + (1L << 38) + 1;\n"
           "      volatile unsigned long ul = 18446744073709551615UL; volatile unsigned u = 4294967295u;\n"
           "      volatile float h = 16777217.0f;\n"
           "      if ((long)d != -2) return 910;\n"
           "      if ((int)d != -2) return 911;\n"
           "      if ((unsigned long)big != 10000000000000000000UL) return 912;\n"
           "      if ((float)l != 4611686568183201792.0f) return 913;\n"
           "      if ((double)ul != 18446744073709551616.0) return 914;\n"
           "      if ((float)u != 4294967296.0f) return 915;\n"
           "      if ((double)l != 4611686293305294848.0) return 916;\n"
           "      if (h != 16777216.0f) return 917;\n"
           "      if ((double)(float)0.1 != "
        << D((double)0.1f)
        << ") return 918;\n"
           "      if ((unsigned)(double)u != 4294967295u) return 919; }\n";
    src << "    return 0;\n}\n";
    EXPECT_EQ("", CompileAndRunMmix(src.str()));
    EXPECT_EQ(0, exit_status) << "case " << exit_status << " of\n" << src.str();
}
