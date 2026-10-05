#include "optimizer_test_fixture.h"
#include "target.h"

// ---------------------------------------------------------------------------
// Type conversion folding tests
// ---------------------------------------------------------------------------

// --- SIGN_EXTEND ---

// SignExtend(ConstInt(3))  →  Copy(ConstLong(3), t)
TEST_F(OptimizerTest, ConvSignExtendIntToLong)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_int(3), make_var("t"));
    body = constant_fold(body);

    AssertFoldedLong(body, 3L);
}

// SignExtend(ConstInt(-5))  →  Copy(ConstLong(-5), t)  — negative preserves sign
TEST_F(OptimizerTest, ConvSignExtendIntNegToLong)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_int(-5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedLong(body, -5L);
}

// SignExtend(ConstChar(127))  →  Copy(ConstInt(127), t)
TEST_F(OptimizerTest, ConvSignExtendCharToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_char(127), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 127);
}

// SignExtend(ConstChar(-128))  →  Copy(ConstInt(-128), t)  — min negative char
TEST_F(OptimizerTest, ConvSignExtendCharNegToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_char(-128), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, -128);
}

// SignExtend(ConstLong(42))  →  Copy(ConstLongLong(42), t)
TEST_F(OptimizerTest, ConvSignExtendLongToLongLong)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_long(42L), make_var("t"));
    body = constant_fold(body);

    AssertFoldedLongLong(body, 42LL);
}

// SignExtend(Var)  →  instruction unchanged
TEST_F(OptimizerTest, ConvSignExtendVarUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_var("x"), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_SIGN_EXTEND);
}

// SignExtend(ConstFloat)  →  instruction unchanged (wrong src type)
TEST_F(OptimizerTest, ConvSignExtendWrongTypeUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_float(1.0), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_SIGN_EXTEND);
}

// --- ZERO_EXTEND ---

// ZeroExtend(ConstUChar(200))  →  Copy(ConstUInt(200), t)  — >127 shows zero-extension.
// No dst_kind supplied (sentinel -1), so the legacy source-derived kind (uint) is used.
TEST_F(OptimizerTest, ConvZeroExtendUCharToUInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_const_uchar(200), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUInt(body, 200u);
}

// The integer promotion `unsigned char → int` lowers to the same ZeroExtend as a cast to
// unsigned int, so the destination kind is what distinguishes them.  With dst_kind = int
// the folded constant is a signed int (value still zero-extended), so later folding (e.g.
// a negation) wraps at int's width rather than the unsigned storage width — task #31.
TEST_F(OptimizerTest, ConvZeroExtendUCharToIntPromotion)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_const_uchar(200), make_var("t"));
    body->u.zero_extend.dst_kind = TAC_CONST_INT;
    body                         = constant_fold(body);

    AssertFoldedInt(body, 200);
}

// ZeroExtend(ConstUInt(100000))  →  Copy(ConstULong(100000), t)
TEST_F(OptimizerTest, ConvZeroExtendUIntToULong)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_const_uint(100000u), make_var("t"));
    body = constant_fold(body);

    AssertFoldedULong(body, 100000UL);
}

// ZeroExtend(ConstULong(1234567890))  →  Copy(ConstULongLong(1234567890), t)
TEST_F(OptimizerTest, ConvZeroExtendULongToULongLong)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_const_ulong(1234567890UL), make_var("t"));
    body = constant_fold(body);

    AssertFoldedULongLong(body, 1234567890ULL);
}

// ZeroExtend(Var)  →  instruction unchanged
TEST_F(OptimizerTest, ConvZeroExtendVarUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_var("x"), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_ZERO_EXTEND);
}

// --- TRUNCATE ---

// Truncate(ConstLong(263))  →  Copy(ConstInt(263), t)  — value fits in 32 bits
TEST_F(OptimizerTest, ConvTruncateLongToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_long(263L), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 263);
}

// Truncate(ConstLong(0x100000007))  →  Copy(ConstInt(7), t)  — high bits cut off
TEST_F(OptimizerTest, ConvTruncateLongHighBitsToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_long(0x100000007L), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 7);
}

// Truncate(ConstLongLong(0x100000009))  →  Copy(ConstInt(9), t)
TEST_F(OptimizerTest, ConvTruncateLongLongToInt)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_TRUNCATE,
                                            make_const_long_long(0x100000009LL), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 9);
}

// Truncate(ConstInt(263))  →  Copy(ConstChar(7), t)  — (int8_t)(263 & 0xFF) = 7
TEST_F(OptimizerTest, ConvTruncateIntToChar)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_int(263), make_var("t"));
    body = constant_fold(body);

    AssertFoldedChar(body, 7);
}

// Truncate(ConstInt(-1))  →  Copy(ConstChar(-1), t)  — negative preserved
TEST_F(OptimizerTest, ConvTruncateIntNegToChar)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_int(-1), make_var("t"));
    body = constant_fold(body);

    AssertFoldedChar(body, -1);
}

// Truncate(ConstULongLong(0x100000003))  →  Copy(ConstUInt(3), t)
TEST_F(OptimizerTest, ConvTruncateULongLongToUInt)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_TRUNCATE,
                                            make_const_ulong_long(0x100000003ULL), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedUInt(body, 3u);
}

// Truncate(ConstULong(0x100000005))  →  Copy(ConstUInt(5), t)
TEST_F(OptimizerTest, ConvTruncateULongToUInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_ulong(0x100000005UL), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUInt(body, 5u);
}

// Truncate(ConstUInt(300))  →  Copy(ConstUChar(44), t)  — 300 & 0xFF = 44
TEST_F(OptimizerTest, ConvTruncateUIntToUChar)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_uint(300u), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUChar(body, 44);
}

// Plain `char` has target-defined signedness, so a truncation to char folds to a
// signed-char constant on a signed-char target (x86_64) and an unsigned-char constant on
// an unsigned-char target (besm6).  This lets the later widening fold through the matching
// SIGN_EXTEND/ZERO_EXTEND path.
TEST_F(OptimizerTest, ConvTruncateIntToCharUnsignedTarget)
{
    const Target *saved = target_config;
    target_config       = target_lookup("besm6");
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_int(250), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUChar(body, 250); // (unsigned char)250 = 250, not -6
    target_config = saved;
}

// A signed value converted to unsigned keeps its word on BESM-6: signed values are
// 41 bits wide, so (unsigned)(signed char)-10 is 2^41 - 10, not 2^48 - 10.
TEST_F(OptimizerTest, ConvSignExtendNegativeToUnsignedBesm6)
{
    const Target *saved = target_config;
    target_config       = target_lookup("besm6");
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_char(-10), make_var("t"));
    body->u.sign_extend.dst_kind = TAC_CONST_UINT;
    body                         = constant_fold(body);

    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UINT);
    EXPECT_EQ(body->u.copy.src->u.constant->u.uint_val, (1ULL << 41) - 10);
    target_config = saved;
}

// The same conversion on a 32-bit-int target wraps modulo 2^32.
TEST_F(OptimizerTest, ConvSignExtendNegativeToUnsignedX86)
{
    const Target *saved = target_config;
    target_config       = target_lookup("x86_64");
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_const_char(-10), make_var("t"));
    body->u.sign_extend.dst_kind = TAC_CONST_UINT;
    body                         = constant_fold(body);

    AssertFoldedUInt(body, 4294967286u);
    target_config = saved;
}

// A zero extension takes the source's own bits, even when copy propagation hands
// it a signed char constant: (int)(unsigned char)(signed char)-1 is 255.
TEST_F(OptimizerTest, ConvZeroExtendSignedCharConstant)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_ZERO_EXTEND, make_const_char(-1), make_var("t"));
    body->u.zero_extend.dst_kind = TAC_CONST_INT;
    body                         = constant_fold(body);

    AssertFoldedInt(body, 255);
}

// A long double conversion to an integer carries no destination kind, so the folder
// only answers when the result fits the 32-bit kind; (unsigned long)1.8e19L is left
// to the run time rather than wrapped to 32 bits.
TEST_F(OptimizerTest, ConvLongDoubleToUintTooWideNotFolded)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT,
                                            make_const_long_double(1.8e19L), make_var("t"));
    body                  = constant_fold(body);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT);
}

TEST_F(OptimizerTest, ConvLongDoubleToIntTooWideNotFolded)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_INT,
                                            make_const_long_double(-1e12L), make_var("t"));
    body                  = constant_fold(body);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_LONG_DOUBLE_TO_INT);
}

TEST_F(OptimizerTest, ConvTruncateIntToCharSignedTarget)
{
    const Target *saved = target_config;
    target_config       = target_lookup("x86_64");
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_TRUNCATE, make_const_int(250), make_var("t"));
    body = constant_fold(body);

    AssertFoldedChar(body, -6); // (int8_t)250 = -6
    target_config = saved;
}

// Truncate(Var)  →  instruction unchanged
TEST_F(OptimizerTest, ConvTruncateVarUnchanged)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_TRUNCATE, make_var("x"), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_TRUNCATE);
}

// --- Integer → floating-point ---

// IntToDouble(ConstInt(42))  →  Copy(ConstDouble(42.0), t)
TEST_F(OptimizerTest, ConvIntToDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_INT_TO_DOUBLE, make_const_int(42), make_var("t"));
    body = constant_fold(body);

    AssertFoldedDouble(body, 42.0);
}

// IntToDouble(ConstInt(-7))  →  Copy(ConstDouble(-7.0), t)
TEST_F(OptimizerTest, ConvIntNegToDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_INT_TO_DOUBLE, make_const_int(-7), make_var("t"));
    body = constant_fold(body);

    AssertFoldedDouble(body, -7.0);
}

// UIntToDouble(ConstUInt(1000000))  →  Copy(ConstDouble(1e6), t)
TEST_F(OptimizerTest, ConvUIntToDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_UINT_TO_DOUBLE, make_const_uint(1000000u), make_var("t"));
    body = constant_fold(body);

    AssertFoldedDouble(body, 1e6);
}

// IntToFloat(ConstInt(5))  →  Copy(ConstFloat(5.0), t)
TEST_F(OptimizerTest, ConvIntToFloat)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_INT_TO_FLOAT, make_const_int(5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedFloat(body, 5.0);
}

// UIntToFloat(ConstUInt(100))  →  Copy(ConstFloat(100.0), t)
TEST_F(OptimizerTest, ConvUIntToFloat)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_UINT_TO_FLOAT, make_const_uint(100u), make_var("t"));
    body = constant_fold(body);

    AssertFoldedFloat(body, 100.0);
}

// IntToLongDouble(ConstInt(3))  →  Copy(ConstLongDouble(3.0L), t)
TEST_F(OptimizerTest, ConvIntToLongDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_INT_TO_LONG_DOUBLE, make_const_int(3), make_var("t"));
    body = constant_fold(body);

    AssertFoldedLongDouble(body, 3.0);
}

// UIntToLongDouble(ConstUInt(7))  →  Copy(ConstLongDouble(7.0L), t)
TEST_F(OptimizerTest, ConvUIntToLongDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE, make_const_uint(7u), make_var("t"));
    body = constant_fold(body);

    AssertFoldedLongDouble(body, 7.0);
}

// IntToDouble(ConstFloat)  →  instruction unchanged (wrong src type)
TEST_F(OptimizerTest, ConvIntToDoubleWrongSrcUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_INT_TO_DOUBLE, make_const_float(1.0), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_INT_TO_DOUBLE);
}

// --- Floating-point → integer (truncate toward zero) ---

// DoubleToInt(ConstDouble(3.7))  →  Copy(ConstInt(3), t)
TEST_F(OptimizerTest, ConvDoubleToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_INT, make_const_double(3.7), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 3);
}

// DoubleToInt(ConstDouble(-3.7))  →  Copy(ConstInt(-3), t)  — truncation toward zero
TEST_F(OptimizerTest, ConvDoubleNegToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_INT, make_const_double(-3.7), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, -3);
}

// DoubleToInt(ConstFloat(2.9))  →  Copy(ConstInt(2), t)  — float src also accepted
TEST_F(OptimizerTest, ConvDoubleToIntFromFloat)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_INT, make_const_float(2.9), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 2);
}

// FloatToInt(ConstFloat(2.9))  →  Copy(ConstInt(2), t)
TEST_F(OptimizerTest, ConvFloatToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_FLOAT_TO_INT, make_const_float(2.9), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, 2);
}

// FloatToInt(ConstFloat(-1.9))  →  Copy(ConstInt(-1), t)  — truncation toward zero
TEST_F(OptimizerTest, ConvFloatNegToInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_FLOAT_TO_INT, make_const_float(-1.9), make_var("t"));
    body = constant_fold(body);

    AssertFoldedInt(body, -1);
}

// DoubleToUInt(ConstDouble(3.9))  →  Copy(ConstUInt(3), t)
TEST_F(OptimizerTest, ConvDoubleToUInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_UINT, make_const_double(3.9), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUInt(body, 3u);
}

// FloatToUInt(ConstFloat(100.1))  →  Copy(ConstUInt(100), t)
TEST_F(OptimizerTest, ConvFloatToUInt)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_FLOAT_TO_UINT, make_const_float(100.1), make_var("t"));
    body = constant_fold(body);

    AssertFoldedUInt(body, 100u);
}

// LongDoubleToInt(ConstLongDouble(4.5L))  →  Copy(ConstInt(4), t)
TEST_F(OptimizerTest, ConvLongDoubleToInt)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_INT,
                                            make_const_long_double(4.5L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, 4);
}

// LongDoubleToInt(ConstLongDouble(-9.9L))  →  Copy(ConstInt(-9), t)
TEST_F(OptimizerTest, ConvLongDoubleNegToInt)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_INT,
                                            make_const_long_double(-9.9L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedInt(body, -9);
}

// LongDoubleToUInt(ConstLongDouble(9.9L))  →  Copy(ConstUInt(9), t)
TEST_F(OptimizerTest, ConvLongDoubleToUInt)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT,
                                            make_const_long_double(9.9L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedUInt(body, 9u);
}

// DoubleToInt(ConstInt)  →  instruction unchanged (wrong src type)
TEST_F(OptimizerTest, ConvDoubleToIntWrongSrcUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_INT, make_const_int(3), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_DOUBLE_TO_INT);
}

// --- Float ↔ float ---

// FloatToDouble(ConstFloat(1.5))  →  Copy(ConstDouble(1.5), t)
TEST_F(OptimizerTest, ConvFloatToDouble)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_FLOAT_TO_DOUBLE, make_const_float(1.5), make_var("t"));
    body = constant_fold(body);

    AssertFoldedDouble(body, 1.5);
}

// FloatToDouble(ConstDouble)  →  instruction unchanged (wrong src type)
TEST_F(OptimizerTest, ConvFloatToDoubleWrongSrcUnchanged)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_FLOAT_TO_DOUBLE, make_const_double(1.0), make_var("t"));
    Tac_Instruction *orig = body;
    body                  = constant_fold(body);

    EXPECT_EQ(body, orig);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_FLOAT_TO_DOUBLE);
}

// DoubleToFloat(ConstDouble(2.0))  →  Copy(ConstFloat(2.0), t)
TEST_F(OptimizerTest, ConvDoubleToFloat)
{
    Tac_Instruction *body =
        make_conversion(TAC_INSTRUCTION_DOUBLE_TO_FLOAT, make_const_double(2.0), make_var("t"));
    body = constant_fold(body);

    AssertFoldedFloat(body, 2.0);
}

// FloatToLongDouble(ConstFloat(3.14))  →  Copy(ConstLongDouble(≈3.14), t)
TEST_F(OptimizerTest, ConvFloatToLongDouble)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE,
                                            make_const_float(3.14), make_var("t"));
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
    EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_LONG_DOUBLE);
    EXPECT_NEAR(f128_to_double(body->u.copy.src->u.constant->u.long_double_val), 3.14, 1e-5);
}

// LongDoubleToFloat(ConstLongDouble(2.5L))  →  Copy(ConstFloat(2.5), t)
TEST_F(OptimizerTest, ConvLongDoubleToFloat)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT,
                                            make_const_long_double(2.5L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedFloat(body, 2.5);
}

// DoubleToLongDouble(ConstDouble(1.5))  →  Copy(ConstLongDouble(1.5L), t)
TEST_F(OptimizerTest, ConvDoubleToLongDouble)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE,
                                            make_const_double(1.5), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedLongDouble(body, 1.5);
}

// LongDoubleToDouble(ConstLongDouble(1.5L))  →  Copy(ConstDouble(1.5), t)
TEST_F(OptimizerTest, ConvLongDoubleToDouble)
{
    Tac_Instruction *body = make_conversion(TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE,
                                            make_const_long_double(1.5L), make_var("t"));
    body                  = constant_fold(body);

    AssertFoldedDouble(body, 1.5);
}
