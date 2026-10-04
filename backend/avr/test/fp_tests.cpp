//
// AVR floating point: binary32 in software.  The runtime (libc/common/float32.c,
// compiled by genavr) against the host's own float arithmetic, bit for bit, called
// from clang's code and from ours; and the selection around the calls.
//
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "avr_test.h"

// Operands: zeros, ordinary values, rounding ties, the extremes, subnormals, the
// infinities and a NaN.
static const uint32_t fvals[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbfc00000, 0x40400000, 0x3dcccccd,
    0x3f800001, 0x33800000, 0x7f7fffff, 0x00800000, 0x00000001, 0x807fffff,
    0x7f800000, 0xff800000, 0x7fc00000, 0x4b800001,
};

static float FromBits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// The bits of `f`, any NaN as the default one: NaN payloads are not compared.
static uint32_t Bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return std::isnan(f) ? 0x7fc00000u : u;
}

static std::string Hex(uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%08x", (unsigned)v);
    return buf;
}

// The line of operands i and j: the four results in hex, then the six comparisons.
static std::string Expected(size_t i, size_t j)
{
    float x = FromBits(fvals[i]), y = FromBits(fvals[j]);
    return Hex(Bits(x + y)) + " " + Hex(Bits(x - y)) + " " + Hex(Bits(x * y)) + " " +
           Hex(Bits(x / y)) + " " + std::to_string(x < y) + std::to_string(x <= y) +
           std::to_string(x > y) + std::to_string(x >= y) + std::to_string(x == y) +
           std::to_string(x != y) + "\n";
}

static std::string ArithProgram(size_t from, size_t to)
{
    std::string vals;
    for (uint32_t v : fvals)
        vals += std::to_string(v) + "UL, ";
    return R"(
void putbyte(int c);
volatile unsigned long vals[] = { )" +
           vals + R"( };
static float f(unsigned long u) { return *(float *)&u; }
static void hex(float v)
{
    unsigned long u = *(unsigned long *)&v;
    if ((u & 0x7fffffffUL) > 0x7f800000UL)
        u = 0x7fc00000UL;
    for (int i = 28; i >= 0; i -= 4)
        putbyte("0123456789abcdef"[(u >> i) & 15]);
}
int main(void)
{
    for (int i = )" + std::to_string(from) +
           "; i < " + std::to_string(to) + R"(; i++)
        for (int j = 0; j < )" +
           std::to_string(sizeof fvals / sizeof fvals[0]) + R"(; j++) {
            float x = f(vals[i]), y = f(vals[j]);
            hex(x + y); putbyte(' ');
            hex(x - y); putbyte(' ');
            hex(x * y); putbyte(' ');
            hex(x / y); putbyte(' ');
            putbyte('0' + (x < y)); putbyte('0' + (x <= y));
            putbyte('0' + (x > y)); putbyte('0' + (x >= y));
            putbyte('0' + (x == y)); putbyte('0' + (x != y));
            putbyte('\n');
        }
    return 0;
}
)";
}

static std::string ArithExpected(size_t from, size_t to)
{
    std::string s;
    for (size_t i = from; i < to; i++)
        for (size_t j = 0; j < sizeof fvals / sizeof fvals[0]; j++)
            s += Expected(i, j);
    return s;
}

// clang's code with our runtime: the helpers' ABI as LLVM assumes it.
TEST_F(AvrTest, RunFloatRuntimeFromClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(ArithExpected(0, 16), ClangRun(ArithProgram(0, 16)));
}

// The same through our own code, half the operands (naive code is slow under qemu).
TEST_F(AvrTest, RunFloatArithmetic)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(ArithExpected(0, 8), CompileAndRunAvr(ArithProgram(0, 8)));
}

TEST_F(AvrTest, RunFloatArithmetic2)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(ArithExpected(8, 16), CompileAndRunAvr(ArithProgram(8, 16)));
}

// Conversions between float and the integers, against the host.
TEST_F(AvrTest, RunFloatConversions)
{
    SKIP_IF_NO_AVR_TOOLS();
    static const int32_t ints[] = { 0, 1, -1, 16777217, -16777217, 2147483647,
                                    -2147483647 - 1, 123456789, 0x7fffff80, 33554435 };
    static const float floats[] = { 0.0f, -0.0f, 0.5f, -0.99f, 2.5f, -2.5f, 32767.9f,
                                    -32768.0f, 1e9f, -2147483648.0f, 16777216.0f };
    std::string src = R"(
void putbyte(int c);
static void hex(unsigned long u)
{
    for (int i = 28; i >= 0; i -= 4)
        putbyte("0123456789abcdef"[(u >> i) & 15]);
    putbyte(' ');
}
static unsigned long fbits(float v) { return *(unsigned long *)&v; }
)";
    std::string expected;
    src += "volatile long ints[] = { ";
    for (int32_t v : ints)
        src += std::to_string(v) + "L, ";
    src += "};\nvolatile float floats[] = { ";
    for (float v : floats) {
        char buf[32];
        snprintf(buf, sizeof buf, "%.9e", v);
        src += std::string(buf) + "f, ";
    }
    src += R"(};
int main(void)
{
    for (int i = 0; i < )" +
           std::to_string(sizeof ints / sizeof ints[0]) + R"(; i++) {
        long v = ints[i];
        hex(fbits((float)v));
        hex(fbits((float)(unsigned long)v));
        hex(fbits((float)(int)v));
        hex(fbits((float)(unsigned)v));
        putbyte('\n');
    }
    for (int i = 0; i < )" +
           std::to_string(sizeof floats / sizeof floats[0]) + R"(; i++) {
        float f = floats[i];
        hex((unsigned long)(long)f);
        hex(f >= 0 ? (unsigned long)f : 0);
        hex((unsigned long)(int)(f / 65536.0f));
        hex((unsigned long)(signed char)(f / 1e8f));
        putbyte('\n');
    }
    return 0;
}
)";
    for (int32_t v : ints) {
        expected += Hex(Bits((float)v)) + " " + Hex(Bits((float)(uint32_t)v)) + " " +
                    Hex(Bits((float)(int16_t)v)) + " " + Hex(Bits((float)(uint16_t)v)) + " \n";
    }
    for (float f : floats) {
        expected += Hex((uint32_t)(int32_t)f) + " " + Hex(f >= 0 ? (uint32_t)f : 0) + " " +
                    Hex((uint32_t)(int32_t)(int16_t)(f / 65536.0f)) + " " +
                    Hex((uint32_t)(int32_t)(signed char)(f / 1e8f)) + " \n";
    }
    EXPECT_EQ(expected, CompileAndRunAvr(src));
}

// Every FP operation is a call: operands in r25:r22 and r21:r18.
TEST_F(AvrTest, FloatAddCalls)
{
    std::string s = Body(CompileToAvr("float f(float a, float b) { return a + b; }"));
    EXPECT_NE(std::string::npos, s.find("ldd r18, Y+5\nldd r19, Y+6\nldd r20, Y+7\n"
                                        "ldd r21, Y+8\ncall __addsf3\nstd Y+9, r22\n"))
        << s;
}

// Negation flips the sign bit inline.
TEST_F(AvrTest, FloatNegateInline)
{
    std::string s = Body(CompileToAvr("double f(double a) { return -a; }"));
    EXPECT_NE(std::string::npos, s.find("subi r25, 128\n")) << s;
    EXPECT_EQ(std::string::npos, s.find("call")) << s;
}

// A comparison tests the helper's result in r24: a > b as 0 < r24.
TEST_F(AvrTest, FloatCompare)
{
    std::string s = Body(CompileToAvr("int f(float a, float b) { return a > b; }"));
    EXPECT_NE(std::string::npos, s.find("call __gtsf2\ncp r1, r24\nldi r24, 1\nbrlt ")) << s;
}

// The truth test ignores the sign: -0.0 is false.
TEST_F(AvrTest, FloatTruthTest)
{
    std::string s = Body(CompileToAvr("int f(float a) { return !a; }"));
    EXPECT_NE(std::string::npos,
              s.find("andi r25, 127\ncp r22, r1\ncpc r23, r1\ncpc r24, r1\ncpc r25, r1\n"))
        << s;
}

// A 16-bit int is widened to 32 bits before __floatsisf, an unsigned with zeros.
TEST_F(AvrTest, FloatFromInt)
{
    std::string s = Body(CompileToAvr("float f(unsigned u) { return u; }"));
    EXPECT_NE(std::string::npos, s.find("mov r24, r1\nmov r25, r1\ncall __floatunsisf\n")) << s;
}

TEST_F(AvrTest, RunFloatTruthAndNegation)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
int main(void)
{
    volatile double z = -0.0, one = 1.0, nan = 0.0 / 0.0;
    if (z) return 1;
    if (!one) return 2;
    if (!nan) return 3;
    if (-one != -1.0 || -(-one) != 1.0) return 4;
    double x = one / 3;
    if (x * 3 != 1.0) return 5;
    long double ld = one;
    float f = ld + 0.5L;
    if (f != 1.5f) return 6;
    return 0;
}
)"));
}
