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
            "std Y+1, r24\nstd Y+2, r25\nstd Y+3, r22\nstd Y+4, r23\n"
            "ldd r24, Y+1\nldd r25, Y+2\nldd r22, Y+3\nldd r23, Y+4\n"
            "add r24, r22\nadc r25, r23\nstd Y+5, r24\nstd Y+6, r25\n"
            "ldd r24, Y+5\nldd r25, Y+6\n",
            "int f(int a, int b) { return a + b; }")

// A long: A in r25:r22, B in r21:r18.
EXPECT_CODE(SubLong,
            "std Y+1, r22\nstd Y+2, r23\nstd Y+3, r24\nstd Y+4, r25\n"
            "std Y+5, r18\nstd Y+6, r19\nstd Y+7, r20\nstd Y+8, r21\n"
            "ldd r22, Y+1\nldd r23, Y+2\nldd r24, Y+3\nldd r25, Y+4\n"
            "ldd r18, Y+5\nldd r19, Y+6\nldd r20, Y+7\nldd r21, Y+8\n"
            "sub r22, r18\nsbc r23, r19\nsbc r24, r20\nsbc r25, r21\n"
            "std Y+9, r22\nstd Y+10, r23\nstd Y+11, r24\nstd Y+12, r25\n"
            "ldd r22, Y+9\nldd r23, Y+10\nldd r24, Y+11\nldd r25, Y+12\n",
            "long f(long a, long b) { return a - b; }")

// Negation: com on the high bytes, neg on the low one, the borrow up by sbci.
TEST_F(AvrTest, NegateLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(long a) { return -a; }"));
    EXPECT_NE(std::string::npos,
              s.find("com r25\ncom r24\ncom r23\nneg r22\nsbci r23, 255\nsbci r24, 255\n"
                     "sbci r25, 255\n"))
        << s;
}

// A 16-bit multiply is inline, from three mul, with r1 cleared after.
TEST_F(AvrTest, MultiplyInt)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(int a, int b) { return a * b; }"));
    EXPECT_NE(std::string::npos,
              s.find("mul r24, r22\nmovw r20, r0\nmul r24, r23\nadd r21, r0\nmul r25, r22\n"
                     "add r21, r0\nclr r1\nmovw r24, r20\n"))
        << s;
}

// Divide and remainder come out of the helper's special registers: the quotient of
// __divmodhi4 in r23:r22, the remainder in r25:r24.
TEST_F(AvrTest, DivideAndRemainderInt)
{
    NaiveSelection();
    std::string d = Body(CompileToAvr("int f(int a, int b) { return a / b; }"));
    EXPECT_NE(std::string::npos, d.find("call __divmodhi4\nstd Y+5, r22\nstd Y+6, r23\n")) << d;
}

TEST_F(AvrTest, RemainderUnsignedLong)
{
    NaiveSelection();
    std::string r = Body(CompileToAvr("unsigned long f(unsigned long a, unsigned long b) "
                                      "{ return a % b; }"));
    EXPECT_NE(std::string::npos,
              r.find("call __udivmodsi4\nstd Y+9, r22\nstd Y+10, r23\nstd Y+11, r24\n"))
        << r;
}

// A comparison: cp/cpc, then 1 or 0 in r24; > swaps the operands of brlt.
TEST_F(AvrTest, CompareGreater)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(int a, int b) { return a > b; }"));
    EXPECT_NE(std::string::npos,
              s.find("cp r22, r24\ncpc r23, r25\nldi r24, 1\nbrlt .Lv1\nclr r24\n"
                     "std Y+5, r24\nstd Y+6, r1\n"))
        << s;
}

EXPECT_CODE(CompareUnsignedLessOrEqual,
            "std Y+1, r24\nstd Y+2, r25\nstd Y+3, r22\nstd Y+4, r23\n"
            "ldd r24, Y+1\nldd r25, Y+2\nldd r22, Y+3\nldd r23, Y+4\n"
            "cp r22, r24\ncpc r23, r25\nldi r24, 1\nbrsh .Lv1\nclr r24\n"
            "std Y+5, r24\nstd Y+6, r1\nldd r24, Y+5\nldd r25, Y+6\n",
            "int f(unsigned a, unsigned b) { return a <= b; }")

// A shift by a constant: whole bytes moved, then bit by bit.
TEST_F(AvrTest, ShiftLongByConstant)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(long a) { return a >> 9; }"));
    EXPECT_NE(std::string::npos,
              s.find("mov r22, r23\nmov r23, r24\nmov r24, r25\nmov r25, r24\nlsl r25\n"
                     "sbc r25, r25\nasr r25\nror r24\nror r23\nror r22\n"))
        << s;
}

// A shift by a variable: a loop counted down in r26.
TEST_F(AvrTest, ShiftByVariable)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("unsigned f(unsigned a, int n) { return a << n; }"));
    EXPECT_NE(std::string::npos, s.find("ldd r26, Y+3\nrjmp .Lv2\nlsl r24\nrol r25\n"
                                        "dec r26\nbrpl .Lv1\n"))
        << s;
}

// Sign extension: the top byte's sign through C, then copies.
TEST_F(AvrTest, SignExtendIntToLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long f(int a) { return a; }"));
    EXPECT_NE(std::string::npos, s.find("ldd r22, Y+1\nldd r23, Y+2\nmov r24, r23\nlsl r24\n"
                                        "sbc r24, r24\nmov r25, r24\n"))
        << s;
}

// Zero extension copies r1.
TEST_F(AvrTest, ZeroExtendUnsignedToLong)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("unsigned long f(unsigned a) { return a; }"));
    EXPECT_NE(std::string::npos, s.find("ldd r22, Y+1\nldd r23, Y+2\nmov r24, r1\nmov r25, r1\n"))
        << s;
}

// A test of zero: cp/cpc against r1.
TEST_F(AvrTest, LogicalNot)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(long a) { return !a; }"));
    EXPECT_NE(std::string::npos,
              s.find("cp r22, r1\ncpc r23, r1\ncpc r24, r1\ncpc r25, r1\nldi r24, 1\nbreq "))
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
    return "int main(void)\n{\n" + decl + c.body + "    return bad;\n}\n";
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
