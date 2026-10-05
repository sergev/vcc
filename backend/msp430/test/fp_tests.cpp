//
// MSP430 floating point: every operation a runtime call but negation and the truth
// test; and runs of our code (and our float32.c/float64.c, compiled by genmsp430)
// against the host, bit for bit.
//
#include <cmath>
#include <cstdint>
#include <cstring>

#include "msp430_test.h"

// A double's arithmetic as GCC calls it: the first operand in r11:r8, which the
// prologue saves, the second in r15:r12.
TEST_F(Msp430Test, DoubleAddCallsHelper)
{
    std::string code = Code(CompileToMsp430("double f(double a, double b) { return a + b; }"));
    EXPECT_EQ(0u, code.find(R"(push r8
push r9
push r10
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov r15, r11
mov 24(r1), r12
mov 26(r1), r13
mov 28(r1), r14
mov 30(r1), r15
call #__mspabi_addd
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(pop r10
pop r9
pop r8
ret
)")) << code;
}

// A float's second operand goes in r15:r14, where it came: nothing to move, and the
// call a tail jump.
EXPECT_CODE(FloatMultiplyCallsHelper, R"(br #__mspabi_mpyf
)", "float f(float a, float b) { return a * b; }")

// Each comparison through its own libgcc predicate, tested against zero: > as r >= 1.
TEST_F(Msp430Test, DoubleCompare)
{
    std::string code = Code(CompileToMsp430("int f(double a, double b) { return a > b; }"));
    EXPECT_NE(std::string::npos, code.find(R"(call #__gtdf2
cmp #1, r12
mov #1, r12
jge )"))
        << code;
}

// Negation flips the sign bit, inline, in the result's slot (a double stays in memory).
TEST_F(Msp430Test, DoubleNegateInline)
{
    std::string code = Code(CompileToMsp430("double f(double a) { return -a; }"));
    EXPECT_NE(std::string::npos, code.find("xor #-32768, 14(r1)\n")) << code;
    EXPECT_EQ(std::string::npos, code.find("call")) << code;
}

// The truth test: any bit but the sign, word by word where the value lies.
TEST_F(Msp430Test, DoubleTruthTest)
{
    std::string code = Code(CompileToMsp430("int f(double a) { return a ? 1 : 2; }"));
    EXPECT_NE(std::string::npos,
              code.find(R"(tst 0(r1)
jne .Lv1
tst 2(r1)
jne .Lv1
tst 4(r1)
jne .Lv1
bit #32767, 6(r1)
jeq )"))
        << code;
}

// Conversions by the helpers GCC's code calls; an int widens to 32 bits first.
TEST_F(Msp430Test, Conversions)
{
    std::string code = Code(CompileToMsp430(R"(
        double a(int p1) { return p1; }
        float b(unsigned p2) { return p2; }
        long c(double p3) { return p3; }
        unsigned long d(float p4) { return p4; }
        double e(float p5) { return p5; }
        float g(double p6) { return p6; }
        long long h(double p7) { return p7; }
    )"));
    for (const char *h : { "call #__mspabi_fltlid\n", R"(clr r13
br #__mspabi_fltulf
)",
                           "call #__mspabi_fixdli\n", "br #__fixunssfsi\n",
                           "call #__mspabi_cvtfd\n", "call #__mspabi_cvtdf\n",
                           "call #__mspabi_fixdlli\n" })
        EXPECT_NE(std::string::npos, code.find(h)) << h << code;
}

namespace {

uint64_t Bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

uint32_t Bits32(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

std::string Hex(uint64_t v, int digits)
{
    char buf[24];
    snprintf(buf, sizeof buf, "%0*llx ", digits, (unsigned long long)v);
    return buf;
}

// The output routines of the runs: bits in hex.
const char print_c[] = R"(
void putbyte(int c);
static void hexw(unsigned pw)
{
    for (int pi = 12; pi >= 0; pi -= 4)
        putbyte("0123456789abcdef"[(pw >> pi) & 15]);
}
static void hexd(double pd)
{
    union { double d; unsigned w[4]; } pu;
    pu.d = pd;
    for (int pi = 3; pi >= 0; pi--)
        hexw(pu.w[pi]);
    putbyte(' ');
}
static void hexf(float pf)
{
    union { float f; unsigned w[2]; } pv;
    pv.f = pf;
    hexw(pv.w[1]);
    hexw(pv.w[0]);
    putbyte(' ');
}
static void hexl(unsigned long pl)
{
    hexw((unsigned)(pl >> 16));
    hexw((unsigned)pl);
    putbyte(' ');
}
)";

const double dops[] = { 0.0, -0.0, 1.0, -2.5, 0.1, 3.0e-310, 1.0e300, 7.25, -1.0e-5, 1.0 / 0.0 };
const float fops[]  = { 0.0f, 1.0f, -2.5f, 0.1f, 1.0e-40f, 3.0e38f, 7.25f, -1.0e-5f };

} // namespace

// double + - * / and sqrt, our code and our runtime, against the host.
TEST_F(Msp430Test, RunDoubleArithmetic)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
double sqrt(double);
volatile double v[] = { 0.0, -0.0, 1.0, -2.5, 0.1, 3.0e-310, 1.0e300, 7.25, -1.0e-5, 0.0 };
int main(void)
{
    v[9] = v[6] * v[6]; // infinity: 1.0 / 0.0 is no static initializer here
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            double a = v[i], b = v[j];
            hexd(a + b); hexd(a - b); hexd(a * b); hexd(a / b);
        }
        hexd(sqrt(v[i] < 0 ? -v[i] : v[i]));
        putbyte('\n');
    }
    return 0;
}
)";
    std::string e;
    for (double a : dops) {
        for (double b : dops) {
            e += Hex(Bits(a + b), 16) + Hex(Bits(a - b), 16) + Hex(Bits(a * b), 16);
            double q = a / b;
            // 0/0 and inf/inf: the host's default NaN; ours is the positive quiet NaN.
            e += Hex(std::isnan(q) ? 0x7ff8000000000000ULL : Bits(q), 16);
        }
        e += Hex(Bits(std::sqrt(a < 0 ? -a : a)), 16) + "\n";
    }
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}

TEST_F(Msp430Test, RunFloatArithmetic)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile float v[] = { 0.0f, 1.0f, -2.5f, 0.1f, 1.0e-40f, 3.0e38f, 7.25f, -1.0e-5f };
int main(void)
{
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float a = v[i], b = v[j];
            hexf(a + b); hexf(a - b); hexf(a * b); hexf(a / b);
        }
        putbyte('\n');
    }
    return 0;
}
)";
    std::string e;
    for (float a : fops) {
        for (float b : fops) {
            e += Hex(Bits32(a + b), 8) + Hex(Bits32(a - b), 8) + Hex(Bits32(a * b), 8);
            float q = a / b;
            e += Hex(std::isnan(q) ? 0x7fc00000u : Bits32(q), 8);
        }
        e += "\n";
    }
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}

// Every comparison, NaN included: each is false for an unordered pair but !=.
TEST_F(Msp430Test, RunComparisons)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile double d[] = { -1.0, 0.0, -0.0, 2.0, 0.0 };
volatile float f[] = { -1.0f, 0.0f, -0.0f, 2.0f, 0.0f };
int main(void)
{
    d[4] = d[1] / d[1]; // NaN, made at run time
    f[4] = f[1] / f[1];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++) {
            double a = d[i], b = d[j];
            float x = f[i], y = f[j];
            putbyte('0' + ((a < b) | (a <= b) << 1 | (a > b) << 2) );
            putbyte('0' + ((a >= b) | (a == b) << 1 | (a != b) << 2));
            putbyte('0' + ((x < y) | (x <= y) << 1 | (x > y) << 2));
            putbyte('0' + ((x >= y) | (x == y) << 1 | (x != y) << 2));
            putbyte('0' + (!a | !x << 1 | (a ? 1 : 0) << 2));
            putbyte(' ');
        }
    return 0;
}
)";
    static const double d[] = { -1.0, 0.0, -0.0, 2.0, NAN };
    std::string e;
    for (double a : d)
        for (double b : d) {
            float x = (float)a, y = (float)b;
            e += (char)('0' + ((a < b) | (a <= b) << 1 | (a > b) << 2));
            e += (char)('0' + ((a >= b) | (a == b) << 1 | (a != b) << 2));
            e += (char)('0' + ((x < y) | (x <= y) << 1 | (x > y) << 2));
            e += (char)('0' + ((x >= y) | (x == y) << 1 | (x != y) << 2));
            e += (char)('0' + (!a | !x << 1 | (a ? 1 : 0) << 2));
            e += ' ';
        }
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}

// Conversions between double, float and the integers of every width.
TEST_F(Msp430Test, RunConversions)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile double d[] = { 0.0, -0.5, 1.75, -32768.9, 65535.5, 123456789.75, -2147483648.0,
                        4294967295.0, 1.0e-320, 3.4028235677973366e38 };
volatile int ii = -12345;
volatile unsigned uu = 54321;
volatile long ll = -1234567890L;
volatile unsigned long ul = 4000000000UL;
volatile long long qq = -1234567890123456789LL;
volatile unsigned long long uq = 18000000000000000000ULL;
int main(void)
{
    for (int i = 0; i < 10; i++) {
        double x = d[i];
        hexf((float)x);
        hexd((double)(float)x);
        if (x > -32769.0 && x < 32768.0) hexw((unsigned)(int)x);
        putbyte(' ');
        if (x > -1.0 && x < 65536.0) hexw((unsigned)x);
        putbyte(' ');
        if (x > -2147483649.0 && x < 2147483648.0) hexl((unsigned long)(long)x);
        if (x > -1.0 && x < 4294967296.0) hexl((unsigned long)x);
        hexd((double)(long long)(x * 1.0e9 < 9.0e18 && x * 1.0e9 > -9.0e18 ? x * 1.0e9 : 0.0));
        putbyte('\n');
    }
    hexd(ii); hexd(uu); hexd(ll); hexd(ul); hexd(qq); hexd(uq);
    hexf(ii); hexf(uu); hexf(ll); hexf(ul); hexf(qq); hexf(uq);
    return 0;
}
)";
    static const double d[] = { 0.0,           -0.5,          1.75,
                                -32768.9,      65535.5,       123456789.75,
                                -2147483648.0, 4294967295.0,  1.0e-320,
                                3.4028235677973366e38 };
    std::string e;
    for (double x : d) {
        e += Hex(Bits32((float)x), 8);
        e += Hex(Bits((double)(float)x), 16);
        if (x > -32769.0 && x < 32768.0)
            e += Hex((uint16_t)(int16_t)x, 4).substr(0, 4);
        e += " ";
        if (x > -1.0 && x < 65536.0)
            e += Hex((uint16_t)x, 4).substr(0, 4);
        e += " ";
        if (x > -2147483649.0 && x < 2147483648.0)
            e += Hex((uint32_t)(int32_t)x, 8);
        if (x > -1.0 && x < 4294967296.0)
            e += Hex((uint32_t)x, 8);
        double y = x * 1.0e9;
        e += Hex(Bits((double)(int64_t)(y < 9.0e18 && y > -9.0e18 ? y : 0.0)), 16);
        e += "\n";
    }
    int16_t ii = -12345;
    uint16_t uu = 54321;
    int32_t ll = -1234567890;
    uint32_t ul = 4000000000u;
    int64_t qq = -1234567890123456789LL;
    uint64_t uq = 18000000000000000000ULL;
    e += Hex(Bits(ii), 16) + Hex(Bits(uu), 16) + Hex(Bits(ll), 16) + Hex(Bits(ul), 16) +
         Hex(Bits((double)qq), 16) + Hex(Bits((double)uq), 16);
    e += Hex(Bits32(ii), 8) + Hex(Bits32(uu), 8) + Hex(Bits32((float)ll), 8) +
         Hex(Bits32((float)ul), 8) + Hex(Bits32((float)qq), 8) + Hex(Bits32((float)uq), 8);
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}
