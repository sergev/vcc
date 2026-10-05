//
// MSP430 integer operations: chains over the words, compares from the high word down,
// shifts, helpers for multiply and divide; and runs of our code against the host.
//
#include <cstdint>

#include "msp430_test.h"

// A 32-bit add: the first operand in r13:r12, the second straight from memory, the
// carry through addc.
TEST_F(Msp430Test, AddLongChain)
{
    std::string code = Code(CompileToMsp430("long g1, g2, g3; void f(void) { g3 = g1 + g2; }"));
    EXPECT_NE(std::string::npos,
              code.find(R"(mov &g1, r13
mov &g1+2, r12
add &g2, r13
addc &g2+2, r12
mov r13, &g3
mov r12, &g3+2
)"))
        << code;
}

// A signed 32-bit compare, in place: the high words signed, then the low words
// unsigned.
EXPECT_CODE(CompareLongSigned, R"(cmp r15, r13
jl .Lv1
jne .Lv3
cmp r14, r12
jlo .Lv1
clr r12
ret
mov #1, r12
ret
)", "int f(long a, long b) { return a < b; }")

EXPECT_CODE(CompareLongUnsigned, R"(cmp r15, r13
jlo .Lv3
jne .Lv1
cmp r14, r12
jhs .Lv1
clr r12
ret
mov #1, r12
ret
)", "int f(unsigned long a, unsigned long b) { return a >= b; }")

// A shift by a small constant is unrolled, through the carry.
TEST_F(Msp430Test, ShiftLeftLongUnrolled)
{
    std::string code = Code(CompileToMsp430("long f(long a) { return a << 2; }"));
    EXPECT_NE(std::string::npos, code.find(R"(rla r12
rlc r13
rla r12
rlc r13
)")) << code;
}

// By 20: a word moved, its sign filled in (r13 still holds the word), then a loop of 4.
TEST_F(Msp430Test, ShiftRightLongByWordThenLoop)
{
    std::string code = Code(CompileToMsp430("long f(long a) { return a >> 20; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r13, r12
rla r13
subc r13, r13
inv r13
mov #4, r15
)"))
        << code;
    EXPECT_NE(std::string::npos, code.find(R"(rra r13
rrc r12
dec r15
jne )")) << code;
}

TEST_F(Msp430Test, NegateLong)
{
    std::string code = Code(CompileToMsp430("long f(long a) { return -a; }"));
    EXPECT_NE(std::string::npos, code.find(R"(inv r12
inv r13
inc r12
adc r13
)")) << code;
}

// Multiply and divide go through the runtime, by the __mspabi_ names GCC's code calls
// (a tail jump, the last thing a frameless function does).
TEST_F(Msp430Test, HelpersByWidth)
{
    std::string code = Code(CompileToMsp430(R"(
        int a(int x, int y) { return x * y; }
        unsigned b(unsigned x, unsigned y) { return x / y; }
        long c(long x, long y) { return x % y; }
        unsigned long d(unsigned long x, unsigned long y) { return x * y; }
        long long e(long long x, long long y) { return x / y; }
    )"));
    for (const char *h : { "br #__mspabi_mpyi\n", "br #__mspabi_divu\n",
                           "br #__mspabi_remli\n", "br #__mspabi_mpyl\n",
                           "call #__mspabi_divlli\n" })
        EXPECT_NE(std::string::npos, code.find(h)) << h << code;
}

// A 64-bit helper takes its first operand in r11:r8, the second in r15:r12, and needs
// no outgoing area.
TEST_F(Msp430Test, Helper64FirstOperandInR8)
{
    std::string code =
        Code(CompileToMsp430("long long f(long long x, long long y) { return x * y; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r12, r8
mov r13, r9
mov r14, r10
mov r15, r11
mov 8(r1), r12
)")) << code;
    EXPECT_NE(std::string::npos, code.find("call #__mspabi_mpyll\n")) << code;
}

// Widening: a signed int's sign word, an unsigned char zero-extended by mov.b.
TEST_F(Msp430Test, SignExtendIntToLong)
{
    std::string code = Code(CompileToMsp430("long f(int a) { return a; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r12, r13
rla r13
subc r13, r13
inv r13
)"))
        << code;
}

EXPECT_CODE(ZeroExtendCharToLong, R"(mov.b r12, r12
clr r13
ret
)", "long f(unsigned char a) { return a; }")

// The output routines our run tests share, compiled by us.
static const char print_c[] = R"(
void putbyte(int c);
static void putu(unsigned long v)
{
    char b[12];
    int i = 0;
    do {
        b[i++] = '0' + v % 10u;
        v /= 10u;
    } while (v);
    while (i)
        putbyte(b[--i]);
}
static void puti(long v)
{
    if (v < 0) {
        putbyte('-');
        putu(-(unsigned long)v);
    } else {
        putu(v);
    }
}
)";

static const int16_t ops16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
static const int32_t ops32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                                 0, 1, -1, 123456789, 65536, -65537 };

// The arithmetic of int and long, compiled by us, against the host: + - * / % & | ^ and
// the comparisons, over operands with every sign and the extremes.
TEST_F(Msp430Test, RunIntArithmetic)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile int a16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
volatile long a32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                        0, 1, -1, 123456789, 65536, -65537 };
static void sp(void) { putbyte(' '); }
int main(void)
{
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int x = a16[i], y = a16[j];
            unsigned ux = x, uy = y;
            puti((int)(ux + uy)); sp(); puti((int)(ux - uy)); sp(); puti((int)(ux * uy)); sp();
            puti(x & y); sp(); puti(x | y); sp(); puti(x ^ y); sp();
            puti((x < y) + 2 * (x <= y) + 4 * (x > y) + 8 * (x >= y) + 16 * (x == y) +
                 32 * (x != y));
            sp();
            puti((ux < uy) + 2 * (ux <= uy) + 4 * (ux > uy) + 8 * (ux >= uy)); sp();
            if (y != 0 && !(x == -32768 && y == -1)) {
                puti(x / y); sp(); puti(x % y); sp();
            }
            if (y != 0) {
                putu(ux / uy); sp(); putu(ux % uy); sp();
            }
            long p = a32[i], q = a32[j];
            unsigned long up = p, uq = q;
            puti((long)(up + uq)); sp(); puti((long)(up - uq)); sp();
            puti((long)(up * uq)); sp();
            puti(p & q); sp(); puti(p | q); sp(); puti(p ^ q); sp();
            puti((p < q) + 2 * (p <= q) + 4 * (p > q) + 8 * (p >= q) + 16 * (p == q) +
                 32 * (p != q));
            sp();
            puti((up < uq) + 2 * (up <= uq) + 4 * (up > uq) + 8 * (up >= uq)); sp();
            if (q != 0 && !(p == -2147483647 - 1 && q == -1)) {
                puti(p / q); sp(); puti(p % q); sp();
            }
            if (q != 0) {
                putu(up / uq); sp(); putu(up % uq);
            }
            putbyte('\n');
        }
    return 0;
}
)";
    std::string e;
    auto num = [&](long long v) { e += std::to_string(v) + " "; };
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int16_t x = ops16[i], y = ops16[j];
            uint16_t ux = x, uy = y;
            num((int16_t)(uint16_t)(ux + uy));
            num((int16_t)(uint16_t)(ux - uy));
            num((int16_t)(uint16_t)(ux * uy));
            num(x & y);
            num(x | y);
            num(x ^ y);
            num((x < y) + 2 * (x <= y) + 4 * (x > y) + 8 * (x >= y) + 16 * (x == y) +
                32 * (x != y));
            num((ux < uy) + 2 * (ux <= uy) + 4 * (ux > uy) + 8 * (ux >= uy));
            if (y != 0 && !(x == -32768 && y == -1)) {
                num(x / y);
                num(x % y);
            }
            if (y != 0) {
                num(ux / uy);
                num(ux % uy);
            }
            int32_t p = ops32[i], q = ops32[j];
            uint32_t up = p, uq = q;
            num((int32_t)(up + uq));
            num((int32_t)(up - uq));
            num((int32_t)(up * uq));
            num(p & q);
            num(p | q);
            num(p ^ q);
            num((p < q) + 2 * (p <= q) + 4 * (p > q) + 8 * (p >= q) + 16 * (p == q) +
                32 * (p != q));
            num((up < uq) + 2 * (up <= uq) + 4 * (up > uq) + 8 * (up >= uq));
            if (q != 0 && !(p == INT32_MIN && q == -1)) {
                num(p / q);
                num(p % q);
            }
            if (q != 0)
                e += std::to_string(up / uq) + " " + std::to_string(up % uq);
            e += "\n";
        }
    EXPECT_EQ(e, CompileAndRunMsp430(src));
    EXPECT_EQ(0, exit_status);
}

// Shifts by every count, constant and variable, of int and long, signed and not.
TEST_F(Msp430Test, RunShifts)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile int vi[] = { -12345, 0x5a5a };
volatile long vl[] = { -2023406815L, 0x12345678L };
static void sp(void) { putbyte(' '); }
#define CONST16(k) puti((int)((unsigned)x << k)); sp(); puti(x >> k); sp(); \
                   putu((unsigned)x >> k); sp();
#define CONST32(k) puti((long)((unsigned long)y << k)); sp(); puti(y >> k); sp(); \
                   putu((unsigned long)y >> k); sp();
int main(void)
{
    for (int i = 0; i < 2; i++) {
        int x = vi[i];
        long y = vl[i];
        for (volatile int n = 0; n < 16; n++) {
            puti((int)((unsigned)x << n)); sp(); puti(x >> n); sp(); putu((unsigned)x >> n); sp();
        }
        for (volatile int n = 0; n < 32; n++) {
            puti((long)((unsigned long)y << n)); sp(); puti(y >> n); sp();
            putu((unsigned long)y >> n); sp();
        }
        CONST16(0) CONST16(1) CONST16(3) CONST16(4) CONST16(7) CONST16(8) CONST16(15)
        CONST32(1) CONST32(3) CONST32(4) CONST32(15) CONST32(16) CONST32(17) CONST32(20)
        CONST32(31)
        putbyte('\n');
    }
    return 0;
}
)";
    static const int16_t vi[] = { -12345, 0x5a5a };
    static const int32_t vl[] = { -2023406815, 0x12345678 };
    std::string e;
    auto num = [&](long long v) { e += std::to_string(v) + " "; };
    for (int i = 0; i < 2; i++) {
        int16_t x = vi[i];
        int32_t y = vl[i];
        for (int n = 0; n < 16; n++) {
            num((int16_t)(uint16_t)((uint16_t)x << n));
            num(x >> n);
            num((uint16_t)x >> n);
        }
        for (int n = 0; n < 32; n++) {
            num((int32_t)((uint32_t)y << n));
            num(y >> n);
            num((uint32_t)y >> n);
        }
        for (int k : { 0, 1, 3, 4, 7, 8, 15 }) {
            num((int16_t)(uint16_t)((uint16_t)x << k));
            num(x >> k);
            num((uint16_t)x >> k);
        }
        for (int k : { 1, 3, 4, 15, 16, 17, 20, 31 }) {
            num((int32_t)((uint32_t)y << k));
            num(y >> k);
            num((uint32_t)y >> k);
        }
        e += "\n";
    }
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}

// long long without the runtime: add, subtract, the logic, compare, shifts, negation
// and the conversions from narrower types.
TEST_F(Msp430Test, RunLongLongInline)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile long long a = 0x123456789abcdef0LL, b = -0x0fedcba987654321LL;
volatile int m = -5;
volatile unsigned char uc = 200;
static void hex(unsigned long long v)
{
    for (int i = 60; i >= 0; i -= 4)
        putbyte("0123456789abcdef"[(int)(v >> i) & 15]);
    putbyte(' ');
}
int main(void)
{
    long long x = a, y = b;
    hex(x + y); hex(x - y); hex(x & y); hex(x | y); hex(x ^ y); hex(-x); hex(~y);
    hex(x << 1); hex(x << 33); hex(y >> 7); hex(y >> 40); hex((unsigned long long)y >> 40);
    for (volatile int n = 0; n < 64; n += 9)
        hex(y >> n);
    hex(m); hex(uc);
    putu((x < y) + 2 * (x > y) + 4 * (x == y) + 8 * ((unsigned long long)x < (unsigned long long)y));
    return 0;
}
)";
    int64_t x = 0x123456789abcdef0LL, y = -0x0fedcba987654321LL;
    std::string e;
    auto hex = [&](uint64_t v) {
        char buf[20];
        snprintf(buf, sizeof buf, "%016llx ", (unsigned long long)v);
        e += buf;
    };
    hex(x + y);
    hex(x - y);
    hex(x & y);
    hex(x | y);
    hex(x ^ y);
    hex(-x);
    hex(~y);
    hex((uint64_t)x << 1);
    hex((uint64_t)x << 33);
    hex(y >> 7);
    hex(y >> 40);
    hex((uint64_t)y >> 40);
    for (int n = 0; n < 64; n += 9)
        hex(y >> n);
    hex((int64_t)-5);
    hex(200);
    e += std::to_string((x < y) + 2 * (x > y) + 4 * (x == y) + 8 * ((uint64_t)x < (uint64_t)y));
    EXPECT_EQ(e, CompileAndRunMsp430(src));
}
