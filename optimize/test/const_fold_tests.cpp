#include "optimizer_test_fixture.h"
#include "target.h"

namespace {
// Temporarily switch the active target for a test, restoring it on scope exit
// (so a width-specific fold does not leak into later, target-agnostic tests).
struct TargetGuard {
    const Target *saved;
    explicit TargetGuard(const char *name) : saved(target_config) { target_config = target_lookup(name); }
    ~TargetGuard() { target_config = saved; }
};
} // namespace

// ---------------------------------------------------------------------------
// Null / error cases
// ---------------------------------------------------------------------------

TEST_F(OptimizerTest, NullBodyReturnsNull)
{
    EXPECT_EQ(optimize_function(nullptr, opt_flags_default(), nullptr), nullptr);
}

// ---------------------------------------------------------------------------
// Unary constant folding
// ---------------------------------------------------------------------------

// ~0  →  Copy(ConstInt(-1), t)
TEST_F(OptimizerTest, UnaryFoldComplement)
{
    Tac_Instruction *body = make_unary(TAC_UNARY_COMPLEMENT, make_const_int(0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, -1);
    ASSERT_NE(body->u.copy.dst, nullptr);
    EXPECT_EQ(body->u.copy.dst->kind, TAC_VAL_VAR);
    EXPECT_STREQ(body->u.copy.dst->u.var_name, "t");
}

// !0  →  Copy(ConstInt(1), t)
TEST_F(OptimizerTest, UnaryFoldNot0)
{
    Tac_Instruction *body = make_unary(TAC_UNARY_NOT, make_const_int(0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// !5  →  Copy(ConstInt(0), t)
TEST_F(OptimizerTest, UnaryFoldNotNonzero)
{
    Tac_Instruction *body = make_unary(TAC_UNARY_NOT, make_const_int(5), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 0);
}

// -(3)  →  Copy(ConstInt(-3), t)
TEST_F(OptimizerTest, UnaryFoldNegate)
{
    Tac_Instruction *body = make_unary(TAC_UNARY_NEGATE, make_const_int(3), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, -3);
}

// Unary with Var src is not folded — instruction unchanged.
TEST_F(OptimizerTest, UnaryFoldVarUnchanged)
{
    Tac_Instruction *body = make_unary(TAC_UNARY_NOT, make_var("x"), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_UNARY);
}

// ---------------------------------------------------------------------------
// Binary constant folding
// ---------------------------------------------------------------------------

// 3 + 4  →  Copy(ConstInt(7), t)
TEST_F(OptimizerTest, BinaryFoldAdd)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_ADD, make_const_int(3), make_const_int(4), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 7);
}

// 6 / 2  →  Copy(ConstInt(3), t)
TEST_F(OptimizerTest, BinaryFoldDivide)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_DIVIDE, make_const_int(6), make_const_int(2), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 3);
}

// 10 % 3  →  Copy(ConstInt(1), t)
TEST_F(OptimizerTest, BinaryFoldRemainder)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_REMAINDER, make_const_int(10), make_const_int(3), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 6 / 0  →  instruction unchanged (division by zero)
TEST_F(OptimizerTest, BinaryFoldDivideByZero)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_DIVIDE, make_const_int(6), make_const_int(0), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_BINARY);
}

// 5 == 5  →  Copy(ConstInt(1), t)  — comparison always yields ConstInt
TEST_F(OptimizerTest, BinaryFoldEqual)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_EQUAL, make_const_int(5), make_const_int(5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 3 < 5  →  Copy(ConstInt(1), t)
TEST_F(OptimizerTest, BinaryFoldLessThan)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_LESS_THAN, make_const_int(3), make_const_int(5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 5 & 3  →  Copy(ConstInt(1), t)
TEST_F(OptimizerTest, BinaryFoldBitwiseAnd)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_BITWISE_AND, make_const_int(5), make_const_int(3), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 1 << 3  →  Copy(ConstInt(8), t)
TEST_F(OptimizerTest, BinaryFoldLeftShift)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_LEFT_SHIFT, make_const_int(1), make_const_int(3), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 8);
}

// Var src — not folded, instruction unchanged.
TEST_F(OptimizerTest, BinaryFoldVarUnchanged)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_ADD, make_var("x"), make_const_int(1), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_BINARY);
}

// optimize_function on a constant binary folds it and reaches fixed point.
// const_fold: Binary(ADD,3,4,t) → Copy(7,t); copy_prop: Return(7); dead_store: Copy removed.
TEST_F(OptimizerTest, BinaryFixedPoint)
{
    // Body: Binary(ADD, ConstInt(3), ConstInt(4), Var("t")) → Return(Var("t"))
    Tac_Instruction *ret = make_return(make_var("t"));
    Tac_Instruction *head =
        make_binary(TAC_BINARY_ADD, make_const_int(3), make_const_int(4), make_var("t"));
    head->next = ret;

    Tac_Instruction *result = optimize_function(head, opt_flags_default(), nullptr);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, TAC_INSTRUCTION_RETURN);
    EXPECT_EQ(result->u.return_.src->u.constant->u.int_val, 7);
}

// optimize_function on a constant unary folds it and reaches fixed point.
// const_fold: Unary(NOT,0,t) → Copy(1,t); copy_prop: Return(1); dead_store: Copy removed.
TEST_F(OptimizerTest, UnaryFixedPoint)
{
    // Body: Unary(NOT, ConstInt(0), Var("t")) → Return(Var("t"))
    Tac_Instruction *ret  = make_return(make_var("t"));
    Tac_Instruction *head = make_unary(TAC_UNARY_NOT, make_const_int(0), make_var("t"));
    head->next            = ret;

    Tac_Instruction *result = optimize_function(head, opt_flags_default(), nullptr);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, TAC_INSTRUCTION_RETURN);
    EXPECT_EQ(result->u.return_.src->u.constant->u.int_val, 1);
}

// ---------------------------------------------------------------------------
// Floating-point constant folding
// ---------------------------------------------------------------------------

// 1.5f + 2.5f  →  Copy(ConstFloat(4.0), t)
TEST_F(OptimizerTest, BinaryFoldFloatAdd)
{
    Tac_Instruction *body =
        make_binary(TAC_BINARY_ADD, make_const_float(1.5), make_const_float(2.5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedFloat(body, 4.0);
}

// 1.5 < 2.5  →  Copy(ConstInt(1), t).  FP orderings carry the _DOUBLE opcode but fold to
// an int 0/1 just like the integer comparisons.
TEST_F(OptimizerTest, BinaryFoldDoubleLessThan)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_LESS_THAN_DOUBLE, make_const_double(1.5),
                                        make_const_double(2.5), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 2.0 >= 2.0  →  Copy(ConstInt(1), t)  — the equality edge folds true.
TEST_F(OptimizerTest, BinaryFoldDoubleGreaterOrEqual)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_GREATER_OR_EQUAL_DOUBLE, make_const_double(2.0),
                                        make_const_double(2.0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 5.0 - 3.0  →  Copy(ConstDouble(2.0), t)
TEST_F(OptimizerTest, BinaryFoldDoubleSubtract)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_SUBTRACT, make_const_double(5.0),
                                        make_const_double(3.0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedDouble(body, 2.0);
}

// 2.0 * 3.0  →  Copy(ConstDouble(6.0), t)
TEST_F(OptimizerTest, BinaryFoldDoubleMultiply)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_MULTIPLY, make_const_double(2.0),
                                        make_const_double(3.0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedDouble(body, 6.0);
}

// 1.0 / 0.0  →  Copy(ConstDouble(inf), t)  — IEEE division by zero is defined, never skipped.
TEST_F(OptimizerTest, BinaryFoldDoubleDivideByZero)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_DIVIDE, make_const_double(1.0),
                                        make_const_double(0.0), make_var("t"));
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_DOUBLE);
}

// 3.0 == 3.0  →  Copy(ConstInt(1), t)  — comparison always yields ConstInt
TEST_F(OptimizerTest, BinaryFoldDoubleEqual)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_EQUAL, make_const_double(3.0),
                                        make_const_double(3.0), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 1);
}

// 1.5L + 2.5L  →  Copy(ConstLongDouble(4.0L), t)
TEST_F(OptimizerTest, BinaryFoldLongDoubleAdd)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_ADD, make_const_long_double(1.5L),
                                        make_const_long_double(2.5L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedLongDouble(body, 4.0);
}

// Long double folds in binary128 on any host (riscv64), and as a double where long
// double is one (BESM-6): 1.0L / 3 to each format's nearest.
TEST_F(OptimizerTest, BinaryFoldLongDoublePrecision)
{
    TargetGuard guard("riscv64");
    Tac_Instruction *body = constant_fold(make_binary(TAC_BINARY_DIVIDE_DOUBLE,
                                                      make_const_long_double(1.0L),
                                                      make_const_long_double(3.0L), make_var("t")));
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    Float128 q = body->u.copy.src->u.constant->u.long_double_val;
    EXPECT_EQ(0x3ffd555555555555ULL, q.hi);
    EXPECT_EQ(0x5555555555555555ULL, q.lo);

    TargetGuard besm("besm6");
    body = constant_fold(make_binary(TAC_BINARY_DIVIDE_DOUBLE, make_const_long_double(1.0L),
                                     make_const_long_double(3.0L), make_var("t")));
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    q = body->u.copy.src->u.constant->u.long_double_val;
    EXPECT_EQ(0, f128_cmp(q, f128_from_double(1.0 / 3)));
}

// The FP-specific binary ops the translator now emits for double/float operands fold
// exactly like their plain counterparts.  1.5 * 2.5 → Copy(ConstDouble(3.75), t).
TEST_F(OptimizerTest, BinaryFoldDoubleOpVariants)
{
    Tac_Instruction *add = constant_fold(make_binary(
        TAC_BINARY_ADD_DOUBLE, make_const_double(1.5), make_const_double(2.5), make_var("t")));
    AssertFoldedDouble(add, 4.0);

    Tac_Instruction *sub = constant_fold(make_binary(
        TAC_BINARY_SUBTRACT_DOUBLE, make_const_double(5.0), make_const_double(3.0), make_var("t")));
    AssertFoldedDouble(sub, 2.0);

    Tac_Instruction *mul = constant_fold(make_binary(
        TAC_BINARY_MULTIPLY_DOUBLE, make_const_double(1.5), make_const_double(2.5), make_var("t")));
    AssertFoldedDouble(mul, 3.75);

    Tac_Instruction *div = constant_fold(make_binary(
        TAC_BINARY_DIVIDE_DOUBLE, make_const_double(7.0), make_const_double(2.0), make_var("t")));
    AssertFoldedDouble(div, 3.5);
}

// ---------------------------------------------------------------------------
// Target-width-aware integer narrowing (regression for TODO task #6).
//
// -5000000 * 10000 == -50000000000.  That fits BESM-6's 41-bit signed int but
// not the host's 32-bit int.  The folder must wrap the result to the *target*
// width, not the host C type width, so the product is not truncated to
// -50000000000 mod 2^32 == 1539607552.
// ---------------------------------------------------------------------------

// On BESM-6 (int is 41-bit) the product is preserved exactly.
TEST_F(OptimizerTest, BinaryFoldMultiplyBesm6Width)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_MULTIPLY, make_const_int(-5000000),
                                        make_const_long(10000), make_var("t"));
    {
        TargetGuard besm6("besm6");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    ASSERT_NE(body->u.copy.src, nullptr);
    EXPECT_EQ(body->u.copy.src->kind, TAC_VAL_CONSTANT);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_INT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.int_val, -50000000000LL);
}

// On a 32-bit-int target the same product wraps at 2^32 (the intended,
// unchanged default behavior for machine-independent folding).
TEST_F(OptimizerTest, BinaryFoldMultiplyHostWidthWraps)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_MULTIPLY, make_const_int(-5000000),
                                        make_const_long(10000), make_var("t"));
    {
        TargetGuard x86("x86_64");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    ASSERT_NE(body->u.copy.src, nullptr);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_INT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.int_val, 1539607552);
}

// ---------------------------------------------------------------------------
// Signed right shift of a negative value is target-dependent (C leaves it
// implementation-defined).  BESM-6's shift unit does no sign extension, so its
// backend shifts logically; the folder follows via Target.right_shift_is_logical
// so a folded expression matches what the machine computes at runtime.
// ---------------------------------------------------------------------------

// On BESM-6 (41-bit int) -8160 >> 5 is logical: the bit pattern 2^41 - 8160
// shifted right by 5 is 68719476481.
TEST_F(OptimizerTest, BinaryFoldRightShiftNegativeBesm6Logical)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_RIGHT_SHIFT, make_const_int(-8160),
                                        make_const_int(5), make_var("t"));
    {
        TargetGuard besm6("besm6");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_INT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.int_val, 68719476481LL);
}

// ---------------------------------------------------------------------------
// A shift count is in range up to the TARGET's storage width, not the host's.
// The folder used to mask the count to 31 (int) or 63 (long), the host widths, so
// on BESM-6 -- where an int is 48 bits of storage -- every count from 32 to 47
// folded to a different shift than the one the machine performs at run time.
// `1u << 36` folded to `1u << 4` == 16 where the hardware gives 2^36, and the
// disagreement was invisible unless the same expression appeared both as a literal
// and with a variable count.
// ---------------------------------------------------------------------------

// The bug, exactly: an unsigned int shifted left by 36 on a 48-bit-int target.
TEST_F(OptimizerTest, BinaryFoldLeftShiftPastHostIntWidthBesm6)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_LEFT_SHIFT, make_const_uint(1),
                                        make_const_int(36), make_var("t"));
    {
        TargetGuard besm6("besm6");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UINT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.uint_val, 1ULL << 36);
}

// And back the other way, so the round trip through bit 37 is exact.  Spelled with an
// unsigned long -- also 48 bits on this target -- because the host `unsigned' the
// fixture takes for a uint constant cannot hold 2^36 to begin with, which is the same
// host-width assumption that put the bug here.
TEST_F(OptimizerTest, BinaryFoldRightShiftPastHostIntWidthBesm6)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_RIGHT_SHIFT, make_const_ulong(1UL << 36),
                                        make_const_int(36), make_var("t"));
    {
        TargetGuard besm6("besm6");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_ULONG);
    EXPECT_EQ(body->u.copy.src->u.constant->u.ulong_val, 1u);
}

// Out of range even for the 48-bit target: C leaves it undefined and the folder
// answers 0, which is what the BESM-6 shift unit produces past 48 bits.
TEST_F(OptimizerTest, BinaryFoldLeftShiftOutOfRangeBesm6IsZero)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_LEFT_SHIFT, make_const_uint(1),
                                        make_const_int(48), make_var("t"));
    {
        TargetGuard besm6("besm6");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UINT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.uint_val, 0u);
}

// The same count of 36 on x86_64, where a 32-bit int makes it out of range: 0, not
// the 16 the old masking gave.  Nothing in range changes for this target.
TEST_F(OptimizerTest, BinaryFoldLeftShiftOutOfRangeX86IsZero)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_LEFT_SHIFT, make_const_uint(1),
                                        make_const_int(36), make_var("t"));
    {
        TargetGuard x86("x86_64");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UINT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.uint_val, 0u);
}

// On x86_64 the same signed >> is arithmetic (sign-preserving): -8160 >> 5 == -255.
TEST_F(OptimizerTest, BinaryFoldRightShiftNegativeX86Arithmetic)
{
    Tac_Instruction *body = make_binary(TAC_BINARY_RIGHT_SHIFT, make_const_int(-8160),
                                        make_const_int(5), make_var("t"));
    {
        TargetGuard x86("x86_64");
        body = constant_fold(body);
    }
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_INT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.int_val, -255);
}
