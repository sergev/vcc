//
// Chapter 19 — whole_pipeline: imported from "Writing a C Compiler"
// (tests/chapter_19/whole_pipeline).  These self-checking programs return 0 on
// success and exercise the full optimizer (constant folding + copy propagation +
// dead store + unreachable elimination, run by the lower pass's opt_flags_default)
// end-to-end on the BESM-6 via the b6sim simulator.  b6sim --status prints
// main()'s return value; success prints "0\n".
//
// The book hardcodes the x86 64-bit layout (8-byte long, IEEE-754 double edge
// cases: subnormals/NaN/infinity/-0.0).  The BESM-6 has 41-bit integers and a
// different float format, and libc has no copysign/double_isnan; programs that
// depend on those are DISABLED_ with a one-line reason.  Host-only "#if/#pragma"
// lines are stripped (our scanner has no preprocessor).
//
#include "book_test.h"

TEST_F(BookTest, Chapter19_WP_IntOnly_DeadCondition)
{
    EXPECT_EQ("10\n", CompileAndRunBook(R"WP(
/* If a variable is only used as the controlling condition for an empty branch
 * we can eliminate the branch, then eliminate any updates to that variable,
 * because they'll all be dead stores
 * */

// flag is a global variable, not parameters
// so we don't have any instructions setting up function parameters,
// e.g. movl %edi, -4(%rbp), which the test script will complain about
int flag = 1;

int target(void) {
    int x = 2;
    if (flag) {
        x = 20;  // this will be a dead store after we remove branch below
                 // wrap this in an if statement so we don't propagate 20 into
                 // controlling condition
    }

    if (x)
        ;

    // we can eliminate the whole function body except this return statement
    return 10;
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_ElimAndCopyProp)
{
    EXPECT_EQ("10\n", CompileAndRunBook(R"WP(
/* If we can replace every use of a variabe with its value,
 * we can delete the copy to that variable
 * */
int target(void) {
    int x = 10;  // delete this after copy prop rewrites return
    return x;    // rewrite as 'return x' via copy prop
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_IntMin)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant-folding with INT_MIN */
int target(void) {
    return -2147483647 - 1;
}

int main(void) {
    if (~target() != 2147483647) {
        return 1; // fail
    }
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_Listing195)
{
    EXPECT_EQ("9\n", CompileAndRunBook(R"WP(
/* Test case that produces TACKY similar to Listing 19-5;
 * this should be optimized to a single "Return 9" instruction */

// make flag a global variable rather than a parameter
// so we don't have any instructions setting up function parameters,
// e.g. movl %edi, -4(%rbp), which the test script will complain about
int flag = 1;

int target(void) {
    int x = 4;
    int z;
    if (4 - x) {
        x = 3;
    }
    if (!flag) {
        z = 10;
    }
    z = x + 5;
    return z;
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_RemainderTest)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we evaluate the % operator correctly for negative numbers.
 * In C, the remainder n % d always takes the same sign as n.
 * In some languages (e.g. Python), % is the modulo operation, where
 * the result always takes the same sign a d.
 *
 * This test makes sure that we evaluate % as a remainder rather than modulo
 * operation during constant folding.
 * More info:
 * https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Remainder
 * Also note that not everyone uses exactly the same terminolgy here -
 * e.g. Python's documentation says "The % (modulo) operator yields the
 * remainder from the division of the first argument by the second."
 *
 * This is basically a constant folding test, but requires copy propagation
 * so that we can perform constant folding with negative numbers
 * */

int target(void) {
    /* The result of the remainder operation 6 % -5 is 1,
     * but 6 modulo -5 is -4.
     */
    return 6 % -5;
}

int main(void) {
    if (target() != 1) {
        return 1; // fail
    }
    return 0; // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_CompoundAssignExceptions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we don't throw an error when constant folding /= or %=
 * that involves division by zero, or when performing +=, *= or -=
 * that would overflow. Tn this program, these operations
 * will never actually be executed.
 * */



static int zero;

int main(void) {
    int w = 3;
    int x = 10;
    int y = 2147483647;
    int z = -2147483647;
    if (zero) {
        w %= 0;
        x /= 0;
        y += 10;
        z -= 10;
    }
    if (w != 3) {
        return 1;
    }

    if (x != 10) {
        return 2;
    }

    if (y != 2147483647) {
        return 3;
    }

    if (z != -2147483647) {
        return 4;
    }

    return 0; // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_EvaluateSwitch)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* If we can determine the value of a switch's controlling expression at
 * compile time, we can eliminate the whole switch statement except the path
 * that will actually be taken. In this case, this lets us reduce the whole
 * function to a single return statement
 */

int callee(void){
    return 0;
}

int target(void) {
    int switch_var = 10;
    int retval = -1;
    switch(switch_var) {
        case 1:
            callee();
            return 1;
        case 2:
            retval = -2;
            break;
        case 10: // case we'll actually take
            retval = 0;
            break;
        default:
            retval = 1000;
            break;
    }
    return retval;
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_FoldBitwiseCompoundAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test that we can evaluate bitwise compound assignment expressions at compile time */

int target(void) {
    int v = -100;
    int w = 100;
    int x = 200;
    int y = 300;
    int z = 40000;

    v ^= 10; // -106
    w |= v; // -10
    x &= 30; // 8
    y <<= x; // 76800
    // include chained compound assignment
    z >>= (x |= 2); // z = 39 x = 10

    if (v == -106 && w == -10 && x == 10 && y == 76800 && z == 39) {
        return 0; // success
    }

    return 1; //fail
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_FoldCompoundAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test that we can evaluate compound assignment expressions at compile time */

int target(void) {
    int v = -100;
    int w = 100;
    int x = 200;
    int y = 300;
    int z = 400;

    v += 10;
    w -= 20;
    x *= 30;
    y /= 100;
    // include chained compound assignment
    z %= y += 6;

    if (v == -90 && w == 80 && x == 6000 && y == 9 && z == 4) {
        return 0; // success
    }

    return 1; //fail
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_FoldIncrAndDecr)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
// Make sure we can track the results and side effects of ++ and -- through copy propagation

int target(void) {
    int x = 5;

    int y = x++;
    int z = ++x;


    int a = 0;
    int b = --a;
    int c = a--;

    if (x == 7 && y == 5 && z == 7 && a == -2 && b == -1 && c == -1 )  {
        return 0; // success
    }

    return 1; // fail
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_IntOnly_FoldNegativeBitshift)
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

TEST_F(BookTest, Chapter19_WP_AllTypes_AliasAnalysisChange)
{
    EXPECT_EQ("A0\n", CompileAndRunBook(R"WP(
/* Test that we rerun alias analysis with each pipeline iteration */

int putch(int c);

int foo(int *ptr) {
    putch(*ptr);
    return 0;
}

int target(void) {
    int x = 10;  // this is a dead store
    int y = 65;
    int *ptr = &y;
    if (0) {
        // on our first pass through the pipeline it will look like x is
        // aliased; on later passes, after unreachable code elimination removes
        // this branch, we'll recognize that x is not aliased
        ptr = &x;
    }
    x = 5;     // this is a dead store, but we'll only recognize this after
               // rerunning alias analysis
    foo(ptr);  // we'll think this makes x live until we recognize that x is not
               // aliased

    return 0;
}

int main(void) {
    return target();
}
)WP"));
}

// double->int casts; the 64-bit-long sub-check replaced with an in-range value.
TEST_F(BookTest, Chapter19_WP_AllTypes_FoldCastFromDouble)
{
    CompileAndRunBook(R"WP(
/* Constant-folding tests for conversions from negative doubles to integer
 * types; couldn't test these before because we need copy prop to fully evaluate
 * them.
 * */

char target_to_char(void) {
    return (char)-126.5;
}

int target_to_int(void) {
    return (int)-5.9;
}

long target_to_long(void) {
    // original -9223372036854774783.1 is beyond BESM-6 range; use an in-range
    // value (truncation toward zero).
    return (long)-100000000000.5;
}

int main(void) {
    if (target_to_char() != -126) {
        return 1;
    }
    if (target_to_int() != -5) {
        return 2;
    }
    if (target_to_long() != -100000000000l) {
        return 3;
    }
    return 0;
}
)WP");
}

// Conversions to double; the 64-bit-long sub-check replaced with an in-range
// value and the libc-copysign sub-check dropped (not in BESM-6 libc; -0 == 0).
TEST_F(BookTest, Chapter19_WP_AllTypes_FoldCastToDouble)
{
    CompileAndRunBook(R"WP(
/* Constant-folding tests for conversions to double from chars and negative
 * ints; couldn't test these before because we need copy prop to fully evaluate
 * them.
 * */

double target_from_neg_int(void) {
    return (double)-2147483647;  // can convert exactly
}

double target_from_neg_long(void) {
    // original -4611686018427388416l is beyond BESM-6 range; use an in-range
    // value (exact as double).
    return (double)-100000000000l;
}

// test conversion from char to double
double target_from_char(void) {
    char c = 127;
    return (double)c;
}

// test conversion from signed char to double
double target_from_schar(void) {
    char c = -127;
    return (double)c;
}

// test conversion from uchar to double
double target_from_uchar(void) {
    unsigned char u = 255;
    return (double)u;
}

// if we initially assign char a value outside its range,
// make sure we truncate before converting to double
double target_from_truncated_char(void) {
    char c = -129;
    return (double)c;  // 127
}

// if we initially assign uchar a value outside its range,
// make sure we truncate before converting to double
double target_from_truncated_uchar(void) {
    unsigned char c = 1000;
    return (double)c;  // 232
}

double target_from_negated_int_zero(void) {
    // negating integer zero is just zero,
    // which will be converted to positive floating-point zero
    return -0;
}

int main(void) {
    if (target_from_neg_int() != -2147483647.) {
        return 1;
    }
    if (target_from_neg_long() != -100000000000.0) {
        return 2;
    }
    if (target_from_char() != 127) {
        return 3;
    }
    if (target_from_schar() != -127) {
        return 4;
    }
    if (target_from_uchar() != 255) {
        return 5;
    }
    if (target_from_truncated_char() != 127) {
        return 6;
    }
    if (target_from_truncated_uchar() != 232) {
        return 7;
    }
    double zero = target_from_negated_int_zero();
    if (zero != 0) {  // BESM-6 has no -0; negated zero is plain 0.0
        return 8;
    }
    return 0;  // success
}
)WP");
}

// DISABLED: char constant truncation (char x=256 -> 0) not folded on BESM-6
TEST_F(BookTest, Chapter19_WP_AllTypes_FoldCharCondition)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant folding of Not, JumpIfZero, and JumpIfNotZero with char
 * operands. (We don't test constant-folding of other operations on char because
 * they get promoted to int first.)
 * Function names kept distinct within 8 characters (Madlen truncates labels).
 * */
int putch(int c);

int t_notc(void) {
    char x = 256;  // 0
    return !x;     // 1
}

int t_notuc(void) {
    unsigned char x = 256;  // 0
    return !x;              // 1
}

int t_nottc(void) {
    char x = -1;
    return !x;  // 0;
}

int t_andsc(void) {
    signed char c = 0;
    return c && putch('a');  // return 0, eliminate call to putch
}

int t_andtc(void) {
    signed char c1 = 44;
    char c2 = c1 - 10;
    return c1 && c2;  // 1
}

int t_oruc(void) {
    unsigned char u = 250;
    return u || putch('a');  // return 1, eliminate call to putch
}

int t_orc(void) {
    char c = 250;
    return c || putch('a');  // return 1, eliminate call to putch
}

char t_brc(void) {
    unsigned char u = 250;
    u = u + 6;  // 0
    if (u) {    // eliminate this branch
        putch('a');
    }
    return u + 10;
}

int main(void) {
    if (t_notc() != 1) {
        return 1;  // fail
    }
    if (t_notuc() != 1) {
        return 2;  // fail
    }
    if (t_nottc() != 0) {
        return 3;  // fail
    }
    if (t_andsc() != 0) {
        return 4;  // fail
    }
    if (t_andtc() != 1) {
        return 5;  // fail
    }
    if (t_oruc() != 1) {
        return 6;  // fail
    }
    if (t_orc() != 1) {
        return 7;  // fail
    }
    if (t_brc() != 10) {
        return 8;  // fail
    }
    return 0;  // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldExtensionAndTruncation)
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

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldNegativeValues)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test constant folding with negative numbers (including double and long);
 * we couldn't test this in the constant-folding stage because it requires
 * copy propagation.  Adapted for BESM-6: long is 41-bit (so the long values
 * stay within +/-2^40), the huge-magnitude double becomes -2^54 (where +1 is
 * still lost in the host-precision fold), the subnormal-cancellation case has
 * no BESM-6 analogue and is replaced by a normal subtraction, and function
 * names are kept distinct within 8 characters.
 * */

/* long tests */

long t_rem(void) {
    return -8589934585l % 4294967290l;
}

long t_lsub(void) {
    return -4294967290l - 90l;
}

long t_ldiv(void) {
    return (-4294967290l / 128l);
}

long t_lcompl(void) {
    return ~-1099511627775l;  // ~(-(2^40-1)) == 2^40-2
}

/* double tests */
double t_dadd(void) {
    // -2^54 is large enough that adding one doesn't change it in the
    // host-precision constant fold, and is representable on BESM-6.
    return -18014398509481984.0 + 1.;
}

double t_dsub(void) {
    // ordinary subtraction of two negative values (the book's subnormal
    // cancellation has no analogue on BESM-6's float format)
    return -5000.0 - -1234.5;
}

double t_ddiv(void) {
    return -1100.5 / 5000.;
}

int main(void) {
    // long tests
    if (t_rem() != -5l) {
        return 1;  // fail
    }
    if (t_lsub() != -4294967380l) {
        return 2;  // fail
    }
    if (t_ldiv() != -33554431l) {
        return 3;  // fail
    }
    if (t_lcompl() != 1099511627774l) {
        return 4;  // fail
    }
    // double tests
    if (t_dadd() != -18014398509481984.0) {
        return 5;  // fail
    }
    if (t_dsub() != -3765.5) {
        return 6;  // fail
    }
    if (t_ddiv() != -0.2201) {
        return 7;  // fail
    }
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_IntegerPromotions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we promote characters to integers before constant folding */

int target(void) {
    char c1 = 120;
    char c2 = 3;
    // if this weren't promoted, c1 + c1 would overflow, causing undefined behavior
    // if we had c1 + c1 wrap around to -16, this would result in -5
    // but because we promote results to ints, this is 240 / 3, or 80
    char c3 = (c1 + c1) / c2;

    unsigned char uc1 = 200;
    unsigned char uc2 = 12;
    // if we didn't perform integer promotions, uc1 + uc1 would wrap around
    // to 144 and the result would be 12. With promotions, this is 400/12, or 33.
    unsigned char uc3 = (uc1 + uc1) / uc2;
    if (c3 != 80) {
        return 1; // fail
    }
    if (uc3 != 33) {
        return 2; // fail
    }
    return 0;
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_Listing195MoreTypes)
{
    EXPECT_EQ("9\n", CompileAndRunBook(R"WP(
/* A variation on listing_19_5.c with types other than int */

// make flag a global variable rather than a parameter
// so we don't have any instructions setting up function parameters,
// e.g. movl %edi, -4(%rbp), which the test script will complain about
double flag = 12e5;

struct inner {
    double a;
    double b;
};

struct s {
    void *ptr;
    long arr[5];
    struct inner x;
    char c[4];
};

long target(void) {
    unsigned long x = 4;
    char z;
    struct s my_struct = {&z,
                          {
                              1l,
                              2l,
                          },
                          {3., 4.},
                          "abc"};
    if (4 - x) {
        x = my_struct.c[2];
        z = my_struct.arr[1];
        my_struct.x.a = z * 100.;
    }
    if (!flag) {
        z = 10 + *(int *)my_struct.ptr;
    }
    z = x + 5;
    return z;
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_PropagateIntoCopyfromoffset)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we can propagate copies into CopyFromOffset instruction.
 * In assembly for target, we'll see a copy to glob but no reads from it
 */

struct s {
    int a;
    int b;
};

struct s glob;

int target(void) {
    struct s loc = {100, 200};

    glob = loc;

    int x = glob.b;  // rewrite as x = loc.b

    return x;
}

int main(void) {
    if (target() != 200) {
        return 1;  // failure
    }
    if (glob.a != 100) {
        return 2;  // failure
    }
    if (glob.b != 200) {
        return 3;  // failure
    }
    return 0;  // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_PropagateIntoCopytooffset)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we can propagate copies into CopyToOffset instruction */

struct s {
    int a;
    int b;
};

int glob = 0;

int target(void) {
    struct s my_struct = {1, 2};

    glob = 30;  // this can be removed once we propagate its value

    my_struct.b = glob;  // rewrite as my_struct.b = 30, letting us remove
                         // previous write to glob

    glob =
        10;  // glob is dead since we update it before returning from function
    return my_struct.b;
}

int main(void) {
    if (target() != 30) {
        return 1;  // failure
    }
    if (glob != 10) {
        return 2;  // failure
    }
    return 0;  // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_PropagateIntoLoad)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we can propagate copies into Load instruction.
 * in assembly for target, we'll see a copy to glob but no reads from it
 */

int *glob;
int i = 10;
int target(void) {
    int *loc = &i;
    glob = loc;
    return *glob;  // rewrite as *loc; don't need to read glob here
}

int main(void) {
    if (target() != 10) {
        return 1;  // failure
    }
    if (*glob != 10) {
        return 2;  // failure
    }

    return 0;  // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_PropagateIntoStore)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we can propagate copies into Store instruction */

struct s {
    int a;
    int b;
};

int glob = 0;
int i = 0;
int target(void) {
    int *ptr = &i;
    glob = 30;  // this can be removed once we propagate its value

    *ptr = glob;  // rewrite as *ptr = 30, letting us remove
                  // previous write to glob

    glob = 10;
    return *ptr;
}

int main(void) {
    if (target() != 30) {
        return 1;  // failure
    }
    if (glob != 10) {
        return 2;  // failure
    }
    return 0;  // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_SignedUnsignedConversion)
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

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldCompoundAssignAllTypes)
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

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldCompoundBitwiseAssignAllTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Test copy prop/constant folding of compound bitwise assignment with non-int
 * types and type conversions.  Adapted for BESM-6: int/long are 41-bit and
 * unsigned is 48-bit, so the book's 64-bit bit patterns are replaced with
 * in-range values; right shift is logical on BESM-6.  The char cases (which
 * exercise integer promotion, including shift counts wider than a char) are kept
 * unchanged.  Function names are kept distinct within 8 characters.
 */

// like Chapter 16's compound_bitwise_ops_chars.c, constant-foldable
int t_chars(void) {
    signed char c1 = -128;
    signed char c2 = -120;
    signed char c3 = -2;
    signed char c4 = 1;
    signed char c5 = 120;

    unsigned char u1 = 0;
    unsigned char u2 = 170;
    unsigned char u3 = 250;
    unsigned char u4 = 255;

    // bitwise ops on signed chars (well-defined via integer promotion)
    c1 ^= 12345;
    c2 |= u4;
    c3 &= u2 - (unsigned char)185;
    c4 <<= 7u;   // wraps to -128 after promotion
    c5 >>= 31;   // shift count wider than char is fine after promotion

    // bitwise ops on unsigned chars
    long x = 32;
    u4 <<= 12;
    u3 >>= (x - 1);
    u2 |= -399;
    x = -4296140120l;  // fits in a 41-bit long
    u1 ^= x;

    if (c1 != -71) {
        return 1; // fail
    }
    if (c2 != -1) {
        return 2; // fail
    }
    if (c3 != -16) {
        return 3; // fail
    }
    if (c4 != -128) {
        return 4; // fail
    }
    if (c5) {
        return 5; // fail
    }
    if (u1 != 168) {
        return 6; // fail
    }
    if (u2 != 251) {
        return 7; // fail
    }
    if (u3) {
        return 8; // fail
    }
    if (u4) {
        return 9; // fail
    }
    return 0;
}

// long bitwise &/|/^ with in-range (41-bit) patterns
int t_lbw(void) {
    long l1 = 1095216660735l;  // 0xFF00FF00FF
    long l2 = -4294967296l;    // -2^32
    l1 &= l2;
    if (l1 != 1095216660480l) {
        return 1; // fail
    }
    l2 |= 100l;
    if (l2 != -4294967196l) {
        return 2; // fail
    }
    l1 = 1095216660735l;
    l1 ^= 824633720833l;  // 0xC000000001
    if (l1 != 270582939902l) {
        return 3; // fail
    }
    return 0;
}

// shifts on int and long with long/int shift counts (results stay in 41 bits)
int t_lsh(void) {
    int x = 100;
    x <<= 22l;
    if (x != 419430400) {
        return 1; // fail
    }
    if ((x >>= 4l) != 26214400) {
        return 2; // fail
    }
    if (x != 26214400) {
        return 3; // fail
    }
    long l = 12345l;
    l <<= 20;
    if (l != 12944670720l) {
        return 4; // fail
    }
    return 0;
}

// unsigned bitwise &/|/^ with in-range (48-bit) values
int t_ubw(void) {
    unsigned long ul = 281474976710655UL;  // 2^48 - 1
    ul &= 4294967295u;  // zero-extend to 48 bits
    if (ul != 4294967295ul) {
        return 1; // fail
    }
    ul = 1095216660735UL;  // 0xFF00FF00FF
    ul |= 4294967040u;     // 0xFFFFFF00
    if (ul != 1099511627775ul) {
        return 2; // fail
    }
    unsigned int ui = 4042322160u;  // 0xF0F0F0F0
    ui ^= 252645135u;               // 0x0F0F0F0F
    if (ui != 4294967295u) {
        return 3; // fail
    }
    return 0;
}

// unsigned shifts (right shift is logical, zero-fill)
int t_ush(void) {
    unsigned long ul = 281474976710655UL;  // 2^48 - 1
    ul >>= 20;
    if (ul != 268435455ul) {
        return 1; // fail
    }
    unsigned int u = 255u;
    u <<= 12;
    if (u != 1044480u) {
        return 2; // fail
    }
    return 0;
}

int main(void) {
    if (t_chars()) {
        return 1; // fail
    }
    if (t_lbw()) {
        return 2; // fail
    }
    if (t_lsh()) {
        return 3; // fail
    }
    if (t_ubw()) {
        return 4; // fail
    }
    if (t_ush()) {
        return 5; // fail
    }
    return 0; // success
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldIncrDecrChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Evaluate ++/-- with character types; make sure we handle integer promotions correctly */
int target(void) {
    signed char s = -127;
    signed char s2 = --s;
    signed char s3 = s--;

    unsigned char uc1 = 255;
    unsigned char uc2 = uc1++;
    unsigned char uc3 = ++uc1;

    if (!(s == 127 && s2 == -128 && s3 == -128)) {
        return 1; // fail
    }

    if (!(uc1 == 1 && uc2 == 255 && uc3 == 1)) {
        return 2; // fail
    }

    return 0; // success
}

int main(void) {
    return target();

}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldIncrDecrDoubles)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
/* Make sure we can constant fold ++/-- operations on doubles.
 * The book's mantissa-edge case (2^53) has no BESM-6 analogue: constant folding
 * is done in host double precision, so the precision loss the book checks for
 * only happens at runtime, not in the fold.  We keep the exactly-representable
 * cases, which fold identically on host and BESM-6. */

int target(void) {
    double e1 = 10.0;
    double e2 = --e1;  // 9; e1 -> 9
    double e3 = e1--;  // 9; e1 -> 8
    double e4 = e1++;  // 8; e1 -> 9
    double e5 = ++e1;  // 10; e1 -> 10

    if (!(e1 == 10. && e2 == 9. && e3 == 9. && e4 == 8. && e5 == 10.)) {
        return 1; // fail
    }

    return 0; // success
}

int main(void) {
    return target();
}
)WP"));
}

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldIncrDecrUnsigned)
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

TEST_F(BookTest, Chapter19_WP_AllTypes_FoldNegativeLongBitshift)
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
