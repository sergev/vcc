#include <gtest/gtest.h>
#include <limits.h>
#include <stdint.h>

#include "ast.h"
#include "semantic.h"
#include "tac.h"
#include "xalloc.h"

// Tests for new_static_init_from_literal().
// Only types accepted by is_arithmetic() are tested as valid targets:
// TYPE_INT, TYPE_UINT, TYPE_LONG, TYPE_ULONG, TYPE_CHAR, TYPE_UCHAR,
// TYPE_SCHAR, TYPE_DOUBLE.

class ConstConvertTest : public ::testing::Test {
protected:
    Tac_StaticInit *result = nullptr;

    void TearDown() override
    {
        tac_free_static_init(result);
        result = nullptr;
        EXPECT_EQ(xtotal_allocated_size(), 0u);
    }

    static Literal int_lit(int v)
    {
        Literal l   = {};
        l.kind      = LITERAL_INT;
        l.u.int_val = v;
        return l;
    }
    static Literal char_lit(char v)
    {
        Literal l    = {};
        l.kind       = LITERAL_CHAR;
        l.u.char_val = v;
        return l;
    }
    static Literal float_lit(double v)
    {
        Literal l    = {};
        l.kind       = LITERAL_FLOAT;
        l.u.real_val = v;
        return l;
    }
    static Type make_type(TypeKind k)
    {
        Type t = {};
        t.kind = k;
        return t;
    }
};

// TYPE_INT

TEST_F(ConstConvertTest, IntFromInt)
{
    auto lit  = int_lit(42);
    auto type = make_type(TYPE_INT);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(result->u.int_val, 42);
}

TEST_F(ConstConvertTest, IntFromChar)
{
    auto lit  = char_lit(-5); // sign-extended to -5
    auto type = make_type(TYPE_INT);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(result->u.int_val, (char)-5);
}

TEST_F(ConstConvertTest, IntFromFloat)
{
    auto lit  = float_lit(3.7); // truncated to 3
    auto type = make_type(TYPE_INT);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(result->u.int_val, 3);
}

// TYPE_UINT

TEST_F(ConstConvertTest, UIntFromInt)
{
    auto lit  = int_lit(42);
    auto type = make_type(TYPE_UINT);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U32);
    EXPECT_EQ(result->u.uint_val, (uint32_t)42);
}

TEST_F(ConstConvertTest, UIntFromNegInt)
{
    auto lit  = int_lit(-1); // wraps to UINT32_MAX (4-byte unsigned int)
    auto type = make_type(TYPE_UINT);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U32);
    EXPECT_EQ(result->u.uint_val, (uint32_t)UINT32_MAX);
}

// TYPE_LONG

TEST_F(ConstConvertTest, LongFromInt)
{
    auto lit  = int_lit(42);
    auto type = make_type(TYPE_LONG);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I64);
    EXPECT_EQ(result->u.long_val, (int64_t)42);
}

TEST_F(ConstConvertTest, LongFromNegInt)
{
    auto lit  = int_lit(-1); // sign-extended to -1LL
    auto type = make_type(TYPE_LONG);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I64);
    EXPECT_EQ(result->u.long_val, (int64_t)-1);
}

// TYPE_ULONG

TEST_F(ConstConvertTest, ULongFromInt)
{
    auto lit  = int_lit(42);
    auto type = make_type(TYPE_ULONG);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U64);
    EXPECT_EQ(result->u.ulong_val, (uint64_t)42);
}

TEST_F(ConstConvertTest, ULongFromFloat)
{
    auto lit  = float_lit(1e18);
    auto type = make_type(TYPE_ULONG);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U64);
    EXPECT_EQ(result->u.ulong_val, (uint64_t)1e18);
}

// TYPE_CHAR

TEST_F(ConstConvertTest, CharFromChar)
{
    auto lit  = char_lit('A');
    auto type = make_type(TYPE_CHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I8);
    EXPECT_EQ(result->u.char_val, (int8_t)65);
}

TEST_F(ConstConvertTest, CharTruncation)
{
    auto lit  = int_lit(300); // 300 & 0xFF = 44
    auto type = make_type(TYPE_CHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I8);
    EXPECT_EQ(result->u.char_val, (int8_t)44);
}

// TYPE_SCHAR

TEST_F(ConstConvertTest, SCharFromInt)
{
    auto lit  = int_lit(65);
    auto type = make_type(TYPE_SCHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I8);
    EXPECT_EQ(result->u.char_val, (int8_t)65);
}

TEST_F(ConstConvertTest, SCharTruncation)
{
    auto lit  = int_lit(300); // 300 & 0xFF = 44
    auto type = make_type(TYPE_SCHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_I8);
    EXPECT_EQ(result->u.char_val, (int8_t)44);
}

// TYPE_UCHAR

TEST_F(ConstConvertTest, UCharFromInt)
{
    auto lit  = int_lit(200);
    auto type = make_type(TYPE_UCHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U8);
    EXPECT_EQ(result->u.uchar_val, (uint8_t)200);
}

TEST_F(ConstConvertTest, UCharFromNegInt)
{
    auto lit  = int_lit(-1); // wraps to 255
    auto type = make_type(TYPE_UCHAR);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_U8);
    EXPECT_EQ(result->u.uchar_val, (uint8_t)255);
}

// TYPE_DOUBLE

TEST_F(ConstConvertTest, DoubleFromInt)
{
    auto lit  = int_lit(42);
    auto type = make_type(TYPE_DOUBLE);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_DOUBLE);
    EXPECT_DOUBLE_EQ(result->u.double_val, 42.0);
}

TEST_F(ConstConvertTest, DoubleFromFloat)
{
    auto lit  = float_lit(3.14);
    auto type = make_type(TYPE_DOUBLE);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_DOUBLE);
    EXPECT_DOUBLE_EQ(result->u.double_val, 3.14);
}

TEST_F(ConstConvertTest, DoubleFromChar)
{
    auto lit  = char_lit('A'); // 65 -> 65.0
    auto type = make_type(TYPE_DOUBLE);
    result    = new_static_init_from_literal(&type, &lit);
    EXPECT_EQ(result->kind, TAC_STATIC_INIT_DOUBLE);
    EXPECT_DOUBLE_EQ(result->u.double_val, 65.0);
}

// Error paths: non-arithmetic target type

TEST_F(ConstConvertTest, NonArithmeticTypeDies)
{
    auto lit  = int_lit(0);
    auto type = make_type(TYPE_VOID);
    EXPECT_DEATH(new_static_init_from_literal(&type, &lit), "Invalid static initializer");
}

TEST_F(ConstConvertTest, StructTypeDies)
{
    auto lit  = int_lit(0);
    auto type = make_type(TYPE_STRUCT);
    EXPECT_DEATH(new_static_init_from_literal(&type, &lit), "Invalid static initializer");
}

// Error paths: unsupported literal kinds

TEST_F(ConstConvertTest, StringLiteralDies)
{
    char str[]       = "test";
    Literal lit      = {};
    lit.kind         = LITERAL_STRING;
    lit.u.string_val = str;
    auto type        = make_type(TYPE_INT);
    EXPECT_DEATH(new_static_init_from_literal(&type, &lit), "Cannot convert string");
}

TEST_F(ConstConvertTest, EnumLiteralDies)
{
    Literal lit = {};
    lit.kind    = LITERAL_ENUM;
    auto type   = make_type(TYPE_INT);
    EXPECT_DEATH(new_static_init_from_literal(&type, &lit), "Cannot convert enum");
}
