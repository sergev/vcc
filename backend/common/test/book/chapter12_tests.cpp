//
// Chapter 12 — Unsigned integers: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_12/valid + explicit_casts
// + implicit_casts + type_specifiers + extra_credit + libraries).  Each program
// defines int main(void); b6sim --status prints its return value, and we compare
// program output against the value computed by host cc.
//
// Key architectural fact.  On BESM-6 a machine word is 48 bits and
// semantic/target.c makes every UNSIGNED integer type a single 48-bit word:
// "unsigned int" == "unsigned long" == "unsigned long long", range 0 .. 2^48-1.
// Signed int/long stay 41-bit (-2^40 .. 2^40-1).  The backend has the full set
// of unsigned helpers (b/uadd, b/usub, b/umul, b/udiv, b/umod, b/uneg, and the
// unsigned comparisons b/ult, b/ule, b/ugt, b/uge).
//
// Chapter 12 is written to prove an x86 compiler distinguishes a 32-bit
// "unsigned int" (wraps at 2^32) from a 64-bit "unsigned long" (wraps at 2^64).
// Programs whose values fit both targets are shared as adapted; the rest are in
// their generic LP64 form, and BESM-6 runs its own versions of them (see
// README.md).
//
#include "book_test.h"

// --- valid (run) ------------------------------------------------------------

// A simple unsigned add: 2^31-1 + 2 == 2^31+1, all within 2^48 (no wraparound).
TEST_F(BookTest, Chapter12_Simple)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    unsigned u = 2147483647u;
    return (u + 2u == 2147483649u);
})"));
}

// Different ways to spell unsigned int/long; the initialized definition is the
// last declaration of each, so no tentative clobber.  The for loop wraps below 0
// after 11 iterations on both a 32-bit and a 48-bit unsigned (2^48-1 and 2^32-1
// are both >= 4294967295U, so the < bound exits the loop either way).
TEST_F(BookTest, Chapter12_UnsignedTypeSpecifiers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned u;
int unsigned u;
unsigned int u = 6;

unsigned long ul;
long unsigned ul;
long int unsigned ul;
unsigned int long ul = 4;

int main(void) {
    if (u != 6u) {
        return 1;
    }

    /* redeclare ul several times */
    long extern unsigned ul;
    unsigned long extern ul;
    int extern unsigned long ul;

    if (ul != 4ul) {
        return 2;
    }

    /* use unsigned type specifier in for loop
     * we'll iterate through this loop 11 times before dropping below 0 and
     * wrapping around
     */
    int counter = 0;
    for (unsigned int index = 10; index < 4294967295U; index = index - 1) {
        counter = counter + 1;
    }

    if (counter != 11) {
        return 3;
    }

    return 0;
})"));
}

// Constant promotion: 2^36 takes unsigned (long) type and the -1l comparison goes
// through the unsigned-long common type; the 3ul+4294967293ul == 2^32 stays
// nonzero (no wrap, well under 2^48).  main returns 0.
TEST_F(BookTest, Chapter12_PromoteConstants)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(long negative_one = 1l; // can't use negative static initializers; negate this in main
long zero = 0l;

int main(void) {

    negative_one = -negative_one;
    /* 2^36 can't be represented as an unsigned int,
     * so it will be promoted to an unsigned long;
     * when we compare this to -1l, we'll convert -1l to
     * an unsigned long with value ULONG_MAX
     */
    if (68719476736u >= negative_one) {
        return 1;
    }

    /* The integer constant with value 2^31 + 10
     * is promoted to signed long, not an unsigned int,
     * so negating it gives us a negative signed value.
     */
    if (-2147483658 >= zero) {
        return 2;
    }

    /* constants with ul suffix are always treated as unsigned long, not unsigned int
     * If these constants were interpreted as unsigned ints, addition would wrap around to 0
     */
    if (!(3ul + 4294967293ul)) {
        return 3;
    }

    return 0;
})"));
}

// Regression test mirroring chapter 11's: a zero-extend whose result feeds a long
// and twelve interfering int locals; all values small, main returns 0.
TEST_F(BookTest, Chapter12_RewriteMovzRegression)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int check_12_ints(int start, int a, int b, int c, int d, int e, int f, int g,
                  int h, int i, int j, int k, int l);

unsigned glob = 5000u;

int main(void) {
    long should_spill = (long)glob;

    int one = glob - 4999;
    int two = one + one;
    int three = 2 + one;
    int four = two * two;
    int five = 6 - one;
    int six = two * three;
    int seven = one + 6;
    int eight = two * 4;
    int nine = three * three;
    int ten = four + six;
    int eleven = 16 - five;
    int twelve = six + six;

    check_12_ints(one, two, three, four, five, six, seven, eight, nine, ten,
                  eleven, twelve, 1);

    int thirteen = glob - 4987u;
    int fourteen = thirteen + 1;
    int fifteen = 28 - thirteen;
    int sixteen = fourteen + 2;
    int seventeen = 4 + thirteen;
    int eighteen = 32 - fourteen;
    int nineteen = 35 - sixteen;
    int twenty = fifteen + 5;
    int twenty_one = thirteen * 2 - 5;
    int twenty_two = fifteen + 7;
    int twenty_three = 6 + seventeen;
    int twenty_four = thirteen + 11;

    check_12_ints(thirteen, fourteen, fifteen, sixteen, seventeen, eighteen,
                  nineteen, twenty, twenty_one, twenty_two, twenty_three,
                  twenty_four, 13);

    if (should_spill != 5000l) {
        return -1;
    }
    return 0;
}

int check_12_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int l, int start) {
    int expected = 0;

    expected = start + 0;
    if (a != expected) {
        return expected;
    }
    expected = start + 1;
    if (b != expected) {
        return expected;
    }
    expected = start + 2;
    if (c != expected) {
        return expected;
    }
    expected = start + 3;
    if (d != expected) {
        return expected;
    }
    expected = start + 4;
    if (e != expected) {
        return expected;
    }
    expected = start + 5;
    if (f != expected) {
        return expected;
    }
    expected = start + 6;
    if (g != expected) {
        return expected;
    }
    expected = start + 7;
    if (h != expected) {
        return expected;
    }
    expected = start + 8;
    if (i != expected) {
        return expected;
    }
    expected = start + 9;
    if (j != expected) {
        return expected;
    }
    expected = start + 10;
    if (k != expected) {
        return expected;
    }
    expected = start + 11;
    if (l != expected) {
        return expected;
    }

    return 0;
})"));
}

// --- DISABLED: backend limitation (not unsigned target semantics) ----------

// Madlen symbols are truncated to 8 characters, so the two file-scope globals
// one_hundred and one_hundred_ulong both become ONE*HUND and collide
// ("twice-described identifier").  A backend name-length limitation, unrelated
// to unsigned width.
TEST_F(BookTest, Chapter12_Comparisons)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int small_uint = 100u;
unsigned int large_uint = 4294967294u; // interpreted as a signed int, this would be -2

unsigned long small_ulong = 100ul;
unsigned long large_ulong = 4294967294ul; // this would have the same value as a signed long

int main(void) {
    // compare unsigned ints (result would be different if interpreted as signed)

    /* False comparisons */
    if (large_uint < small_uint)
        return 1;
    if (large_uint <= small_uint)
        return 2;
    if (small_uint >= large_uint)
        return 3;
    if (small_uint > large_uint)
        return 4;
    /* True comparisons */
    if (!(small_uint <= large_uint))
        return 5;
    if (!(small_uint < large_uint))
        return 6;
    if (!(large_uint > small_uint))
        return 7;
    if (!(large_uint >= small_uint))
        return 8;

    // compare unsigned longs (result would be the same if interpreted as signed)
    /* False comparisons: */
    if (large_ulong < small_ulong)
        return 9;
    if (large_ulong <= small_ulong)
        return 10;
    if (small_ulong >= large_ulong)
        return 11;
    if (small_ulong > large_ulong)
        return 12;
    /* True comparisons */
    if (!(small_ulong <= large_ulong))
        return 13;
    if (!(small_ulong < large_ulong))
        return 14;
    if (!(large_ulong > small_ulong))
        return 15;
    if (!(large_ulong >= small_ulong))
        return 16;

    return 0;
})"));
}

// A tentative redeclaration (signed int static i;) follows the initialized
// definition (int static signed i = 5;), and likewise int long l; follows
// long l = 7;.  With the chapter-10 "tentative clobber" bug fixed (task #19),
// the trailing redeclaration no longer re-emits an uninitialized toplevel.
TEST_F(BookTest, Chapter12_SignedTypeSpecifiers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(static int i;
signed extern i;
int static signed i = 5;
signed int static i;

long signed l;
long l = 7;
int long l;
signed long int l;

int main(void) {
    int signed extern i;
    extern signed long l;

    if (i != 5) {
        return 1;
    }

    if (l != 7) {
        return 2;
    }

    /* use signed type specifier in for loop */
    int counter = 0;
    for (signed int index = 10; index > 0; index = index - 1) {
        counter = counter + 1;
    }

    if (counter != 10) {
        return 3;
    }

    return 0;
})"));
}

TEST_F(BookTest, Chapter12_ArithmeticOps)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui_a;
unsigned int ui_b;

unsigned long ul_a;
unsigned long ul_b;

int addition(void) {
    return (ui_a + 2147483653u == 2147483663u);
}

int subtraction(void) {
    return (ul_a - ul_b == 281474976709000ul);
}

int multiplication(void) {
    return (ui_a * ui_b == 3221225472u);
}

int division(void) {
    return (ui_a / ui_b == 0);
}

int div_large(void) {
    return (ui_a / ui_b == 2);
}

int div_lit(void) {
    return (ul_a / 5ul == 219902325555ul);
}

int remaind(void) {
    return (ul_b % ul_a == 5ul);
}
int complement(void) {
    return (~ui_a == 0);
}

int main(void) {

    ui_a = 10u;
    if (!addition()) {
        return 1;
    }

    ul_a = 281474976710000ul;
    ul_b = 1000ul;
    if (!subtraction()) {
        return 2;
    }

    ui_a = 1073741824u;
    ui_b = 3u;
    if (!multiplication()) {
        return 3;
    }

    ui_a = 100u;
    ui_b = 4294967294u;

    if (!division()) {
        return 4;
    }

    ui_a = 4294967294u;
    ui_b = 2147483647u;
    if (!div_large()) {
        return 5;
    }

    ul_a = 1099511627775ul;
    if (!div_lit()) {
        return 6;
    }

    ul_a = 100ul;
    ul_b = 281474976710605ul;
    if (!remaind()) {
        return 7;
    }

    ui_a = 281474976710655U;
    if (!complement()) {
        return 8;
    }

    return 0;
})"));
}

// Unsigned arithmetic wraps around modulo 2^32 (unsigned int) or 2^64 (unsigned
// long).
TEST_F(BookTest, Chapter12_ArithmeticWraparound)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui_a;
unsigned int ui_b;

unsigned long ul_a;
unsigned long ul_b;

int addition(void) {
    return ui_a + ui_b == 0u;
}

int subtraction(void) {
    return (ul_a - ul_b == 18446744073709551606ul);
}

int neg(void) {
    return -ul_a == 18446744073709551615UL;
}

int main(void) {
    ui_a = 4294967293u;
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

// a = -a wraps at the unsigned long modulus 2^64.
TEST_F(BookTest, Chapter12_Locals)
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

    if (a != 18446744065119617024ul) {
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

// Logical operators on unsigned values; ul is seeded nonzero (< 2^48) so not(ul)==0.
TEST_F(BookTest, Chapter12_Logical)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int not(unsigned long ul) {
    return !ul;
}

int if_cond(unsigned u) {
    if (u) {
        return 1;
    }
    return 0;
}

int and(unsigned long ul, int i) {
    return ul && i;
}

int or(int i, unsigned u) {
    return i || u;
}

int main(void) {
    unsigned long ul = 123456789012345ul; // nonzero, < 2^48
    unsigned int u = 2147483648u; // 2^31
    unsigned long zero = 0l;
    if (not(ul)) {
        return 1;
    }
    if (!not(zero)) {
        return 2;
    }
    if(!if_cond(u)) {
        return 3;
    }
    if(if_cond(zero)) {
        return 4;
    }

    if (and(zero, 1)) {
        return 5;
    }

    if (!or(1, u)) {
        return 6;
    }

    return 0;
})"));
}

// x near the top of the 48-bit unsigned range (2^48 - 56).
TEST_F(BookTest, Chapter12_StaticVariables)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(static unsigned long x = 281474976710600ul; // 2^48 - 56

unsigned long zero_long;
unsigned zero_int;

int main(void)
{
    if (x != 281474976710600ul)
        return 0;
    x = x + 10;
    if (x != 281474976710610ul)
        return 0;
    if (zero_long || zero_int)
        return 0;
    return 1;
})"));
}

// Usual arithmetic conversions: long is wider than unsigned int, so uint vs long
// compares as long; int vs unsigned long compares as unsigned long.
TEST_F(BookTest, Chapter12_CommonType)
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
    return (result == 4294967295l); // (uint)(-1) = 2^32-1
}

int main(void) {

    if (!int_gt_uint(-100, 100u)) {
        return 1;
    }

    if (!(int_gt_ulong(-1, 18446744073709551606ul))) {
        return 2;
    }

    if (!uint_gt_long(100u, -100l)) {
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

// Conversions by assignment, argument passing and return: truncation, sign and
// zero extension.
TEST_F(BookTest, Chapter12_ConvertByAssignment)
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
    if (!check_int(9223372036854775813ul, 5)) { // 2^63 + 5 truncates to 5
        return 1;
    }

    if (!check_long(2147483658u, 2147483658l)) {
        return 2;
    }

    if (!check_ulong(-1, 18446744073709551615UL)) {
        return 3;
    }

    if (return_extended_uint(2147483658u) != 2147483658l) {
        return 4;
    }

    if (return_extended_int(-1) != 18446744073709551615UL) {
        return 5;
    }

    long l = return_truncated_ulong(1125902054326372ul); // 2^50 + 2^31 + 100 truncates to INT_MIN + 100
    if (l != -2147483548l) {
        return 6;
    }

    if (!extend_on_assignment(2147483658u, 2147483658l)){
        return 7;
    }

    int i = 4294967196u; // 2^32 - 100 converts to -100
    if (i != -100) {
        return 8;
    }

    return 0;
})"));
}

// Static initializers are converted to the variable's type at compile time.
TEST_F(BookTest, Chapter12_StaticInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int u = 1152921506754330636l; // 2^60 + 2^31 + 12
int i = 2147483650u;
long l = 9223372036854775900u; // note: this has type unsigned long
long l2 = 2147483650u;
unsigned long ul = 4294967294u;
unsigned long ul2 = 9223372036854775798l;
int i2 = 9223372039002259606ul; // 2^63 + 2^31 + 150
unsigned ui2 = 9223372039002259606ul;

int main(void)
{
    if (u != 2147483660u)
        return 1;
    if (i != -2147483646)
        return 2;
    if (l != -9223372036854775716l)
        return 3;
    if (l2 != 2147483650l)
        return 4;
    if (ul != 4294967294ul)
        return 5;
    if (ul2 != 9223372036854775798ul)
        return 6;
    if (i2 != -2147483498)
        return 7;
    if (ui2 != 2147483798u)
        return 8;
    return 0;
})"));
}

// (signed)ui == -96, which sign-extends to unsigned long.
TEST_F(BookTest, Chapter12_ChainedCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui = 4294967200u; // 2^32 - 96

int main(void) {

    if ((long) (signed) ui != -96l)
        return 1;

    if ((unsigned long) (signed) ui != 18446744073709551520ul) // 2^64 - 96
        return 2;

    return 0;
})"));
}

// int to unsigned long sign-extends: (unsigned long)(-10) is 2^64-10.
TEST_F(BookTest, Chapter12_Extension)
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

    if (!int_to_ulong(-10, 18446744073709551606ul)) {
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

// Casting unsigned long through a 32-bit type truncates; back to unsigned long, an
// unsigned int zero-extends and an int sign-extends.
TEST_F(BookTest, Chapter12_RoundTripCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned long a = 8589934580ul; // 2^33 - 12

int main(void) {

    unsigned long b = (unsigned long) (unsigned int) a;

    if (b != 4294967284ul)
        return 1;

    b = (unsigned long) (signed int) a;
    if (b != 18446744073709551604ul)
        return 2;

    return 0;
})"));
}

// Signed/unsigned same-size conversions keep the bit pattern: (ulong)(-1000) is
// 2^64-1000.
TEST_F(BookTest, Chapter12_SameSizeConversion)
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

    if (!long_to_ulong(-1000l, 18446744073709550616ul)) {
        return 3;
    }

    if (!ulong_to_long(18446744073709550616ul, -1000l)) {
        return 4;
    }

    return 0;
})"));
}

// On BESM-6 unsigned int and unsigned long are both 48-bit, so (unsigned int)ul is
// identity; the real truncation is unsigned(48) -> signed int(41), dropping bits 48-42.
TEST_F(BookTest, Chapter12_Truncate)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int ulong_to_int(unsigned long ul, int expected) {
    int result = (int) ul;
    return (result == expected);
}

int ulong_to_uint(unsigned long ul, unsigned expected) {
    return ((unsigned int) ul == expected);
}

int long_to_uint(long l, unsigned int expected) {
    return (unsigned int) l == expected;
}

int main(void) {
    if (!long_to_uint(100l, 100u)) {
        return 1;
    }

    if (!long_to_uint(-96l, 2199023255456u)) { // 41-bit pattern of -96 = 2^41 - 96
        return 2;
    }

    if (!ulong_to_int(100ul, 100)) {
        return 3;
    }

    if (!ulong_to_uint(100ul, 100u)) {
        return 4;
    }

    if (!ulong_to_uint(4294967200ul, 4294967200u)) {
        return 5;
    }

    if (!ulong_to_int(2199023255456ul, -96)) { // 2^41 - 96 -> -96
        return 6;
    }

    if (!ulong_to_uint(281474976710560ul, 281474976710560u)) { // 2^48 - 96, identity
        return 7;
    }

    return 0;
})"));
}

// 48-bit unsigned: disjoint low/high operands; ul stays within the 41 bits that -1 sets.
TEST_F(BookTest, Chapter12_BitwiseUnsignedOps)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = 16777215u; // 2^24 - 1, low bits set
    unsigned long ul = 1099511627776ul; // 2^40, a single high bit

    if ((ui & ul) != 0)
        return 1;

    if ((ui | ul) != 1099528404991ul) // 2^40 + 2^24 - 1
        return 2;

    signed int i = -1;
    if ((i & ul) != ul)
        return 3;

    if ((i | ul) != i)
        return 4;

    return 0;
})"));
}

// ui = -1u is 2^32-1; shifts of unsigned values are logical.
TEST_F(BookTest, Chapter12_BitwiseUnsignedShift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = -1u;  // 2^32 - 1

    if ((ui << 2l) != 4294967292) { // 2^32 - 4
        return 1;
    }

    if ((ui >> 2) != 1073741823) { // 2^30 - 1
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

// x /= -10l computes in long: (2^32 - 1) / -10 = -429496729, converted back to
// unsigned int.
TEST_F(BookTest, Chapter12_CompoundAssignUint)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    unsigned int x = -1u;
    x /= -10l;
    return (x == 3865470567u);
})"));
}

// Signed >> is arithmetic (so -2 >>= 3u gives -1); 2^64-1 <<= 44 keeps only the top
// 20 bits.
TEST_F(BookTest, Chapter12_CompoundBitshift)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    int i = -2;
    i >>= 3u;

    if (i != -1) {
        return 1;
    }

    unsigned long ul = 18446744073709551615UL;  // 2^64 - 1
    ul <<= 44;                             // 0 out lower 44 bits
    if (ul != 18446726481523507200ul) {
        return 2;  // fail
    }
    return 0;  // success
})"));
}

// Compound bitwise operators on unsigned operands of mixed widths.
TEST_F(BookTest, Chapter12_CompoundBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    unsigned long ul = 18446460386757245432ul;
    // -1000 sign-extends to unsigned long.
    ul &= -1000;
    if (ul != 18446460386757244952ul ) {
        return 1; // fail
    }

    ul |= 4294967040u; // 0xffff_ff00 - zero-extended to unsigned long

    if (ul != 18446460386824683288ul ) {
        return 2; // fail
    }

    int i = 123456;

    unsigned int ui = 4042322160u;
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

// ui++ at UINT_MAX (2^32-1) wraps to 0.
TEST_F(BookTest, Chapter12_PostfixPrecedence)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = 4294967295U; // 2^32 - 1

    if (((unsigned long)ui++) != 4294967295U) {
        return 1; // fail
    }
    if (ui) {
        return 2; // fail - ui should be 0 after update
    }
    return 0; // success
})"));
}

// 48-bit switch: case 2^35+10 is not truncated, so call it with that exact value.
TEST_F(BookTest, Chapter12_SwitchUint)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int switch_on_uint(unsigned int ui) {
    switch (ui) {
        case 5u:
            return 0;
        case 4294967286l:
            return 1;
        case 34359738378ul:
            return 2;
        default:
            return 3;
    }
}

int main(void) {
    if (switch_on_uint(5) != 0)
        return 1;
    if (switch_on_uint(4294967286) != 1)
        return 2;
    if (switch_on_uint(34359738378) != 2)
        return 3;
    return 0;
})"));
}

// ++/-- on unsigned values wrap around.
TEST_F(BookTest, Chapter12_UnsignedIncrDecr)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int i = 0;

    if (i-- != 0) {
        return 1;
    }
    if (i != 4294967295U) {// wraparound from 0 to UINT_MAX
        return 2;
    }

    if (--i != 4294967294U) {
        return 3;
    }
    if (i != 4294967294U) {
        return 4;
    }

    unsigned long l = 18446744073709551614UL;
    if (l++ != 18446744073709551614UL) {
        return 5;
    }
    if (l != 18446744073709551615UL) { 
        return 6;
    }
    if (++l != 0) { // wraparound from ULONG_MAX to 0
        return 7;
    }
    if (l != 0) {
        return 8;
    }
    return 0; // success
})"));
}

// libraries: many unsigned args across the calling convention. Values >= 2^48
// (orig 2^63 / 2^64-range) substituted to fit the BESM-6 48-bit unsigned word;
// c keeps the "max unsigned" check as UINT_MAX (2^48-1).
TEST_F(BookTest, Chapter12_UnsignedArgsLibrary)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int accept_unsigned(unsigned int a, unsigned int b, unsigned long c, unsigned long d,
                 unsigned int e, unsigned int f, unsigned long g, unsigned int h,
                 unsigned long i);

int main(void) {
    return accept_unsigned(1, 4294967295u, 281474976710655u, 140737488355328u, 2147483648u, 0, 123456, 2147487744u, 200000000000000u);
}

int accept_unsigned(unsigned int a, unsigned int b, unsigned long c, unsigned long d,
                 unsigned int e, unsigned int f, unsigned long g,
                 unsigned int h, unsigned long i) {
    if (a != 1u) {
        return 1;
    }
    if (b != 4294967295u) {
        return 2;
    }
    if (c != 281474976710655u) {
        return 3;
    }
    if (d != 140737488355328u) {
        return 4;
    }
    if (e != 2147483648u) {
        return 5;
    }
    if (f != 0u) {
        return 8;
    }
    if (g != 123456u) {
        return 9;
    }
    if (h != 2147487744u) {
        return 10;
    }
    if (i != 200000000000000u) {
        return 11;
    }
    return 0;
})"));
}

// libraries: unsigned global read through accessors with uint->uint / uint->int
// / uint->long conversions. Dropped the orig's `ui = -1` -> 2^32-1 wraparound (a
// 32/64-bit boundary with no BESM-6 analogue: unsigned int is 48-bit and long is
// 41-bit) in favour of an in-range value that round-trips exactly through all
// three accessors.
TEST_F(BookTest, Chapter12_UnsignedGlobalVarLibrary)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(extern unsigned int ui;
unsigned int return_uint(void);
int return_uint_as_signed(void);
long return_uint_as_long(void);

int main(void) {
    if (ui != 4294967200u)
        return 0;

    ui = 1000000000u;

    if (return_uint() != 1000000000u)
        return 0;

    if (return_uint_as_signed() != 1000000000)
        return 0;

    if (return_uint_as_long() != 1000000000l)
        return 0;

    return 1;
}

unsigned int ui = 4294967200u;

unsigned int return_uint(void) {
    return ui;
}

int return_uint_as_signed(void) {
    return ui;
}

long return_uint_as_long(void) {
    return ui;
})"));
}
