// long long on rv32: the inline register-pair sequences and the runtime routines
// (libc/ilp32/int64.c), against the same operations done by the host.  Built into
// riscv32-tests only.
#include <cinttypes>
#include <cstring>

#include "riscv_test.h"

static_assert(RISCV_TEST_XLEN == 32, "llong_tests.cpp belongs in riscv32-tests");

using LongLongTest = RiscvTest;

// Output without printf: a 64-bit value in hex, or a decimal int.
static const char *const kPrelude = R"(
#include <stdio.h>
typedef unsigned long long u64;
typedef long long i64;
static void hex(u64 x)
{
    for (int i = 60; i >= 0; i -= 4)
        putchar("0123456789abcdef"[(x >> i) & 15]);
    putchar(' ');
}
static void nl(void) { putchar('\n'); }
)";

static const uint64_t kValues[] = {
    0,
    1,
    UINT64_MAX, // -1
    2,
    (uint64_t)-2,
    7,
    0x7fffffff,
    0x80000000,
    0xffffffff,
    0x100000000,
    0x123456789abcdef0,
    0x7fffffffffffffff,
    0x8000000000000000,
    (uint64_t)-0x123456789,
    1000000007,
    0x8000000000000001,
    0x20000000000001,
    0x1000001000000001,
};
static const int kNumValues = sizeof(kValues) / sizeof(kValues[0]);

static std::string Hex(uint64_t x)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%016" PRIx64 " ", x);
    return buf;
}

// The values as a C initializer.
static std::string ValuesInit()
{
    std::string s = "static u64 v[] = {";
    for (uint64_t x : kValues)
        s += "0x" + Hex(x).substr(0, 16) + "ULL, ";
    return s + "};\n#define N " + std::to_string(kNumValues) + "\n";
}

static uint64_t DoubleBits(double d)
{
    uint64_t b;
    memcpy(&b, &d, 8);
    return b;
}

static uint64_t FloatBits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

TEST_F(LongLongTest, Arithmetic)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + R"(
int main(void)
{
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            u64 a = v[i], b = v[j];
            i64 sa = (i64)a, sb = (i64)b;
            hex(a + b);
            hex(a - b);
            hex(a * b);
            hex(a & b);
            hex(a | b);
            hex(a ^ b);
            if (b) {
                hex(a / b);
                hex(a % b);
                if (!(sb == -1 && sa == (i64)0x8000000000000000ULL)) {
                    hex(sa / sb);
                    hex(sa % sb);
                }
            }
            nl();
        }
        hex(-v[i]);
        hex(~v[i]);
        hex(!v[i]);
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        for (uint64_t b : kValues) {
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            expect += Hex(a + b) + Hex(a - b) + Hex(a * b) + Hex(a & b) + Hex(a | b) + Hex(a ^ b);
            if (b) {
                expect += Hex(a / b) + Hex(a % b);
                if (!(sb == -1 && sa == INT64_MIN))
                    expect += Hex((uint64_t)(sa / sb)) + Hex((uint64_t)(sa % sb));
            }
            expect += "\n";
        }
        expect += Hex(0 - a) + Hex(~a) + Hex(!a) + "\n";
    }
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

TEST_F(LongLongTest, Comparisons)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + R"(
int main(void)
{
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            u64 a = v[i], b = v[j];
            i64 sa = (i64)a, sb = (i64)b;
            int bits = (a == b) | (a != b) << 1 | (a < b) << 2 | (a <= b) << 3 | (a > b) << 4 |
                       (a >= b) << 5 | (sa < sb) << 6 | (sa <= sb) << 7 | (sa > sb) << 8 |
                       (sa >= sb) << 9;
            if (sa < sb)
                bits |= 1 << 10;
            if (a > b)
                bits |= 1 << 11;
            hex(bits);
        }
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        for (uint64_t b : kValues) {
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            int bits = (a == b) | (a != b) << 1 | (a < b) << 2 | (a <= b) << 3 | (a > b) << 4 |
                       (a >= b) << 5 | (sa < sb) << 6 | (sa <= sb) << 7 | (sa > sb) << 8 |
                       (sa >= sb) << 9 | (sa < sb) << 10 | (a > b) << 11;
            expect += Hex(bits);
        }
        expect += "\n";
    }
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

static const int kCounts[] = { 0, 1, 5, 31, 32, 33, 40, 63 };

TEST_F(LongLongTest, Shifts)
{
    SKIP_IF_NO_RISCV_TOOLS();
    // Variable counts, of int and of long long type, and each count as a constant.
    std::string src = std::string(kPrelude) + ValuesInit() + "static int c[] = {";
    for (int n : kCounts)
        src += std::to_string(n) + ", ";
    src += "};\nstatic void constant(u64 a)\n{\n    i64 s = (i64)a;\n";
    for (int n : kCounts) {
        std::string k = std::to_string(n);
        src += "    hex(a << " + k + ");\n    hex(a >> " + k + ");\n    hex(s >> " + k + ");\n";
    }
    src += R"(}
int main(void)
{
    for (int i = 0; i < N; i++) {
        u64 a = v[i];
        i64 s = (i64)a;
        for (int j = 0; j < 8; j++) {
            int n = c[j];
            u64 m = n;
            hex(a << n);
            hex(a >> n);
            hex(s >> n);
            hex(a << m);
            hex(s >> m);
            hex(1 << (m & 15));
        }
        nl();
        constant(a);
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        int64_t s = (int64_t)a;
        std::string constant;
        for (int n : kCounts) {
            expect += Hex(a << n) + Hex(a >> n) + Hex((uint64_t)(s >> n)) + Hex(a << n) +
                      Hex((uint64_t)(s >> n)) + Hex(1u << (n & 15));
            constant += Hex(a << n) + Hex(a >> n) + Hex((uint64_t)(s >> n));
        }
        expect += "\n" + constant + "\n";
    }
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

static const double kDoubles[] = {
    0.0,  0.5,   -0.5,   1.5,     -1.5, 4294967296.5, -4294967297.75, 123456789012.9,
    1e18, -1e18, 9.2e18, -9.2e18,
};

TEST_F(LongLongTest, Conversions)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + "static double d[] = {";
    for (double x : kDoubles) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.17g, ", x);
        src += buf;
    }
    src += R"(};
static u64 dbits(double x) { return *(u64 *)&x; }
static u64 fbits(float x) { return *(unsigned *)&x; }
int main(void)
{
    for (int i = 0; i < N; i++) {
        u64 a = v[i];
        i64 s = (i64)a;
        hex(dbits((double)a));
        hex(dbits((double)s));
        hex(fbits((float)a));
        hex(fbits((float)s));
        int x = (int)a;
        unsigned u = (unsigned)a;
        short h = (short)s;
        unsigned char c = (unsigned char)a;
        hex((i64)x);
        hex((u64)u);
        hex((i64)h);
        hex((u64)c);
        hex((i64)(signed char)c);
        nl();
    }
    for (int i = 0; i < 12; i++) {
        hex((u64)(i64)d[i]);
        hex((u64)(i64)(float)d[i]);
        if (d[i] >= 0) {
            hex((u64)d[i]);
            hex((u64)(d[i] * 2));
            hex((u64)(float)d[i]);
        }
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        int64_t s = (int64_t)a;
        expect += Hex(DoubleBits((double)a)) + Hex(DoubleBits((double)s)) +
                  Hex(FloatBits((float)a)) + Hex(FloatBits((float)s));
        expect += Hex((uint64_t)(int64_t)(int32_t)a) + Hex((uint32_t)a) +
                  Hex((uint64_t)(int64_t)(int16_t)a) + Hex((uint8_t)a) +
                  Hex((uint64_t)(int64_t)(int8_t)a) + "\n";
    }
    for (double x : kDoubles) {
        expect += Hex((uint64_t)(int64_t)x) + Hex((uint64_t)(int64_t)(float)x);
        if (x >= 0)
            expect += Hex((uint64_t)x) + Hex((uint64_t)(x * 2)) + Hex((uint64_t)(float)x);
        expect += "\n";
    }
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

TEST_F(LongLongTest, CallsAndMemory)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = std::string(kPrelude) + R"(
#include <stdarg.h>
i64 g = -5000000000LL;
u64 table[3] = { 1, 0xfedcba9876543210ULL, 3 };
struct s { char c; i64 x; int y; u64 z; };
struct s gs = { 'A', 0x1122334455667788LL, 9, 77 };
// a0/a1 for x, a2 for i, a4/a5 for y (an aligned pair skips nothing when not variadic).
i64 f1(i64 x, int i, i64 y) { return x * i + y; }
// The fourth long long is split: low word in a7, high word on the stack.
i64 f2(int a, i64 b, i64 c, i64 d, i64 e) { return a + b + c + d + e; }
// Entirely on the stack, 8-aligned after an int.
i64 f3(i64 a, i64 b, i64 c, i64 d, int e, i64 f) { return a + b + c + d + e + f; }
i64 sum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    i64 s = 0;
    for (int i = 0; i < n; i++) {
        int k = va_arg(ap, int);
        s += k * va_arg(ap, i64);
    }
    va_end(ap);
    return s;
}
u64 *ptr(int i) { return &table[i]; }
struct s mk(i64 x) { struct s r = { 'B', x, 1, (u64)x * 2 }; return r; }
int which(i64 x)
{
    switch (x) {
    case 0x100000000LL: return 1;
    case -1: return 2;
    case 5: return 3;
    default: return 4;
    }
}
int main(void)
{
    hex(f1(0x100000000LL, 3, -1));
    hex(f2(1, 2, 3, 0x300000000LL, 0x400000000LL));
    hex(f3(1, 2, 3, 4, 5, 0x600000000LL));
    hex(sum(3, 1, 0x100000000LL, 2, 5LL, 3, -1LL));
    hex(sum(5, 1, 1LL, 1, 2LL, 1, 3LL, 1, 4LL, 1, 0x500000000LL));
    nl();
    hex(g);
    g += 1;
    hex(g++);
    hex(--g);
    hex(*ptr(1));
    *ptr(2) <<= 40;
    hex(table[2]);
    u64 *p = table;
    p[0] = p[1] / 3;
    hex(table[0]);
    nl();
    hex(gs.x);
    hex(gs.z);
    gs.x = gs.x * 2 + gs.y;
    hex(gs.x);
    struct s t = mk(-3);
    hex(t.x);
    hex(t.z);
    hex(t.c + t.y);
    nl();
    hex(which(0x100000000LL));
    hex(which(-1));
    hex(which(5));
    hex(which(0xffffffffLL));
    i64 k = 0;
    while (k < 0x300000000LL)
        k += 0x100000000LL;
    hex(k);
    hex(k ? 1 : 2);
    hex(k && g);
    int i = 7;
    hex(k + i);
    hex(i - k);
    unsigned u = 0xffffffffu;
    hex(k + u);
    nl();
    return 0;
}
)";
    std::string expect;
    expect += Hex(0x300000000ull - 1) + Hex(1 + 2 + 3 + 0x300000000ull + 0x400000000ull) +
              Hex(1 + 2 + 3 + 4 + 5 + 0x600000000ull) + Hex(0x100000000ull + 10 - 3) +
              Hex(1 + 2 + 3 + 4 + 0x500000000ull) + "\n";
    expect += Hex((uint64_t)-5000000000ll) + Hex((uint64_t)-4999999999ll) +
              Hex((uint64_t)-4999999999ll) + Hex(0xfedcba9876543210ull) + Hex(3ull << 40) +
              Hex(0xfedcba9876543210ull / 3) + "\n";
    expect += Hex(0x1122334455667788ull) + Hex(77) + Hex(0x1122334455667788ull * 2 + 9) +
              Hex((uint64_t)-3) + Hex((uint64_t)-6) + Hex('B' + 1) + "\n";
    expect += Hex(1) + Hex(2) + Hex(3) + Hex(4) + Hex(0x300000000ull) + Hex(1) + Hex(1) +
              Hex(0x300000007ull) + Hex((uint64_t)(7 - 0x300000000ll)) +
              Hex(0x300000000ull + 0xffffffffull) + "\n";
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

// Copies the allocator coalesces as pairs, or must not: a source still live after the
// copy, a rotation, a swap, and the words of one value moved to another's.
TEST_F(LongLongTest, Copies)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = std::string(kPrelude) + R"(
u64 swapw(u64 x) { u64 y = x; x = x >> 32 | x << 32; return x ^ (y + 1); }
i64 fib(int n) { i64 a = 0, b = 1; while (n-- > 0) { i64 t = a + b; a = b; b = t; } return a; }
void swap(u64 *p, u64 *q) { u64 a = *p, b = *q, t = a; a = b; b = t; *p = a; *q = b; }
i64 keep(i64 a, i64 b) { i64 c = a; a = b * 3; b = c - 1; return a * 5 + b + c; }
int main(void)
{
    hex(swapw(0x0123456789abcdefULL));
    hex(fib(90));
    u64 x = 0x1111111122222222ULL, y = 0x3333333344444444ULL;
    swap(&x, &y);
    hex(x);
    hex(y);
    hex(keep(0x100000005LL, -0x200000007LL));
    nl();
    return 0;
}
)";
    uint64_t sw     = 0x0123456789abcdefull;
    uint64_t fib = 0, b = 1;
    for (int i = 0; i < 90; i++) {
        uint64_t t = fib + b;
        fib        = b;
        b          = t;
    }
    int64_t ka = 0x100000005ll, kb = -0x200000007ll;
    std::string expect = Hex((sw >> 32 | sw << 32) ^ (sw + 1)) + Hex(fib) +
                         Hex(0x3333333344444444ull) + Hex(0x1111111122222222ull) +
                         Hex((uint64_t)(kb * 3 * 5 + (ka - 1) + ka)) + "\n";
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
}

TEST_F(LongLongTest, Runtime)
{
    SKIP_IF_NO_RISCV_TOOLS();
    // Division by a divisor with the top bit set, and the float rounding that a
    // conversion through double would get wrong.
    std::string src = std::string(kPrelude) + R"(
static u64 fbits(float x) { return *(unsigned *)&x; }
int main(void)
{
    u64 n = 0xffffffffffffffffULL, d = 0x8000000000000001ULL;
    hex(n / d);
    hex(n % d);
    hex(d / n);
    hex(d % n);
    u64 big = 0x1000001000000001ULL;
    hex(fbits((float)big));
    hex(fbits((float)(i64)big));
    hex(fbits((float)-(i64)big));
    nl();
    return 0;
}
)";
    uint64_t n = UINT64_MAX, d = 0x8000000000000001ull, big = 0x1000001000000001ull;
    std::string expect = Hex(n / d) + Hex(n % d) + Hex(d / n) + Hex(d % n) +
                         Hex(FloatBits((float)big)) + Hex(FloatBits((float)(int64_t)big)) +
                         Hex(FloatBits((float)-(int64_t)big)) + "\n";
    EXPECT_EQ(expect, CompileAndRunRiscv(src));
    EXPECT_EQ(Hex(0x5d800001), Hex(FloatBits((float)big))) << "the host rounds as expected";
}
