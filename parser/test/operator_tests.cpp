#include "fixture.h"

// Test unary operator: &x
TEST_F(ParserTest, ParseUnaryAddress)
{
    DeclOrStmt *body = GetFunctionBody("void f() { &x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_ADDRESS, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: *x
TEST_F(ParserTest, ParseUnaryDeref)
{
    DeclOrStmt *body = GetFunctionBody("void f() { *x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_DEREF, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: +x
TEST_F(ParserTest, ParseUnaryPlus)
{
    DeclOrStmt *body = GetFunctionBody("void f() { +x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_PLUS, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: -x
TEST_F(ParserTest, ParseUnaryNeg)
{
    DeclOrStmt *body = GetFunctionBody("void f() { -x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_NEG, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: ~x
TEST_F(ParserTest, ParseUnaryBitNot)
{
    DeclOrStmt *body = GetFunctionBody("void f() { ~x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_BIT_NOT, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: !x
TEST_F(ParserTest, ParseUnaryLogNot)
{
    DeclOrStmt *body = GetFunctionBody("void f() { !x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_LOG_NOT, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: ++x
TEST_F(ParserTest, ParseUnaryPreInc)
{
    DeclOrStmt *body = GetFunctionBody("void f() { ++x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_PRE_INC, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: --x
TEST_F(ParserTest, ParseUnaryPreDec)
{
    DeclOrStmt *body = GetFunctionBody("void f() { --x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_UNARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(UNARY_PRE_DEC, stmt->u.expr->u.unary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.unary_op.expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.unary_op.expr->u.var);
}

// Test unary operator: x++
TEST_F(ParserTest, ParseUnaryPostInc)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x++; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_POST_INC, stmt->u.expr->kind);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.post_inc->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.post_inc->u.var);
}

// Test unary operator: x--
TEST_F(ParserTest, ParseUnaryPostDec)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x--; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_POST_DEC, stmt->u.expr->kind);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.post_dec->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.post_dec->u.var);
}

// Test unary operator: sizeof x
TEST_F(ParserTest, ParseSizeofExpr)
{
    DeclOrStmt *body = GetFunctionBody("void f() { sizeof x; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_SIZEOF_EXPR, stmt->u.expr->kind);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.sizeof_expr->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.sizeof_expr->u.var);
}

// Test unary operator: sizeof(int)
TEST_F(ParserTest, ParseSizeofType)
{
    DeclOrStmt *body = GetFunctionBody("void f() { sizeof(int); }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_SIZEOF_TYPE, stmt->u.expr->kind);
    EXPECT_EQ(TYPE_INT, stmt->u.expr->u.sizeof_type->kind);
}

// Test unary operator: _Alignof(int)
TEST_F(ParserTest, ParseAlignof)
{
    DeclOrStmt *body = GetFunctionBody("void f() { _Alignof(int); }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_ALIGNOF, stmt->u.expr->kind);
    EXPECT_EQ(TYPE_INT, stmt->u.expr->u.align_of->kind);
}

// __builtin_va_class(type): a type name, like _Alignof
TEST_F(ParserTest, ParseVaClass)
{
    DeclOrStmt *body = GetFunctionBody("void f() { __builtin_va_class(double); }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_VA_CLASS, stmt->u.expr->kind);
    EXPECT_EQ(TYPE_DOUBLE, stmt->u.expr->u.va_class->kind);
}

// Test binary operator: x * y
TEST_F(ParserTest, ParseBinaryMul)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x * y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_MUL, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x / y
TEST_F(ParserTest, ParseBinaryDiv)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x / y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_DIV, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x % y
TEST_F(ParserTest, ParseBinaryMod)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x % y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_MOD, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x + y
TEST_F(ParserTest, ParseBinaryAdd)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x + y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_ADD, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x - y
TEST_F(ParserTest, ParseBinarySub)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x - y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_SUB, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x << y
TEST_F(ParserTest, ParseBinaryLeftShift)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x << y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_LEFT_SHIFT, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x >> y
TEST_F(ParserTest, ParseBinaryRightShift)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x >> y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_RIGHT_SHIFT, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x < y
TEST_F(ParserTest, ParseBinaryLessThan)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x < y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_LT, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x > y
TEST_F(ParserTest, ParseBinaryGreaterThan)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x > y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_GT, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x <= y
TEST_F(ParserTest, ParseBinaryLessEqual)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x <= y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_LE, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x >= y
TEST_F(ParserTest, ParseBinaryGreaterEqual)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x >= y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_GE, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x == y
TEST_F(ParserTest, ParseBinaryEqual)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x == y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_EQ, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x != y
TEST_F(ParserTest, ParseBinaryNotEqual)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x != y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_NE, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x & y
TEST_F(ParserTest, ParseBinaryBitAnd)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x & y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_BIT_AND, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x ^ y
TEST_F(ParserTest, ParseBinaryBitXor)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x ^ y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_BIT_XOR, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x | y
TEST_F(ParserTest, ParseBinaryBitOr)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x | y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_BIT_OR, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x && y
TEST_F(ParserTest, ParseBinaryLogAnd)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x && y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_LOG_AND, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test binary operator: x || y
TEST_F(ParserTest, ParseBinaryLogOr)
{
    DeclOrStmt *body = GetFunctionBody("void f() { x || y; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    EXPECT_EQ(EXPR_BINARY_OP, stmt->u.expr->kind);
    EXPECT_EQ(BINARY_LOG_OR, stmt->u.expr->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.left->kind);
    EXPECT_STREQ("x", stmt->u.expr->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, stmt->u.expr->u.binary_op.right->kind);
    EXPECT_STREQ("y", stmt->u.expr->u.binary_op.right->u.var);
}

// Test type cast
TEST_F(ParserTest, ParseTypeCast)
{
    DeclOrStmt *body = GetFunctionBody(R"(
typedef int foo;
double bar;
enum { qux = 42 };
int f() {
    return ((foo)-1) + ((bar)-2) + ((qux)-3);
}
)");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_RETURN, stmt->kind);

    Expr *ret = stmt->u.expr;
    EXPECT_EQ(EXPR_BINARY_OP, ret->kind);
    EXPECT_EQ(BINARY_ADD, ret->u.binary_op.op);

    /* return ((foo)-1) + ((bar)-2) + ((qux)-3);
     *  = (((foo)-1) + ((bar)-2)) + ((qux)-3)  (left-associative +)
     * Only (foo) is a type-name cast; (bar) and (qux) are parenthesized
     * primary expressions, so the last two terms are subtractions. */

    Expr *outer_add = ret->u.binary_op.left;
    Expr *last_term = ret->u.binary_op.right;
    EXPECT_EQ(EXPR_BINARY_OP, outer_add->kind);
    EXPECT_EQ(BINARY_ADD, outer_add->u.binary_op.op);

    Expr *cast_foo = outer_add->u.binary_op.left;
    Expr *bar_sub2 = outer_add->u.binary_op.right;

    EXPECT_EQ(EXPR_CAST, cast_foo->kind);
    ASSERT_NE(nullptr, cast_foo->u.cast.type);
    EXPECT_EQ(TYPE_TYPEDEF_NAME, cast_foo->u.cast.type->kind);
    EXPECT_STREQ("foo", cast_foo->u.cast.type->u.typedef_name.name);
    Expr *neg_one = cast_foo->u.cast.expr;
    EXPECT_EQ(EXPR_UNARY_OP, neg_one->kind);
    EXPECT_EQ(UNARY_NEG, neg_one->u.unary_op.op);
    EXPECT_EQ(EXPR_LITERAL, neg_one->u.unary_op.expr->kind);
    EXPECT_EQ(LITERAL_INT, neg_one->u.unary_op.expr->u.literal->kind);
    EXPECT_EQ(1, neg_one->u.unary_op.expr->u.literal->u.int_val);

    EXPECT_EQ(EXPR_BINARY_OP, bar_sub2->kind);
    EXPECT_EQ(BINARY_SUB, bar_sub2->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, bar_sub2->u.binary_op.left->kind);
    EXPECT_STREQ("bar", bar_sub2->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_LITERAL, bar_sub2->u.binary_op.right->kind);
    EXPECT_EQ(LITERAL_INT, bar_sub2->u.binary_op.right->u.literal->kind);
    EXPECT_EQ(2, bar_sub2->u.binary_op.right->u.literal->u.int_val);

    EXPECT_EQ(EXPR_BINARY_OP, last_term->kind);
    EXPECT_EQ(BINARY_SUB, last_term->u.binary_op.op);
    EXPECT_EQ(EXPR_LITERAL, last_term->u.binary_op.left->kind);
    EXPECT_EQ(LITERAL_ENUM, last_term->u.binary_op.left->u.literal->kind);
    EXPECT_STREQ("qux", last_term->u.binary_op.left->u.literal->u.enum_const);
    EXPECT_EQ(EXPR_LITERAL, last_term->u.binary_op.right->kind);
    EXPECT_EQ(LITERAL_INT, last_term->u.binary_op.right->u.literal->kind);
    EXPECT_EQ(3, last_term->u.binary_op.right->u.literal->u.int_val);
}

// Test cast to a type-name that begins with a type qualifier (const).
// Regression: the cast lookahead used to test only is_type_specifier(), so a
// type-name starting with const/volatile/restrict was not recognized as a cast.
TEST_F(ParserTest, ParseCastQualifiedPointer)
{
    DeclOrStmt *body = GetFunctionBody(R"(
int f(char *p) {
    return (const unsigned char *)p != 0;
}
)");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;
    EXPECT_EQ(STMT_RETURN, stmt->kind);

    Expr *ret = stmt->u.expr;
    EXPECT_EQ(EXPR_BINARY_OP, ret->kind);
    EXPECT_EQ(BINARY_NE, ret->u.binary_op.op);

    Expr *cast = ret->u.binary_op.left;
    EXPECT_EQ(EXPR_CAST, cast->kind);
    ASSERT_NE(nullptr, cast->u.cast.type);

    Type *type = cast->u.cast.type;
    EXPECT_EQ(TYPE_POINTER, type->kind);
    ASSERT_NE(nullptr, type->u.pointer.target);
    EXPECT_EQ(TYPE_UCHAR, type->u.pointer.target->kind);
    ASSERT_NE(nullptr, type->u.pointer.target->qualifiers);
    EXPECT_EQ(TYPE_QUALIFIER_CONST, type->u.pointer.target->qualifiers->kind);

    EXPECT_EQ(EXPR_VAR, cast->u.cast.expr->kind);
    EXPECT_STREQ("p", cast->u.cast.expr->u.var);
}

// ---------------------------------------------------------------------------
// Comma operator (C11 6.5.17)
// ---------------------------------------------------------------------------

// `a, b` is a binary node, not two siblings on ->next.  Regression: the parser
// used to hang the right operand off Expr->next -- the argument-list sibling
// link, which no consumer reads on an ordinary expression -- and then return
// the *left* operand, so `b` was dropped and never evaluated.
TEST_F(ParserTest, ParseCommaOperator)
{
    DeclOrStmt *body = GetFunctionBody("void f() { a, b; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_STMT);
    Stmt *stmt = body->u.stmt;

    EXPECT_EQ(STMT_EXPR, stmt->kind);
    Expr *comma = stmt->u.expr;
    EXPECT_EQ(EXPR_BINARY_OP, comma->kind);
    EXPECT_EQ(BINARY_COMMA, comma->u.binary_op.op);
    EXPECT_EQ(nullptr, comma->next);

    EXPECT_EQ(EXPR_VAR, comma->u.binary_op.left->kind);
    EXPECT_STREQ("a", comma->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, comma->u.binary_op.right->kind);
    EXPECT_STREQ("b", comma->u.binary_op.right->u.var);
}

// The comma operator is left-associative: `a, b, c` is `((a, b), c)`.  The old
// code reused the single ->next slot, so the middle operand was overwritten.
TEST_F(ParserTest, ParseCommaChainIsLeftAssociative)
{
    DeclOrStmt *body = GetFunctionBody("void f() { a, b, c; }");
    Stmt *stmt       = body->u.stmt;
    EXPECT_EQ(STMT_EXPR, stmt->kind);

    Expr *outer = stmt->u.expr;
    EXPECT_EQ(EXPR_BINARY_OP, outer->kind);
    EXPECT_EQ(BINARY_COMMA, outer->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, outer->u.binary_op.right->kind);
    EXPECT_STREQ("c", outer->u.binary_op.right->u.var);

    Expr *inner = outer->u.binary_op.left;
    EXPECT_EQ(EXPR_BINARY_OP, inner->kind);
    EXPECT_EQ(BINARY_COMMA, inner->u.binary_op.op);
    EXPECT_EQ(EXPR_VAR, inner->u.binary_op.left->kind);
    EXPECT_STREQ("a", inner->u.binary_op.left->u.var);
    EXPECT_EQ(EXPR_VAR, inner->u.binary_op.right->kind);
    EXPECT_STREQ("b", inner->u.binary_op.right->u.var);
}

// An argument list is a separate production (one parse_assignment_expression()
// per argument), so `f(a, b)` must stay two arguments threaded on ->next with
// no comma node in sight.
TEST_F(ParserTest, ParseCommaNotInArgumentList)
{
    DeclOrStmt *body = GetFunctionBody("void f() { g(a, b); }");
    Stmt *stmt       = body->u.stmt;
    EXPECT_EQ(STMT_EXPR, stmt->kind);

    Expr *call = stmt->u.expr;
    EXPECT_EQ(EXPR_CALL, call->kind);

    Expr *arg1 = call->u.call.args;
    ASSERT_NE(nullptr, arg1);
    EXPECT_EQ(EXPR_VAR, arg1->kind);
    EXPECT_STREQ("a", arg1->u.var);

    Expr *arg2 = arg1->next;
    ASSERT_NE(nullptr, arg2);
    EXPECT_EQ(EXPR_VAR, arg2->kind);
    EXPECT_STREQ("b", arg2->u.var);
    EXPECT_EQ(nullptr, arg2->next);
}

// ...but the extra parentheses in `g((a, b))` make it one argument whose value
// is the comma expression.
TEST_F(ParserTest, ParseCommaAsSingleArgument)
{
    DeclOrStmt *body = GetFunctionBody("void f() { g((a, b)); }");
    Stmt *stmt       = body->u.stmt;
    EXPECT_EQ(STMT_EXPR, stmt->kind);

    Expr *call = stmt->u.expr;
    EXPECT_EQ(EXPR_CALL, call->kind);

    Expr *arg = call->u.call.args;
    ASSERT_NE(nullptr, arg);
    EXPECT_EQ(nullptr, arg->next);
    EXPECT_EQ(EXPR_BINARY_OP, arg->kind);
    EXPECT_EQ(BINARY_COMMA, arg->u.binary_op.op);
    EXPECT_STREQ("a", arg->u.binary_op.left->u.var);
    EXPECT_STREQ("b", arg->u.binary_op.right->u.var);
}

// A declarator list also consumes TOKEN_COMMA, in its own production: `int a, b;`
// stays two declarators and grows no comma node.
TEST_F(ParserTest, ParseCommaNotInDeclaratorList)
{
    DeclOrStmt *body = GetFunctionBody("void f() { int a, b; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_DECL);

    Declaration *decl = body->u.decl;
    EXPECT_EQ(DECL_VAR, decl->kind);

    InitDeclarator *d1 = decl->u.var.declarators;
    ASSERT_NE(nullptr, d1);
    EXPECT_STREQ("a", d1->name);
    ASSERT_NE(nullptr, d1->next);
    EXPECT_STREQ("b", d1->next->name);
    EXPECT_EQ(nullptr, d1->next->next);
}

// So does an initialiser list: `int x[] = { 1, 2 };` stays two initialisers.
TEST_F(ParserTest, ParseCommaNotInInitializerList)
{
    DeclOrStmt *body = GetFunctionBody("void f() { int x[] = { 1, 2 }; }");
    EXPECT_EQ(body->kind, DECL_OR_STMT_DECL);

    InitDeclarator *d = body->u.decl->u.var.declarators;
    ASSERT_NE(nullptr, d);
    ASSERT_NE(nullptr, d->init);
    EXPECT_EQ(INITIALIZER_COMPOUND, d->init->kind);

    InitItem *item = d->init->u.items;
    ASSERT_NE(nullptr, item);
    ASSERT_NE(nullptr, item->next);
    EXPECT_EQ(nullptr, item->next->next);
}
