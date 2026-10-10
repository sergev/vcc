//
// Unit tests for type coercion functions:
//   coerce_for_assignment()  — used for assignments, function args, and returns
//   get_common_type()        — usual arithmetic conversions for binary operators
//   convert_to_kind()        — integer promotions for unary operators
//
// Each test writes a small C snippet, parses it, runs typecheck_program(), and
// then inspects the annotated AST to verify that casts were (or were not)
// inserted, and that fatal_error() fires when types are incompatible.
//
// C11 references:
//   §6.3.1.1  — Integer promotions
//   §6.3.1.8  — Usual arithmetic conversions
//   §6.3.2.3  — Pointer conversions / null pointer constant
//   §6.5.16.1 — Assignment constraints
//   §6.5.2.2  — Function call argument conversions
//   §6.8.6.4  — Return statement type constraints
//
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "internal.h"
#include "parser.h"
#include "scanner.h"
#include "semantic.h"
#include "structtab.h"
#include "symtab.h"
#include "target.h"
#include "typetab.h"
#include "xalloc.h"

class CoercionTest : public ::testing::Test {
    const std::string test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    FILE *input_file{};

public:
    Program *program{};

protected:
    void SetUp() override
    {
        auto filename = test_name + ".c";
        input_file    = fopen(filename.c_str(), "w+");
        ASSERT_NE(nullptr, input_file);
        semantic_debug = 0;
    }

    void TearDown() override
    {
        fclose(input_file);
        if (program) {
            free_program(program);
        }
        symtab_print();
        structtab_print();
        nametab_destroy();
        symtab_destroy();
        structtab_destroy();
        typetab_destroy();
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    FILE *CreateTempFile(const char *content)
    {
        fwrite(content, 1, strlen(content), input_file);
        rewind(input_file);
        return input_file;
    }

    void ParseProgram(const char *content)
    {
        program = parse(CreateTempFile(content));
        EXPECT_NE(nullptr, program);
    }

    // Return the return-expression of the first function in the program.
    // Assumes the program has a single function whose body is a single STMT_RETURN.
    Expr *ReturnExpr()
    {
        ExternalDecl *ext = program->decls;
        Stmt *ret         = ext->u.function.body->u.compound->u.stmt;
        EXPECT_EQ(ret->kind, STMT_RETURN);
        return ret->u.expr;
    }

    // Return the return-expression of the Nth function (0-based) in the program.
    Expr *ReturnExprOfFunc(int n)
    {
        ExternalDecl *ext = program->decls;
        for (int i = 0; i < n; ++i)
            ext = ext->next;
        Stmt *ret = ext->u.function.body->u.compound->u.stmt;
        EXPECT_EQ(ret->kind, STMT_RETURN);
        return ret->u.expr;
    }

    // Return the first argument of the call expression in the Nth external decl.
    // Assumes body is one STMT_EXPR containing one EXPR_CALL.
    Expr *FirstCallArg(int n)
    {
        ExternalDecl *ext = program->decls;
        for (int i = 0; i < n; ++i)
            ext = ext->next;
        Stmt *s = ext->u.function.body->u.compound->u.stmt;
        EXPECT_EQ(s->kind, STMT_EXPR);
        Expr *call = s->u.expr;
        EXPECT_EQ(call->kind, EXPR_CALL);
        return call->u.call.args;
    }

    // Return the EXPR_ASSIGN from the first statement of the first function.
    // Assumes body is one STMT_EXPR containing one EXPR_ASSIGN.
    Expr *AssignExpr()
    {
        ExternalDecl *ext = program->decls;
        Stmt *s           = ext->u.function.body->u.compound->u.stmt;
        EXPECT_EQ(s->kind, STMT_EXPR);
        Expr *e = s->u.expr;
        EXPECT_EQ(e->kind, EXPR_ASSIGN);
        return e;
    }

    // Return the EXPR_ASSIGN from the first statement of the Nth function (0-based).
    Expr *AssignExprOfFunc(int n)
    {
        ExternalDecl *ext = program->decls;
        for (int i = 0; i < n; ++i)
            ext = ext->next;
        Stmt *s = ext->u.function.body->u.compound->u.stmt;
        EXPECT_EQ(s->kind, STMT_EXPR);
        Expr *e = s->u.expr;
        EXPECT_EQ(e->kind, EXPR_ASSIGN);
        return e;
    }
};

// ─── A. Same type — coerce_for_assignment() must not insert a cast ───────────

TEST_F(CoercionTest, SameInt)
{
    ParseProgram("int f(int x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->kind, EXPR_VAR);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, SameDouble)
{
    ParseProgram("double f(double x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->kind, EXPR_VAR);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, SameChar)
{
    ParseProgram("char f(char x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->kind, EXPR_VAR);
    EXPECT_EQ(ret->type->kind, TYPE_CHAR);
}

TEST_F(CoercionTest, SameIntPtr)
{
    ParseProgram("int *f(int *x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->kind, EXPR_VAR);
    ASSERT_NE(ret->type, nullptr);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_INT);
}

// ─── B. Arithmetic → arithmetic — cast must be inserted ──────────────────────

TEST_F(CoercionTest, IntToDouble)
{
    ParseProgram("double f(int x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(ret->u.cast.expr->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, DoubleToInt)
{
    ParseProgram("int f(double x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, CharToInt)
{
    ParseProgram("int f(char x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_CHAR);
}

TEST_F(CoercionTest, IntToChar)
{
    ParseProgram("char f(int x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_CHAR);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, FloatLiteralHasTypeFloat)
{
    ParseProgram("float f(void) { return 1.0f; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->type->kind, TYPE_FLOAT);
}

TEST_F(CoercionTest, DoubleLiteralHasTypeDouble)
{
    ParseProgram("double f(void) { return 1.0; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, IntToFloat)
{
    ParseProgram("float f(int x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_FLOAT);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, FloatToDouble)
{
    ParseProgram("double f(float x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_FLOAT);
}

TEST_F(CoercionTest, CharToDouble)
{
    ParseProgram("double f(char x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, LongToInt)
{
    ParseProgram("int f(long x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.cast.expr->type->kind, TYPE_LONG);
}

// ─── C. Null pointer constant → pointer ──────────────────────────────────────

// The integer literal 0 is a null pointer constant (§6.3.2.3) and must be
// implicitly converted to any pointer type.

TEST_F(CoercionTest, NullLiteralToIntPtr)
{
    ParseProgram("int *f() { return 0; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    ASSERT_NE(ret->type, nullptr);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_INT);
}

TEST_F(CoercionTest, NullLiteralToCharPtr)
{
    ParseProgram("char *f() { return 0; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_CHAR);
}

TEST_F(CoercionTest, NullLiteralToVoidPtr)
{
    ParseProgram("void *f() { return 0; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_VOID);
}

// ─── D. void* ↔ pointer — bidirectional implicit conversion ──────────────────

// §6.3.2.3: A pointer to void may be converted to/from a pointer to any object
// type; the resulting pointer shall be equal to the original.

TEST_F(CoercionTest, VoidPtrToIntPtr)
{
    ParseProgram("int *f(void *p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    ASSERT_NE(ret->type, nullptr);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_INT);
}

TEST_F(CoercionTest, IntPtrToVoidPtr)
{
    ParseProgram("void *f(int *p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    ASSERT_NE(ret->type, nullptr);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_VOID);
}

TEST_F(CoercionTest, VoidPtrToCharPtr)
{
    ParseProgram("char *f(void *p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_CHAR);
}

TEST_F(CoercionTest, CharPtrToVoidPtr)
{
    ParseProgram("void *f(char *p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_VOID);
}

TEST_F(CoercionTest, VoidPtrRoundTrip)
{
    // void* → int* and int* → void* in one function; no error expected.
    ParseProgram(R"(
        void *g(int *p) { return p; }
        int  *h(void *p) { return p; }
    )");
    typecheck_program(program);
}

// ─── E. Typedef resolution ────────────────────────────────────────────────────

TEST_F(CoercionTest, TypedefArithmetic)
{
    ParseProgram("typedef int MyInt; double f(MyInt x) { return x; }");
    typecheck_program(program);
    Expr *ret = ReturnExprOfFunc(1); // typedef decl is decl 0, f is decl 1
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, TypedefPtrToVoidPtr)
{
    ParseProgram("typedef int *IntPtr; void *f(IntPtr p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExprOfFunc(1);
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_VOID);
}

TEST_F(CoercionTest, VoidPtrToTypedefPtr)
{
    ParseProgram("typedef int *IntPtr; IntPtr f(void *p) { return p; }");
    typecheck_program(program);
    Expr *ret = ReturnExprOfFunc(1);
    ASSERT_EQ(ret->kind, EXPR_CAST);
    EXPECT_EQ(ret->type->kind, TYPE_POINTER);
    EXPECT_EQ(ret->type->u.pointer.target->kind, TYPE_INT);
}

// ─── F. Function argument coercion ───────────────────────────────────────────

// §6.5.2.2 p7: arguments are converted as if by assignment to the param type.

TEST_F(CoercionTest, FuncArgIntToDouble)
{
    // g is decl 0 (prototype), f is decl 1 (definition with the call).
    ParseProgram(R"(
        void g(double x);
        void f(int v) { g(v); }
    )");
    typecheck_program(program);
    Expr *arg = FirstCallArg(1);
    ASSERT_EQ(arg->kind, EXPR_CAST);
    EXPECT_EQ(arg->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, FuncArgNullToIntPtr)
{
    ParseProgram(R"(
        void g(int *p);
        void f(void) { g(0); }
    )");
    typecheck_program(program);
    Expr *arg = FirstCallArg(1);
    ASSERT_EQ(arg->kind, EXPR_CAST);
    EXPECT_EQ(arg->type->kind, TYPE_POINTER);
    EXPECT_EQ(arg->type->u.pointer.target->kind, TYPE_INT);
}

TEST_F(CoercionTest, FuncArgVoidPtrToIntPtr)
{
    ParseProgram(R"(
        void g(int *p);
        void f(void *vp) { g(vp); }
    )");
    typecheck_program(program);
    Expr *arg = FirstCallArg(1);
    ASSERT_EQ(arg->kind, EXPR_CAST);
    EXPECT_EQ(arg->type->kind, TYPE_POINTER);
    EXPECT_EQ(arg->type->u.pointer.target->kind, TYPE_INT);
}

TEST_F(CoercionTest, FuncArgIntPtrToVoidPtr)
{
    ParseProgram(R"(
        void g(void *p);
        void f(int *q) { g(q); }
    )");
    typecheck_program(program);
    Expr *arg = FirstCallArg(1);
    ASSERT_EQ(arg->kind, EXPR_CAST);
    EXPECT_EQ(arg->type->kind, TYPE_POINTER);
    EXPECT_EQ(arg->type->u.pointer.target->kind, TYPE_VOID);
}

TEST_F(CoercionTest, FuncArgArrayDecay)
{
    // §6.7.6.3 p7: int[42] param adjusts to int*; array arg decays to int*.
    // No cast inserted — argument is a pointer-typed expression, not EXPR_CAST.
    ParseProgram(R"(
        int foo[42];
        int bar(int[42]);
        void quz() { bar(foo); }
    )");
    typecheck_program(program);
    Expr *arg = FirstCallArg(2); // decls: [0]=foo [1]=bar [2]=quz
    EXPECT_NE(arg->kind, EXPR_CAST);
    EXPECT_EQ(arg->type->kind, TYPE_POINTER);
    EXPECT_EQ(arg->type->u.pointer.target->kind, TYPE_INT);
}

// ─── G. Error cases — incompatible types must call fatal_error() ─────────────

TEST_F(CoercionTest, Error_IntToIntPtr)
{
    ParseProgram("int *f(int x) { return x; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_IntPtrToInt)
{
    ParseProgram("int f(int *p) { return p; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_IncompatiblePtrs)
{
    ParseProgram("double *f(int *p) { return p; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_CharPtrToIntPtr)
{
    ParseProgram("int *f(char *p) { return p; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_FloatToPtr)
{
    ParseProgram("int *f(float x) { return x; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_StructToInt)
{
    ParseProgram("struct S { int x; }; int f(struct S s) { return s; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, Error_FuncArgIncompat)
{
    ParseProgram(R"(
        void g(int *p);
        void f(double *q) { g(q); }
    )");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

// ─── H. get_common_type() — usual arithmetic conversions ─────────────────────

// The result type of a binary arithmetic expression and whether implicit casts
// were inserted into the operands verify the usual arithmetic conversions.

TEST_F(CoercionTest, CharPlusChar_PromotesToInt)
{
    // §6.3.1.1: char operands are promoted to int before arithmetic.
    ParseProgram("int f(char a, char b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    // The return has type int; the + expression itself carries TYPE_INT.
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    // Both operands should have been cast to int.
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, IntPlusDouble_GivesDouble)
{
    // §6.3.1.8: double dominates; int operand is widened.
    ParseProgram("double f(int a, double b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
    // int operand must be cast to double; double operand stays as-is.
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, IntPlusLong_GivesLong)
{
    // §6.3.1.8: larger integer rank dominates.
    ParseProgram("long f(int a, long b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_LONG);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_LONG);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
}

TEST_F(CoercionTest, FloatPlusFloat_GivesFloat)
{
    // Same float type — no cast needed, result is float.
    ParseProgram("float f(float a, float b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_FLOAT);
    // Both operands already float — no cast expected.
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
}

TEST_F(CoercionTest, FloatPlusInt_GivesFloat)
{
    // §6.3.1.8 step 4: if either operand is float, the other converts to float.
    ParseProgram("float f(float a, int b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_FLOAT);
    // float operand stays as-is; int operand must be cast to float.
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_FLOAT);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_FLOAT);
}

// ─── I. convert_to_kind() — integer promotions via unary operators ────────────

// §6.3.1.1: char/short operands of unary +/-/~ are promoted to int.

TEST_F(CoercionTest, CharUnaryMinus_PromotesToInt)
{
    ParseProgram("int f(char x) { return -x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_NEG);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    // Operand should have been promoted.
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, CharUnaryBitNot_PromotesToInt)
{
    ParseProgram("int f(char x) { return ~x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_BIT_NOT);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, ShortUnaryPlus_PromotesToInt)
{
    // C11 §6.3.1.1: short is promoted to int before unary +.
    ParseProgram("int f(short x) { return +x; }");
    typecheck_program(program);
    // The unary + itself yields TYPE_INT after promotion; no outer cast needed.
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_PLUS);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    // Operand must have been promoted to int.
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

// ─── J. Struct / union coercion ───────────────────────────────────────────────

TEST_F(CoercionTest, SameStructAssign)
{
    // Assigning a struct to the same struct type is valid (§6.5.16.1).
    ParseProgram("struct A { int x; }; struct A f(struct A a) { return a; }");
    typecheck_program(program);
    Expr *ret = ReturnExprOfFunc(1);
    // No cast required — struct types match.
    EXPECT_EQ(ret->kind, EXPR_VAR);
}

TEST_F(CoercionTest, DiffStructError)
{
    // Assigning struct B to struct A must be rejected (§6.5.16.1 — types must
    // be compatible; two distinct struct tags are never compatible).
    ParseProgram(R"(
        struct A { int x; };
        struct B { int x; };
        struct A f(struct B b) { return b; }
    )");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

TEST_F(CoercionTest, SameUnionAssign)
{
    // Assigning a union to the same union type is valid (§6.5.16.1).
    ParseProgram("union U { int x; }; union U f(union U u) { return u; }");
    typecheck_program(program);
    Expr *ret = ReturnExprOfFunc(1);
    EXPECT_EQ(ret->kind, EXPR_VAR);
}

TEST_F(CoercionTest, DiffUnionError)
{
    // Assigning union B to union A must be rejected (different tags).
    ParseProgram(R"(
        union A { int x; };
        union B { int x; };
        union A f(union B b) { return b; }
    )");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

// ─── H additions — get_common_type() with short and float ────────────────────

TEST_F(CoercionTest, ShortPlusShort_GivesInt)
{
    // §6.3.1.1: both short operands are promoted to int; result is int.
    ParseProgram("int f(short a, short b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, ShortPlusInt_GivesInt)
{
    // §6.3.1.1: short is promoted to int; combined with int the result is int.
    ParseProgram("int f(short a, int b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, IntPlusFloat_GivesFloat)
{
    // §6.3.1.8 step 4: float dominates int; symmetric form of FloatPlusInt.
    ParseProgram("float f(int a, float b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_FLOAT);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_FLOAT);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_FLOAT);
}

TEST_F(CoercionTest, FloatPlusDouble_GivesDouble)
{
    // §6.3.1.8 step 3: double dominates float; float operand is widened.
    ParseProgram("double f(float a, double b) { return a + b; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(ret->u.binary_op.right->kind, EXPR_VAR);
    EXPECT_EQ(ret->u.binary_op.right->type->kind, TYPE_DOUBLE);
}

// ─── I additions — unary short promotions ────────────────────────────────────

TEST_F(CoercionTest, ShortUnaryMinus_PromotesToInt)
{
    // §6.3.1.1: short is promoted to int before unary -.
    ParseProgram("int f(short x) { return -x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_NEG);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, ShortUnaryBitNot_PromotesToInt)
{
    // §6.3.1.1: short is promoted to int before unary ~.
    ParseProgram("int f(short x) { return ~x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_BIT_NOT);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, UShortUnaryPlus_PromotesToInt)
{
    // §6.3.1.1: unsigned short is promoted to int before unary +.
    ParseProgram("int f(unsigned short x) { return +x; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_UNARY_OP);
    EXPECT_EQ(ret->u.unary_op.op, UNARY_PLUS);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.unary_op.expr->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.unary_op.expr->type->kind, TYPE_INT);
}

// ─── K. Compound assignment operators ────────────────────────────────────────
//
// §6.5.16.2: compound assignment (arithmetic ops)
// §6.5.16.3: compound assignment (bitwise ops)
// §6.5.6 p2: pointer arithmetic for += and -=
//
// All test functions use only parameters so the body is a single STMT_EXPR,
// allowing AssignExpr() to retrieve the EXPR_ASSIGN directly.

// ─── K.1 ASSIGN_SIMPLE via direct assignment statement ────────────────────────

TEST_F(CoercionTest, SimpleAssign_SameType_NoCast)
{
    // Same type: no cast should be inserted.
    ParseProgram("void f(int x, int y) { x = y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_SIMPLE);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    EXPECT_EQ(e->u.assign.value->kind, EXPR_VAR);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, SimpleAssign_ArithCast)
{
    // int rhs assigned to double lhs: rhs must be widened.
    ParseProgram("void f(double x, int y) { x = y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_SIMPLE);
    EXPECT_EQ(e->type->kind, TYPE_DOUBLE);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, SimpleAssign_NullPtrConst)
{
    // Integer constant 0 is a null pointer constant (§6.3.2.3); must cast to int*.
    ParseProgram("void f(int *p, int n) { p = 0; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_SIMPLE);
    ASSERT_EQ(e->type->kind, TYPE_POINTER);
    EXPECT_EQ(e->type->u.pointer.target->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_POINTER);
}

// ─── K.2 ASSIGN_ADD / ASSIGN_SUB with pointer lhs ────────────────────────────

TEST_F(CoercionTest, PlusAssign_Ptr_IntRhs_CastToLong)
{
    // §6.5.6 p2: int offset for pointer += must be converted to long.
    ParseProgram("void f(int *p, int n) { p += n; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    ASSERT_EQ(e->type->kind, TYPE_POINTER);
    EXPECT_EQ(e->type->u.pointer.target->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_LONG);
}

TEST_F(CoercionTest, PlusAssign_Ptr_CharRhs_CastToLong)
{
    // char offset must also be widened to long.
    ParseProgram("void f(int *p, char n) { p += n; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_LONG);
}

TEST_F(CoercionTest, PlusAssign_Ptr_LongRhs_NoCast)
{
    // long offset is already the target kind — convert_to_kind must not insert a cast.
    ParseProgram("void f(int *p, long n) { p += n; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    EXPECT_EQ(e->u.assign.value->kind, EXPR_VAR);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_LONG);
}

TEST_F(CoercionTest, MinusAssign_Ptr_IntRhs_CastToLong)
{
    // -= on a pointer is symmetric with +=.
    ParseProgram("void f(int *p, int n) { p -= n; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_SUB);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_LONG);
    ASSERT_EQ(e->type->kind, TYPE_POINTER);
}

// ─── K.3 ASSIGN_ADD / ASSIGN_SUB with arithmetic lhs ─────────────────────────

TEST_F(CoercionTest, PlusAssign_Int_DoubleRhs_NoCast)
{
    // x += y is x = x + y (C11 6.5.16.2p3): computed in double, so the double rhs
    // stays as it is; the result is converted back to int.
    ParseProgram("void f(int x, double y) { x += y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_VAR);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, PlusAssign_Double_IntRhs_Cast)
{
    // int rhs widened to double lhs type.
    ParseProgram("void f(double x, int y) { x += y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    EXPECT_EQ(e->type->kind, TYPE_DOUBLE);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, MinusAssign_Float_IntRhs_Cast)
{
    // int rhs widened to float lhs type.
    ParseProgram("void f(float x, int y) { x -= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_SUB);
    EXPECT_EQ(e->type->kind, TYPE_FLOAT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_FLOAT);
}

TEST_F(CoercionTest, PlusAssign_Int_IntRhs_NoCast)
{
    // Same arithmetic type: convert_to_type must not insert a cast.
    ParseProgram("void f(int x, int y) { x += y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_ADD);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    EXPECT_EQ(e->u.assign.value->kind, EXPR_VAR);
}

// ─── K.4 All other arithmetic compound operators ──────────────────────────────

TEST_F(CoercionTest, MulAssign_Double_IntRhs_Cast)
{
    ParseProgram("void f(double x, int y) { x *= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_MUL);
    EXPECT_EQ(e->type->kind, TYPE_DOUBLE);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_DOUBLE);
}

TEST_F(CoercionTest, DivAssign_Int_DoubleRhs_NoCast)
{
    ParseProgram("void f(int x, double y) { x /= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_DIV);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_VAR);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_DOUBLE);
}

// A long rhs wider than int keeps its type: the remainder is computed in long.
TEST_F(CoercionTest, ModAssign_Int_LongRhs_NoCast)
{
    ParseProgram("void f(int x, long y) { x %= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_MOD);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_VAR);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_LONG);
}

TEST_F(CoercionTest, LShiftAssign_Int_ShortRhs_Cast)
{
    ParseProgram("void f(int x, short y) { x <<= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_LEFT);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, RShiftAssign_Int_LongRhs_Cast)
{
    ParseProgram("void f(int x, long y) { x >>= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_RIGHT);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, AndAssign_Int_ShortRhs_Cast)
{
    ParseProgram("void f(int x, short y) { x &= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_AND);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, XorAssign_Int_CharRhs_Cast)
{
    ParseProgram("void f(int x, char y) { x ^= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_XOR);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

TEST_F(CoercionTest, OrAssign_Int_LongRhs_Cast)
{
    ParseProgram("void f(int x, long y) { x |= y; }");
    typecheck_program(program);
    Expr *e = AssignExpr();
    EXPECT_EQ(e->u.assign.op, ASSIGN_OR);
    EXPECT_EQ(e->type->kind, TYPE_INT);
    ASSERT_EQ(e->u.assign.value->kind, EXPR_CAST);
    EXPECT_EQ(e->u.assign.value->type->kind, TYPE_INT);
}

// ─── K.5 Error cases ─────────────────────────────────────────────────────────

TEST_F(CoercionTest, Error_PlusAssign_Ptr_FloatRhs)
{
    // float is not an integer type — pointer += float must be rejected.
    ParseProgram("void f(int *p, float n) { p += n; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "invalid operands to '");
}

TEST_F(CoercionTest, Error_PlusAssign_Ptr_PtrRhs)
{
    // pointer rhs for pointer += is also rejected.
    ParseProgram("void f(int *p, int *q) { p += q; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "invalid operands to '");
}

TEST_F(CoercionTest, Error_MulAssign_PtrLhs)
{
    // *= on a pointer lhs is not pointer arithmetic — both sides must be arithmetic.
    ParseProgram("void f(int *p, int n) { p *= n; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "invalid operands to '");
}

TEST_F(CoercionTest, Error_DivAssign_PtrRhs)
{
    // pointer rhs for integer /= is not arithmetic.
    ParseProgram("void f(int x, int *p) { x /= p; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "invalid operands to '");
}

// Plain `char` signedness is target-defined (C11 §6.2.5p15): it is signed on x86_64 and
// unsigned on BESM-6.  `signed char`/`unsigned char` are fixed regardless of target.
// is_signed() reflects this so that emit_cast picks SIGN_EXTEND vs ZERO_EXTEND correctly.
TEST_F(CoercionTest, PlainCharSignednessFollowsTarget)
{
    const Target *saved = target_config;
    Type *c             = new_type(TYPE_CHAR, __func__, __FILE__, __LINE__);
    Type *sc            = new_type(TYPE_SCHAR, __func__, __FILE__, __LINE__);
    Type *uc            = new_type(TYPE_UCHAR, __func__, __FILE__, __LINE__);

    target_config = target_lookup("x86_64");
    EXPECT_TRUE(is_signed(c));
    EXPECT_TRUE(is_signed(sc));
    EXPECT_FALSE(is_signed(uc));

    target_config = target_lookup("besm6");
    EXPECT_FALSE(is_signed(c)); // plain char is unsigned on BESM-6
    EXPECT_TRUE(is_signed(sc)); // signed char unaffected
    EXPECT_FALSE(is_signed(uc));

    free_type(c);
    free_type(sc);
    free_type(uc);
    target_config = saved;
}

// ─── J. _Bool ────────────────────────────────────────────────────────────────

// C11 §6.3.1.1p2: _Bool promotes to int like every other narrow integer type.  This is
// load-bearing on BESM-6, where _Bool is int-sized: without the promotion the size
// comparison at the tail of get_common_type finds the two equal and picks the *unsigned*
// operand — _Bool — as the common type of `b + i`.
TEST_F(CoercionTest, BoolPlusIntPromotesToInt)
{
    ParseProgram("int f(_Bool b, int i) { return b + i; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
    EXPECT_EQ(ret->u.binary_op.left->type->kind, TYPE_INT);
}

// `b + b` must be int too: two _Bool operands would otherwise short-circuit on the
// same-kind test and give the sum type _Bool, so 1 + 1 would re-normalize to 1.
TEST_F(CoercionTest, BoolPlusBoolPromotesToInt)
{
    ParseProgram("int f(_Bool a, _Bool b) { return a + b; }");
    typecheck_program(program);
    EXPECT_EQ(ReturnExpr()->type->kind, TYPE_INT);
}

// The shift operands are promoted individually (§6.3.1.1 again): without it `b << 1` has
// type _Bool, coerce_for_assignment sees identical kinds and inserts no cast, and nothing
// re-normalizes the 2.
TEST_F(CoercionTest, BoolShiftPromotesToInt)
{
    ParseProgram("int f(_Bool b) { return b << 1; }");
    typecheck_program(program);
    Expr *ret = ReturnExpr();
    ASSERT_EQ(ret->kind, EXPR_BINARY_OP);
    EXPECT_EQ(ret->type->kind, TYPE_INT);
    EXPECT_EQ(ret->u.binary_op.left->kind, EXPR_CAST);
}

// A compound assignment to a _Bool lvalue computes in int and converts the result back,
// for the bitwise and shift operators as well as the arithmetic ones — that conversion
// back is where the 0/1 normalization happens.
TEST_F(CoercionTest, BoolCompoundAssignComputesInInt)
{
    ParseProgram("void f(_Bool b, int i) { b |= i; }");
    typecheck_program(program);
    Expr *assign = AssignExpr();
    EXPECT_EQ(assign->u.assign.value->type->kind, TYPE_INT);
}

// _Bool is one machine word on BESM-6 and one byte on the byte-addressed targets.  The
// word is not a nicety: on this target the 1-byte size *means* byte-packed storage and a
// fat byte pointer, which is the wrong representation for a one-bit type.
TEST_F(CoercionTest, BoolSizeFollowsTarget)
{
    const Target *saved = target_config;
    Type *b             = new_type(TYPE_BOOL, __func__, __FILE__, __LINE__);

    target_config = target_lookup("x86_64");
    EXPECT_EQ(get_size(b), 1u);
    EXPECT_EQ(get_alignment(b), 1u);

    target_config = target_lookup("besm6");
    EXPECT_EQ(get_size(b), 6u);
    EXPECT_EQ(get_alignment(b), 6u);
    EXPECT_FALSE(is_signed(b)); // _Bool is unsigned (C11 §6.2.5p6)
    EXPECT_TRUE(is_integer(b));
    EXPECT_TRUE(is_promotable_narrow(b));

    free_type(b);
    target_config = saved;
}
