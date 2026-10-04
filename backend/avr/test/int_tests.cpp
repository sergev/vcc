//
// AVR integer operations: golden byte chains, and every operator run on qemu against
// the host's arithmetic.
//
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "avr_test.h"

EXPECT_CODE(AddInt,
            R"(std Y+1, r24
std Y+2, r25
std Y+3, r22
std Y+4, r23
ldd r24, Y+1
ldd r25, Y+2
ldd r22, Y+3
ldd r23, Y+4
add r24, r22
adc r25, r23
std Y+5, r24
std Y+6, r25
ldd r24, Y+5
ldd r25, Y+6
)",
            "int f(int a, int b) { return a + b; }")

// A long: A in r25:r22, B in r21:r18.
EXPECT_CODE(SubLong,
            R"(std Y+1, r22
std Y+2, r23
std Y+3, r24
std Y+4, r25
std Y+5, r18
std Y+6, r19
std Y+7, r20
std Y+8, r21
ldd r22, Y+1
ldd r23, Y+2
ldd r24, Y+3
ldd r25, Y+4
ldd r18, Y+5
ldd r19, Y+6
ldd r20, Y+7
ldd r21, Y+8
sub r22, r18
sbc r23, r19
sbc r24, r20
sbc r25, r21
std Y+9, r22
std Y+10, r23
std Y+11, r24
std Y+12, r25
ldd r22, Y+9
ldd r23, Y+10
ldd r24, Y+11
ldd r25, Y+12
)",
            "long f(long a, long b) { return a - b; }")

// Negation: com on the high bytes, neg on the low one, the borrow up by sbci.
TEST_F(AvrTest, NegateLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(long a) { return -a; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(com r25
com r24
com r23
neg r22
sbci r23, 255
sbci r24, 255
sbci r25, 255
)"))
        << s;
}

// A 16-bit multiply is inline, from three mul, with r1 cleared after.
TEST_F(AvrTest, MultiplyInt)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(int a, int b) { return a * b; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(mul r24, r22
movw r20, r0
mul r24, r23
add r21, r0
mul r25, r22
add r21, r0
clr r1
movw r24, r20
)"))
        << s;
}

// Divide and remainder come out of the helper's special registers: the quotient of
// __divmodhi4 in r23:r22, the remainder in r25:r24.
TEST_F(AvrTest, DivideAndRemainderInt)
{
    NaiveSelection();
    std::string d = Body(CompileToAvr("int f(int a, int b) { return a / b; }"));
    EXPECT_NE(std::string::npos, d.find(R"(call __divmodhi4
std Y+5, r22
std Y+6, r23
)")) << d;
}

TEST_F(AvrTest, RemainderUnsignedLong)
{
    NaiveSelection();
    std::string r = Body(CompileToAvr("unsigned long f(unsigned long a, unsigned long b) "
                                      "{ return a % b; }"));
    EXPECT_NE(std::string::npos,
              r.find(R"(call __udivmodsi4
std Y+9, r22
std Y+10, r23
std Y+11, r24
)"))
        << r;
}

// A comparison: cp/cpc, then 1 or 0 in r24; > swaps the operands of brlt.
TEST_F(AvrTest, CompareGreater)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(int a, int b) { return a > b; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(cp r22, r24
cpc r23, r25
ldi r24, 1
brlt .Lv1
clr r24
std Y+5, r24
std Y+6, r1
)"))
        << s;
}

EXPECT_CODE(CompareUnsignedLessOrEqual,
            R"(std Y+1, r24
std Y+2, r25
std Y+3, r22
std Y+4, r23
ldd r24, Y+1
ldd r25, Y+2
ldd r22, Y+3
ldd r23, Y+4
cp r22, r24
cpc r23, r25
ldi r24, 1
brsh .Lv1
clr r24
std Y+5, r24
std Y+6, r1
ldd r24, Y+5
ldd r25, Y+6
)",
            "int f(unsigned a, unsigned b) { return a <= b; }")

// A shift by a constant: whole bytes moved, then bit by bit.
TEST_F(AvrTest, ShiftLongByConstant)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(long a) { return a >> 9; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(mov r22, r23
mov r23, r24
mov r24, r25
mov r25, r24
lsl r25
sbc r25, r25
asr r25
ror r24
ror r23
ror r22
)"))
        << s;
}

// A shift by a variable: a loop counted down in r26.
TEST_F(AvrTest, ShiftByVariable)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("unsigned f(unsigned a, int n) { return a << n; }"));
    EXPECT_NE(std::string::npos, s.find(R"(ldd r26, Y+3
rjmp .Lv2
lsl r24
rol r25
dec r26
brpl .Lv1
)"))
        << s;
}

// Sign extension: the top byte's sign through C, then copies.
TEST_F(AvrTest, SignExtendIntToLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(int a) { return a; }"));
    EXPECT_NE(std::string::npos, s.find(R"(ldd r22, Y+1
ldd r23, Y+2
mov r24, r23
lsl r24
sbc r24, r24
mov r25, r24
)"))
        << s;
}

// Zero extension copies r1.
TEST_F(AvrTest, ZeroExtendUnsignedToLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("unsigned long f(unsigned a) { return a; }"));
    EXPECT_NE(std::string::npos, s.find(R"(ldd r22, Y+1
ldd r23, Y+2
mov r24, r1
mov r25, r1
)"))
        << s;
}

// A test of zero: cp/cpc against r1.
TEST_F(AvrTest, LogicalNot)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(long a) { return !a; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(cp r22, r1
cpc r23, r1
cpc r24, r1
cpc r25, r1
ldi r24, 1
breq )"))
        << s;
}

//
// Run tests: each case computes on volatile operands and compares with the host's
// result; main returns the number of a case that differs, or 0.  The cases are split
// into groups, as every temporary has a slot of its own.
//
namespace {

// The C type of host type T on AVR.
template <typename T>
std::string CType()
{
    if (std::is_signed<T>::value)
        return sizeof(T) == 8 ? "long long" : sizeof(T) == 4 ? "long" : "int";
    return sizeof(T) == 8 ? "unsigned long long" : sizeof(T) == 4 ? "unsigned long" : "unsigned";
}

// A value of type T as a C constant of its AVR type.
template <typename T>
std::string Literal(T v)
{
    if (std::is_signed<T>::value)
        return "(" + CType<T>() + ")" + std::to_string((long long)v) + "LL";
    return "(" + CType<T>() + ")" + std::to_string((unsigned long long)v) + "ULL";
}

struct Cases {
    std::string body;
    int n = 0;

    template <typename T>
    void add(const std::string &expr, T expected)
    {
        n++;
        body += "    if ((" + expr + ") != " + Literal(expected) + ") bad = " + std::to_string(n) +
                ";\n";
    }
};

enum Group { ARITH, MULDIV, COMPARE, UNARY };

// The operators of `group` on type T over `vals`, as one program.
template <typename T>
std::string IntProgram(Group group, const std::vector<T> &vals)
{
    using U = typename std::make_unsigned<T>::type;
    Cases c;
    std::string decl = "    int bad = 0;\n";
    for (size_t i = 0; i < vals.size(); i++)
        decl += "    volatile " + CType<T>() + " v" + std::to_string(i) + " = " +
                Literal(vals[i]) + ";\n";
    const int bits = 8 * sizeof(T);
    for (size_t i = 0; i < vals.size(); i++) {
        T x           = vals[i];
        std::string a = "v" + std::to_string(i);
        if (group == UNARY) {
            c.add("-" + a, (T)(0 - (U)x));
            c.add("~" + a, (T)~x);
            c.add("(int)!" + a, (int16_t)!x);
            for (int k : { 0, 1, 7, 8, 9, bits / 2 + 3, bits - 1 }) {
                c.add(a + " << " + std::to_string(k), (T)((U)x << k));
                c.add(a + " >> " + std::to_string(k), (T)(x >> k));
                c.add(a + " << (volatile int){" + std::to_string(k) + "}", (T)((U)x << k));
                c.add(a + " >> (volatile int){" + std::to_string(k) + "}", (T)(x >> k));
            }
            continue;
        }
        for (size_t j = 0; j < vals.size(); j++) {
            T y           = vals[j];
            std::string b = "v" + std::to_string(j);
            if (group == ARITH) {
                c.add(a + " + " + b, (T)((U)x + (U)y));
                c.add(a + " - " + b, (T)((U)x - (U)y));
                c.add(a + " & " + b, (T)(x & y));
                c.add(a + " | " + b, (T)(x | y));
                c.add(a + " ^ " + b, (T)(x ^ y));
            } else if (group == MULDIV) {
                c.add(a + " * " + b, (T)((U)x * (U)y));
                bool overflow = std::is_signed<T>::value && y == (T)-1 &&
                                x == std::numeric_limits<T>::min();
                if (y != 0 && !overflow) {
                    c.add(a + " / " + b, (T)(x / y));
                    c.add(a + " % " + b, (T)(x % y));
                }
            } else {
                c.add("(int)(" + a + " < " + b + ")", (int16_t)(x < y));
                c.add("(int)(" + a + " <= " + b + ")", (int16_t)(x <= y));
                c.add("(int)(" + a + " > " + b + ")", (int16_t)(x > y));
                c.add("(int)(" + a + " >= " + b + ")", (int16_t)(x >= y));
                c.add("(int)(" + a + " == " + b + ")", (int16_t)(x == y));
                c.add("(int)(" + a + " != " + b + ")", (int16_t)(x != y));
            }
        }
    }
    return R"(int main(void)
{
)" + decl + c.body + R"(    return bad;
}
)";
}

const std::vector<int16_t> ints       = { 32767, -32768, -1, 7, 300 };
const std::vector<uint16_t> unsigneds = { 65535, 32768, 1, 7, 300 };
const std::vector<int32_t> longs      = { 2147483647, -2147483647 - 1, -1, 100000, -70000 };
const std::vector<uint32_t> ulongs    = { 4294967295u, 2147483648u, 1, 100000, 65536 };
const std::vector<int64_t> llongs     = { INT64_MAX, INT64_MIN, -1, 0x123456789aLL,
                                          -0x100000000LL };
const std::vector<uint64_t> ullongs   = { UINT64_MAX, 1ull << 63, 1, 0x123456789aULL,
                                          0xffffffffULL };

} // namespace

#define INT_RUN_TEST(name, group, vals)                               \
    TEST_F(AvrTest, RunInt##name)                                    \
    {                                                                \
        SKIP_IF_NO_AVR_TOOLS();                                      \
        EXPECT_EQ("0\n", CompileAndRunBook(IntProgram(group, vals))); \
    }

INT_RUN_TEST(Arith, ARITH, ints)
INT_RUN_TEST(MulDiv, MULDIV, ints)
INT_RUN_TEST(Compare, COMPARE, ints)
INT_RUN_TEST(Unary, UNARY, ints)
INT_RUN_TEST(UnsignedArith, ARITH, unsigneds)
INT_RUN_TEST(UnsignedMulDiv, MULDIV, unsigneds)
INT_RUN_TEST(UnsignedCompare, COMPARE, unsigneds)
INT_RUN_TEST(UnsignedUnary, UNARY, unsigneds)
INT_RUN_TEST(LongArith, ARITH, longs)
INT_RUN_TEST(LongMulDiv, MULDIV, longs)
INT_RUN_TEST(LongCompare, COMPARE, longs)
INT_RUN_TEST(LongUnary, UNARY, longs)
INT_RUN_TEST(UnsignedLongArith, ARITH, ulongs)
INT_RUN_TEST(UnsignedLongMulDiv, MULDIV, ulongs)
INT_RUN_TEST(UnsignedLongCompare, COMPARE, ulongs)
INT_RUN_TEST(UnsignedLongUnary, UNARY, ulongs)
// No long long multiply or divide: those helpers come with the C library (M15).
INT_RUN_TEST(LongLongArith, ARITH, llongs)
INT_RUN_TEST(LongLongCompare, COMPARE, llongs)
INT_RUN_TEST(LongLongUnary, UNARY, llongs)
INT_RUN_TEST(UnsignedLongLongArith, ARITH, ullongs)
INT_RUN_TEST(UnsignedLongLongCompare, COMPARE, ullongs)
INT_RUN_TEST(UnsignedLongLongUnary, UNARY, ullongs)

// Width conversions: truncation, sign and zero extension, through volatile operands.
TEST_F(AvrTest, RunIntConversions)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
int main(void)
{
    volatile signed char c = -5;
    volatile unsigned char uc = 250;
    volatile int i = -300;
    volatile unsigned u = 65000;
    volatile long l = -100000;
    volatile unsigned long ul = 4000000000UL;
    volatile long long ll = -5000000000LL;
    if ((long)c != -5L) return 1;
    if ((unsigned long)uc != 250UL) return 2;
    if ((long)i != -300L) return 3;
    if ((long)u != 65000L) return 4;
    if ((long long)l != -100000LL) return 5;
    if ((unsigned long long)ul != 4000000000ULL) return 6;
    if ((int)l != 31072) return 7;
    if ((signed char)i != -44) return 8;
    if ((unsigned char)u != 232) return 9;
    if ((int)ll != 3584) return 10;
    if ((long)ll != -705032704L) return 11;
    if ((unsigned long long)c != 18446744073709551611ULL) return 12;
    if ((int)uc != 250) return 13;
    if ((unsigned)c != 65531u) return 14;
    return 0;
}
)"));
}
