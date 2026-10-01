//
// Constant expressions
//
#include "fixture.h"

TEST_F(ParserTest, EnumConstant_negative)
{
    // Expected constant expression in enum constant.
    EXPECT_DEATH(program = parse(CreateTempFile("enum { a = x; };")), "");
}

TEST_F(ParserTest, StructBitfieldConstant_negative)
{
    // Expected constant expression in field bitwidth.
    EXPECT_DEATH(program = parse(CreateTempFile("struct { int a : x; };")), "");
}

TEST_F(ParserTest, AlignAsConstant_negative)
{
    // Expected constant expression in alignas() argument.
    EXPECT_DEATH(program = parse(CreateTempFile("int x = _Alignas(y);")), "");
}

TEST_F(ParserTest, StaticAssertConstant_negative)
{
    // Expected constant expression in static assert.
    EXPECT_DEATH(program = parse(CreateTempFile("_Static_assert(x, \"msg\");")), "");
}

TEST_F(ParserTest, CaseConstant_negative)
{
    // Expected constant expression in case label.
    EXPECT_DEATH(program = parse(CreateTempFile("int main() { switch (x) { case y: return; } }")),
                 "");
}

// A cast to any scalar type keeps a constant expression constant.  The parser cannot
// resolve a typedef name, so it accepts one here and leaves the scalar check to typecheck.
TEST_F(ParserTest, CastConstant_scalarTargets)
{
    ExternalDecl *first = GetExternalDecl(R"(
typedef unsigned long size_t;
enum E { A };
_Static_assert((unsigned char)-1 == 255, "");
_Static_assert((signed char)-1 == -1, "");
_Static_assert((unsigned short)-1 > 0, "");
_Static_assert((unsigned)-1 > 0, "");
_Static_assert((unsigned long)-1 > 0, "");
_Static_assert((unsigned long long)-1 > 0, "");
_Static_assert((enum E)0 == A, "");
_Static_assert((size_t)-1 > 0, "");
_Static_assert((int *)0 == 0, "");
)");
    // Every _Static_assert above parsed; count them and check the last one's shape.
    int asserts       = 0;
    Declaration *last = nullptr;
    for (ExternalDecl *d = first; d; d = d->next) {
        if (d->kind == EXTERNAL_DECL_DECLARATION && d->u.declaration->kind == DECL_STATIC_ASSERT) {
            asserts++;
            last = d->u.declaration;
        }
    }
    EXPECT_EQ(9, asserts);
    ASSERT_NE(nullptr, last);
    Expr *cond = last->u.static_assrt.condition;
    ASSERT_EQ(EXPR_BINARY_OP, cond->kind);
    ASSERT_EQ(EXPR_CAST, cond->u.binary_op.left->kind);
    EXPECT_EQ(TYPE_POINTER, cond->u.binary_op.left->u.cast.type->kind);
}

// A cast to an aggregate type is never a constant expression.
TEST_F(ParserTest, CastConstant_aggregateTarget_negative)
{
    EXPECT_DEATH(
        program =
            parse(CreateTempFile("struct S { int a; };\n_Static_assert((struct S)0, \"msg\");\n")),
        "Expected constant expression");
}

// A plain decimal constant is a signed int.
TEST_F(ParserTest, IntegerConstant_plain)
{
    Declaration *decl = GetDeclaration("int x = 5;");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(5, lit->u.int_val);
}

// A `U` suffix (no `L`) makes a small constant `unsigned int`, not signed.
TEST_F(ParserTest, IntegerConstant_uSuffix)
{
    Declaration *decl = GetDeclaration("unsigned x = 5U;");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_UINT, lit->kind);
    EXPECT_EQ(5u, lit->u.uint_val);
}

// A `U` suffix alone is enough to keep all 48 bits of a wide unsigned constant:
// the value exceeds the host's unsigned int, so it widens to unsigned long, but
// no explicit `L` is required and the full value survives.
TEST_F(ParserTest, IntegerConstant_uSuffixWide)
{
    Declaration *decl = GetDeclaration("unsigned long x = 0xFFFFFFFFFFFFU;");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_ULONG, lit->kind);
    EXPECT_EQ(0xFFFFFFFFFFFFUL, lit->u.ulong_val);
}

// The `UL` suffix still selects unsigned long.
TEST_F(ParserTest, IntegerConstant_ulSuffix)
{
    Declaration *decl = GetDeclaration("unsigned long x = 5UL;");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_ULONG, lit->kind);
    EXPECT_EQ(5UL, lit->u.ulong_val);
}

// --- Multi-character constants (GCC-style big-endian byte packing) ---

// A single character is a plain int (unchanged behavior).
TEST_F(ParserTest, CharConstant_single)
{
    Declaration *decl = GetDeclaration("int x = 'a';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ('a', lit->u.int_val);
}

// '\0' is a single zero byte, type int.
TEST_F(ParserTest, CharConstant_nul)
{
    Declaration *decl = GetDeclaration("int x = '\\0';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0, lit->u.int_val);
}

// '\xFF' is a single byte 0xFF (not sign-extended), type int.
TEST_F(ParserTest, CharConstant_byteEscape)
{
    Declaration *decl = GetDeclaration("int x = '\\xFF';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0xFF, lit->u.int_val);
}

// Two ASCII characters pack big-endian into an int: 'ab' -> 0x6162.
TEST_F(ParserTest, CharConstant_twoBytes)
{
    Declaration *decl = GetDeclaration("int x = 'ab';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0x6162, lit->u.int_val);
}

// Three ASCII characters: 'abc' -> 0x616263.
TEST_F(ParserTest, CharConstant_threeBytes)
{
    Declaration *decl = GetDeclaration("int x = 'abc';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0x616263, lit->u.int_val);
}

// Escapes contribute one byte each and pack: '\n\t' -> 0x0A09.
TEST_F(ParserTest, CharConstant_escapesPacked)
{
    Declaration *decl = GetDeclaration("int x = '\\n\\t';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0x0A09, lit->u.int_val);
}

// A single 2-byte UTF-8 character keeps its raw bytes: 'é' (C3 A9) -> 0xC3A9.
TEST_F(ParserTest, CharConstant_utf8TwoByte)
{
    Declaration *decl = GetDeclaration("int x = '\xC3\xA9';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0xC3A9, lit->u.int_val);
}

// A 4-byte UTF-8 character (U+1F600, F0 9F 98 80) packs to a value that exceeds
// a 32-bit int yet survives in the 64-bit host storage, still type int.
TEST_F(ParserTest, CharConstant_utf8FourByteWide)
{
    Declaration *decl = GetDeclaration("int x = '\xF0\x9F\x98\x80';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_INT, lit->kind);
    EXPECT_EQ(0xF09F9880, lit->u.int_val);
}

// Exactly six packed bytes is unsigned (48-bit): 'abcdef' -> 0x616263646566.
TEST_F(ParserTest, CharConstant_sixBytesUnsigned)
{
    Declaration *decl = GetDeclaration("unsigned x = 'abcdef';");
    Literal *lit      = decl->u.var.declarators->init->u.expr->u.literal;
    EXPECT_EQ(LITERAL_UINT, lit->kind);
    EXPECT_EQ(0x616263646566ULL, lit->u.uint_val);
}

// More than eight packed bytes is a fatal error.
TEST_F(ParserTest, CharConstant_tooLong_negative)
{
    EXPECT_DEATH(program = parse(CreateTempFile("int x = 'abcdefghi';")), "");
}

// A stray UTF-8 continuation byte (no lead) is a fatal error.
TEST_F(ParserTest, CharConstant_strayContinuation_negative)
{
    EXPECT_DEATH(program = parse(CreateTempFile("int x = '\x80';")), "");
}

// A UTF-8 lead byte followed by a non-continuation byte is a fatal error.
TEST_F(ParserTest, CharConstant_badUtf8Sequence_negative)
{
    EXPECT_DEATH(program = parse(CreateTempFile("int x = '\xC3z';")), "");
}
