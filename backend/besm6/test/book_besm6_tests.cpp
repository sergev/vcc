//
// The BESM-6 versions of the "Writing a C Compiler" programs whose results depend on
// integer widths or sizes: 41-bit int and long, 48-bit unsigned, 6-byte words.  The
// shared suite (backend/common/test/book/) holds their generic versions, which
// BESM-6 skips.
//
#include "book_test.h"

// Runs the BESM-6 versions without the shared suite's skip list.
class Besm6BookTest : public CodegenTest {};

// Chapter 20 helpers.
static const std::string EX  = "#include <stdlib.h>\n";
static const std::string ID  = "int id(int x) { return x; }\n";
static const std::string DBLID = "double dbl_id(double x) { return x; }\n";
static const std::string UID = "unsigned unsigned_id(unsigned u) { return u; }\n";
static const std::string UCID = "unsigned char uchar_id(unsigned char uc) { return uc; }\n";
static const std::string C1I = R"H(int check_one_int(int actual, int expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1U = R"H(int check_one_uint(unsigned int actual, unsigned int expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1UC = R"H(int check_one_uchar(unsigned char actual, unsigned char expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1L = R"H(int check_one_long(long actual, long expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1UL = R"H(int check_one_ulong(unsigned long actual, unsigned long expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1D = R"H(int check_one_double(double actual, double expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C14D = R"H(int check_14_doubles(double a, double b, double c, double d, double e, double f,
                     double g, double h, double i, double j, double k, double l,
                     double m, double n, double start) {
    double args[14] = {a, b, c, d, e, f, g, h, i, j, k, l, m, n};
    for (int p = 0; p < 14; p++) { if (args[p] != start + p) exit(-1); }
    return 0;
}
)H";

//
// Chapter 11
//

// (40 << 30) == 4.3e10, in the 41-bit long range.
TEST_F(Besm6BookTest, Chapter11_Bitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    long l = 137438953472l; // 2^37
    int shiftcount = 2;

    if (l >> shiftcount != 34359738368l /* 2 ^ 35 */) {
        return 1;
    }
    if (l << shiftcount != 549755813888 /* 2 ^ 39 */) {
        return 2;
    }
    if (l << 2 != 549755813888 /* 2 ^ 39 */) {
        return 3;
    }
    if ((40l << 30) !=  42949672960l) {
        return 4;
    }
    long long_shiftcount = 3l;
    int i_neighbor1 = 0;
    int i = -2147483645; // -2^31 + 3
    int i_neighbor2 = 0;
    // BESM-6 >> is logical (no sign extension), so a negative value's 41-bit
    // pattern shifts in zeros and the result is a large positive number.
    if (i >> long_shiftcount != 274609471488l) {
        return 5;
    }
    i = -1;
    if (i >> 10l != 2147483647) {
        return 6;
    }
    if (i_neighbor1) {
        return 7;
    }
    if (i_neighbor2) {
        return 8;
    }
    return 0;
})"));
}

// Compound assignment to int values, including c *= 10000 with c = -5000000
// (-5e10, which fits the 41-bit int range).  i, b and c arrive as runtime
// arguments so the optimizer cannot constant-fold the whole computation away;
// the multiply therefore runs through the b/mul runtime helper.  (The matching
// compile-time constant fold of -5000000 * 10000 is covered by the optimizer
// unit test optimize/const_fold_tests.cpp.)
TEST_F(Besm6BookTest, Chapter11_CompoundAssignToInt)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int test(int i, int b, int c) {
    i += 2147483648l;
    if (i != 2147483628) {
        return 1;
    }
    if (b != 2147483647) {
        return 2;
    }
    b /= -34359738367l;
    if (b) {
        return 3;
    }
    if (i != 2147483628) {
        return 4;
    }
    if (c != -5000000) {
        return 5;
    }
    c *= 10000l;
    if (c != -50000000000l) {
        return 6;
    }
    return 0;
}

int main(void) {
    return test(-20, 2147483647, -5000000);
})"));
}

// l <<= 23 == 1.04e11, in the 41-bit long range.
TEST_F(Besm6BookTest, Chapter11_CompoundBitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int x = 100;
    x <<= 22l;
    if (x != 419430400) {
        return 1;
    }
    if ((x >>= 4l) != 26214400) {
        return 2;
    }
    if (x != 26214400) {
        return 3;
    }
    long l = 12345l;
    if ((l <<= 23) != 103557365760l) {
        return 4;
    }
    l = -l;
    if ((l >>= 10) != 2046353408l) { // BESM-6 >> is logical: -103557365760 -> 2046353408
        return 5;
    }
    return 0;
})"));
}

// On x86 (int)(2^32+2) == 2; on BESM-6 it is unchanged (no truncation).
TEST_F(Besm6BookTest, Chapter11_ConvertByAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int return_truncated_long(long l) {
    return l;
}

long return_extended_int(int i) {
    return i;
}

int truncate_on_assignment(long l, int expected) {
    int result = l;
    return result == expected;
}

int main(void) {
    long result = return_truncated_long(4294967298l);
    if (result != 4294967298l) {
        return 1;
    }
    result = return_extended_int(-10);
    if (result != -10) {
        return 2;
    }
    int i = 4294967298l;
    if (i != 4294967298l) {
        return 3;
    }
    if (!truncate_on_assignment(17179869184l, 17179869184l)) {
        return 4;
    }
    return 0;
})"));
}

// On x86 the long arguments truncate to int at 32 bits; on BESM-6 int and
// long are both 41-bit, so they pass through unchanged.
TEST_F(Besm6BookTest, Chapter11_ConvertFunctionArguments)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int foo(long a, int b, int c, int d, long e, int f, long g, int h) {
    if (a != -1l)
        return 1;
    if (b != 4294967298l)
        return 2;
    if (c != -4294967296l)
        return 3;
    if (d != 21474836475l)
        return 4;
    if (e != -101l)
        return 5;
    if (f != -123)
        return 6;
    if (g != -10l)
        return 7;
    if (h != 549755813888l)
        return 8;
    return 0;
}

int main(void) {
    int a = -1;
    long int b = 4294967298;
    long c = -4294967296;
    long d = 21474836475;
    int e = -101;
    long f = -123;
    int g = -10;
    long h = 549755813888;
    return foo(a, b, c, d, e, f, g, h);
})"));
}

// On x86 the static int initializer 2^33 truncates to 0; on BESM-6 it fits a
// 41-bit int unchanged.
TEST_F(Besm6BookTest, Chapter11_ConvertStaticInitializer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int i = 8589934592l; // 2^33, fits 41-bit int
long j = 123456;

int main(void) {
    if (i != 8589934592l) {
        return 1;
    }
    if (j != 123456l) {
        return 2;
    }
    return 0;
})"));
}

// On x86 (int) of 2^33 is 0 by truncation; on BESM-6 a 41-bit int holds it
// unchanged, so return_l_as_int returns the full value.
TEST_F(Besm6BookTest, Chapter11_LongGlobalVar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(extern long int l;
long return_l(void);
int return_l_as_int(void);

int main(void) {
    if (return_l() != 8589934592l)
        return 1;
    if (return_l_as_int() != 8589934592l)
        return 2;
    l = l - 10l;
    if (return_l() != 8589934582l)
        return 3;
    if (return_l_as_int() != 8589934582l)
        return 4;
    return 0;
}

long int l = 8589934592l; // 2^33

long return_l(void) {
    return l;
}

int return_l_as_int(void) {
    return l;
})"));
}

// On x86 the case labels 2^33 / ~3.4e10 truncate to 0 / -1; on BESM-6 they are
// distinct in-range 41-bit ints, so each case is reached by its own value.
TEST_F(Besm6BookTest, Chapter11_SwitchInt)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int switch_on_int(int i) {
    switch(i) {
        case 5:
            return 0;
        case 8589934592l: // 2^33
            return 1;
        case 34359738367l: // ~3.4e10
            return 2;
        default:
            return 3;
    }
}

int main(void) {
    if (switch_on_int(5) != 0)
        return 1;
    if (switch_on_int(8589934592l) != 1)
        return 2;
    if (switch_on_int(34359738367l) != 2)
        return 3;
    if (switch_on_int(17179869184) != 3)
        return 4;
    return 0;
})"));
}

// On x86 (int)(2^34+5) == 5; on BESM-6 a 41-bit int holds 2^34+5 unchanged.
TEST_F(Besm6BookTest, Chapter11_Truncate)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int truncate(long l, int expected) {
    int result = (int) l;
    return (result == expected);
}

int main(void)
{
    if (!truncate(10l, 10)) {
        return 1;
    }
    if (!truncate(-10l, -10)) {
        return 2;
    }
    if (!truncate(17179869189l, // 2^34 + 5
                  17179869189l)) {
        return 3;
    }
    if (!truncate(-17179869179l, // (-2^34) + 5
                  -17179869179l)) {
        return 4;
    }
    int i = (int)17179869189l; // 2^34 + 5
    if (i != 17179869189l)
        return 5;
    return 0;
})"));
}

//
// Chapter 12
//

// Unsigned arithmetic wraps at the 48-bit modulus 2^48 == 281474976710656.
// addition: (2^48-3) + 3 == 2^48 wraps to 0; subtraction: 10 - 20 == 2^48-10;
// neg: -1 == 2^48-1.  (The book wrote these around x86 2^32/2^64 wraparound.)
TEST_F(Besm6BookTest, Chapter12_ArithmeticWraparound)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui_a;
unsigned int ui_b;

unsigned long ul_a;
unsigned long ul_b;

int addition(void) {
    return ui_a + ui_b == 0u;
}

int subtraction(void) {
    return (ul_a - ul_b == 281474976710646ul);
}

int neg(void) {
    return -ul_a == 281474976710655ul;
}

int main(void) {
    ui_a = 281474976710653u;
    ui_b = 3u;
    if (!addition()) {
        return 1;
    }

    ul_a = 10ul;
    ul_b = 20ul;
    if (!subtraction()) {
        return 2;
    }

    ul_a = 1ul;
    if (!neg()) {
        return 3;
    }

    return 0;
})"));
}

// ui = -1u is 2^48-1 on BESM-6 (48-bit unsigned); shifts wrap at 48 bits.
TEST_F(Besm6BookTest, Chapter12_BitwiseUnsignedShift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = -1u;  // 2^48 - 1, or 281474976710655

    if ((ui << 2l) != 281474976710652) { // 2^48 - 4
        return 1;
    }

    if ((ui >> 2) != 70368744177663) { // 2^46 - 1
        return 2;
    }

    static int shiftcount = 5;
    if ((1000000u >> shiftcount) != 31250) {
        return 3;
    }

    if ((1000000u << shiftcount) != 32000000) {
        return 4;
    }

    return 0;
})"));
}

// (signed)ui reinterprets the 41-bit pattern: ui carries bit 41, so (signed)ui == -96.
TEST_F(Besm6BookTest, Chapter12_ChainedCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui = 2199023255456u; // 2^41 - 96

int main(void) {

    if ((long) (signed) ui != -96l)
        return 1;

    if ((unsigned long) (signed) ui != 2199023255456ul) // same-size copy: 2^41 - 96
        return 2;

    return 0;
})"));
}

// On BESM-6 signed and unsigned types are the same size, so the common type of
// any signed/unsigned pair is the unsigned one (unlike x86, where long is wider
// than unsigned int). Thus uint vs long compares as unsigned: -100 becomes a
// huge value and 100u is not greater. (-1) read as unsigned is 2^41-1.
TEST_F(Besm6BookTest, Chapter12_CommonType)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int int_gt_uint(int i, unsigned int u) {
    return i > u;
}

int int_gt_ulong(int i, unsigned long ul) {
    return i > ul;
}

int uint_gt_long(unsigned int u, long l) {
    return u > l;
}

int uint_lt_ulong(unsigned int u, unsigned long ul) {
    return u < ul;
}

int long_gt_ulong(long l, unsigned long ul) {
    return l > ul;
}

int ternary_int_uint(int flag, int i, unsigned int ui) {
    long result = flag ? i : ui;
    return (result == -1l); // (uint)(-1) = 2^41-1, read back as long = -1
}

int main(void) {

    if (!int_gt_uint(-100, 100u)) {
        return 1;
    }

    if (!(int_gt_ulong(-1, 1000000ul))) {
        return 2;
    }

    if (uint_gt_long(100u, -100l)) { // unsigned compare: 100 < (unsigned)(-100)
        return 3;
    }

    if (!uint_lt_ulong(1073741824u, 34359738368ul)) {
        return 4;
    }

    if (!long_gt_ulong(-1l, 1000ul)) {
        return 5;
    }

    if (!ternary_int_uint(1, -1, 1u)) {
        return 6;
    }

    return 0;
})"));
}

// x = -1u is 2^48-1; the signed long divisor converts to the common unsigned type as a
// bit-pattern copy (see Chapter12_SameSizeConversion), so -10l becomes 2^41-10.
// (Routed through a parameter so the conversion uses the clean 41-bit long value.)
TEST_F(Besm6BookTest, Chapter12_CompoundAssignUint)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(unsigned int div_assign(unsigned int x, long d) {
    x /= d;
    return x;
}

int main(void) {
    // (2^48 - 1) / (unsigned)(-10l) = (2^48 - 1) / (2^41 - 10) = 128
    return (div_assign(-1u, -10l) == 128u);
})"));
}

// A constant-count signed >> is arithmetic on BESM-6 (so -2 >>= 3u stays -1); the unsigned
// long is 48-bit, so 2^48-1 <<= 44 keeps only the top 4 bits.
TEST_F(Besm6BookTest, Chapter12_CompoundBitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    int i = -2;
    i >>= 3u;
    // BESM-6 signed >> is logical: the 41-bit pattern of -2 (2^41 - 2) >> 3 = 274877906943.
    if (i != 274877906943) {
        return 1;
    }

    unsigned long ul = 281474976710655UL;  // 2^48 - 1
    ul <<= 44;                             // 0 out lower 44 bits
    if (ul != 263882790666240ul) {
        return 2;  // fail
    }
    return 0;  // success
})"));
}

// 48-bit unsigned long operands; ui is set to the 48-bit pattern of l so ui ^= l zeroes it.
TEST_F(Besm6BookTest, Chapter12_CompoundBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    unsigned long ul = 279263001115128ul;
    // On BESM-6 int->unsigned long is a same-size reinterpret: the int -1000 word
    // is 2^41-1000 (bits 42-48 zero), so the AND clears ul's bits 42-48 rather than
    // sign-extending -1000 across the full 48 bits as on a 64-bit target.
    ul &= -1000;
    if (ul != 2186070915096ul) {
        return 1; // fail
    }

    ul |= 4294967040u; // 0xffff_ff00 - zero-extended to unsigned long

    if (ul != 2186138353432ul) {
        return 2; // fail
    }

    int i = 123456;
    // (unsigned)(-252645136) on BESM-6 is the 41-bit pattern 2^41-252645136, so ui
    // must hold that for ui ^= l to cancel to 0.
    unsigned int ui = 2198770610416u;
    long l = -252645136;
    if (ui ^= l) {
        return 3; // fail
    }

    if (ui) {
        return 4; // fail
    }
    if (i != 123456) {
        return 5;
    }
    if (l != -252645136) {
        return 6;
    }

    return 0; // success
})"));
}

// Same-size int<->unsigned conversions are bit-pattern COPYs on BESM-6, so a
// value in [2^40, 2^41) read as int is negative and -1 read as unsigned is
// 2^41-1; a uint below 2^40 stays positive when read as int.
TEST_F(Besm6BookTest, Chapter12_ConvertByAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int check_int(int converted, int expected) {
    return (converted == expected);
}

int check_long(long converted, long expected) {
    return (converted == expected);
}

int check_ulong(unsigned long converted, unsigned long expected) {
    return (converted == expected);
}

long return_extended_uint(unsigned int u) {
    return u;
}

unsigned long return_extended_int(int i) {
    return i;
}

int return_truncated_ulong(unsigned long ul) {
    return ul;
}

int extend_on_assignment(unsigned int ui, long expected) {
    long result = ui; // implicit conversion causes zero-extension
    return result == expected;
}

int main(void) {
    if (!check_int(2199023255547ul, -5)) { // 2^41-5 read as int = -5
        return 1;
    }

    if (!check_long(2147483658u, 2147483658l)) {
        return 2;
    }

    if (!check_ulong(-1, 2199023255551ul)) { // 2^41 - 1
        return 3;
    }

    if (return_extended_uint(2147483658u) != 2147483658l) {
        return 4;
    }

    if (return_extended_int(-1) != 2199023255551UL) { // 2^41 - 1
        return 5;
    }

    long l = return_truncated_ulong(2199023255448ul); // 2^41-104 read as int
    if (l != -104l) {
        return 6;
    }

    if (!extend_on_assignment(2147483658u, 2147483658l)){
        return 7;
    }

    int i = 4294967196u; // 4294967196 < 2^40, stays positive as int
    if (i != 4294967196) {
        return 8;
    }

    return 0;
})"));
}

// (unsigned long)(-10) is a same-size COPY: the 41-bit pattern of -10 read as
// unsigned is 2^41-10 (bits 48-42 stay zero), not 2^64-10.
TEST_F(Besm6BookTest, Chapter12_Extension)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int int_to_ulong(int i, unsigned long expected) {
    unsigned long result = (unsigned long) i;
    return result == expected;
}

int uint_to_long(unsigned int ui, long expected) {
    long result = (long) ui;
    return result == expected;
}

int uint_to_ulong(unsigned ui, unsigned long expected){
    return (unsigned long) ui == expected;
}

int main(void) {
    if (!int_to_ulong(10, 10ul)) {
        return 1;
    }

    if (!int_to_ulong(-10, 2199023255542ul)) { // 2^41 - 10
        return 2;
    }

    if (!uint_to_long(4294967200u, 4294967200l)) {
        return 3;
    }

    if (!uint_to_ulong(4294967200u, 4294967200ul)) {
        return 4;
    }
    if ((unsigned long) 4294967200u != 4294967200ul) {
        return 5;
    }
    return 0;
})"));
}

// a = -a expects a 2^64-range result (18446744065119617024ul).
// a = -a wraps at the 48-bit unsigned modulus (2^48 - a) on BESM-6.
TEST_F(Besm6BookTest, Chapter12_Locals)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned long a = 8589934592ul; // this number is outside the range of int
    int b = -1;
    long c = -8589934592l; // also outside the range of int
    unsigned int d = 10u;

    if (a != 8589934592ul) {
        return 1;
    }
    if (b != -1){
        return 2;
    }
    if (c != -8589934592l) {
        return 3;
    }
    if (d != 10u) {
        return 4;
    }

    a = -a;
    b = b - 1;
    c = c + 8589934594l;
    d = d * 268435456u; // result is between INT_MAX and UINT_MAX

    if (a != 281466386776064ul) {
        return 5;
    }
    if (b != -2) {
        return 6;
    }
    if (c != 2) {
        return 7;
    }
    if (d != 2684354560u) {
        return 8;
    }

    return 0;
})"));
}

// ui++ at UINT_MAX (2^48-1) wraps to 0 on BESM-6's 48-bit unsigned int.
TEST_F(Besm6BookTest, Chapter12_PostfixPrecedence)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = 281474976710655U; // 2^48 - 1

    if (((unsigned long)ui++) != 281474976710655U) {
        return 1; // fail
    }
    if (ui) {
        return 2; // fail - ui should be 0 after update
    }
    return 0; // success
})"));
}

// On BESM-6 unsigned int and signed int are both one word, and a fits in the
// 41-bit signed range, so casting through either type is a bit-pattern-
// preserving no-op: b equals a in both cases.
TEST_F(Besm6BookTest, Chapter12_RoundTripCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned long a = 8589934580ul; // 2^33 - 12

int main(void) {

    unsigned long b = (unsigned long) (unsigned int) a;

    if (b != 8589934580ul)
        return 1;

    b = (unsigned long) (signed int) a;
    if (b != 8589934580ul)
        return 2;

    return 0;
})"));
}

// Signed/unsigned same-size conversions are bit-pattern COPYs: (ulong)(-1000)
// is the 41-bit pattern of -1000 read as unsigned = 2^41-1000.
TEST_F(Besm6BookTest, Chapter12_SameSizeConversion)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int uint_to_int(unsigned int ui, int expected) {
    return (int) ui == expected;
}

int int_to_uint(int i, unsigned int expected) {
    return (unsigned int) i == expected;
}

int ulong_to_long(unsigned long ul, signed long expected) {
    return (signed long) ul == expected;
}

int long_to_ulong(long l, unsigned long expected) {
    return (unsigned long) l == expected;
}

int main(void) {

    if (!int_to_uint(10, 10u)) {
        return 1;
    }

    if (!uint_to_int(10u, 10)) {
        return 2;
    }

    if (!long_to_ulong(-1000l, 2199023254552ul)) { // 2^41 - 1000
        return 3;
    }

    if (!ulong_to_long(2199023254552ul, -1000l)) { // 2^41 - 1000 -> -1000
        return 4;
    }

    return 0;
})"));
}

// Static initializers convert to the variable's type by same-size COPY: a
// ulong in [2^40, 2^41) read as int is negative but read as unsigned stays
// positive; values below 2^40 are unchanged across signed/unsigned.
TEST_F(Besm6BookTest, Chapter12_StaticInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int u = 2147483660l;
int i = 2147483650u;
long l = 2147483660ul; // note: this has type unsigned long
long l2 = 2147483650u;
unsigned long ul = 4294967294u;
unsigned long ul2 = 2147483798l;
int i2 = 1099511629574ul; // 2^40 + 1798, read as int = negative
unsigned ui2 = 1099511629574ul;

int main(void)
{
    if (u != 2147483660u)
        return 1;
    if (i != 2147483650)
        return 2;
    if (l != 2147483660l)
        return 3;
    if (l2 != 2147483650l)
        return 4;
    if (ul != 4294967294ul)
        return 5;
    if (ul2 != 2147483798ul)
        return 6;
    if (i2 != -1099511625978)
        return 7;
    if (ui2 != 1099511629574u)
        return 8;
    return 0;
})"));
}

// --/-- underflow wraparound on BESM-6. An unsigned subtract underflow is true 48-bit
// modular arithmetic (b/usub), so the decrement-from-0 wrap lands at 2^48-1 for both
// unsigned int and unsigned long. (Contrast with reinterpreting a signed -1 as unsigned,
// which keeps the 41-bit pattern 2^41-1; see the conversion tests above.)
TEST_F(Besm6BookTest, Chapter12_UnsignedIncrDecr)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int i = 0;

    if (i-- != 0) {
        return 1;
    }
    if (i != 281474976710655U) { // underflow from 0 -> 2^48 - 1
        return 2;
    }

    if (--i != 281474976710654U) {
        return 3;
    }
    if (i != 281474976710654U) {
        return 4;
    }

    unsigned long l = 0;
    if (l-- != 0) {
        return 5;
    }
    if (l != 281474976710655UL) { // underflow from 0 -> 2^48 - 1
        return 6;
    }
    if (--l != 281474976710654UL) {
        return 7;
    }
    if (l != 281474976710654UL) {
        return 8;
    }
    return 0; // success
})"));
}

//
// Chapter 14
//

// extra_credit/bitshift_dereferenced_ptrs: BESM-6 unsigned int is 48-bit, so
// 4294967295 << 2 does not wrap (== 17179869180).
TEST_F(Besm6BookTest, Chapter14_BitshiftDereferencedPtrs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui = 4294967295;

unsigned int *get_ui_ptr(void){
    return &ui;
}

int shiftcount = 5;

int main(void) {

    if ((*get_ui_ptr() << 2l) != 17179869180) {
        return 1;
    }

    if ((*get_ui_ptr() >> 2) != 1073741823) {
        return 2;
    }

    int *shiftcount_ptr = &shiftcount;
    if ((1000000u >> *shiftcount_ptr) != 31250) {
        return 3;
    }
    if ((1000000u << *shiftcount_ptr) != 32000000) {
        return 4;
    }

    return 0;
})"));
}

// extra_credit/compound_bitwise_dereferenced_ptrs: ul reduced into 48-bit
// range; & with -1000 also clears bits 42-48 (the int's pattern is 41-bit), so
// the results are recomputed. For ui ^= l to cancel, ui must equal the 41-bit
// unsigned image of the negative long l (2^41 - 252645136).
TEST_F(Besm6BookTest, Chapter14_CompoundBitwiseDereferencedPtrs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned long ul = 200000000000000ul;

int main(void) {

    unsigned long *ul_ptr = &ul;
    *ul_ptr &= -1000;
    if (ul != 2087907000320ul) {
        return 1;
    }
    *ul_ptr |= 4294967040u;

    if (ul != 2091649072896ul) {
        return 2;
    }
    int i = 123456;
    unsigned int ui = 2198770610416u; // 2^41 - 252645136
    long l = -252645136;
    unsigned int *ui_ptr = &ui;
    long *l_ptr = &l;
    if (*ui_ptr ^= *l_ptr) {
        return 3;
    }
    if (ui) {
        return 4;
    }

    if (i != 123456) {
        return 5;
    }
    if (l != -252645136) {
        return 6;
    }

    return 0;
})"));
}

// extra_credit/incr_and_decr_through_pointer: an unsigned subtract underflow is true
// 48-bit modular arithmetic (b/usub), so 0ul-- lands at 2^48-1 on BESM-6.
TEST_F(Besm6BookTest, Chapter14_IncrAndDecrThroughPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    int *y = &x;

    if (++*y != 11) {
        return 1;
    }
    if (x != 11) {
        return 2;
    }

    if (--*y != 10) {
        return 3;
    }

    if (x != 10) {
        return 4;
    }

    if ((*y)++ != 10) {
        return 5;
    }

    if (x != 11) {
        return 6;
    }

    if ((*y)-- != 11) {
        return 7;
    }

    if (x != 10) {
        return 8;
    }

    unsigned long ul = 0;
    unsigned long *ul_ptr = &ul;
    if ((*ul_ptr)--) {
        return 9;
    }
    if (ul != 281474976710655UL) { // underflow from 0 -> 2^48 - 1
        return 10;
    }

    double d = 0.0;
    double *d_ptr = &d;
    if (++(*d_ptr) != 1.0) {
        return 11;
    }
    if (d != 1.0) {
        return 12;
    }

    return 0;
})"));
}

//
// Chapter 15
//

// initialization/automatic: out-of-range unsigned/double initializers replaced
// with in-range ones; conversions recomputed for 41/48-bit widths.
TEST_F(Besm6BookTest, Chapter15_Automatic)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test initialzing one-dimensional arrays with automatic storage duration */

/* Initialize array with three constants */
int test_simple(void) {
    unsigned long arr[3] = {281474976710655UL, 140737488355327UL,
                            100ul};

    return (arr[0] == 281474976710655UL &&
            arr[1] == 140737488355327UL && arr[2] == 100ul);
}

/* if an array is partially initialized, any elements that aren't
 * explicitly initialized should be zero.
 */
int test_partial(void) {
    double arr[5] = {1.0, 123e4};

    // make sure first two elements have values from initializer and last three
    // are zero
    return (arr[0] == 1.0 && arr[1] == 123e4 && !arr[2] && !arr[3] && !arr[4]);
}

/* An initializer can include non-constant expressions, including function
 * parameters */
int test_non_constant(long negative_7billion, int *ptr) {
    *ptr = 1;
    extern int three(void);
    long var = negative_7billion * three();  // -21 billion
    long arr[5] = {
        negative_7billion,
        three() * 7l,                      // 21
        -(long)*ptr,                       // -1
        var + (negative_7billion ? 2 : 3)  // -21 billion  + 2
    };  // fifth element  not initialized, should be 0

    return (arr[0] == -7000000000 && arr[1] == 21l && arr[2] == -1l &&
            arr[3] == -20999999998l && arr[4] == 0l);
}

// helper function for test case above
int three(void) {
    return 3;
}

long global_one = 1l;
/* elements in a compound initializer are converted to the right type as if by
 * assignment */
int test_type_conversion(int *ptr) {
    *ptr = -100;

    unsigned long arr[4] = {
        1000000.0,  // convert double to ulong
        *ptr,  // dereference to get int (-100), convert to ulong = 2^41 - 100
        (unsigned int)4294967295U,  // stays in 48-bit unsigned int
        -global_one                 // (unsigned long)(-1) = 2^41 - 1
    };

    return (arr[0] == 1000000ul &&
            arr[1] == 2199023255452ul && arr[2] == 4294967295U &&
            arr[3] == 2199023255551ul);
}

/* Initializing an array must not corrupt other objects on the stack. */
int test_preserve_stack(void) {
    int i = -1;

    /* Initialize with expressions of long type - make sure they're truncated
     * before being copied into the array.
     * Also use an array of < 16 bytes so it's not 16-byte aligned, so there are
     * eightbytes that include both array elements and other values.
     * Also leave last element uninitialized; in assembly, we should set it to
     * zero without overwriting what follows
     */
    int arr[3] = {global_one * 2l, global_one + three()};
    unsigned int u = 2684366905;

    // check surrounding objects
    if (i != -1) {
        return 0;
    }
    if (u != 2684366905) {
        return 0;
    }

    // check arr itself
    return (arr[0] == 2 && arr[1] == 4 && !arr[2]);
}

int main(void) {
    if (!test_simple()) {
        return 1;
    }

    if (!test_partial()) {
        return 2;
    }

    long negative_seven_billion = -7000000000l;
    int i = 0;  // value of i doesn't matter, functions will always overwrite it
    if (!test_non_constant(negative_seven_billion, &i)) {
        return 3;
    }

    if (!test_type_conversion(&i)) {
        return 4;
    }

    if (!test_preserve_stack()) {
        return 5;
    }

    return 0;  // success
})"));
}

// extra_credit/compound_assign_to_subscripted_val: 48-bit unsigned wrap — element [1] is
// set to 2^48-2 so += 2 wraps to 0, and the multiply wraps mod 2^48 (not 13).
TEST_F(Besm6BookTest, Chapter15_CompoundAssignToSubscriptedVal)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test compound assignment where LHS is a subscript expression

unsigned unsigned_arr[4] = {4294967295U, 281474976710654U, 4294967293U, 4294967292U};

int idx = 2;
long long_idx = 1;

int main(void) {
    long_idx = -long_idx; // -1
    // flat array
    unsigned_arr[1] += 2;  // should wrap around to 0
    if (unsigned_arr[1]) {
        return 1;  // fail
    }
    unsigned_arr[idx] -= 10.0;
    if (unsigned_arr[idx] != 4294967283U) {
        return 2;  // fail
    }

    unsigned *unsigned_ptr = unsigned_arr + 4;  // pointer one past end
    unsigned_ptr[long_idx] /= 10;  // pointer to last element, unsigned_arr[3]
    if (unsigned_arr[3] != 429496729U) {
        return 3;  // fail
    }

    // unsigned_arr[2]; 4294967283 * 4294967295 (wraps mod 2^48)
    unsigned_ptr[long_idx *= 2] *= unsigned_arr[0];
    if (unsigned_arr[2] != 281414847168525u) {
        return 4;  // fail
    }

    // unsigned_arr[2 + -2] --> unsigned_arr[0]
    if ((unsigned_arr[idx + long_idx] %= 10) != 5) {
        return 5;  // fail
    }

    // validate other three four elements; make sure updating one didn't
    // accidentally clobber its neighbors
    if (unsigned_arr[0] != 5u) {
        return 6;  // fail
    }

    if (unsigned_arr[1]) {  // should still be 0
        return 7;           // fail
    }

    if (unsigned_arr[2] != 281414847168525u) {
        return 8;  // fail
    }

    if (unsigned_arr[3] != 429496729U) {
        return 9;  // fail
    }

    return 0;
})"));
}

// extra_credit/compound_bitwise_subscript: 48-bit-fitting masks substituted for
// the 2^63 / 0xffffffff00000000 patterns; results recomputed (<<= wraps mod 2^48).
TEST_F(Besm6BookTest, Chapter15_CompoundBitwiseSubscript)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// compound bitwise assignment on subscript expressions
int main(void) {
    unsigned long arr[4] = {
        4294967296ul,               // 2^32
        281474959933440ul,          // 0xffffff_000000
        140737488355328ul,          // 2^47
        16557351571215ul            // 0x0f0f_0f0f_0f0f
    };

    // &=
    arr[1] &= arr[3];
    if (arr[1] != 16557350584320ul /* 0x0f0f0f_000000 */) {
        return 1;
    }

    // |=
    arr[0] |= arr[1];
    if (arr[0] != 16557350584320ul) {
        return 2;
    }

    // ^=
    arr[2] ^= arr[3];
    if (arr[2] != 157294839926543ul) {
        return 3;
    }

    // >>=
    arr[3] >>= 25;
    if (arr[3] != 493447ul) {
        return 4;
    }

    // <<=
    arr[1] <<= 12;
    if (arr[1] != 264913582817280ul) {
        return 5;
    }

    return 0; // success
})"));
}

// extra_credit/compound_pointer_assignment: the 2^63 longs (whose difference is
// 1) are replaced with in-range longs; the `4294967295U + i` that wrapped to 3
// at 32 bits uses the 48-bit UINT_MAX so it still wraps to 3.
TEST_F(Besm6BookTest, Chapter15_CompoundPointerAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Pointer arithmetic with +=/-=

int i = 4;

int int_array(void) {
    int arr[6] = {1, 2, 3, 4, 5, 6};
    int *ptr = arr;

    // basic +=
    if (*(ptr += 5) != 6) {
        return 1; // fail
    }
    if (ptr[0] != 6) {
         return 2; // fail
    }

    if (ptr != arr + 5) {
        return 3;
    }

    // basic -=
    if (*(ptr -=3) != 3) {
        return 4; // fail
    }
    if (ptr[0] != 3) {
        return 5;
    }
    if (ptr != arr + 2) {
        return 6;
    }

    // += w/ more complex rval
    if ((ptr += i - 1) != arr + 5) {
        return 7;
    }

    if (*ptr != 6) {
        return 8;
    }

    // with rval of different types
    // here, rval is unsigned and wraps around
    if ((ptr -= (281474976710655U + i)) != arr + 2) {
        return 9;
    }

    if (*ptr != 3) {
        return 10;
    }

    long l = 1099511627775l;
    if ((ptr += l - 1099511627774l) != arr + 3) {
        return 11;
    }

    if (*ptr != 4) {
        return 12;
    }

    return 0; // success
}

int double_array(void) {
    // identical to int_array but with static double array instead
    static double arr[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double *ptr = arr;

    // basic +=
    if (*(ptr += 5) != 6) {
        return 1; // fail
    }
    if (ptr[0] != 6) {
         return 2; // fail
    }

    if (ptr != arr + 5) {
        return 3;
    }

    // basic -=
    if (*(ptr -=3) != 3) {
        return 4; // fail
    }
    if (ptr[0] != 3) {
        return 5;
    }
    if (ptr != arr + 2) {
        return 6;
    }

    // += w/ more complex rval
    if ((ptr += i - 1) != arr + 5) {
        return 7;
    }

    if (*ptr != 6) {
        return 8;
    }

    // with rval of different types
    // here, rval is unsigned and wraps around
    if ((ptr -= (281474976710655U + i)) != arr + 2) {
        return 9;
    }

    if (*ptr != 3) {
        return 10;
    }

    long l = 1099511627775l;
    if ((ptr += l - 1099511627774l) != arr + 3) {
        return 11;
    }

    if (*ptr != 4) {
        return 12;
    }

    return 0;
}

int main(void) {
    int result;

    if ((result = int_array())) {
        return result; // int_array returned non-zero result - fail
    }
    if ((result = double_array())) {
        return result + 12; // double_array returned non-zero result - fail
    }
    return 0; // success
})"));
}

// casts/implicit_and_explicit_conversions: reading the long elements -1 and -4
// through an unsigned long lvalue yields their 41-bit patterns (2^41-1, 2^41-4).
TEST_F(Besm6BookTest, Chapter15_ImplicitAndExplicitConversions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly track both implicit type conversions via array decay
 * and explicit casts
 */


int main(void) {
    long arr[4] = {-1,-2,-3,-4};

    // (long *) cast here is a no-op, since arr already decays to a pointer to its first element
    if (arr != (long *) arr) {
        return 1;
    }

    // taking address with & and explicitly converting to pointer to array
    // both result in address of arr with same type
    if ((long (*)[4]) arr != &arr) {
        return 2;
    }

    // reinterpret arr as an array of unsigned longs
    // NOTE: effective type rules usually don't let you read an object
    // with an lvalue of different type, but reading signed integer thru
    // corresponding unsigned type, and vice versa, is okay.
    unsigned long *unsigned_arr = (unsigned long *)arr;
    if (unsigned_arr[0] != 2199023255551UL) { // (unsigned long)(-1) = 2^41-1
        return 3;
    }

    if (unsigned_arr[3] != 2199023255548UL) { // (unsigned long)(-4) = 2^41-4
        return 4;
    }

    return 0;
})"));
}

//
// Chapter 16
//

// chars/access_through_char_pointer (adapted for BESM-6): an int occupies one
// 48-bit word = 6 bytes in big-endian order (byte #0 = MSB, byte #5 = LSB), so
// reading it through a char* inspects those six bytes rather than x86's four.
TEST_F(Besm6BookTest, Chapter16_AccessThroughCharPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can read an object through a pointer to a character type */

int main(void) {

    /* Inspect the six big-endian bytes of an int held in one 48-bit word:
     * byte #0 is the most significant byte, byte #5 the least significant. */
    int x = 100;
    char *byte_ptr = (char *) &x;

    /* the value lives in the low byte; the five higher bytes are zero */
    if (byte_ptr[5] != 100) {
        return 1;
    }

    if (byte_ptr[0] || byte_ptr[1] || byte_ptr[2] || byte_ptr[3] || byte_ptr[4]) {
        return 2;
    }

    /* a value spanning two bytes demonstrates big-endian ordering in the word */
    int y = 0x0102; /* 258 */
    byte_ptr = (char *) &y;
    if (byte_ptr[5] != 2) {
        return 3;
    }

    if (byte_ptr[4] != 1) {
        return 4;
    }

    return 0;
})"));
}

// extra_credit/bitshift_chars: BESM-6 right shift of a negative value is logical
// (zero-fill of the 41-bit pattern), so the negative cases yield large positives.
//
// KNOWN FAILURE (return 5, pre-existing, orthogonal to plain-char signedness — task #31):
// `-(uc << 5u) >> 5u` with `unsigned char uc` const-folds to a *uint* (48-bit) value because
// the optimizer represents `(int)(unsigned char)` via ZERO_EXTEND→uint, so the unary minus
// wraps at 2^48 instead of int's 2^41 (yields 8796093021953, not 68719476481).  The integer
// promotion of `unsigned char` is to `int`, not `unsigned int` (the test's own comment), so the
// fix belongs in const-fold's ZERO_EXTEND result-kind handling, not here.
TEST_F(Besm6BookTest, Chapter16_BitshiftChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test << and >> operators with chars (or mix of chars and other types)

int main(void) {
    unsigned char uc = 255;

    // uc is promoted to int, then shifted
    if ((uc >> 3) != 31) {
        return 2; // fail
    }

    signed char sc = -127;
    signed char c = 5; // plain char unsigned on BESM-6; keep signed for the shift-promotion check
    // sc is promoted to int, then shifted (logical: (2^41 - 127) >> 5)
    if ((sc >> c) != 68719476732) {
        return 3;  // fail
    }

    // make sure c << 3ul is promoted to int, not unsigned long (logical: (2^41 - 40) >> 3)
    if (((-(c << 3ul)) >> 3) != 274877906939) {
        return 4;  // fail
    }

    // make sure uc << 5u is promoted to int, not unsigned int (logical: (2^41 - 8160) >> 5)
    if ((-(uc << 5u) >> 5u) != 68719476481l) {
        return 5; // fail
    }

    return 0;
})"));
}

// extra_credit/bitwise_ops_chars: the 48-bit unsigned analogue of 2^32-659 is 2^48-659.
TEST_F(Besm6BookTest, Chapter16_BitwiseOpsChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// make sure we perform integer promotions when performing bitwise operations on chars

int main(void) {
    unsigned char uc = 135;
    signed char c = -116; // plain char is unsigned on BESM-6; keep the negative value signed
    if ((uc & c) != 132) {
        return 1;  // fail
    }

    if ((uc | c) != -113) {
        return 2;  // fail
    }

    if (((c ^ 1001u) | 360l) != 281474976709997) { // 2^48 - 659
        return 3; // fail
    }

    return 0;
})"));
}

// chars/common_type: the ternary's unsigned-int common type, narrowed to the long
// return, wraps back to -10 on BESM-6 (41-bit long). char_lt_int/char_lt_uchar are
// renamed c_lt_int/c_lt_uchar so they stay distinct within Madlen's 8-char limit.
TEST_F(Besm6BookTest, Chapter16_CommonType)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly find the common type of character types and other
 * types (it's always the other type - or, if both are character types, it's int) */

long ternary(int flag, signed char c) { // plain char unsigned on BESM-6; keep c signed
    return flag ? c : 1u;
}

int c_lt_int(char c, int i) {
    return c < i;
}

int uchar_gt_long(unsigned char uc, long l) {
    return uc > l;
}

int c_lt_uchar(signed char c, unsigned char u) { // plain char unsigned on BESM-6; keep c signed
    return c < u;
}

int signed_char_le_char(signed char s, char c) {
    return s <= c;
}

char ten = 10;
int multiply(void) {
    char i = 10.75 * ten;
    return i == 107;
}

int main(void) {
    if (ternary(1, -10) != -10l) {
        return 1;
    }

    if (!c_lt_int((char)1, 256)) {
        return 2;
    }

    if (!uchar_gt_long((unsigned char)100, -2)) {
        return 3;
    }

    signed char c = -1; // plain char unsigned on BESM-6; keep c signed
    unsigned char u = 2;
    if (!c_lt_uchar(c, u)) {
        return 4;
    }

    signed char s = -1;
    if (!signed_char_le_char(s, c)) {
        return 5;
    }

    if (!multiply()) {
        return 6;
    }

    return 0;
})"));
}

// chars/convert_by_assignment: out-of-range source values are replaced with
// in-range ones that keep the same low byte (the task #11 "value parts").  Helpers
// whose names collide in Madlen's 8-char limit were renamed: `check_char_on_stack`
// (vs `check_char` → `check_ch`) → `check_stk`, and the `return_extended_uchar` /
// `return_extended_schar` / `return_truncated_long` trio (the first two both →
// `return_e`, and even `ret_ext_uc`/`ret_ext_sc` still share `ret_ext_`) → the
// 8-char-distinct `rxt_uc`/`rxt_sc`/`rtrunc`.  The `check_uint` expectation uses the
// BESM-6 41-bit unsigned value (2^41-10) instead of x86's 2^32-10 (task #14).  Plain
// char is unsigned on BESM-6, so the negative-valued `array` is declared `signed char`.
TEST_F(Besm6BookTest, Chapter16_ConvertByAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test implicit conversions to and from character types as if by assignment. */

int check_int(int converted, int expected) { return (converted == expected); }
int check_uint(unsigned int converted, unsigned int expected) { return (converted == expected); }
int check_long(long converted, long expected) { return (converted == expected); }
int check_ulong(unsigned long converted, unsigned long expected) { return (converted == expected); }
int check_double(double converted, double expected) { return (converted == expected); }
int check_char(char converted, char expected) { return (converted == expected); }
int check_uchar(unsigned char converted, unsigned char expected) { return (converted == expected); }
int check_stk(signed char expected, int dummy1, int dummy2, int dummy3,
                        int dummy4, int dummy5, int dummy6, signed char converted) {
    return converted == expected;
}

int rxt_uc(unsigned char c) { return c; }
unsigned long rxt_sc(signed char sc) { return sc; }
unsigned char rtrunc(long l) { return l; }

int main(void) {
    signed char sc = -10;
    if (!check_long(sc, -10l)) return 1;
    if (!check_uint(sc, 2199023255542u)) return 2; // (unsigned int)(-10) = 2^41-10 on BESM-6
    if (!check_double(sc, -10.0)) return 3;

    unsigned char uc = 246;
    if (!check_uchar(sc, uc)) return 4;

    char c = -10;
    if (!check_char(-10, c)) return 5;
    if (!check_char(4294967286u, c)) return 6;
    // if (!check_char(-10.0, c)) return 7; -- undefined behavior on ARM
    if (!check_stk(c, 0, 0, 0, 0, 0, 0, -10.0)) return 8;

    if (!check_int(uc, 246)) return 9;
    if (!check_ulong(uc, 246ul)) return 10;
    char expected_char = -10;
    if (!check_char(uc, expected_char)) return 11;

    if (!check_uchar(281474976710646ul, uc)) return 12; // low byte 246

    if (rxt_uc(uc) != 246) return 13;
    if (rxt_sc(sc) != 2199023255542ul) return 14; // (ulong)(-10)=2^41-10
    if (rtrunc(5369233654l) != uc) return 15;

    signed char array[3] = {0, 0, 0}; // plain char unsigned on BESM-6; keep signed wrap
    array[1] = 128;
    if (array[0] || array[2] || array[1] != -128) return 16;
    array[1] = 281474976710530ul; // low byte 130 (was 9.2e18)
    if (array[0] || array[2] || array[1] != -126) return 17;
    array[1] = -2.6;
    if (array[0] || array[2] || array[1] != -2) return 18;

    unsigned char uchar_array[3] = {0, 0, 0};
    uchar_array[1] = 1099511627520l; // low byte 0 (was 2^44)
    if (uchar_array[0] || uchar_array[2] || uchar_array[1] != 0) return 19;
    uchar_array[1] = 2147483898u;
    if (uchar_array[0] || uchar_array[2] || uchar_array[1] != 250) return 20;

    unsigned int ui = 4294967295U;
    static unsigned char uc_static;
    ui = uc_static;
    if (ui) return 21;

    signed long l = -1;
    static signed s_static = 0;
    l = s_static;
    if (l) return 22;

    return 0;
})"));
}

// chars/explicit_casts: the static-local + 8-char-collision + 41-bit-value parts are
// adapted here (helpers renamed to short forms like `c2uc`/`sc2ui` because the book names
// all collided in Madlen's 8-char limit; `sc2ui` uses the BESM-6 41-bit 2^41-10 value,
// task #14; the `static long *null_ptr` cast works).  The `(double)(unsigned char)` bit-7
// conversion bug (task #30) is fixed: a sub-word integer source is now promoted to a full
// word before the int->FP conversion.
TEST_F(Besm6BookTest, Chapter16_ExplicitCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test explicit conversions to and from character types */

unsigned char c2uc(char c) { return (unsigned char)c; }
signed char c2sc(char c) { return (signed char)c; }
signed char uc2c(unsigned char u) { return (signed char)u; } // plain char unsigned on BESM-6
signed char uc2sc(unsigned char u) { return (signed char)u; }
unsigned char sc2uc(signed char u) { return (unsigned char)u; }
int c2i(char c) { return (int)c; }
unsigned long c2ul(char c) { return (unsigned long)c; }
long sc2l(signed char s) { return (long)s; }
unsigned int sc2ui(signed char s) { return (unsigned int)s; }
double sc2d(signed char s) { return (double)s; }
int uc2i(unsigned char u) { return (int)u; }
unsigned int uc2ui(unsigned char u) { return (unsigned int)u; }
long uc2l(unsigned char u) { return (long)u; }
unsigned long uc2ul(unsigned char u) { return (unsigned long)u; }
double uc2d(unsigned char u) { return (double)u; }
char i2c(int i) { return (char)i; }
char ui2c(unsigned int u) { return (char)u; }
signed char d2c(double d) { return (signed char)d; } // plain char unsigned on BESM-6: (char)negative-double is UB
signed char ul2sc(unsigned long l) { return (signed char)l; }
unsigned char i2uc(int i) { return (unsigned char)i; }
unsigned char ui2uc(unsigned int ui) { return (unsigned char)ui; }
unsigned char l2uc(long l) { return (unsigned char)l; }
unsigned char ul2uc(unsigned long ul) { return (unsigned char)ul; }
unsigned char d2uc(double d) { return (unsigned char)d; }
signed char l2sc(long l) { return (signed char)l; }

int main(void) {
    char c = 127;
    if (c2uc(c) != 127) return 1;
    if (c2i(c) != 127) return 2;
    if (c2ul(c) != 127) return 3;

    signed char sc = -10;
    if (sc2uc(sc) != 246) return 4;
    if (sc2l(sc) != -10) return 5;
    if (sc2ui(sc) != 2199023255542u) return 6; // (unsigned int)(-10) = 2^41-10 on BESM-6
    if (sc2d(sc) != -10.0) return 7;

    unsigned char uc = 250;
    if (uc2i(uc) != 250) return 8;
    if (uc2l(uc) != 250) return 9;
    if (uc2ui(uc) != 250) return 10;
    if (uc2ul(uc) != 250) return 11;
    if (uc2d(uc) != 250.0) return 12;
    if (uc2sc(uc) != -6) return 13;
    if (uc2c(uc) != -6) return 14;

    c = (char)-128;
    if (i2c(128) != c) return 15;
    c = (char)-6;
    if (ui2c(2147483898u) != c) return 16;
    if (d2c(-2.6) != -2) return 17; // d2c returns signed char on BESM-6

    if (l2sc(1099511627520l)) return 18; // low byte 0
    sc = (signed char)-126;
    if (ul2sc(281474976710530ul) != sc) return 19; // low byte 130

    uc = (unsigned char)200;
    if (i2uc(-1234488) != uc) return 20;
    if (ui2uc(4293732808) != uc) return 21;
    if (l2uc(1099511627720l) != uc) return 22; // low byte 200
    if (ul2uc(281474976710600ul) != uc) return 23; // low byte 200
    if (d2uc(200.99) != uc) return 24;

    static long *null_ptr;
    char zero = (char)null_ptr;
    if (zero) return 25;

    c = 32;
    int *i = (int *)c;
    if ((char)i != c) return 26;

    if ((char)300 != (char)44) return 27;

    return 0;
})"));
}

//
// Chapter 17
//

// sizeof/sizeof_array: arrays keep their type under sizeof (no decay), array
// parameters are adjusted to pointers, and sizeof of a string literal is its
// decoded byte length incl. NUL (sizeof "Hello, World!" == 14).
TEST_F(Besm6BookTest, Chapter17_SizeofArray)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that arrays don't decay to pointers
 * when they're the operands of sizeof expression */

unsigned long sizeof_adjusted_param(int arr[3]) {
    // this should return the size of arr's _adjusted_ type,
    // so it should return the size of a pointer (6) instead of 18
    return sizeof arr;
}

int main(void) {
    // flat array
    int arr[3];
    if (sizeof arr != 18) {
        return 1;
    }

    long nested_arr[4][5];

    // arr[2] has type long[5], so its size is 6 * 5 = 30
    if (sizeof nested_arr[2] != 30) {
        return 2;
    }

    // string literals also don't decay to pointers in sizeof expressions
    if (sizeof "Hello, World!" != 14) {
        return 3;
    }

    // parameters declared with array type are adjusted to pointers,
    // and sizeof reflects this
    if (sizeof_adjusted_param(arr) != 6) {
        return 4;
    }

    return 0;
})"));
}

// sizeof/sizeof_basic_types: size of all basic types (char==1, word types==6).
TEST_F(Besm6BookTest, Chapter17_SizeofBasicTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we can get the size of all basic type */

int main(void) {
    if (sizeof(char) != 1) {
        return 1;
    }

    if (sizeof(signed char) != 1) {
        return 2;
    }

    if (sizeof(unsigned char) != 1) {
        return 3;
    }

    if (sizeof(int) != 6) {
        return 4;
    }
    if (sizeof(unsigned int) != 6) {
        return 5;
    }

    if (sizeof(long) != 6) {
        return 6;
    }
    if (sizeof(unsigned long) != 6) {
        return 7;
    }

    if (sizeof(double) != 6) {
        return 8;
    }

    return 0;
})"));
}

// extra_credit/sizeof_bitwise: size of bitwise/bitshift expressions (common
// type / promoted left operand; all word types are 6 here).
TEST_F(Besm6BookTest, Chapter17_SizeofBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of bitwise and bitshift expression
int main(void) {
    long l = 0;
    int i = 0;
    char c = 0;

    // result type for &, |, ^ is common type
    if (sizeof (c & i) != 6) {
        return 1;  // fail
    }

    if (sizeof (i | l) != 6) {
        return 2;  // fail
    }

    // character operands are promoted
    if (sizeof (c ^ c) != 6) {
        return 3;  // fail
    }

    // result type for <<, >> is type of left operand
    if (sizeof (i << l) != 6) {
        return 4; // fail
    }

    // character operands are promoted
    if (sizeof (c << i) != 6) {
        return 5; // fail
    }

    if (sizeof (l >> c) != 6) {
        return 6; // fail
    }

    return 0;
})"));
}

// extra_credit/sizeof_compound: size of compound-assignment expressions, which
// are not evaluated (the type of the left operand; uc %= 2 stays char size 1).
TEST_F(Besm6BookTest, Chapter17_SizeofCompound)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get size of compound expressions (and don't evaluate
// them)

int main(void) {
    long long_arr[2] = {1, 2};
    int i = 3;
    unsigned char uc = 4;
    double d = 5.0;
    long *ptr = long_arr;

    if (sizeof(long_arr[1] *= 10) != 6) {
        return 1;  // fail
    }
    if (sizeof(i /= 10ul) != 6) {
        return 2;  // fail
    }
    if (sizeof(uc %= 2) != 1) {
        return 3;  // fail
    }
    if (sizeof(d -= 11) != 6) {
        return 4;  // fail
    }
    if (sizeof(ptr += 1) != 6) {
        return 5;  // fail
    }

    // make sure we didn't actually evaluate any sizeof operands
    if (long_arr[0] != 1) {
        return 6;  // fail
    }
    if (long_arr[1] != 2) {
        return 7;  // fail
    }
    if (i != 3) {
        return 8;  // fail
    }
    if (uc != 4) {
        return 9;  // fail
    }
    if (d != 5.0) {
        return 10;  // fail
    }
    if (ptr != long_arr) {
        return 11;  // fail
    }

    return 0;  // success
})"));
}

// extra_credit/sizeof_compound_bitwise: size of compound bitwise expressions
// (not evaluated; left-operand type, signed-char results stay 1).
TEST_F(Besm6BookTest, Chapter17_SizeofCompoundBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of compound bitwise operations
// (and don't evaluate them)

int main(void) {
    signed char sc = 10;
    unsigned int u = 10000u;
    long l = -99999;

    if (sizeof(sc &= l) != 1) {
        return 1;  // fail
    }

    if (sizeof(l |= u) != 6) {
        return 2;  // fail
    }

    if (sizeof(u ^= l) != 6) {
        return 3;  // fail
    }
    if (sizeof(l >>= sc) != 6) {
        return 4;
    }
    if (sizeof(sc <<= sc) != 1) {
        return 5;
    }

    // make sure we didn't perform updates
    if (sc != 10) {
        return 6;  // fail
    }
    if (u != 10000u) {
        return 7;  // fail
    }
    if (l != -99999) {
        return 8;  // fail
    }

    return 0;
})"));
}

// sizeof/sizeof_consts: the type, and size, of all constants (char const has
// int type; word types are 6 bytes).
TEST_F(Besm6BookTest, Chapter17_SizeofConsts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly determine the type, and size, of all constants */

int main(void) {
    // test that character constants have integer type, not character type;
    // we couldn't test this in the previous chapter
    if (sizeof 'a' != 6) {
        return 1;
    }

    // int
    if (sizeof 2147483647 != 6) {
        return 2;
    }

    // unsigned int
    if (sizeof 4294967295U != 6) {
        return 3;
    }

    // long
    if (sizeof 2l != 6) {
        return 4;
    }

    // unsigned long
    if (sizeof 0ul != 6) {
        return 5;
    }

    // double
    if (sizeof 1.0 != 6) {
        return 6;
    }
    return 0;
})"));
}

// sizeof/sizeof_derived_types: sizes of derived (pointer and array) types,
// including the nested abstract declarator double(*([3][4]))[2].
TEST_F(Besm6BookTest, Chapter17_SizeofDerivedTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we accurately calculate the size of derived (pointer and array)
 * types */

int main(void) {
    // start with a simple array type
    // 2 * 6 = 12; sizeof int is 6
    if (sizeof(int[2]) != 12) {
        return 1;
    }

    // try a nested array type
    // 3 * 6 * 17 * 9 == 2754; sizeof char is 1
    if (sizeof(char[3][6][17][9]) != 2754) {
        return 2;
    }

    // now try some pointer types; these are always 6 bytes (one word) no matter
    // what they point to
    if (sizeof(int *) != 6) {
        return 4;
    }

    if (sizeof(int(*)[2][4][6]) !=
        6) {  // pointer to a big array is still a pointer
        return 5;
    }

    if (sizeof(char *) != 6) {
        return 6;
    }

    // array of pointers
    // this is an array of three arrays of four pointers; 3 * 4 * 6 = 72
    // each pointer points to element type "array of four doubles"
    // but that doesn't impact the size of this type
    if (sizeof(double(*([3][4]))[2]) != 72) {
        return 7;
    }

    return 0;
})"));
}

// BESM-6: static buffer instead of malloc; sizeof checks use BESM-6 word sizes.
TEST_F(Besm6BookTest, Chapter17_SizeofExpressions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly get the size of a range of expressions */

int main(void) {
    double d;

    if (sizeof d != 6) {
        return 2;
    }

    unsigned char c;

    if (sizeof c != 1) {
        return 3;
    }

    static char sbuf[100];
    void *buffer = sbuf;

    if (sizeof(buffer) != 6) {
        return 4;
    }

    if (sizeof ((int)d) != 6) {
        return 5;
    }

    if (sizeof (d ? c : 10l) != 6) {
        return 6;
    }

    if (sizeof (c = 10.0) != 1) {
        return 7;
    }

    return 0;
})"));
}

// libraries/sizeof_extern, shrunk to fit BESM-6 core: the book's double[1000][2000]
// is 12M words (core is only 32K), so use double[10][20] == 200 words; sizeof is
// 200 elements * 6 bytes/word == 1200.
TEST_F(Besm6BookTest, Chapter17_SizeofExtern)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(double large_array[10][20];

int main(void) {
    return sizeof large_array == 1200;
})"));
}

// extra_credit/sizeof_incr: size of ++/-- expressions (not evaluated; operand
// type, char results stay 1).  `static` dropped on arr.
TEST_F(Besm6BookTest, Chapter17_SizeofIncr)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of ++ and -- expressions (and don't evaluate them)

int main(void) {
    int i = 0;
    long l = 0;
    char arr[3] = {0, 0, 0};
    char *ptr = arr;
    if (sizeof (i++) != 6) {
        return 1; // fail
    }

    if (sizeof (arr[0]--) != 1) {
        return 2; // fail
    }


    if (sizeof (++l) != 6) {
        return 3; // fail
    }

    if (sizeof (--arr[1]) != 1) {
        return 4; // fail
    }

    if (sizeof (ptr--) != 6) {
        return 5;
    }

    // make sure we didn't actually increment/decrement anything

    if (i) {
        return 6; // fail
    }

    if (l) {
        return 7; // fail
    }

    if (arr[0] || arr[1] || arr[2]) {
        return 8; // fail
    }

    if (ptr != arr) {
        return 9; // fail
    }

    return 0; // success
})"));
}

// sizeof/sizeof_not_evaluated: sizeof does not evaluate its operand (foo, which
// would call exit, is never run).  sizeof(int) == 6 on BESM-6.
TEST_F(Besm6BookTest, Chapter17_SizeofNotEvaluated)
{
    EXPECT_EQ("6\n", CompileAndRunBook(R"(#include <stdlib.h>
int foo(void) { exit(10); }

int main(void) {
  // make sure foo isn't actually called
  return sizeof(foo());
})"));
}

// sizeof/sizeof_result_is_ulong: sizeof yields an unsigned long (size 6 here);
// second check exercises its unsignedness, independent of the size value.
TEST_F(Besm6BookTest, Chapter17_SizeofResultIsUlong)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that sizeof expression results in an unsigned long */

int main(void) {

    // sizeof result is a ulong, so _its_ size is 6 (one word)
    if (sizeof sizeof (char) != 6) {
        return 1;
    }

    // make sure sizeof result is unsigned
    // since the common type of ulong and int is ulong,
    // the result of subtraction here will be positive unsigned int
    // (and 0 in comparison will also be converted to 0u)
    if (sizeof 4 - sizeof 4 - 1 < 0) {
        return 2;
    }

    return 0;
})"));
}

// sizeof/simple: two forms of sizeof (type names and expressions).
TEST_F(Besm6BookTest, Chapter17_SizeofSimple)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Basic test of two forms of sizeof: referring to type names and expressions */

int main(void) {
    if (sizeof (int) != 6) {
        return 1;
    }

    if (sizeof 3.0 != 6) {
        return 2;
    }

    return 0;
})"));
}

//
// Chapter 18
//

// BESM-6: helper names shortened to stay distinct within 8 chars; the pointed-to union
// uses a local object instead of malloc; punned bytes read big-endian (byte #0 = MSB);
// the 64-bit long is brought into the 41-bit range and strcmp strings are UPPERCASE so the
// automatic (ASCII) char data matches the KOI-7-repacked string constants.
TEST_F(Besm6BookTest, Chapter18_CopyThruPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
union simple {
    int i;
    long l;
    char c;
    unsigned char uc_arr[3];
};

union has_union {
    double d;
    union simple u;
    union simple *u_ptr;
};

struct simple_struct {
    long l;
    double d;
    unsigned int u;
};

union has_struct {
    long l;
    struct simple_struct s;
};

struct struct_with_union {
    union simple u;
    unsigned long ul;
};

union complex_union {
    double d_arr[2];
    struct struct_with_union s;
    union has_union *u_ptr;
};
// Test copying whole structs/unions through pointers (incl. to/from array members)



int strcmp(char* s1, char* s2);

// case 1: *x = y
int cptoptr(void) {
    union simple y;
    y.l = -20;
    union simple xobj;
    union simple* x = &xobj;
    *x = y;

    // validate (uc_arr reads big-endian bytes #0,#1,#2 of -20 = 1,255,255)
    if (x->l != -20 || x->i != -20 || x->uc_arr[0] != 1 || x->uc_arr[1] != 255 || x->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1;  // success
}

// case 2: x = *y
int cpfrptr(void) {
    // define/initialize a union object containing a struct
    struct simple_struct my_struct = { 999999999999l, 20e3, 2147483650u };
    static union has_struct my_union;
    my_union.s = my_struct;

    // get a pointer to that union
    union has_struct* union_ptr;
    union_ptr = &my_union;

    // copy from pointer to another union
    union has_struct another_union = *union_ptr;

    // validate
    if (another_union.s.l != 999999999999l || another_union.s.d != 20e3 || another_union.s.u != 2147483650u) {
        return 0; // fail
    }

    return 1;
}

// case 3: copies to and from array members (using a union w/ trailing padding)

// size is 12 bytes; take largest member (10 bytes)
// and pad to 4-byte alignment (b/c ui is 4-byte aligned)
union with_padding {
    char arr[10];
    unsigned int ui;
};

int cparrmem(void) {

    // define/initialize an array of unions
    union with_padding union_array[3] = { {"FOOBAR"}, {"HELLO"}, {"ITSAUNION"} };

    // copy element out of array
    union with_padding another_union = union_array[0];
    union with_padding yet_another_union = { "BLAHBLAH" };

    // copy an element into the array
    union_array[2] = yet_another_union;

    // validate
    if (strcmp(union_array[0].arr, "FOOBAR") || strcmp(union_array[1].arr, "HELLO") || strcmp(union_array[2].arr, "BLAHBLAH")) {
        return 0; // fail
    }

    if (strcmp(another_union.arr, "FOOBAR")) {
        return 0; // fail
    }

    // check yet_another_union too, even though we didn't update it
    if (strcmp(yet_another_union.arr, "BLAHBLAH")) {
        return 0; // fail
    }

    return 1; // success

}

int main(void) {
    if (!cptoptr()){
        return 1;
    }

    if (!cpfrptr()) {
        return 2;
    }

    if (!cparrmem()) {
        return 3;
    }

    return 0; // success
}
)PROG"));
}

// Adapted for BESM-6: calloc replaced by a zero-initialized static array
// (heap not yet wired up, task #23); unsigned int wraps at 2^48 and plain char
// is unsigned, so the wide unsigned and negative-char literals are adjusted.
TEST_F(Besm6BookTest, Chapter18_IncrStructMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test prefix and postfix ++ and -- with structure members

struct inner {
    char c;
    unsigned int u;
};

struct outer {
    unsigned long l;
    struct inner *in_ptr;
    int array[3];
};

int main(void) {
    static struct inner zeroed[3]; // zero-initialized, stands in for calloc
    struct outer my_struct = {
        // l
        999999999999ul,
        // in_ptr
        zeroed,
        // array
        {-1000, -2000, -3000},
    };
    struct outer *my_struct_ptr = &my_struct;

    // prefix ++
    if (++my_struct.l != 1000000000000ul) {
        return 1; // fail
    }

    // prefix --
    if (--my_struct.in_ptr[0].u != 281474976710655U) { // unsigned wraparound
        return 2; // fail
    }

    // postfix ++
    if (my_struct.in_ptr->c++) {
        return 3; // fail
    }

    // postfix --
    if (my_struct_ptr->array[1]-- != -2000) {
        return 4; // fail
    }

    // validate current state of my_struct - make sure we performed updates
    // and didn't clobber anything
    if (my_struct_ptr->l != 1000000000000ul) {
        return 5; // fail
    }

    if (my_struct.in_ptr->c != 1) {
        return 6; // fail
    }
    if (my_struct_ptr->in_ptr->u !=  281474976710655U) {
        return 7; // fail
    }

    if (my_struct_ptr->array[1] != -2001) {
        return 8; // fail
    }

    if (my_struct_ptr->array[0] != -1000 || my_struct_ptr->array[2] != -3000) {
        return 9; // fail
    }

    // ++/-- w/ pointers to structs
    // first let's populate the struct array at my_struct_ptr->in_ptr
    my_struct_ptr->in_ptr[1].c = -1;
    my_struct_ptr->in_ptr[1].u = 1u;
    my_struct_ptr->in_ptr[2].c = 'X';
    my_struct_ptr->in_ptr[2].u = 100000u;

    (++my_struct_ptr->in_ptr)->c--; // decrement struct array[1].c
    my_struct_ptr->in_ptr++->u++; // decrement stuct_array[1].u, increment in_ptr

    // validate - in_ptr currently points to array member at index 2

    // element 0 (now at index -2) should have same values as last time we checked
    if (my_struct_ptr->in_ptr[-2].c != 1 || my_struct_ptr->in_ptr[-2].u != 281474976710655U) {
        return 10;
    }

    // we decremented c in element 1 (now at index -1), didn't change u
    // (plain char is unsigned on BESM-6, so 255 decremented reads back as 254)
    if (my_struct_ptr->in_ptr[-1].c != 254) {
        return 11; // fail
    }

    if (my_struct_ptr->in_ptr[-1].u != 2) {
        return 12; // fail
    }

    // didn't change any values in last array element (now at index 0)
    if (my_struct_ptr->in_ptr[0].c != 'X' || my_struct_ptr->in_ptr[0].u != 100000u) {
        return 13; // fail
    }

    return 0;
}
)PROG"));
}

// malloc + pointer-to-integer byte-address arithmetic.
TEST_F(Besm6BookTest, Chapter18_MemberOffsets)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// struct declarations for size/layout tests
//
// On BESM-6 a word is 6 bytes, every aggregate member is word-aligned, and a
// struct's sizeof is rounded up to a multiple of 6.

struct eight_bytes {
    int i;   // bytes 0-5 (one word)
    char c;  // byte 6
             // padded up to a word multiple -> sizeof 12
};

struct two_bytes {
    char arr[2];  // bytes 0-1
                  // padded up to a word -> sizeof 6
};

struct three_bytes {
    char arr[3];  // bytes 0-2
                  // padded up to a word -> sizeof 6
};

struct sixteen_bytes {
    struct eight_bytes eight;  // bytes 0-11
    struct two_bytes two;      // bytes 12-17
    struct three_bytes three;  // bytes 18-23
};                             // sizeof 24

struct wonky {
    char arr[19];
};  // sizeof 24 (19 data bytes + 5 bytes padding up to a word multiple)

struct internal_padding {
    char c;    // byte 0
    double d;  // byte 6 (word-aligned)
};             // sizeof 12

struct contains_struct_array {
    char c;                              // byte 0
    struct eight_bytes struct_array[3];  // bytes 6-41 (word-aligned)
};                                       // sizeof 42
/* Get the addresses of structure members to validate their offset and alignment
 * (including nested members accessed through chains of . and -> operations)
 * and addresses of one-past-the-end of structs to validate trailing padding.
 *
 * On BESM-6 a pointer-to-integer cast does not decode to a byte address, so we
 * compute byte offsets as char* - char* differences, which the backend decodes
 * via b/pdiff. */

// test 1: validate struct w/ scalar members (includes trailing padding)
// test member accesses of the form &x.y
int test_eightbytes(void) {
    struct eight_bytes s;
    char *start = (char *)&s;
    char *i_addr = (char *)&s.i;
    char *c_addr = (char *)&s.c;
    char *end = (char *)(&s + 1);

    // first element should always have same address as whole struct
    if (start != i_addr) {
        return 0;
    }

    // next element is one word in, at byte 6
    if (c_addr - start != 6) {
        return 0;
    }

    // end of struct is at byte 12 (padded up to a word multiple)
    if (end - start != 12) {
        return 0;
    }

    return 1;  // success
}

// test 2: validate struct w/ padding between members (accessing struct thru
// pointer) test member accesses of the form &x->y
int test_internal_padding(void) {
    struct internal_padding obj;
    struct internal_padding *s_ptr = &obj;
    char *start = (char *)s_ptr;
    char *c_addr = (char *)&s_ptr->c;
    char *d_addr = (char *)&s_ptr->d;
    char *end = (char *)(s_ptr + 1);

    // first element should always have same address as whole struct
    if (start != c_addr) {
        return 0;
    }

    // next element is word-aligned, at byte 6
    if (d_addr - c_addr != 6) {
        return 0;
    }

    // size of whole struct is 12 bytes
    if (end - start != 12) {
        return 0;
    }

    return 1;  // success
}

// test 3: validate struct that contains an array
// test member accesses of the form &x.y[i], x.y + i
int test_three_bytes(void) {
    // use static struct here to make sure that doesn't impact address
    // calculation
    static struct three_bytes s;

    char *start = (char *)&s;
    char *arr_addr = (char *)&s.arr;
    char *arr0_addr = (char *)&s.arr[0];
    char *arr1_addr = (char *)&s.arr[1];
    // different way to calculate same address as above
    char *arr1_addr_alt = (char *)(s.arr + 1);
    char *arr2_addr = (char *)&s.arr[2];
    char *arr_end = (char *)(s.arr + 3);
    char *struct_end = (char *)(&s + 1);

    // struct, array, and first array element should all have same address
    if (start != arr_addr) {
        return 0;
    }

    if (start != arr0_addr) {
        return 0;
    }

    // s.arr[1] and s.arr[2] should be at byte offsets 1 and 2
    if (arr1_addr - start != 1) {
        return 0;
    }

    if (arr1_addr != arr1_addr_alt) {
        return 0;
    }

    if (arr2_addr - start != 2) {
        return 0;
    }

    // arr_end is one past the 3-element char array, at byte offset 3
    if (arr_end - start != 3) {
        return 0;
    }

    // struct_end is at byte offset 6 (struct padded up to a word multiple)
    if (struct_end - start != 6) {
        return 0;
    }

    return 1;  // success
}

// test 4: validate struct containing nested structs
// test accesses of the form &x->y.z, &x->y.z[i],
// &x.y.z, &x.y.z[i]
int test_sixteen_bytes(void) {
    static struct sixteen_bytes s;
    struct sixteen_bytes *s_ptr = &s;

    // get addresses of various members through s_ptr
    char *start = (char *)s_ptr;
    char *eight_addr = (char *)&s_ptr->eight;
    char *eight_i_addr = (char *)&s_ptr->eight.i;
    char *eight_c_addr = (char *)&s_ptr->eight.c;
    char *two = (char *)&s_ptr->two;
    char *two_arr = (char *)s_ptr->two.arr;
    char *two_arr0 = (char *)&s_ptr->two.arr[0];
    char *two_arr1 = (char *)&s_ptr->two.arr[1];
    char *two_arr_end = (char *)(s_ptr->two.arr + 2);
    char *two_end = (char *)(&s_ptr->two + 1);
    char *three = (char *)&s_ptr->three;
    // not going to validate every individual element in three.arr
    // since we already did that for two.arr
    char *three_end = (char *)(&s_ptr->three + 1);
    char *struct_end = (char *)(s_ptr + 1);

    // struct, first member, first member's first member all have same address
    if (start != eight_addr) {
        return 0;
    }

    if (start != eight_i_addr) {
        return 0;
    }

    if (eight_c_addr - start != 6) {
        return 0;
    }

    // next member starts at byte 12
    if (two - start != 12) {
        return 0;
    }

    if (two_arr - start != 12) {
        return 0;
    }

    if (two_arr0 - start != 12) {
        return 0;
    }

    // validate next array element in s_ptr->two.arr
    if (two_arr1 - start != 13) {
        return 0;
    }

    // one past the 2-element char array, at byte 14
    if (two_arr_end - start != 14) {
        return 0;
    }

    // s_ptr->two is padded to a word, so its end is at byte 18
    if (two_end - start != 18) {
        return 0;
    }

    if (three - start != 18) {
        return 0;
    }

    if (three_end - start != 24) {
        return 0;
    }

    if (struct_end - start != 24) {
        return 0;
    }

    // now get addresses of a few members thru s directly and make sure they're
    // the same

    char *eight_i_addr_alt = (char *)&s.eight.i;
    char *eight_c_addr_alt = (char *)&s.eight.c;
    char *two_arr_alt = (char *)s.two.arr;
    char *two_arr1_alt = (char *)&s.two.arr[1];
    char *three_alt = (char *)&s.three;

    if (eight_i_addr_alt != eight_i_addr) {
        return 0;
    }

    if (eight_c_addr_alt != eight_c_addr) {
        return 0;
    }

    if (two_arr_alt != two_arr) {
        return 0;
    }

    if (two_arr1_alt != two_arr1) {
        return 0;
    }

    if (three_alt != three) {
        return 0;
    }

    return 1;  // success
}

// test 5: validate array of irregularly-sized structs; make sure there's no
// padding b/t array elements test access of the form x[i].y, &x[i].y[j]
int test_wonky_array(void) {
    struct wonky wonky_array[5];
    char *array_start = (char *)wonky_array;
    char *elem3 = (char *)(wonky_array + 3);
    char *elem3_arr = (char *)wonky_array[3].arr;
    char *elem2_arr2 = (char *)&wonky_array[2].arr[2];
    char *elem2_arr_end = (char *)(wonky_array[2].arr + 19);
    char *elem4_arr_end = (char *)(wonky_array[4].arr + 19);
    char *array_end = (char *)(wonky_array + 5);

    // each element is 24 bytes (19 data bytes + 5 bytes padding)
    if (elem3 - array_start != 24 * 3) {
        return 0;
    }

    if (elem3_arr != elem3) {
        return 0;
    }

    if (elem2_arr2 - array_start != 24 * 2 + 2) {
        return 0;
    }

    // 5 bytes of trailing padding b/t last data byte of elem2 and start of elem3
    if (elem3 - elem2_arr_end != 5) {
        return 0;
    }

    // 5 bytes of trailing padding b/t last data byte of elem4 and array end
    if (array_end - elem4_arr_end != 5) {
        return 0;
    }

    if (array_end - array_start != 24 * 5) {
        return 0;
    }

    return 1;  // success
}

// test 6: validate array of structs containing arrays of structs
// test access of the form x[i].y->z, x->y->z, where x and y are arrays that
// decay to pointers
int test_contains_struct_array_array(void) {
    struct contains_struct_array arr[3];
    char *array_start = (char *)arr;
    char *first_scalar_elem = (char *)(&arr[0].c);

    // arr[0].struct_array[0].i
    char *outer0_inner0_i = (char *)(&arr[0].struct_array->i);

    // arr[0].struct_array[0].c
    char *outer0_inner0_c = (char *)(&arr->struct_array->c);

    // one-past-the-end of arr[0].struct_array
    char *outer0_end = (char *)(arr->struct_array + 3);

    // start of arr[1] (should be the same as one-past-end of
    // arr[0].struct_array)
    char *outer1 = (char *)(&arr[1]);

    // struct_array of arr[1]
    char *outer1_arr = (char *)(arr[1].struct_array);

    // arr[1].struct_array[1].i
    char *outer1_inner1_i = (char *)&(((arr + 1)->struct_array + 1)->i);

    // arr[2].struct_array[0].c
    char *outer2_inner0_c = (char *)&((arr + 2)->struct_array->c);

    // validate pointers to start of struct
    if (first_scalar_elem != array_start) {
        return 0;
    }

    // 6 bytes into array (struct_array offset in contains_struct_array is 6,
    // i offset in eight_bytes is 0)
    if (outer0_inner0_i - array_start != 6) {
        return 0;
    }

    // 12 bytes into array (struct_array offset is 6,
    // c offset in eight_bytes is 6)
    if (outer0_inner0_c - array_start != 12) {
        return 0;
    }

    // no trailing padding in arr[0] (sizeof 42 is a word multiple)
    if (outer0_end != outer1) {
        return 0;
    }

    // check offsets in arr[1]
    if (outer1_arr - array_start != 48) {
        return 0;
    }

    if (outer1_arr - outer1 != 6) {
        return 0;
    }

    // arr[1] is 42 bytes into arr
    // arr[1].struct_array is 6 bytes into arr[1]
    // arr[1].struct_array[1] is 12 bytes into struct_array
    // arr[1].struct_array[1].i is 0 bytes into arr[1].struct_array[1]
    // total offset: 42+6+12 = 60
    if (outer1_inner1_i - array_start != 60) {
        return 0;
    }

    // arr[2] is 84 bytes into arr
    // arr[2].struct_array is 6 bytes into arr[2]
    // arr[2].struct_array[0] is 0 bytes into arr[2].struct_array
    // arr[2].struct_array[0].c is 6 bytes into eight_bytes
    // total offset: 84 + 6 + 6 = 96
    if (outer2_inner0_c - array_start != 96) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    if (!test_eightbytes()) {
        return 1;
    }

    if (!test_internal_padding()) {
        return 2;
    }

    if (!test_three_bytes()) {
        return 3;
    }

    if (!test_sixteen_bytes()) {
        return 4;
    }

    if (!test_wonky_array()) {
        return 5;
    }

    if (!test_contains_struct_array_array()) {
        return 6;
    }

    return 0;  // success
}
)PROG"));
}

TEST_F(Besm6BookTest, Chapter18_NestedStaticStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */


// standard library function
int strcmp(char *s1, char *s2);

// structure type defs
struct inner {
    int one_i;
    signed char two_arr[3];
    unsigned three_u;
};

struct outer {
    long one_l;
    struct inner two_struct;
    char *three_msg;
    double four_d;
};

// declarations of global vars (defined in client)
extern struct outer all_zeros;
extern struct outer partial;
extern struct outer full;
extern struct outer converted;
extern struct outer struct_array[3];

// declarations of validation functions (defined in lib)
int test_uninitialized(void);
int test_partially_initialized(void);
int test_fully_intialized(void);
int test_implicit_conversions(void);
int test_array_of_structs(void);
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */




// structs defined here
// validation functions defined in library

// case 1: struct with no explicit initializer should be all zeros
struct outer all_zeros;

// case 2: partially initialized struct
struct outer partial = {
    100l,
    {10, {10}},  // leave arr[1], arr[2], and y uninitialized
    "Hello!"};   // leave d uninitialized

struct outer full = {
    1000000000000l,
    {1000, "OK",
     4292870144u},  // can initialized signed char array w/ static string
    "Another message",
    2e12};

struct outer converted = {
    10.5,  // 10l
    {
        2147483650u,  // 2147483650
        {
            15.6,             // 15
            17592186044419l,  // 3
            2147483777u       // -127
        },
        1152921506754330624ul  // 2147483648u
    },
    0ul,         // null pointer
    4292870144ul  // 4292870144.0
};

struct outer struct_array[3] = {{1, {2, "ab", 3}, 0, 5},
                                {6, {7, "cd", 8}, "Message", 9}};

int main(void) {
    if (!test_uninitialized()) {
        return 1;
    }

    if (!test_partially_initialized()) {
        return 2;
    }

    if (!test_fully_intialized()) {
        return 3;
    }

    if (!test_implicit_conversions()) {
        return 4;
    }

    if (!test_array_of_structs()) {
        return 5;
    }

    return 0;  // success
}
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */


// structs defined in client but visible here
// validation functions defined here

// case 1: struct with no explicit initializer should be all zeros
// struct outer all_zeros;
int test_uninitialized(void) {
    // validate elements in struct outer
    if (all_zeros.one_l || all_zeros.three_msg || all_zeros.four_d) {
        return 0;
    }

    // validate elements in struct inner
    if (all_zeros.two_struct.one_i || all_zeros.two_struct.two_arr[0] ||
        all_zeros.two_struct.two_arr[1] || all_zeros.two_struct.two_arr[2] ||
        all_zeros.two_struct.three_u) {
        return 0;
    }

    return 1;  // success
}

// case 2: partially initialized struct
/*
    struct outer partial = {
        100l,
        {10, {10}},  // leave arr[1], arr[2], and y uninitialized
        "Hello!"};   // leave d uninitialized
*/
int test_partially_initialized(void) {
    // validate elements in struct outer
    if (partial.one_l != 100l || strcmp(partial.three_msg, "Hello!")) {
        return 0;
    }

    if (partial.four_d) {  // this wasn't explicitly initialized, should be 0
        return 0;
    }

    // validate elements in struct inner
    if (partial.two_struct.one_i != 10 || partial.two_struct.two_arr[0] != 10) {
        return 0;
    }

    if (partial.two_struct.two_arr[1] || partial.two_struct.two_arr[2] ||
        partial.two_struct
            .three_u) {  // not explicitly initialized, should be 0
        return 0;
    }

    return 1;  // success
}

// case 3: fully initialized struct
/*
    struct outer full = {
        1000000000000l,
        {1000, "OK",
        4292870144u},  // can initialized signed char array w/ static string
        "Another message",
        2e12};
*/
int test_fully_intialized(void) {
    // validate elements in struct outer
    if (full.one_l != 1000000000000l ||
        strcmp(full.three_msg, "Another message") || full.four_d != 2e12) {
        return 0;
    }

    // validate elemetns in string inner
    if (full.two_struct.one_i != 1000 || full.two_struct.two_arr[0] != 'O' ||
        full.two_struct.two_arr[1] != 'K' || full.two_struct.two_arr[2] != 0 ||
        full.two_struct.three_u != 4292870144u) {
        return 0;
    }

    return 1;  // success
}

// case 4: implicit conversion of scalar elements
/*
    struct outer converted = {
        10.5,  // 10l
        {
            2147483650u,  // 2147483650
            {
                15.6,             // 15
                17592186044419l,  // 3
                2147483777u       // -127
            },
            1152921506754330624ul  // 2147483648u
        },
        0ul,         // null pointer
        4292870144ul  // 4292870144.0
    };
*/
int test_implicit_conversions(void) {
    // validate elements in struct outer
    if (converted.one_l != 10l || converted.three_msg != 0 ||
        converted.four_d != 4292870144.0) {
        return 0;
    }

    // validate elements in struct inner
    if (converted.two_struct.one_i != 2147483650 ||
        converted.two_struct.two_arr[0] != 15 ||
        converted.two_struct.two_arr[1] != 3 ||
        converted.two_struct.two_arr[2] != -127 ||
        converted.two_struct.three_u != 2147483648u) {
        return 0;
    }

    return 1;  // success
}

// case 5: array of structures
/*
    struct outer struct_array[3] = {{1, {2, "ab", 3}, 0, 5},
                                        {6, {7, "cd", 8}, "Message", 9}};
*/
int test_array_of_structs(void) {
    // leave last element uninitialized

    // validate outer members of array element 0
    if (struct_array[0].one_l != 1 || struct_array[0].three_msg != 0 ||
        struct_array[0].four_d != 5) {
        return 0;
    }

    // validate nested members of array element 0
    if (struct_array[0].two_struct.one_i != 2 ||
        strcmp((char *)struct_array[0].two_struct.two_arr, "ab") ||
        struct_array[0].two_struct.three_u != 3) {
        return 0;
    }

    // validate outer members of array element 1
    if (struct_array[1].one_l != 6 ||
        strcmp((char *)struct_array[1].three_msg, "Message") ||
        struct_array[1].four_d != 9) {
        return 0;
    }

    // validate nested members of array element 1
    if (struct_array[1].two_struct.one_i != 7 ||
        strcmp((char *)struct_array[1].two_struct.two_arr, "cd") ||
        struct_array[1].two_struct.three_u != 8) {
        return 0;
    }

    // validate array element 2 - should be all 0s
    if (struct_array[2].one_l || struct_array[2].three_msg ||
        struct_array[2].four_d) {
        return 0;
    }

    // validate nested members of array element 2
    if (struct_array[2].two_struct.one_i ||
        struct_array[2].two_struct.two_arr[0] ||
        struct_array[2].two_struct.two_arr[1] ||
        struct_array[2].two_struct.two_arr[2] ||
        struct_array[2].two_struct.three_u) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

// BESM-6: char members read byte #0 (MSB); array-of-pointers case rewritten to use
// local storage instead of calloc (no heap dependency).
TEST_F(Besm6BookTest, Chapter18_NestedUnionAccess)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
union simple {
    int i;
    long l;
    char c;
    unsigned char uc_arr[3];
};

union has_union {
    double d;
    union simple u;
    union simple *u_ptr;
};

struct simple_struct {
    long l;
    double d;
    unsigned int u;
};

union has_struct {
    long l;
    struct simple_struct s;
};

struct struct_with_union {
    union simple u;
    unsigned long ul;
};

union complex_union {
    double d_arr[2];
    struct struct_with_union s;
    union has_union *u_ptr;
};
/* Test access to nested union members through dot, arrow, and subscript operators */


int autodot(void) {
    // Test nested access with . in unions/structs containing unions
    // with automatic storage duration

    // access union in union
    union has_union x;
    x.u.l = 200000u;
    if (x.u.i != 200000) {
        return 0; // fail
    }

    // access struct in union
    union has_struct y;
    y.s.l = -5555l;
    y.s.d = 10.0;
    y.s.u = 100;

    if (y.l != -5555l) {
        return 0; // fail
    }

    // access union in struct in union
    union complex_union z;
    z.s.u.i = 12345;
    z.s.ul = 0;

    if (z.s.u.c != 0) { // byte #0 (MSB) of 12345 is zero
        return 0; // fail
    }

    if (z.d_arr[1]) { // bytes 8-15 of  union; same spot as z.s.ul
        return 0; // fail
    }

    // get/derefrence address of various members
    unsigned int *some_int_ptr = &y.s.u;
    union simple *some_union_ptr = &z.s.u;

    if (*some_int_ptr != 100 || (*some_union_ptr).i != 12345) {
        return 0; // fail
    }

    return 1; // success
}

int statdot(void) {
    // identical to test_auto_dot but using objects
    // with static storage duration

    // access union in union
    static union has_union x;
    x.u.l = 200000u;
    if (x.u.i != 200000) {
        return 0; // fail
    }

    // access struct in union
    static union has_struct y;
    y.s.l = -5555l;
    y.s.d = 10.0;
    y.s.u = 100;

    if (y.l != -5555l) {
        return 0; // fail
    }

    // access union in struct in union
    static union complex_union z;
    z.s.u.i = 12345;
    z.s.ul = 0;

    if (z.s.u.c != 0) { // byte #0 (MSB) of 12345 is zero
        return 0; // fail
    }

    if (z.d_arr[1]) { // bytes 8-15 of  union; same spot as z.s.ul
        return 0; // fail
    }

    return 1; // success
}

int autoarr(void) {
    // Test nested access in unions w/ automatic storage duration,
    // using only -> operator
    union simple inner = {100};
    union has_union outer;
    union has_union *outer_ptr = &outer;
    outer_ptr->u_ptr = &inner;
    if (outer_ptr->u_ptr->i != 100) {
        return 0; // fail
    }

    // write through nested access
    outer_ptr->u_ptr->l = -10;

    // read through other members that should have same value
    // c reads byte #0 (MSB) of -10 = 1; i and l read the full word = -10
    if (outer_ptr->u_ptr->c != 1 || outer_ptr->u_ptr->i != -10 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr (bytes #0,#1,#2 of -10 = 1,255,255)
    if (outer_ptr->u_ptr->uc_arr[0] != 1 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1; // success
}

int statarr(void) {
    // identical to test_auto_arrow but with objects of static storage duration
    static union simple inner = {100};
    static union has_union outer;
    static union has_union *outer_ptr;
    outer_ptr = &outer;
    outer_ptr->u_ptr = &inner;
    if (outer_ptr->u_ptr->i != 100) {
        return 0; // fail
    }

    // write through nested access
    outer_ptr->u_ptr->l = -10;

    // read through other members that should have same value
    // c reads byte #0 (MSB) of -10 = 1; i and l read the full word = -10
    if (outer_ptr->u_ptr->c != 1 || outer_ptr->u_ptr->i != -10 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr (bytes #0,#1,#2 of -10 = 1,255,255)
    if (outer_ptr->u_ptr->uc_arr[0] != 1 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1; // success
}

int arrunis(void) {
    // test access to array of unions
    union has_union arr[3];
    arr[0].u.l = -10000;
    arr[1].u.i = 200;
    arr[2].u.c = -120;

    // arr[1].u.i = 200 → byte #0 (MSB) is 0; arr[2].u.c = -120 stores byte 136
    if (arr[0].u.l != -10000 || arr[1].u.c != 0 || arr[2].u.uc_arr[0] != 136) {
        return 0; // fail
    }

    return 1; // success
}

int arrptrs(void) {
    // test access to array of union pointers (local storage, no heap)
    union has_union *ptr_arr[3];
    union has_union storage[3];
    union simple inner_storage[3];
    for (int i = 0; i < 3; i = i + 1) {
        ptr_arr[i] = &storage[i];
        ptr_arr[i]->u_ptr = &inner_storage[i];
        ptr_arr[i]->u_ptr->l = i;
    }

    if (ptr_arr[0]->u_ptr->l != 0 || ptr_arr[1]->u_ptr->l != 1 || ptr_arr[2]->u_ptr->l != 2) {
        return 0; // fail
    }

    return 1;
}


int main(void) {
    if (!autodot()) {
        return 1;
    }

    if (!statdot()) {
        return 2;
    }

    if (!autoarr()) {
        return 3;
    }

    if (!statarr()) {
        return 4;
    }

    if (!arrunis()) {
        return 5;
    }

    if (!arrptrs()) {
        return 6;
    }

    return 0;
}
)PROG"));
}

// size_and_offset_calculations/sizeof_exps: sizeof of expressions of struct type
// (block-scope `static` dropped — no static-local storage; sizeof never evaluates
// its operand, so the null get_twentybyte_ptr() is never dereferenced).
TEST_F(Besm6BookTest, Chapter18_SizeofExps)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct eight_bytes { int i; char c; };
struct two_bytes { char arr[2]; };
struct three_bytes { char arr[3]; };
struct sixteen_bytes { struct eight_bytes eight; struct two_bytes two; struct three_bytes three; };
struct seven_bytes { struct two_bytes two; struct three_bytes three; struct two_bytes two2; };
struct twentyfour_bytes { struct seven_bytes seven; struct sixteen_bytes sixteen; };
struct twenty_bytes { struct sixteen_bytes sixteen; struct two_bytes two; };
struct wonky { char arr[19]; };
struct internal_padding { char c; double d; };
struct contains_struct_array { char c; struct eight_bytes struct_array[3]; };
struct twenty_bytes *get_twentybyte_ptr(void) { return 0; }
int main(void) {
    struct contains_struct_array arr_struct;
    if (sizeof arr_struct.struct_array[2] != 12) return 1;
    struct twentyfour_bytes twentyfour;
    if (sizeof twentyfour.seven.two2 != 6) return 2;
    if (sizeof get_twentybyte_ptr()->sixteen.three != 6) return 3;
    if (sizeof get_twentybyte_ptr()->sixteen != 24) return 4;
    if (sizeof twentyfour.seven != 18) return 5;
    if (sizeof twentyfour != 42) return 6;
    if (sizeof *get_twentybyte_ptr() != 30) return 7;
    if (sizeof *((struct wonky *)0) != 24) return 8;
    extern struct internal_padding struct_array[4];
    if (sizeof struct_array[0] != 12) return 9;
    if (sizeof arr_struct != 42) return 10;
    if (sizeof struct_array != 48) return 11;
    if (sizeof arr_struct.struct_array != 36) return 12;
    return 0;
})"));
}

// size_and_offset_calculations/sizeof_type: sizeof of struct/array types.
TEST_F(Besm6BookTest, Chapter18_SizeofType)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct eight_bytes { int i; char c; };
struct two_bytes { char arr[2]; };
struct three_bytes { char arr[3]; };
struct sixteen_bytes { struct eight_bytes eight; struct two_bytes two; struct three_bytes three; };
struct seven_bytes { struct two_bytes two; struct three_bytes three; struct two_bytes two2; };
struct twentyfour_bytes { struct seven_bytes seven; struct sixteen_bytes sixteen; };
struct twenty_bytes { struct sixteen_bytes sixteen; struct two_bytes two; };
struct wonky { char arr[19]; };
struct internal_padding { char c; double d; };
struct contains_struct_array { char c; struct eight_bytes struct_array[3]; };
int main(void) {
    if (sizeof(struct eight_bytes) != 12) return 1;
    if (sizeof(struct two_bytes) != 6) return 2;
    if (sizeof(struct three_bytes) != 6) return 3;
    if (sizeof(struct sixteen_bytes) != 24) return 4;
    if (sizeof(struct seven_bytes) != 18) return 5;
    if (sizeof(struct twentyfour_bytes) != 42) return 6;
    if (sizeof(struct twenty_bytes) != 30) return 7;
    if (sizeof(struct wonky) != 24) return 8;
    if (sizeof(struct internal_padding) != 12) return 9;
    if (sizeof(struct contains_struct_array) != 42) return 10;
    if (sizeof(struct internal_padding[4]) != 48) return 11;
    if (sizeof(struct wonky[2]) != 48) return 12;
    return 0;
})"));
}

TEST_F(Besm6BookTest, Chapter18_StaticStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */

int strcmp(char *s1, char *s2);

struct s {
    double one_d;
    char *two_msg;
    unsigned char three_arr[3];
    int four_i;
};

// static structures defined in client
extern struct s uninitialized;
extern struct s partial;
extern struct s partial_with_array;
extern struct s converted;

// validation functions defined in library

// case 1: struct with no explicit initializer should be all zeros
int test_uninitialized(void);

// case 2: partially initialized struct
int test_partially_initialized(void);

// case 3: partially initialized array w/in struct
int test_partial_inner_init(void);

// case 4: implicit conversion of scalar elements
int test_implicit_conversion(void);
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */



// case 1: struct with no explicit initializer should be all zeros
struct s uninitialized;

// case 2: partially initialized struct
struct s partial = {1.0, "Hello"};

// case 3: partially initialized array w/in struct
struct s partial_with_array = {3.0, "!", {1}, 2};

// case 4: implicit conversion of scalar elements
struct s converted = {
    1099511627775l,  // 1099511627775.0
    0l,              // null ptr
    "ABC",           // {'A', 'B', 'C'}
    17179869189l     // 17179869189
};

int main(void) {
    if (!test_uninitialized()) {
        return 1;
    }

    if (!test_partially_initialized()) {
        return 2;
    }

    if (!test_partial_inner_init()) {
        return 3;
    }

    if (!test_implicit_conversion()) {
        return 4;
    }

    return 0;  // success
}
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */


// structs defined in client but visible here
// validation functions defined here

// case 1: struct with no explicit initializer should be all zeros
// struct s uninitialized;
int test_uninitialized(void) {
    // make sure all elements are zero
    if (uninitialized.one_d || uninitialized.two_msg ||
        uninitialized.three_arr[0] || uninitialized.three_arr[1] ||
        uninitialized.three_arr[2] || uninitialized.four_i) {
        return 0;
    }
    return 1;  // success
}

// case 2: partially initialized struct
// struct s partial = {1.0, "Hello"};
int test_partially_initialized(void) {
    // validate first two elements
    if (partial.one_d != 1.0 || strcmp(partial.two_msg, "Hello")) {
        return 0;
    }

    // validate that remaining elements are zero
    if (partial.three_arr[0] || partial.three_arr[1] || partial.three_arr[2] ||
        partial.four_i) {
        return 0;
    }

    return 1;  // success
}

// case 3: partially initialized array w/in struct
// struct s partial with_array = {3.0, "!", {1}, 2};
int test_partial_inner_init(void) {
    // validate explicitly initialzed elements
    if (partial_with_array.one_d != 3.0 ||
        strcmp(partial_with_array.two_msg, "!") ||
        partial_with_array.three_arr[0] != 1 ||
        partial_with_array.four_i != 2) {
        return 0;
    }

    // validate that last two elements of arr are 0
    if (partial_with_array.three_arr[1] || partial_with_array.three_arr[2]) {
        return 0;
    }

    return 1;  // success
}

// case 4: implicit conversion of scalar elements
/*
    struct s converted = {
        1099511627775l,  // 1099511627775.0
        0l,              // null ptr
        "ABC",           // {'A', 'B', 'C'}
        17179869189l     // 17179869189
    };
*/
int test_implicit_conversion(void) {
    // validate elements
    if (converted.one_d != 1099511627775.0 || converted.two_msg ||
        converted.three_arr[0] != 'A' || converted.three_arr[1] != 'B' ||
        converted.three_arr[2] != 'C' || converted.four_i != 17179869189) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

// BESM-6: char is unsigned and reads big-endian (byte #0 = MSB); unsigned long is one
// 48-bit word (6 live bytes, so arr[6]/arr[7] are in the zero second word); the double
// -1.0 has the native bit pattern exponent=64, sign=1, zero mantissa = 2^47 + 2^40.
TEST_F(Besm6BookTest, Chapter18_StaticUnionAccess)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test access to static union members with . and ->
union u {
    unsigned long l;
    double d;
    char arr[8];
};

static union u my_union = { 281474976710655UL }; // 2^48 - 1 (all 48 bits set)
static union u* union_ptr = 0;

int main(void) {
    union_ptr = &my_union;
    if (my_union.l != 281474976710655UL) {
        return 1; // fail
    }

    // word 0 is all-ones (bytes 0-5 = 255); arr[6]/arr[7] live in the zero second word
    for (int i = 0; i < 6; i = i + 1) {
        if (my_union.arr[i] != 255) {
            return 2; // fail
        }
    }
    if (my_union.arr[6] != 0 || my_union.arr[7] != 0) {
        return 3; // fail
    }

    union_ptr->d = -1.0;

    if (union_ptr->l != 141836999983104UL) {
        return 4; // fail
    }

    // byte #0 (MSB) of -1.0 is 0x81 = 129; bytes #1-5 are zero
    if (union_ptr->arr[0] != 129) {
        return 5; // fail
    }
    for (int i = 1; i < 6; i = i + 1) {
        if (my_union.arr[i]) {
            return 6; // fail
        }
    }

    // the second word is untouched by the one-word double write
    if (union_ptr->arr[6] != 0 || union_ptr->arr[7] != 0) {
        return 7; // fail
    }

    return 0; // success
}
)PROG"));
}

// BESM-6: validate helpers renamed to stay distinct within 8 chars; char members read
// byte #0 (MSB), so a small int written through the union reads back 0 there; strcmp
// strings and the struct char member use UPPERCASE so source/KOI-7 encodings agree.
TEST_F(Besm6BookTest, Chapter18_StaticUnionInits)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
int strcmp(char* s1, char* s2);

// Test case 1 - simple union w/ scalar elements (and padding)
union simple {
    int i;
    char c;
    double d;
};

extern union simple s;
int vsimple(void);

// Test case 2 - union w/ another union as first element
union has_union {
    union simple u;
    char c;
};

extern union has_union h;
int vhasun(void);

// Test case 3 - struct containing partially initialized array of unions
// (make sure we initialize padding to 0 for each of them)
struct has_union_array {
    union has_union union_array[4];
    char c;
    union simple s;
};


extern struct has_union_array my_struct;
int vhasarr(void);


// Test case 4 - an uninitialized static union (make sure we initialize the
// whole thing, including padding, to zeroes)

extern union has_union all_zeros;
int vuninit(void);

// Test case 5 - an array of unions with trailing padding. Make sure padding
// is included
union with_padding {
    char arr[13];
    long l;
}; // extra 3 bytes of padding to make it 8-byte aligned

extern union with_padding padded_union_array[3];
int vpadarr(void);
// Test initialization of static unions; make sure uninitialized
// unions/sub-objects are initialized to zero

// Test case 1 - simple union w/ scalar elements

union simple s = {217};

// Test case 2 - union w/ another union as first element

union has_union h = {{77}};

// Test case 3 - struct containing partially initialized array of unions
// (make sure we initialize uninitialized values to zero)

struct has_union_array my_struct = {
    {{{'a'}}, {{'b'}}, {{'c'}}}, 'X', {'Y'}
};

// Test case 4 - uninitialized union (make sure whole thing is initialized to
// 0, not just first element)

union has_union all_zeros;

// Test case 5 - an array of unions with trailing padding. Make sure padding
// is included
union with_padding padded_union_array[3] = {
    {"FIRST STRING"}, {"STRING TWO"}, {
        "STRING THREE"
    }
};

int main(void) {
    if (!vsimple()) {
        return 1;
    }

    if (!vhasun()){
        return 2;
    }

    if (!vhasarr()) {
        return 3;
    }

    if (!vuninit()) {
        return 4;
    }

    if (!vpadarr()) {
        return 5;
    }

    return 0;
}
// Test initialization of static unions; make sure uninitialized unions are initialized to zero


int vsimple(void) {
    // s.c reads byte #0 (MSB) of int 217 = 0; char is unsigned on BESM-6
    return (s.c == 0 && s.i == 217);
}

int vhasun(void) {
    // u.c and h.c read byte #0 (MSB) of int 77 = 0; the int member holds 77
    return (h.u.c == 0 && h.c == 0 && h.u.i == 77);
}

int vhasarr(void) {

    // validate array of unions
    // first validate elements 0-2
    for (int i = 0; i < 3; i = i + 1) {
        int expected = 'a' + i;
        // the int member holds 'a'+i; the char views read byte #0 (MSB) = 0
        if (my_struct.union_array[i].u.c != 0
            || my_struct.union_array[i].c != 0
            || my_struct.union_array[i].u.i != expected) {
            return 0;
        }
    }

    // last array element should be all 0s (including bytes that
    // aren't part of first member) b/c it's uninitialized
    if (my_struct.union_array[3].u.d != 0.0) {
        return 0;
    }

    // validate other elements of struct
    if (my_struct.c != 'X') {
        return 0; // fail
    }

    // s.i holds 'Y'; s.c reads byte #0 (MSB) = 0
    if (my_struct.s.c != 0 || my_struct.s.i != 'Y') {
        return 0; // fail
    }

    return 1;
}

int vuninit(void) {
    if (all_zeros.u.d != 0.0) {
        return 0; // fail
    }
    return 1;
}

int vpadarr(void) {
    if (strcmp(padded_union_array[0].arr, "FIRST STRING") != 0) {
        return 0; // fail
    }

    if (strcmp(padded_union_array[1].arr, "STRING TWO") != 0) {
        return 0; // fail
    }

    if (strcmp(padded_union_array[2].arr, "STRING THREE") != 0) {
        return 0; // fail
    }

    return 1;
}
)PROG"));
}

// member_access/union_init_and_member_access: union init + member access.
// BESM-6: reading -1l back through the unsigned-long member yields its 41 value
// bits (2^41-1); through the char member it yields byte #0 (MSB, bits 48-41) =
// 0b00000001 = 1 (bits 48-42 are the zero exponent field, bit 41 is the sign).
TEST_F(Besm6BookTest, Chapter18_UnionInitAndMemberAccess)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
union u { double d; long l; unsigned long ul; char c; };
int main(void) {
    union u x = {20};
    if (x.d != 20.0) return 1;
    union u *ptr = &x;
    ptr->l = -1l;
    if (ptr->l != -1l) return 2;
    if (ptr->ul != 2199023255551UL) return 3;
    if (x.c != 1) return 4;
    return 0;
})"));
}

// extra_credit/size_and_offset/union_sizes: sizeof of union types, BESM-6 layout.
TEST_F(Besm6BookTest, Chapter18_UnionSizes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct eight_bytes { int i; char c; };
struct wonky { char arr[19]; };
union no_padding { char c; unsigned char uc; signed char arr[11]; };
union with_padding { signed char arr[10]; unsigned int ui; };
union contains_array { union with_padding arr1[2]; union no_padding arr[3]; };
union double_and_int { int i; double d; };
union contains_structs { struct wonky x; struct eight_bytes y; };
union contains_structs *get_union_ptr(void);
int main(void) {
    if (sizeof(union no_padding) != 12) return 1;
    if (sizeof(union with_padding) != 12) return 2;
    if (sizeof(union contains_array) != 36) return 3;
    if (sizeof(union double_and_int) != 6) return 4;
    if (sizeof(union contains_structs) != 24) return 5;
    union no_padding x = { 1 };
    union contains_array y = { {{{-1, 2}} }};
    if (sizeof x != 12) return 6;
    if (sizeof y.arr1 != 24) return 7;
    if (sizeof * get_union_ptr() != 24) return 8;
    return 0;
}
union contains_structs *get_union_ptr(void) { return 0; }
)"));
}

// block-scope static + temporary lifetime + union punning.  We implicitly take the
// address of a union with temporary lifetime (the conditional-expression result) and
// subscript a char member of it — exercising gen_lval's EXPR_COND case.
//
// Union char-punning values are BESM-6-specific: a `long` is one 48-bit word whose
// bytes pack 6/word most-significant-first, so the byte that distinguishes the two
// initializers is arr[5] (the low byte), not arr[0] as on little-endian x86.  Plain
// char is unsigned here, so the bytes are the positive low-byte values 234 / 210
// (= 9876543210 & 0xFF / 1234567890 & 0xFF).  get_flag() toggles 0->1 then 1->0, so
// the first access selects union1 and the second selects union2.
TEST_F(Besm6BookTest, Chapter18_UnionTempLifetime)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
struct has_char_array {
    char arr[8];
};

union has_array {
    long l;
    struct has_char_array s;
};

int get_flag(void) {
    static int flag = 0;
    flag = !flag;
    return flag;
}

int main(void) {
    union has_array union1 = {9876543210l};
    union has_array union2 = {1234567890l};

    // first access selects union1
    if ((get_flag() ? union1 : union2).s.arr[5] != 234) {
        return 1; // fail
    }

    // then access selects union2
    if ((get_flag() ? union1 : union2).s.arr[5] != 210) {
        return 2; // fail
    }

    return 0; // success
}
)PROG"));
}

// union_copy/unions_in_conditionals: a union value in a ?: expression.  BESM-6: the
// char member reads byte #0 (MSB), so one.c = byte#0 of -1 = 1 and two.c = byte#0 of
// 100 = 0 (100 occupies only bits 7-1, so the MSB byte is zero).
TEST_F(Besm6BookTest, Chapter18_UnionsInConditionals)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
union u { long l; int i; char c; };
int choose_union(int flag) {
    union u one;
    union u two;
    one.l = -1;
    two.i = 100;
    return (flag ? one : two).c;
}
int main(void) {
    if (choose_union(1) != 1) return 1;
    if (choose_union(0) != 0) return 2;
    return 0;
})"));
}

//
// Chapter 19
//

TEST_F(Besm6BookTest, Chapter19_WP_AllTypes_FoldCompoundAssignAllTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test copy prop/constant folding of compound assignment with non-integer
 * types and type conversions.  Adapted for BESM-6: plain char is unsigned, so
 * cases that rely on a signed wrap use `signed char`; the out-of-range
 * double/unsigned-long rows are dropped; int/long are 41-bit, so values that
 * wrapped at 32 bits on x86 don't wrap here (recomputed); function names are
 * kept distinct within 8 characters.
 */

// like chapter 16's compound_assign_chars.c but constant-foldable
int t_chars(void) {
    signed char c = 100;
    signed char c2 = 100;
    c += c2; // 200 promoted to int, truncates to signed char -56
    if (c != -56) {
        return 1; // fail
    }

    unsigned char uc = 200;
    c2 = -100;
    uc /= c2; // (int)200 / (int)(-100) == -2, back to unsigned char 254
    if (uc != 254) {
        return 2; // fail
    }

    uc -= 250.0; // convert uc to double, do operation, convert back
    if (uc != 4) {
        return 3; // fail
    }

    signed char sc = -70;
    sc *= c;
    if (sc != 80) {
        return 4; // fail
    }

    if ((sc %= c) != 24) {
        return 5; // fail
    }

    return 0; // success
}

// like chapter 13's compound_assign.c
int t_dbl(void) {
    double d = 10.0;
    d /= 4.0;
    if (d != 2.5) {
        return 1;
    }
    d *= 10000.0;
    if (d != 25000.0) {
        return 2;
    }
    return 0;
}

// like chapter 13's compound_assign_implicit_cast.c (out-of-range ulong row dropped)
int t_dblcast(void) {
    double d = 1000.5;
    d += 1000; // convert 1000 to double, add, store
    if (d != 2000.5) {
        return 1;
    }
    int i = 10;
    i += 0.99999; // promote i to double, add .99999, truncate back to int
    if (i != 10) {
        return 2;
    }
    return 0;
}

// like chapter 12's compound_assign_uint.c.  On BESM-6 unsigned int is 48-bit,
// so -1u is 2^48-1; dividing through the common type yields 128.
int t_uint(void) {
    unsigned int x = -1u;
    x /= -10l;
    if (x != 128) {
        return 1; // fail
    }
    return 0;
}

// like chapter 11's compound_assign_to_int.c; int is 41-bit so the products
// stay in range (no 32-bit wraparound).
int t_a2i(void) {
    int i = -20;
    int b = 2147483647;
    int c = -5000000;

    i += 2147483648l; // 2^31; result fits in a 41-bit int
    if (i != 2147483628) {
        return 1;
    }
    if (b != 2147483647) {
        return 2;
    }

    b /= -34359738367l; // -(2^35 - 1); |b| is smaller, so result is 0
    if (b) {
        return 3;
    }
    if (i != 2147483628) {
        return 4;
    }
    if (c != -5000000) {
        return 5;
    }

    c *= 10000l; // -5e10 fits in 41 bits (unlike the 32-bit wrap the book checks)
    if (c != -50000000000l) {
        return 6;
    }

    return 0;
}

// like chapter 11's compound_assign_to_long.c
int t_a2l(void) {
    long l = -34359738368l; // -2^35
    int i = -10;
    l -= i; // convert i to long, then subtract
    if (l != -34359738358l) {
        return 1;
    }
    return 0;
}

int main(void) {
    if (t_chars()) {
        return 1;
    }
    if (t_dbl()) {
        return 2;
    }
    if (t_dblcast()) {
        return 3;
    }
    if (t_uint()) {
        return 4;
    }
    if (t_a2i()) {
        return 5;
    }
    if (t_a2l()) {
        return 6;
    }
    return 0; // success
}
)WP"));
}

TEST_F(Besm6BookTest, Chapter19_WP_AllTypes_FoldExtensionAndTruncation)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant folding of sign extension, zero extension, and truncation.
 * On BESM-6 int/long/long long are all 41-bit, so the book's 64<->32-bit width
 * cases are no-ops with no analogue and are dropped; the meaningful narrowing
 * and extension happens at the 8-bit char boundary.  Plain char is unsigned on
 * BESM-6, so cases that rely on a signed result use `signed char` explicitly.
 * A signed->unsigned widening zero-extends the value's 41-bit pattern, e.g.
 * (unsigned long)(-1000) == 2^41-1000.  Function names are kept distinct within
 * 8 characters.
 * */

/* int -> char/signed char truncation and sign-extension back to int.
 * make sure we actually perform truncation/extension rather than treating
 * chars as full words. */
int t_c_int(void) {
    int i = 257;
    unsigned char c = i;   // 1
    signed char sc = 255;  // -1
    i = 2147483647;        // INT-range value with all low bits set
    signed char sc2 = i;   // -1
    i = -129;              // need to zero the upper bits on widening
    signed char sc3 = i;   // 127
    i = 128;               // need to sign-extend on widening
    signed char sc4 = i;   // -128
    if (c != 1) {
        return 1;  // fail
    }
    if (sc != -1) {
        return 2;  // fail
    }
    if (sc2 != -1) {
        return 3;  // fail
    }
    if (sc3 != 127) {
        return 4;  // fail
    }
    if (sc4 != -128) {
        return 5;  // fail
    }
    return 0;  // success
}

/* int -> unsigned char truncation and zero-extension back to int */
int t_uc_int(void) {
    int i = 767;
    unsigned char uc1 = i;  // 255
    i = 512;
    unsigned char uc2 = i;  // 0
    i = -2147483647;        // INT-range value
    unsigned char uc3 = i;  // 1
    if (uc1 != 255) {
        return 1;  // fail
    }
    if (uc2) {
        return 2;  // fail
    }
    if (uc3 != 1) {
        return 3;  // fail
    }
    return 0;  // success
}

/* signed -> unsigned widening (zero-extend the 41-bit pattern) */
int t_i2ul(void) {
    int i = -1000;
    unsigned long u = (unsigned long)i;  // 2^41 - 1000 == 2199023254552
    if (u != 2199023254552ul) {
        return 1;  // fail
    }
    if (u % 50ul != 2) {
        return 2;  // fail
    }
    return 0;  // success
}

/* unsigned long -> unsigned int is identity here (both 48-bit) */
int t_ul2u(void) {
    unsigned long ul = 281474976710655UL;  // 2^48 - 1
    unsigned int u = (unsigned int)ul;
    if (u != 281474976710655U) {
        return 1;  // fail
    }
    if (u / 20 != 14073748835532U) {
        return 2;  // fail
    }
    return 0;  // success
}

int main(void) {
    if (t_c_int()) {
        return 1;  // fail
    }
    if (t_uc_int()) {
        return 2;  // fail
    }
    if (t_i2ul()) {
        return 3;  // fail
    }
    if (t_ul2u()) {
        return 4;  // fail
    }
    return 0;
}
)WP"));
}

TEST_F(Besm6BookTest, Chapter19_WP_AllTypes_FoldIncrDecrUnsigned)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Propagate ++/-- with unsigned integers (make sure they wrap around correctly).
 * On BESM-6 `unsigned` is 48-bit, so UINT_MAX is 2^48-1 = 281474976710655. */

int target(void) {
    unsigned int u = 0;
    unsigned int u2 = --u;
    unsigned int u3 = u--;

    unsigned int u4 = 281474976710655U;
    unsigned int u5 = u4++;
    unsigned int u6 = ++u4;

    if (!(u == 281474976710654U && u2 == 281474976710655U && u3 == 281474976710655U)) {
        return 1; // fail
    }

    if (!(u4 == 1 && u5 == 281474976710655U && u6 == 1)) {
        return 2; // fail
    }

    return 0; // success
}

int main(void) {
    return target();

}
)WP"));
}

TEST_F(Besm6BookTest, Chapter19_WP_AllTypes_FoldNegativeLongBitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant folding >> with a negative long source value.  On BESM-6 a signed right
 * shift is logical (the shift unit does no sign extension), so the fold matches the
 * backend: the 41-bit pattern of -2^40 (which is 2^40) >> 22 == +262144.
 */

long target(void) {
    return (-1099511627775l - 1) >> 22u;
}

int main(void) {
    if (target() != 262144) {
        return 1;
    }

    return 0; // success
}
)WP"));
}

TEST_F(Besm6BookTest, Chapter19_WP_AllTypes_SignedUnsignedConversion)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant-folding of conversions between signed and unsigned integers,
 * allowing for further copy propagation.
 *
 * On BESM-6 signed int/long are 41-bit and unsigned int/long are 48-bit, so a
 * signed->unsigned conversion zero-extends the value's 41-bit pattern:
 * (unsigned)(-1) == 2^41-1 == 2199023255551.  The reverse (unsigned->signed) is
 * only well-defined when the unsigned value fits in the 41-bit signed range
 * (the wider value would otherwise lose its high bits), so the round-trip case
 * uses an in-range value.  Function names are kept distinct within 8 characters.
 * */

unsigned int t_i2u(void) {
    int i = -1;
    // after constant folding this cast, we can propagate the value of u
    // into the return statement
    unsigned int u = (unsigned)i;  // 2^41 - 1
    return u / 10u;                // 219902325555
}

unsigned long t_l2ul(void) {
    long l = -200l;
    unsigned long ul = (unsigned long)l;  // 2^41 - 200
    return ul / 10;                       // 219902325535
}

int t_i2ucmp(void) {
    int i = -1;
    unsigned int u = (unsigned)i;
    return u > 1000000u;  // 1: 2^41-1 is a large unsigned value
}

int t_rt(void) {
    unsigned int u = 100000u;
    int i = (int)u;  // in-range, well-defined: 100000
    return i + 1;    // 100001
}

int main(void) {
    if (t_i2u() != 219902325555u) {
        return 1;  // fail
    }
    if (t_l2ul() != 219902325535ul) {
        return 2;  // fail
    }
    if (t_i2ucmp() != 1) {
        return 3;  // fail
    }
    if (t_rt() != 100001) {
        return 4;  // fail
    }

    return 0;  // success
}
)WP"));
}

TEST_F(Besm6BookTest, Chapter19_WP_IntOnly_FoldNegativeBitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant folding >> with a negative source value.  On BESM-6 a signed right
 * shift is logical (the shift unit does no sign extension), so the fold matches the
 * backend: the 41-bit pattern of -20000 (2^41 - 20000) >> 3 = 274877904444.
 */

int target(void) {
    return -20000 >> 3;
}

int main(void) {
    if (target() != 274877904444) {
        return 1;
    }

    return 0; // success
}
)WP"));
}

//
// Chapter 20
//

// Adapted for BESM-6.  Plain `char` is unsigned here, so neg_char/not_char use
// `signed char` to keep the sign-extension.  The `a` check expects 2^41-1, not the
// C-conforming ULONG_MAX (2^48-1): BESM-6 deviates from C11 §6.3.1.3p2 — a
// signed→unsigned conversion is a pure reinterpretation with no sign extension, so
// `(unsigned long)id(-1)` keeps the 41-bit signed pattern 0o37777777777777.
TEST_F(Besm6BookTest, Chapter20_AllNoCoal_TypeConversionInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + DBLID + UID + UCID + C1I +
                                            C1U + C1UC + C1L + C1UL + C1D + C14D + R"WP(
int glob;
int test_movsx_src(int i) {
    check_one_int(i - 10, -5);
    long l = 0;
    l = (long)i;
    check_one_long(l, 5l);
    return 0;
}
signed char glob_char = 10;
int test_movsx_dst(void) {
    unsigned long a = id(-1);
    unsigned long b = id(2);
    signed char neg_char = -glob_char;
    signed char not_char = ~glob_char;
    int c = (int)glob_char;
    long d = id(4);
    unsigned int e = (unsigned int)neg_char;
    long f = (long) not_char;
    check_one_ulong(a, 2199023255551ul); // 2^41 - 1 for besm6
    check_one_ulong(b, 2ul);
    check_one_int(c, 10);
    check_one_long(d, 4l);
    check_one_uint(e, -10);
    check_one_long(f, -11);
    return 0;
}
unsigned int glob_uint;
int test_movzx_src(unsigned int u) {
    check_one_uint(u + 10u, 30u);
    long l = (long)u;
    check_one_long(l, 20l);
    return 0;
}
int test_movzx_dst(void) {
    long a = (long)unsigned_id(2000u);
    unsigned long b = (unsigned long)unsigned_id(1000u);
    unsigned long c = (unsigned long)unsigned_id(255u);
    long d = (long)unsigned_id(4294967295U);
    long e = (long)unsigned_id(2147483650u);
    unsigned long f = (unsigned long)unsigned_id(80u);
    check_one_long(a, 2000l);
    check_one_ulong(b, 1000ul);
    check_one_ulong(c, 255ul);
    check_one_long(d, 4294967295l);
    check_one_long(e, 2147483650l);
    check_one_ulong(f, 80ul);
    return 0;
}
int test_movzbq_src(unsigned char c) {
    unsigned char d = c + 1;
    check_one_uchar(d, 13);
    long l = (long)c;
    check_one_long(l, 12);
    return 0;
}
int test_movzb_dst(void) {
    int a = (int)uchar_id(200);
    unsigned int b = (unsigned int)uchar_id(100);
    unsigned long c = (unsigned long)uchar_id(255);
    long d = (long)uchar_id(77);
    long e = (long)uchar_id(125);
    unsigned long f = (unsigned long)uchar_id(80);
    check_one_int(a, 200);
    check_one_uint(b, 100u);
    check_one_ulong(c, 255ul);
    check_one_long(d, 77l);
    check_one_long(e, 125l);
    check_one_ulong(f, 80ul);
    return 0;
}
int test_cvtsi2sd_src(int i) {
    check_one_int(i + 10, 16);
    double d = (double)i;
    check_one_double(d, 6.0);
    return 0;
}
int global_int = 5000;
long global_long = 5005;
int test_cvtsi2sd_dst(void) {
    double d0 = (double)global_int;
    double d1 = (double)(global_long - 4l);
    double d2 = (double)(global_int + 2);
    double d3 = (double)(global_long - 2l);
    double d4 = (double)(global_int + 4);
    double d5 = (double)(global_int + 5);
    double d6 = (double)(global_int + 6);
    double d7 = (double)(global_int + 7);
    double d8 = (double)(global_int + 8);
    double d9 = (double)(global_int + 9);
    double d10 = (double)(global_int + 10);
    double d11 = (double)(global_int + 11);
    double d12 = (double)(global_int + 12);
    double d13 = (double)(global_int + 13);
    double d14 = (double)(global_int + 14);
    global_long = (long)d14;
    check_14_doubles(d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13,
                     5000);
    check_one_int(global_int, 5000);
    check_one_long(global_long, 5014l);
    return 0;
}
double glob_dbl;
int test_cvttsd2si_src(double d) {
    glob_dbl = d + 10.0;
    int i = (int)d;
    check_one_int(i, 7);
    check_one_double(glob_dbl, 17.0);
    return 0;
}
int test_cvttsd2si_dst(void) {
    int a = (int)dbl_id(-200.0);
    long b = (long)dbl_id(-300.0);
    int c = (int)dbl_id(-400.0);
    long d = (long)dbl_id(-500.0);
    int e = (int)dbl_id(-600.0);
    long f = (long)dbl_id(-700.0);
    check_one_int(a, -200);
    check_one_long(b, -300l);
    check_one_int(c, -400);
    check_one_long(d, -500l);
    check_one_int(e, -600);
    check_one_long(f, -700l);
    return 0;
}
int main(void) {
    test_movsx_src(5);
    test_movsx_dst();
    test_movzx_src(20u);
    test_movzx_dst();
    test_movzbq_src(12);
    test_movzb_dst();
    test_cvtsi2sd_src(6);
    test_cvtsi2sd_dst();
    test_cvttsd2si_src(7.0);
    test_cvttsd2si_dst();
    return 0;
}
)WP"));
}
