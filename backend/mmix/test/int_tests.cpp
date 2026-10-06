//
// MMIX integer operations: the constant generator, the selected instructions, and runs
// against the host's arithmetic.
//
#include <climits>
#include <initializer_list>
#include <cstdint>
#include <sstream>
#include <type_traits>

#include "mmix_test.h"

extern "C" {
#include "internal.h"
}

// The constant generator's choice, as assembly lines.
static std::string Steps(uint64_t value)
{
    ConstStep steps[4];
    int n = mmix_const_steps(value, steps);
    std::string s;
    for (int i = 0; i < n; i++) {
        char buf[64];
        if (steps[i].op == MMIX_NEGU)
            snprintf(buf, sizeof buf, "negu $1, 0, %u\n", steps[i].arg);
        else
            snprintf(buf, sizeof buf, "%s $1, #%x\n", mmix_mnemonic[steps[i].op], steps[i].arg);
        s += buf;
    }
    return s;
}

// A value with few nonzero wydes is set and completed with inc; one with few wydes
// other than #ffff starts from -1 and clears; -1..-255 is one negu.
TEST(MmixConst, Table)
{
    EXPECT_EQ("setl $1, #0\n", Steps(0));
    EXPECT_EQ("setl $1, #c8\n", Steps(200));
    EXPECT_EQ("setl $1, #ffff\n", Steps(0xffff));
    EXPECT_EQ("setml $1, #1\n", Steps(0x10000));
    EXPECT_EQ("seth $1, #8000\n", Steps(0x8000000000000000ull));
    EXPECT_EQ("seth $1, #4004\n", Steps(0x4004000000000000ull)); // the double 2.5
    EXPECT_EQ(R"(setl $1, #cdef
incml $1, #89ab
incmh $1, #4567
inch $1, #123
)",
              Steps(0x0123456789abcdefull));
    EXPECT_EQ(R"(setl $1, #ffff
incml $1, #ffff
)", Steps(0xffffffffull));
    EXPECT_EQ(R"(setmh $1, #ffff
inch $1, #ffff
)", Steps(0xffffffff00000000ull));
    EXPECT_EQ("negu $1, 0, 1\n", Steps(UINT64_MAX));
    EXPECT_EQ("negu $1, 0, 255\n", Steps((uint64_t)-255));
    EXPECT_EQ(R"(negu $1, 0, 1
andnl $1, #ff
)", Steps((uint64_t)-256));
    EXPECT_EQ(R"(negu $1, 0, 1
andnl $1, #ffff
)", Steps((uint64_t)-65536));
    EXPECT_EQ(R"(negu $1, 0, 1
andnl $1, #3e7
)", Steps((uint64_t)-1000));
    EXPECT_EQ(R"(negu $1, 0, 1
andnh $1, #8000
)", Steps((uint64_t)INT64_MAX));
    // A tie (three either way) goes to set.
    EXPECT_EQ(R"(setml $1, #8000
incmh $1, #ffff
inch $1, #ffff
)",
              Steps((uint64_t)(int64_t)INT32_MIN));
    // Every value the generator emits is the value asked for.
    for (uint64_t v : std::initializer_list<uint64_t>{
             0ull, 1ull, 255ull, 256ull, 0x10001ull, 0xffff0000ffffull, 0xfffffffffffeull,
             0x8000000000000001ull, 0x7fffffff00000000ull, (uint64_t)-2, (uint64_t)-70000,
             (uint64_t)INT64_MIN }) {
        ConstStep steps[4];
        int n     = mmix_const_steps(v, steps);
        uint64_t r = 0;
        for (int i = 0; i < n; i++) {
            unsigned a = steps[i].arg;
            switch (steps[i].op) {
            case MMIX_NEGU: r = (uint64_t)0 - a; break;
            case MMIX_SETL: r = a; break;
            case MMIX_SETML: r = (uint64_t)a << 16; break;
            case MMIX_SETMH: r = (uint64_t)a << 32; break;
            case MMIX_SETH: r = (uint64_t)a << 48; break;
            case MMIX_INCML: r += (uint64_t)a << 16; break;
            case MMIX_INCMH: r += (uint64_t)a << 32; break;
            case MMIX_INCH: r += (uint64_t)a << 48; break;
            case MMIX_ANDNL: r &= ~(uint64_t)a; break;
            case MMIX_ANDNML: r &= ~((uint64_t)a << 16); break;
            case MMIX_ANDNMH: r &= ~((uint64_t)a << 32); break;
            case MMIX_ANDNH: r &= ~((uint64_t)a << 48); break;
            default: ADD_FAILURE() << "unexpected step"; break;
            }
        }
        EXPECT_EQ(v, r) << std::hex << v;
    }
}

// A constant byte is the Z immediate; the operation and its store.
EXPECT_CODE(AddImmediate,
            R"(subu $254, $254, 16
sto $0, $254, 0
ldo $248, $254, 0
addu $248, $248, 200
sto $248, $254, 8
ldo $0, $254, 8
addu $254, $254, 16
pop 1, 0
)",
            "long f(long a) { a = a + 200; return a; }")

// The signed add is addu: add would only set overflow bits in rA.  The int result is
// stored in its own width, which wraps it.
TEST_F(MmixTest, SignedAddIsAddu)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("int f(int a, int b) { return a + b; }"));
    EXPECT_NE(std::string::npos, code.find(R"(addu $248, $248, $249
sttu $248, )")) << code;
    EXPECT_EQ(std::string::npos, code.find("add $")) << code;
}

// A comparison is cmp and a conditional set; against zero the value decides alone.
TEST_F(MmixTest, Comparisons)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        R"(int f1(long a, long b) { return a < b; }
int f2(unsigned a, unsigned b) { return a >= b; }
int f3(long a) { return a > 7; }
int f4(long a) { return a < 0; }
int f5(unsigned long a) { return a == 0; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(cmp $248, $248, $249
zsn $248, $248, 1
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(cmpu $248, $248, $249
zsnn $248, $248, 1
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(cmp $248, $248, 7
zsp $248, $248, 1
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldo $248, $254, 0
zsn $248, $248, 1
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldo $248, $254, 0
zsz $248, $248, 1
)")) << code;
}

// Signed division: div floors, and the fix-up truncates; the remainder from rR.
EXPECT_CODE(SignedDivide,
            R"(subu $254, $254, 24
sto $0, $254, 0
sto $1, $254, 8
ldo $248, $254, 0
ldo $249, $254, 8
div $250, $248, $249
get $255, rR
xor $248, $248, $249
zsn $248, $248, 1
csz $248, $255, 0
addu $250, $250, $248
sto $250, $254, 16
ldo $0, $254, 16
addu $254, $254, 24
pop 1, 0
)",
            "long f(long a, long b) { return a / b; }")

TEST_F(MmixTest, SignedRemainder)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("long f(long a, long b) { return a % b; }"));
    EXPECT_NE(std::string::npos, code.find(R"(div $250, $248, $249
get $255, rR
xor $248, $248, $249
zsn $248, $248, $249
csz $248, $255, 0
subu $250, $255, $248
)"))
        << code;
}

TEST_F(MmixTest, UnsignedDivide)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("unsigned long f(unsigned long a) { return a % 10; }"));
    EXPECT_NE(std::string::npos, code.find(R"(divu $248, $248, 10
get $248, rR
)")) << code;
}

TEST_F(MmixTest, UnaryOps)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        R"(long f1(long a) { return -a; }
long f2(long a) { return ~a; }
int f3(long a) { return !a; }
)"));
    EXPECT_NE(std::string::npos, code.find("negu $248, 0, $248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("nor $248, $248, 0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("zsz $248, $248, 1\n")) << code;
}

TEST_F(MmixTest, Shifts)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        R"(long f1(long a, int n) { return a >> n; }
unsigned long f2(unsigned long a) { return a >> 3; }
long f3(long a) { return a << 60; }
)"));
    EXPECT_NE(std::string::npos, code.find("sr $248, $248, $249\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sru $248, $248, 3\n")) << code;
    EXPECT_NE(std::string::npos, code.find("slu $248, $248, 60\n")) << code;
}

// A width conversion loads the source from its own width, extended as it says; a
// truncation loads only the low-order bytes, which are the last (big-endian).
TEST_F(MmixTest, WidthConversions)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        R"(long f1(int a) { return a; }
unsigned long f2(unsigned a) { return a; }
int f3(long a) { return (signed char)a; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldt $248, $254, 0
sto $248, )")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldtu $248, $254, 0
sto $248, )")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldb $248, $254, 7
stbu $248, )")) << code;
}

// The expression of `op` on a and b in C, and on the host.
template <typename T>
static void Case(std::ostringstream &src, int &n, const char *type, const char *op, T a, T b,
                 T r)
{
    const char *u = std::is_unsigned<T>::value ? "UL" : "";
    src << "    { volatile " << type << " a = " << +a << u << ", b = " << +b << u << "; if (("
        << type << ")(a " << op << " b) != (" << type << ")" << +r << u << ") return " << ++n
        << "; }\n";
}

// Division and remainder of every sign combination, at 64 and 32 bits, against the
// host's; LONG_MIN / -1, which floors and truncates alike, and the unsigned ones.
TEST_F(MmixTest, RunDivisionTable)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::ostringstream src;
    src << R"(int main(void)
{
)";
    int n = 0;
    for (long a : { 7L, -7L, 6L, -6L, 0L, 1L, -1L, LONG_MAX, LONG_MIN + 1 })
        for (long b : { 2L, -2L, 3L, -3L, 1L, -1L, 7L, LONG_MAX }) {
            Case<long>(src, n, "long", "/", a, b, a / b);
            Case<long>(src, n, "long", "%", a, b, a % b);
        }
    for (int a : { 7, -7, INT_MAX, INT_MIN + 1, -100 })
        for (int b : { 2, -2, 5, -5, -1 }) {
            Case<int>(src, n, "int", "/", a, b, a / b);
            Case<int>(src, n, "int", "%", a, b, a % b);
        }
    for (unsigned long a : { 7UL, ULONG_MAX, 1UL << 63, 12345678901UL })
        for (unsigned long b : { 2UL, 3UL, ULONG_MAX, 1UL << 63 }) {
            Case<unsigned long>(src, n, "unsigned long", "/", a, b, a / b);
            Case<unsigned long>(src, n, "unsigned long", "%", a, b, a % b);
        }
    for (unsigned a : { 7U, UINT_MAX, 1U << 31 })
        for (unsigned b : { 2U, 10U, UINT_MAX }) {
            Case<unsigned>(src, n, "unsigned", "/", a, b, a / b);
            Case<unsigned>(src, n, "unsigned", "%", a, b, a % b);
        }
    src << "    { volatile long a = -9223372036854775807L - 1, b = -1;\n"
           "      if (a / b != a) return 250;\n"
           "      if (a % b != 0) return 251; }\n";
    src << R"(    return 0;
}
)";
    EXPECT_EQ("", CompileAndRunMmix(src.str()));
    EXPECT_EQ(0, exit_status) << "case " << exit_status << " of\n" << src.str();
}

// Narrow results wrap in their own width; shifts, bitwise operations and comparisons at
// every width against the host's.
TEST_F(MmixTest, RunWidths)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::ostringstream src;
    src << R"(int main(void)
{
)";
    int n = 0;
    // Not a signed int that overflows: that is undefined, and like GCC's, our sum is
    // left unextended (RegallocSignedNotReextended).
    Case<unsigned>(src, n, "unsigned", "+", UINT_MAX, 1, 0);
    Case<unsigned>(src, n, "unsigned", "-", 0, 1, UINT_MAX);
    Case<short>(src, n, "short", "+", 32767, 1, (short)-32768);
    Case<unsigned short>(src, n, "unsigned short", "*", 300, 300, (unsigned short)90000);
    Case<signed char>(src, n, "signed char", "+", 127, 1, (signed char)-128);
    Case<unsigned char>(src, n, "unsigned char", "-", 0, 1, 255);
    Case<long>(src, n, "long", ">>", -1024, 3, -128);
    Case<int>(src, n, "int", ">>", -1024, 3, -128);
    Case<unsigned>(src, n, "unsigned", ">>", 0x80000000U, 31, 1);
    Case<unsigned long>(src, n, "unsigned long", ">>", 1UL << 63, 63, 1);
    Case<long>(src, n, "long", "<<", 1, 62, 1L << 62);
    Case<int>(src, n, "int", "<<", 1, 31, INT_MIN);
    Case<long>(src, n, "long", "&", -1, 0x0f0f, 0x0f0f);
    Case<long>(src, n, "long", "|", 0x0f00, 0x00f0, 0x0ff0);
    Case<long>(src, n, "long", "^", -1, 1, -2);
    Case<long>(src, n, "long", "<", -1, 1, 1);
    Case<unsigned long>(src, n, "unsigned long", "<", ULONG_MAX, 1, 0);
    Case<int>(src, n, "int", ">=", INT_MIN, INT_MAX, 0);
    Case<unsigned>(src, n, "unsigned", ">", UINT_MAX, 0, 1);
    Case<long>(src, n, "long", "==", LONG_MIN, LONG_MIN, 1);
    Case<long>(src, n, "long", "!=", 0, 0, 0);
    src << R"(    { volatile long x = 0x123456789abcdefL; volatile int i = (int)x;
      volatile unsigned short s = (unsigned short)x; volatile signed char c = (signed char)x;
      if (i != (int)0x89abcdef) return 200;
      if (s != 0xcdef) return 201;
      if (c != (signed char)0xef) return 202;
      if ((long)c != -17) return 203;
      if ((unsigned long)(unsigned char)c != 0xef) return 204;
      if ((long)(unsigned)i != 0x89abcdefL) return 205;
      if (-x != -0x123456789abcdefL) return 206;
      if (~x != -0x123456789abcdefL - 1) return 207;
      if (!x != 0) return 208; }
)";
    src << R"(    return 0;
}
)";
    EXPECT_EQ("", CompileAndRunMmix(src.str()));
    EXPECT_EQ(0, exit_status) << "case " << exit_status << " of\n" << src.str();
}
