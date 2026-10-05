//
// Chapter 19 — optimizer tests, imported from "Writing a C Compiler"
// (tests/chapter_19: constant_folding, copy_propagation, dead_store_elimination,
// unreachable_code_elimination, incl. dont_propagate/dont_elim negatives).
//
// Each book program is compiled and run through the full typecheck + translate +
// optimize pipeline; we pin the optimized TAC YAML (PipelineTest::OptimizeYaml).
// Following the book's convention, the inspection points are the target* funcs;
// where a program defines them, its self-checking main() driver is dropped (that
// runtime check is exercised by the whole_pipeline BESM-6 run tests in
// backend/besm6/chapter19_tests.cpp).  Programs whose logic lives in main are
// kept whole.  Goldens reflect the BESM-6 target (int/long are 41-bit, one
// word).  The book's host-only "#if/#pragma" lines are stripped (our scanner has
// no preprocessor).
//
// Cases the BESM-6 target/optimizer can't handle are DISABLED_ with a one-line
// reason (NaN constant-fold loops; a static-local name collision under the
// no-shadowing rule).
//
// A few programs whose optimized TAC is very large (e.g. copy propagation through
// nested loops) would need multi-thousand-line exact goldens; those use a compact
// instruction-kind histogram assertion (PipelineTest::KindHistogram) instead,
// which still pins the optimized instruction mix.
//
// This file is generated content pinned against the optimizer's actual output;
// regenerate goldens if the optimizer's TAC emission changes intentionally.
//
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "pipeline_test_fixture.h"

// fatal_error() is defined once for the whole optimizer-tests binary in
// pipeline_tests.cpp; the compiler libraries call it.

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldBinary)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can constant-fold binary expressions, including
 * the arithmetic +, -, *, /, and % operations, and the relational
 * ==, !=, <, <=, >, and >= operations.
 */

// arithmetic tests
int target_add(void) {
    return 100 + 200;  // 300
}

int target_sub(void) {
    return 2 - 2147483647;  // -2147483645
}

int target_mult(void) {
    return 1000 * 1000;  // 1000000
}

int target_div(void) {
    return 1111 / 4;  // 277
}

int target_rem(void) {
    return 10 % 3;  // 1
}

// relational tests
int target_eq_true(void) {
    return 2147483647 == 2147483647;  // 1
}

int target_eq_false(void) {
    return 2147483647 == 2147483646;  // 0
}

int target_neq_true(void) {
    return 1111 != 1112;  // 1
}

int target_neq_false(void) {
    return 1112 != 1112;  // 0
}

int target_gt_true(void) {
    return 10 > 1;  // 1
}

int target_gt_false(void) {
    return 10 > 10;  // 0
}

int target_ge_true(void) {
    return 123456 >= 123456;  // 1
}

int target_ge_false(void) {
    return 2147 >= 123456;  // 0
}

int target_lt_true(void) {
    // 256 < 2^30 + 256
    return 256 < 1073742080;  // 1
}

int target_lt_false(void) {
    return 256 < 0;  // 0
}

int target_le_true(void) {
    return 123456 <= 123457;  // 1
}

int target_le_false(void) {
    return 123458 <= 123457;  // 0
}

int val_to_negate = 2147483645;
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 300
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -2147483645
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1000000
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 277
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldConditionalJump)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of JumpIfZero and JumpIfNotZero instructions
 * resulting from && and || operations.
 * */

// We'll emit two TACKY instructions of the form
// JumpIfZero(0, false_label)
// both should be rewritten as Jump instructions
int target_jz_to_jmp(void) {
    return 0 && 0; // 0
}

// We'll emit two TACKY instructions of the form
// JumpIfZero(1, false_label)
// both should be removed
int target_remove_jz(void) {
    return 1 && 1; // 1
}

// We'll emit two JumpIfNotZero instructions:
// JumpIfNotZero(3, true_label)
// JumpIfNotZero(99, true_label)
// both should be written as Jump instructions
int target_jnz_to_jmp(void) {
    return 3 || 99; // 1
}

// We'll emit two JumpIfNotZero instructions:
// JumpIfNotZero(0, true_label)
// JumpIfNotZero(1, true_label)
// we should remove the first, rewrite the second as a Jump instruction
int target_remove_jnz(void) {
    return 0 || 1; // 1
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldControlFlow)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of JumpIfZero and JumpIfNotZero instructions
 * resulting from conditional ?: expressions, if statements, and loops.
 * */

int target_if(void) {
    if (0)
        return 1;
    return 0;
}

int target_if_else_true(void) {
    if (1) {
        return 2;
    } else {
        return 3;
    }
}

int target_if_else_false(void) {
    if (0) {
        return 2;
    } else {
        return 3;
    }
}

int target_conditional_true(void) {
    return 1 ? 2 : 3;
}

int target_conditional_false(void) {
    return 0 ? 4 : 5;
}

int target_do_loop(void) {
    int retval = 0;
    do {
        retval = 10;
    } while (0);
    return retval;
}

int target_while_loop_false(void) {
    int retval = 0;
    while (0) {
        retval = 10;
    }
    return retval;
}

int target_while_loop_true(void) {
    int retval = 0;
    while (1048576) {  // 1048576 == 2^20
        retval = 10;
        break;
    }
    return retval;
}

int target_for_loop_true(void) {
    int retval = 0;
    for (int i = 100; 123;) {
        retval = i;
        break;
    }
    return retval;
}

int target_for_loop_false(void) {
    int retval = 0;
    for (int i = 100; 0;) {
        retval = i;
        break;
    }
    return retval;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 100
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldException)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test cases where result of constant-folded expression is undefined (i.e.
 * division by zero and overflow). In this particular case, these expressions
 * wouldn't actually be evaluated at runtime, so the program's behavior is
 * well-defined; the main thing we're testing here is that compiler doesn't
 * crash during the constant folding pass. There are no target_ functions
 * because we don't inspect the assembly in this program.
 * */

int main(void) {
    int dead_div_by_zero = 1 || (1 / 0); // we short-circuit before evaluating 1 / 0
    int dead_zero_remainder = 0 && (100 % 0); // we short-circuit before evaluating 100 % 0
    int overflow = 0 ? (2147483647 + 10) : 100; // 2147483647 + 10 would overflow, but we skip it and just evaluate 100

    if (dead_div_by_zero != 1) {
        return 1;
    }
    if (dead_zero_remainder != 0) {
        return 2;
    }

    if (overflow != 100) {
        return 3;
    }

    return 0;  // success
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldUnary)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can constant-fold !, -, and ~ expressions. */

int target_negate(void) {
    return -3;
}

int target_negate_zero(void) {
    return -0;
}

int target_not(void) {
    return !1024;
}

int target_not_zero(void) {
    return !0;
}

int target_complement(void) {
    return ~1;
}

int three = 3;
int two = 2;
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -3
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -2
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_IntOnly_FoldBitwise)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant-folding the bitwise &, |, ^, >>, and << expressions */

int target_and(void) {
    // 0x0f0f_0f0f & 0x00ff_00ff
    return 252645135 & 16711935;
}

int target_or(void) {
    // 0x0f0f_0f0f | 0x00ff_00ff
    return 252645135 | 16711935;
}

int target_xor(void){
    // 0x0f0f_0f0f ^ 0x00ff_00ff
    return 252645135 ^ 16711935;
}

int target_shift_left(void) {
    return 291 << 18;
}

int target_shift_right(void) {
    return 252645135 >> 9;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 983055
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 268374015
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 267390960
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 76283904
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 493447
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldCastFromDouble)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of casts from double to integer types,
 * making sure the results are correctly rounded.
 * */

char target_to_char(void) {
    return (char)126.5;
}

unsigned char target_to_uchar(void) {
    return (unsigned char)254.9;
}

int target_to_int(void) {
    return (int)5.9;
}

unsigned target_to_uint(void) {
    // constant in the range of uint but not int
    return (unsigned)2147483750.5;
}

long target_to_long(void) {
    // nearest representable double is 9223372036854774784.0,
    // which will be converted to long int 9223372036854774784
    return (long)9223372036854774783.1;
}

unsigned long target_to_ulong(void) {
    // constant in the range of ulong but not long
    return (unsigned long)13835058055282163712.5;
}

unsigned long target_implicit(void) {
    // same as target_to_ulong but cast is implicit; make sure we still constant fold it
    return 3458764513821589504.0;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: char
      value: 126
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uchar
      value: 254
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 2147483750
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 9223372036854774784
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 13835058055282163712
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 3458764513821589504
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldCastToDouble)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of casts from integer types to double
 * making sure the results are correctly rounded.
 * */

// can convert 32-bit ints to double exactly
double target_from_int(void) {
    return (double)1000;
}

// cast a double outside the range of int
// (See example in Chapter 13, "Converting an Unsigned Integer to a double")
double target_from_uint(void) {
    return (double)4294967290u;
}

// this value is exactly between two representable doubles;
// using ties-to-even, it should be converted to 4611686018427387904.0
// (From double-rounding example in Chapter 13, "Converting an Unsigned Integer
// to a double")
double target_from_long(void) {
    return (double)4611686018427388416l;
}

// convert a value outside the range of signed long
// (From double-rounding example in Chapter 13, "Converting an Unsigned Integer
// to a double")
double target_from_ulong(void) {
    return (double)9223372036854776833ul;
}

// same as target_from_int but cast is implicit;
// make sure we still constant fold it
double target_implicit(void) {
    return 1000;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.f4p+9
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.fffffff4p+31
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1p+62
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.0000000000001p+63
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.f4p+9
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldConditionalJump)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of JumpIfZero and JumpIfNotZero instructions
 * resulting from && and || operations, with operand types other than int.
 * Identical to chapter_19/constant_folding/int_only/fold_conditional_jump.c
 * but with non-int operands
 * */

// We'll emit two TACKY instructions of the form
// JumpIfZero(0, false_label)
// both should be rewritten as Jump instructions
int target_jz_to_jmp(void) {
    return 0l && 0; // 0
}

// We'll emit two TACKY instructions of the form
// JumpIfZero(1, false_label)
// both should be removed
int target_remove_jz(void) {
    return 1u && 1.; // 1
}

// We'll emit two JumpIfNotZero instructions:
// JumpIfNotZero(3, true_label)
// JumpIfNotZero(99, true_label)
// both should be written as Jump instructions
int target_jnz_to_jmp(void) {
    return 3.5 || 99ul; // 1
}

// We'll emit two JumpIfNotZero instructions:
// JumpIfNotZero(0, true_label)
// JumpIfNotZero(1, true_label)
// we should remove the first, rewrite the second as a Jump instruction
int target_remove_jnz(void) {
    return 0ul || 1; // 1
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldDouble)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of all operations on doubles and make sure they're
 * correctly rounded.
 * */


double target_add(void) {
    // Because 1.2345e60 is so large, adding one to it doesn't change its value
    return 1.2345e60 + 1.;
}

double target_sub(void) {
    // make sure we properly calculate the difference between two very close
    // subnormal numbers
    return 5.85543871245623688067e-311 - 5.85543871245574281503e-311;
}

double target_mult(void) {
    return 2.1 * 3.0;
}

double target_div(void) {
    return 1100.5 / 5000.;
}

double target_div_underflow(void) {
    // this result should underflow to zero
    return 0.5e-100 / 2e307;
}

double target_neg(void) {
    return -.000000275;
}

int target_not(void) {
    return !1e30;
}

int target_eq(void) {
    // these decimal constants should be rounded to the same floating-point
    // value, so this will return 1
    return 0.1 == 0.10000000000000001;
}

int target_neq(void) {
    // these should compare unequal; 5e-324 will be rounded to the subnormal
    // number just above zero
    return 5e-324 != 0.0;
}

int target_gt(void) {
    return 1e308 > 1e307;
}

int target_ge(void) {
    return 3.1 >= 3.1;
}

int target_lt(void) {
    // these decimal constants should be rounded to the same floating-point
    // value, so this will return 0
    return 0.1 < 0.10000000000000001;
}

int target_le(void) {
    return 0.5 <= 0.;
}

double target_negate_zero(void) {
    // make sure this gives us negative zero and not zero
    return -0.0;
}

double target_infinity(void) {
    // this will result in infinity
    return 1e308 * 2.;
}

int target_compare_infininty(void) {
    // infinity == infinity
    return 10e308 == 12e308;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.89559ac537b6bp+199
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1p-1074
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.9333333333334p+2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x1.c2c3c9eecbfb1p-3
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: 0x0p+0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: -0x1.27476ca61b882p-22
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: -0x0p+0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: double
      value: inf
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldDoubleCastException)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test case where result of casting double to integer is undefined (because
 * the result is out of range). The program's behavior is well-defined
 * because the cast operations aren't actually executed; the main thing we're
 * testing here is that compiler doesn't crash during the constant folding pass.
 * There are no target_ functions
 * because we don't inspect the assembly in this program.
 * */



int main(void) {
    int dead_int_cast = 0 ? (int)2147483649.0 : 100; // in the range of uint but not int
    unsigned int dead_uint_cast = 0 ? (unsigned int) 34359738368.0 : 200; // in the range of long but not uint
    signed long dead_long_cast = 1 ? 300 : 9223372036854777856.0; // in the range of unsigned long but not long
    unsigned long dead_ulong_cast = 1 ? 200 : (unsigned long)200e300; //outside the range of unsigned long
    return dead_int_cast + dead_uint_cast + dead_long_cast + dead_ulong_cast;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 800
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldExtensionsAndCopies)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can constant-fold zero- and sign-extensions from shorter to
 * longer ints, and conversions from one integer type to another of the same
 * size (e.g. long to unsigned long). We inspect the assembly for sign-extension
 * operations to make sure there are no movsx instructions, but not for
 * zero-extension or conversions between types of the same size because those
 * turn into a single 'mov' instruction whether they're sign extended or not.
 * We also can't test sign- or zero-extension of character types yet because
 * there are no constants of character type. The whole_pipeline/ folder has more
 * robust tests for constant folding of all these type conversions - we can test
 * them more thoroughly once other optimizations are enabled.
 */

long uint_to_long(void) {
    return (long)4294967295U;
}

unsigned long uint_to_ulong(void) {
    return (unsigned long)4294967295U;
}

/* These next two are target_* functions b/c they require sign extension */
unsigned long target_int_to_ulong(void) {
    return (unsigned long)2147483647;
}

long target_int_to_long(void) {
    return (long)1;
}

int uint_to_int(void) {
    // outside the range of int; will be negative
    return (int)4294967200U;
}

unsigned int int_to_uint(void) {
    return (unsigned)2147480000;
}

long ulong_to_long(void) {
    // outside the range of long; will be negative
    return (long)18446744073709551615UL;
}

unsigned long long_to_ulong(void) {
    return 2147483650l;
}

long implicit(void) {
    // same as ulong_to_long, but cast is implicit
    return 18446744073709551615UL;
}

long one = 1l;
long ninety_six = 96l;
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 4294967295
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 4294967295
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 2147483647
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4294967200
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2147480000
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 18446744073709551615
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 2147483650
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 18446744073709551615
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldLong)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of operations on long ints.
 * Make sure we correctly handle operations that require all 64 bits.
 * Tests do not involve any overflow, since that's undefined behavior.
 */
long target_add(void) {
    // we can add longs when the result exceeds INT_MAX
    return 2147483647l + 1000l;
}

long target_sub(void) {
    // we can subtract longs when the result is smaller than INT_MIN
    return 1000l - 9223372036854773807l;
}

long target_mult(void) {
    // can multiply longs when the result exceeds INT_MAX
    return 35184372088832l * 4l;
}

long target_div(void) {
    // both operands are larger than INT_MAX
    return 9223372036854775807l / 3147483647l;
}

long target_rem(void) {
    // both operands are larger than INT_MAX
    return 9223372036854775807l % 3147483647l;
}

long target_complement(void) {
    // alternating 1s and 0s
    return ~6148914691236517206l;
}

long target_neg(void) {
    // except for most significant bit, upper 32 bits of negated value are all
    // zeros
    return -(9223372036854775716l);
}

int target_not(void) {
    // 2^56 + 2^45 + 2^44
    // lower 32 bits are all zeros
    return !72110370596061184l;
}

int target_eq(void) {
    return 9223372036854775716l == 9223372036854775716l;
}

int target_neq(void) {
    // lower 32 bits of 72110370596061184l are all zeros
    return 72110370596061184l != 0l;
}

int target_gt(void) {
    // second operand is greater, but if we only looked at lower
    // 32 bits we'd think the first was greater
    return 549755813889l > 17592186044416l ;  // 2^39 + 1 > 2^44
}

int target_ge(void) {
    return 400l >= 399l;
}

int target_lt(void) {
    // compare two values whose lower 32 bits are identical
    return 17592186044416l < 549755813888l;  // 2^44 < 2^39
}

int target_le(void) {
    // if we interpreted this as a signed int it would be negative
    return 2147483648l <= 0l;
}

long sub_result = 9223372036854772807l;
long complement_result = 6148914691236517207l;
long neg_result = 9223372036854775716l;
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 2147484647
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: -9223372036854772807
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 140737488355328
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 2930395538
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 1758008721
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: -6148914691236517207
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: -9223372036854775716
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (124 instructions).
TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldTruncate)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test constant folding of all conversions from longer to shorter integer
 * types. For now, we just verify that the behavior is correct, without
 * inspecting the assembly code; we can't tell whether a truncate operation was
 * constant folded because it turns into a single 'mov' instruction either way.
 * The whole_pipeline/ folder has more robust tests for constant folding of
 * truncate operations - in these tests, constant-folding truncate enables
 * other optimizations.
 */


// truncate long
int long_to_int(void) {
    // 2^45 + 2^35 + 1234
    return (int)35218731828434l;
}

unsigned int long_to_uint(void) {
    // 2^45 + 2^35 + 1234
    return (unsigned int)35218731828434l;
}

char long_to_char(void) {
    // LONG_MAX
    return (char)9223372036854775807l;
}

signed char long_to_schar(void) {
    // 2^62 + 128
    return (signed char)4611686018427388032l;
}

unsigned char long_to_uchar(void) {
    // UINT_MAX
    return (unsigned char)4294967295UL;
}

// truncate unsigned long
int ulong_to_int(void) {
    // ULONG_MAX
    return (int)18446744073709551615UL;
}

unsigned int ulong_to_uint(void) {
    return (unsigned int)18446744073709551615UL;
}

char ulong_to_char(void) {
    return (char)4294967295UL;
}

signed char ulong_to_schar(void) {
    return (signed char)4611686018427388032ul;
}

unsigned char ulong_to_uchar(void) {
    // 2^63 + 255
    return (unsigned char)9223372036854776063ul;
}

// truncate int
char int_to_char(void) {
    return (char)1274;
}

signed char int_to_schar(void) {
    // INT_MAX
    return (signed char)2147483647;
}

unsigned char int_to_uchar(void) {
    return (unsigned char)1274;
}

// truncate unsigned int
char uint_to_char(void) {
    return (char)2147483901u;  // 2^31 + 253
}

signed char uint_to_schar(void) {
    return (signed char)2147483660u;  // 2^31 + 12
}

unsigned char uint_to_uchar(void) {
    return (unsigned char)2147483901u;
}

// same as uint_to_uchar but implicit cast
unsigned char implicit(void) {
    return 2147483901u;
}

int one = 1;
int six = 6;
int three = 3;
int one_twenty_eight = 128;

int main(void) {
    // truncate longs

    // 0x0000_2008_0000_04d2 --> 0x0000_04d2
    if (long_to_int() != 1234) {
        return 1;
    }
    if (long_to_uint() != 1234u) {
        return 2;
    }

    // 0x7fff_ffff_ffff_ffff --> 0xff
    if (long_to_char() != -one) {
        return 3;
    }

    // 0x4000_0000_0000_0080 --> 0x80
    if (long_to_schar() != -one_twenty_eight) {
        return 4;
    }

    // 0x0000_0000_ffff_ffff -> 0xff
    if (long_to_uchar() != 255) {
        return 5;
    }

    // truncate ulongs

    // 0xffff_ffff_ffff_ffff --> 0xffff_ffff
    if (ulong_to_int() != -one) {
        return 6;
    }
    if (ulong_to_uint() != 4294967295U) {
        return 7;
    }

    // 0x7fff_ffff_ffff_ffff --> 0xff
    if (ulong_to_char() != -one) {
        return 8;
    }

    // 0x4000_0000_0000_0080 --> 0x80
    if (ulong_to_schar() != -one_twenty_eight) {
        return 9;
    }

    // 0x0000_0000_ffff_ffff -> 0xff
    if (ulong_to_uchar() != 255) {
        return 10;
    }

    // truncate ints

    // 0x0000_04fa -> 0xfa
    if (int_to_char() != -six) {
        return 11;
    }

    // 0x7fff_ffff -> 0xff
    if (int_to_schar() != -one) {
        return 12;
    }

    // 0x0000_04fa -> 0xfa
    if (int_to_uchar() != 250) {
        return 13;
    }

    // truncate uints

    // 0x8000_00fd --> 0xfd
    if (uint_to_char() != -three) {
        return 14;
    }

    // 0x8000_000c -> 0x0c
    if (uint_to_schar() != 12) {
        return 15;
    }
    // 0x8000_00fd --> 0xfd
    if (uint_to_uchar() != 253) {
        return 16;
    }
    if (implicit() != 253) {
        return 17;
    }
    return 0;
}
)SRC")),
              "binary=17 fun_call=17 jump_if_zero=17 label=17 return=35 sign_extend=8 unary=8 zero_extend=5");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldUint)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of all operations on unsigned ints;
 * make sure they wrap around correctly
 * and that we evaluate them with unsigned division/comparison functions.
 */
unsigned int target_add(void) {
    // result exceeds UINT_MAX and wraps around past 0
    return 4294967295U + 10u;
}

unsigned int target_sub(void) {
    // result is less then 0 and wraps back round past UINT_MAX
    return 10u - 12u;
}

unsigned int target_mult(void) {
    // wraps back around to 2147483648u
    return 2147483648u * 3u;
}

unsigned int target_div(void) {
    // result would be different if we interpreted values as signed
    return 4294967286u / 10u;
}

unsigned int target_rem(void) {
    // result would be different if we interpreted values as signed
    return 4294967286u % 10u;
}

unsigned int target_complement(void) {
    return ~1u;
}

unsigned int target_neg(void) {
    return -10u;
}

int target_not(void) {
    return !65536u;  // 2^16
}

int target_eq(void) {
    return 100u == 100u;
}

int target_neq(void) {
    // these have identical binary representations except for the most
    // significant bit
    return 2147483649u != 1u;
}

int target_gt(void) {
    // make sure we're using unsigned comparisons;
    // if we interpret these as signed integers,
    // we'll think 2147483649u is negative and return 0
    return 2147483649u > 1000u;
}

int target_ge(void) {
    return 4000000000u >= 3999999999u;
}

int target_lt(void) {
    // as with target_gt, make sure we don't interpret 2147483649u
    // as a negative signed integer
    return 2147483649u < 1000u;
}

int target_le(void) {
    return 4000000000u <= 3999999999u;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 9
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4294967294
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 2147483648
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 429496728
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 6
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4294967294
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4294967286
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldUlong)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant folding of all operations on unsigned longs;
 * make sure that they wrap around correctly,
 * that we evaluate them with unsigned division/comparison functions,
 * and that we can evaluate expressions requiring all 64 bits.
 */
unsigned long target_add(void) {
    // result exceeds ULONG_MAX and wraps around past 0
    return 18446744073709551615UL + 10ul;
}

unsigned long target_sub(void) {
    // result is less then 0 and wraps back around past ULONG_MAX
    return 10ul - 12ul;
}

unsigned long target_mult(void) {
    // wraps back around to 9223372036854775808ul
    return 9223372036854775808ul * 3ul;
}

unsigned long target_div(void) {
    return 18446744073709551614ul / 10ul;
}

unsigned long target_rem(void) {
    return 18446744073709551614ul % 10ul;
}

unsigned long target_complement(void) {
    return ~1ul;
}

unsigned long target_neg(void) {
    return -(9223372036854775900ul);
}

int target_not(void) {
    return !4294967296UL;  // 2^32
}

int target_eq(void) {
    return 18446744073709551615UL == 18446744073709551615UL;
}

int target_neq(void) {
    // these have identical binary representations except for the most
    // significant bit
    return 9223372036854775809ul != 1ul;
}

int target_gt(void) {
    // make sure we're using unsigned comparisons;
    // if we interpret these as signed integers,
    // we'll think 9223372036854775809ul is negative and return 0
    return 9223372036854775809ul > 1000ul;
}

int target_ge(void) {
    // 200ul would be greater if we only considered lower 32 bits
    return 9223372036854775809ul >= 200ul;
}

int target_lt(void) {
    // as with target_gt, make sure we don't interpret 9223372036854775809ul
    // as a negative signed integer
    return 9223372036854775809ul < 1000ul;
}

int target_le(void) {
    return 9223372036854775809ul <= 200ul;
}

int target_le2(void) {
    // make sure we're evaluating <= and not <
    return 9223372036854775809ul <= 9223372036854775809ul;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 9
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 18446744073709551614
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 9223372036854775808
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 1844674407370955161
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 18446744073709551614
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 9223372036854775716
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_NegativeZero)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If we deduplicate floating-point StaticConstant constructs, make sure we
 * distinguish between constants with the same value but different alignments.
 * Specifically, if we've already added an ordinary constant -0.0, and then we
 * need a 16-byte aligned -0.0 to use for negation, don't just reuse the
 * previous 8-byte aligned one. (It's okay to either keep them as separate
 * constants, or merge them and keep the higher alignment.) This is a regression
 * test for a bug in the reference implementation. Note that we can only catch
 * this bug once we implement constant folding; before then, we don't add
 * positive StaticConstants.
 * No 'target' function here because we're just looking for correctness,
 * not inspecting assembly.
 * */

double x = 5.0;

int main(void) {
    double d = -0.0;  // add normal constant -0. to list of top-level constants
    return (-x > d); // add 16-byte-aligned constant -0. to negate x
}
)SRC"),
              R"OPT(- instruction:
  kind: unary
  op: negate_double
  src:
    kind: var
    name: x
  dst:
    kind: var
    name: %1
- instruction:
  kind: binary
  op: greater_than_double
  src1:
    kind: var
    name: %1
  src2:
    kind: constant
    const:
      kind: double
      value: -0x0p+0
  dst:
    kind: var
    name: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %2
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_CastNanNotExecuted)
{
    OptimizeYaml(R"SRC(
// Make sure the compiler doesn't complain if you try to cast NaN to an int
// in code that isn't executed. (If it actually did execute it would be
// undefined behavior)

static int flse = 0;

int main(void) {
    int retval = 0;
    if (flse) {
        retval = (int) (0.0/0.0);
    }
    return retval;
}
)SRC");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldBitwiseLong)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant-folding the bitwise &, |, ^, >>, and << expressions with long operands */

long target_and(void) {
    // 0x0f0f_0f0f_0f0f_0f0f & 0x00ff_00ff_00ff_00ff
    return 1085102592571150095l & 71777214294589695l;
}

long target_or(void) {
    // 0x0f0f_0f0f_0f0f_0f0f | 0x00ff_00ff_00ff_00ff
    return 1085102592571150095l | 71777214294589695l;
}

long target_xor(void){
    // 0x0f0f_0f0f_0f0f_0f0f ^ 0x00ff_00ff_00ff_00ff
    return 1085102592571150095l ^ 71777214294589695l;
}

long target_shift_left(void) {
    return 1l << 62;
}

long target_shift_right(void) {
    return 72057589742960640l >> 35;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 4222189076152335
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 1152657617789587455
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 1148435428713435120
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 4611686018427387904
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 2097151
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldBitwiseUnsigned)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test constant-folding the bitwise &, |, ^, >>, and << expressions with unsigned operands */

unsigned target_and(void) {
    // 0xf0f0_f0f0 & 0xff00_ff00
    return 4042322160u & 4278255360u;
}

unsigned long target_or(void) {
    // 0x0f0f_0f0f_0f0f_0f0f | 0xff00_ff00_ff00_ff00
    return 1085102592571150095ul | 18374966859414961920ul;
}

unsigned int target_xor(void) {
    // 0xf0f0_f0f0 ^ 0x0ff0_0ff0
    return 4042322160u ^ 267390960u;
}

unsigned int target_shift_uint_left(void) {
    return 10u << 24l; // doesn't matter that right operand is different type
}

unsigned long target_shift_ulong_left(void) {
    return 2286249799ul << 33u; // result wrap arounds
}

// make sure right shift is logical, not arithmetic
unsigned int target_shift_uint_right(void) {
    return 4294967296u >> 16;
}

unsigned long target_shift_ulong_right(void) {
    return 9223372041149743104ul >> 21l;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4026593280
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 18379189048491114255
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 4278255360
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 167772160
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 1191992160673595392
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: uint
      value: 65536
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: ulong
      value: 4398046513152
)OPT");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_FoldNan)
{
    OptimizeYaml(R"SRC(
/* Test that we can constant fold an operation that results in NaN;
 * the whole_pipeline folder includes a test that we can constant fold
 * operations _using_ NaN.
 */

int double_isnan(double d); // defined in tests/chapter_13/helper_libs/nan.c

double target_nan(void){
    return 0./0.;
}
)SRC");
}

TEST_F(PipelineTest, Chapter19_CF_AllTypes_ReturnNan)
{
    OptimizeYaml(R"SRC(
/* Test case where we return NaN after constant folding */

int double_isnan(double d); // defined in tests/chapter_13/helper_libs/nan.c

double target(void) {
    return 0.0 / 0.0;
}
)SRC");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_ConstantPropagation)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* A basic test of constant propagation in a function with no control flow
 * structures
 * */
int target(void) {
    int x = 3;
    int y = x;
    return x + y;  // should become return 6
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 6
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DifferentPathsSameCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If two instances of the copy x = y appear on different paths to some use
 * of x, we can propagate that copy.
 * */
int target(int flag) {
    int x = 0;
    if (flag) {
        x = 3;
    } else {
        x = 3;
    }
    return x;  // this should become 'return 3'
}
)SRC"),
              R"OPT(- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: jump
  target: %1
- instruction:
  kind: label
  name: %0
- instruction:
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DifferentSourceValuesSameCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* We can propagate x = y if it appears on all paths to some use of x,
 * even if y doesn't have the same value on all those paths.
 * Based on Figure 19-5.
 * */

int callee(int a, int b) {
    return a + b;
}
int target(int flag) {
    // use static variables here so we can't coalesce x and y
    // into the same register, or into EDI and ESI, once we implement
    // register coalescing; otherwise it might look like we've propagated
    // x = y when we haven't
    static int x;
    static int y;
    if (flag) {
        y = 20;
        x = y;
    } else {
        y = 100;
        x = y;
    }
    // x = y reaches here, though with different values of y
    return callee(x, y);
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 20
  dst:
    kind: var
    name: y
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 20
  dst:
    kind: var
    name: x
- instruction:
  kind: jump
  target: %1
- instruction:
  kind: label
  name: %0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst:
    kind: var
    name: y
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst:
    kind: var
    name: x
- instruction:
  kind: label
  name: %1
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: y
    - val:
      kind: var
      name: y
  dst:
    kind: var
    name: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %2
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (32 instructions).
TEST_F(PipelineTest, Chapter19_CP_IntOnly_Fig198)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test case based on Figure 19-8:
 * Make sure we run iterative algorithm until the results converge.
 * */
static int called_counter = 0;

int callee(int i) {
    if (i == 3 && called_counter == 0) {
        // we're on first loop iteration; iterate one more time
        called_counter = 1;
        return 1;
    }
    if (i == 4 && called_counter == 1) {
        // we're on second loop iteration; stop
        called_counter = 2;
        return 0;
    }

    // if we hit this point, something has gone wrong!
    // set called_counter to indicate error, then terminate loop
    called_counter = -1;
    return 0;
}

int target(void) {
    int y = 3;
    int keep_looping;
    do {
        // After analyzing each basic block once,
        // it will look like we could rewrite this as
        // x = callee(3), but once the algorithm converges
        // we'll know that isn't safe.
        keep_looping = callee(y);
        y = 4;
    } while (keep_looping);  // loop should terminate after first iteration
    return y;                // should become return 4
}
)SRC")),
              "binary=6 copy=7 fun_call=1 jump=2 jump_if_not_zero=1 jump_if_zero=4 label=7 return=4");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_InitAllCopies)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we initialize each basic block with the set of all copies
 * in the function
 * */

int counter = 0;

int increment_counter(void) {
    counter = counter + 1;
    return 0;
}

int target(void) {
    int y = 3;
    do {
        // when we first process this block,
        // y = 3 will reach it from one predecessor, and we won't have
        // visited the other yet; make sure we still recognize
        // that y = 3 reaches this block (and its successor)
        increment_counter();
    } while (counter < 5);
    return y;  // this should become return 3
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: counter
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: %0
  dst:
    kind: var
    name: counter
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: %y
- instruction:
  kind: label
  name: %2
- instruction:
  kind: fun_call
  fun_name: increment_counter
  dst:
    kind: var
    name: %3
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: counter
  src2:
    kind: constant
    const:
      kind: int
      value: 5
  dst:
    kind: var
    name: %4
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %4
  target: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %y
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_KillAndAddCopies)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test how we handle copies in the transfer function:
 * x = src should generate a copy _and_ kill previous copies
 * where x is the source or destination
 * */
static int globvar;

int set_globvar(int i) {
    globvar = i;
    return 0;
}

int callee(int a, int b) {
    return a + b;
}

int target(int param) {
    int x = param;
    // should be able to propagate param into var but we don't explicitly
    // check that here
    set_globvar(x);
    int y = x;  // gen y = x;
    x = 10;     // kill x = param and y = x, gen x = 10
    // make sure we propagate x = 10 but not y = x
    return callee(x, y);  // becomes callee(10, y)
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %i
  dst:
    kind: var
    name: globvar
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: set_globvar
  args:
    - val:
      kind: var
      name: %param
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 10
    - val:
      kind: var
      name: %param
  dst:
    kind: var
    name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_KilledThenRedefined)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If two identical copies to x appear on the path to some use of x,
 * and the first one is killed, make sure we can still propagate the second.
 * */

int x = 0;
int y = 0;

int callee(void) {
    y = x * 2;  // make sure x still has the right value at this point
    return 5;
}

int target(void) {
    x = 2;         // gen x = 2
    x = callee();  // kill x = 2
    x = 2;         // gen x = 2 again
    return x;      // should become "return 2"
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: x
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: %0
  dst:
    kind: var
    name: y
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: x
- instruction:
  kind: fun_call
  fun_name: callee
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: x
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_MultiPathNoKill)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If there are multiple paths from x = 3 to a use of x, but the copy
 * isn't killed on any of those paths, we can propagate it.
 * */
int var = 0;
int callee(void) {
    var = var + 1;
    return 0;
}

int target(int flag) {
    int x = 3;
    if (flag)
        callee();
    return x;  // should become return 3
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: var
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: %0
  dst:
    kind: var
    name: var
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: fun_call
  fun_name: callee
  dst:
    kind: var
    name: %2
- instruction:
  kind: jump
  target: %1
- instruction:
  kind: label
  name: %0
- instruction:
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
)OPT");
}
