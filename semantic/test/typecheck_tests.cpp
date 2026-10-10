#include "typecheck_fixture.h"

extern "C" {
#include "srcloc.h"

[[noreturn]] void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    diag_vreport(diag_loc, "error", message, ap);
    va_end(ap);
    exit(1);
}
};

//
// Test Case 1: Simple Integer Variable Declaration and Expression
// Tests a global variable declaration, integer addition, and function return.
//
// Expected Symtab:
// - x: {name="x", type=TYPE_INT, kind=SYM_STATIC, u.static_var={global=true,
// init_kind=INIT_INITIALIZED, init_list={kind=TAC_STATIC_INIT_I64, u.long_val=42, next=NULL}}}
// - main: {name="main", type=TYPE_FUNCTION(return_type=TYPE_INT, params=NULL), kind=SYM_FUNC,
// u.func={defined=true, global=true}}
//
// Expected Typetab:
// - Empty (no structs).
//
// Expected AST Type Fields:
// - Program->decls[0] (var x):
//   - InitDeclarator->type: TYPE_INT
//   - Initializer->type: TYPE_INT (for 42)
//   - Initializer->u.expr->type: TYPE_INT
// - Program->decls[1] (function main):
//   - ExternalDecl->u.function.type: TYPE_FUNCTION(return_type=TYPE_INT, params=NULL)
//   - Stmt(STMT_RETURN)->u.expr->type: TYPE_INT (for x + 1)
//   - Expr(EXPR_BINARY_OP, ADD)->type: TYPE_INT
//   - Expr(EXPR_VAR, "x")->type: TYPE_INT
//   - Expr(EXPR_LITERAL, 1)->type: TYPE_INT
//
TEST_F(TypecheckTest, TypecheckIntVarExpr)
{
    ParseProgram(R"(
        int x = 42;
        int main() {
            return x + 1;
        }
    )");
    typecheck_program(program);

    // Check symbol x.
    const Symbol *x = symtab_get("x");
    ASSERT_NE(x, nullptr);
    EXPECT_STREQ(x->name, "x");
    EXPECT_EQ(x->kind, SYM_STATIC);
    EXPECT_TRUE(x->u.static_var.global);
    EXPECT_EQ(x->u.static_var.init_kind, INIT_INITIALIZED);

    ASSERT_NE(x->type, nullptr);
    EXPECT_EQ(x->type->kind, TYPE_INT);

    Tac_StaticInit *init = x->u.static_var.init_list;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(init->u.int_val, 42);
    EXPECT_EQ(init->next, nullptr);

    // Check symbol main.
    const Symbol *main = symtab_get("main");
    ASSERT_NE(main, nullptr);
    EXPECT_STREQ(main->name, "main");
    EXPECT_EQ(main->kind, SYM_FUNC);
    EXPECT_TRUE(main->u.func.defined);
    EXPECT_TRUE(main->u.func.global);

    ASSERT_NE(main->type, nullptr);
    EXPECT_EQ(main->type->kind, TYPE_FUNCTION);
    EXPECT_EQ(main->type->u.function.params, nullptr);
    EXPECT_FALSE(main->type->u.function.variadic);

    const Type *type = main->type->u.function.return_type;
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->kind, TYPE_INT);

    // Check types in AST.
    ExternalDecl *ext0 = program->decls;
    ExternalDecl *ext1 = ext0->next;
    ASSERT_NE(ext0, nullptr);
    ASSERT_NE(ext1, nullptr);
    EXPECT_EQ(ext1->next, nullptr);
    EXPECT_EQ(ext0->kind, EXTERNAL_DECL_DECLARATION);
    EXPECT_EQ(ext1->kind, EXTERNAL_DECL_FUNCTION);

    // Program->decls[0] (var x):
    Declaration *decl0 = ext0->u.declaration;
    EXPECT_EQ(decl0->kind, DECL_VAR);
    EXPECT_EQ(decl0->u.var.declarators->type->kind, TYPE_INT);
    EXPECT_STREQ(decl0->u.var.declarators->name, "x");
    EXPECT_EQ(decl0->u.var.declarators->init, nullptr);

    // Program->decls[1] (function main):
    EXPECT_EQ(ext1->u.function.type->kind, TYPE_FUNCTION);
    EXPECT_EQ(ext1->u.function.body->kind, STMT_COMPOUND);
    EXPECT_EQ(ext1->u.function.body->u.compound->kind, DECL_OR_STMT_STMT);

    Stmt *stmt = ext1->u.function.body->u.compound->u.stmt;
    EXPECT_EQ(stmt->kind, STMT_RETURN);

    Expr *expr = stmt->u.expr;
    EXPECT_EQ(expr->type->kind, TYPE_INT);
    EXPECT_EQ(expr->kind, EXPR_BINARY_OP);
    EXPECT_EQ(expr->u.binary_op.op, BINARY_ADD);
    EXPECT_EQ(expr->u.binary_op.left->type->kind, TYPE_INT);
    EXPECT_EQ(expr->u.binary_op.right->type->kind, TYPE_INT);
}

//
// Test Case 2: Struct Declaration and Member Access
// Tests struct definition, initialization, and dot operator.
//
// Expected Symtab:
// - p: {name="p", type=TYPE_STRUCT("Point"), kind=SYM_STATIC, u.static_var={global=true,
// init_kind=INIT_INITIALIZED, init_list=[{kind=TAC_STATIC_INIT_I64, u.long_val=1},
// {kind=TAC_STATIC_INIT_ZERO, u.zero_bytes=4}, {kind=TAC_STATIC_INIT_DOUBLE, u.double_val=2.0}]}}
// - get_y: {name="get_y", type=TYPE_FUNCTION(return_type=TYPE_DOUBLE, params=NULL), kind=SYM_FUNC,
// u.func={defined=true, global=true}}
//
// Expected Typetab:
// - Point: {tag="Point", alignment=8, size=16, members=[{name="x", type=TYPE_INT, offset=0},
// {name="y", type=TYPE_DOUBLE, offset=8}], member_count=2}
//
// Expected AST Type Fields:
// - Program->decls[0] (struct Point):
//   - Declaration->u.var.specifiers->type: TYPE_STRUCT("Point")
// - Program->decls[1] (var p):
//   - InitDeclarator->type: TYPE_STRUCT("Point")
//   - Initializer->type: TYPE_STRUCT("Point")
//   - Initializer->u.items[0]->init->type: TYPE_INT (for 1)
//   - Initializer->u.items[0]->init->u.expr->type: TYPE_INT
//   - Initializer->u.items[1]->init->type: TYPE_DOUBLE (for 2.0)
//   - Initializer->u.items[1]->init->u.expr->type: TYPE_DOUBLE
// - Program->decls[2] (function get_y):
//   - ExternalDecl->u.function.type: TYPE_FUNCTION(return_type=TYPE_DOUBLE, params=NULL)
//   - Stmt(STMT_RETURN)->u.expr->type: TYPE_DOUBLE (for p.y)
//   - Expr(EXPR_FIELD_ACCESS, "y")->type: TYPE_DOUBLE
//   - Expr(EXPR_VAR, "p")->type: TYPE_STRUCT("Point")
//
TEST_F(TypecheckTest, TypecheckStructDeclMemberAccess)
{
    ParseProgram(R"(
        struct Point {
            int x;
            double y;
        };
        struct Point p = {1, 2.0};
        double get_y() {
            return p.y;
        }
    )");
    typecheck_program(program);

    // structtab: Point with int x@0, double y@8, size=16, align=8
    const StructDef *pt = structtab_find("Point");
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->alignment, 8);
    EXPECT_EQ(pt->size, 16);
    ASSERT_NE(pt->members, nullptr);
    EXPECT_STREQ(pt->members->name, "x");
    EXPECT_EQ(pt->members->type->kind, TYPE_INT);
    EXPECT_EQ(pt->members->offset, 0);
    ASSERT_NE(pt->members->next, nullptr);
    EXPECT_STREQ(pt->members->next->name, "y");
    EXPECT_EQ(pt->members->next->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(pt->members->next->offset, 8);
    EXPECT_EQ(pt->members->next->next, nullptr);

    // symtab: p
    const Symbol *p = symtab_get("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->kind, SYM_STATIC);
    EXPECT_TRUE(p->u.static_var.global);
    EXPECT_EQ(p->u.static_var.init_kind, INIT_INITIALIZED);
    ASSERT_NE(p->type, nullptr);
    EXPECT_EQ(p->type->kind, TYPE_STRUCT);

    // symtab: get_y
    const Symbol *get_y = symtab_get("get_y");
    ASSERT_NE(get_y, nullptr);
    EXPECT_EQ(get_y->kind, SYM_FUNC);
    EXPECT_TRUE(get_y->u.func.defined);
    EXPECT_TRUE(get_y->u.func.global);
    ASSERT_NE(get_y->type, nullptr);
    EXPECT_EQ(get_y->type->u.function.return_type->kind, TYPE_DOUBLE);

    // AST: field access p.y has type TYPE_DOUBLE
    ExternalDecl *ext2 = program->decls->next->next;
    ASSERT_NE(ext2, nullptr);
    EXPECT_EQ(ext2->kind, EXTERNAL_DECL_FUNCTION);
    Stmt *ret = ext2->u.function.body->u.compound->u.stmt;
    EXPECT_EQ(ret->kind, STMT_RETURN);
    Expr *field = ret->u.expr;
    EXPECT_EQ(field->kind, EXPR_FIELD_ACCESS);
    ASSERT_NE(field->type, nullptr);
    EXPECT_EQ(field->type->kind, TYPE_DOUBLE);
}

//
// Test Case 3: Array and String Literal Initialization
// Tests array initialization with a string literal and subscript operation.
//
TEST_F(TypecheckTest, TypecheckArrayStringLiteralInit)
{
    ParseProgram(R"(
        char str[] = "hello";
        int main() {
            return str[0];
        }
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

TEST_F(TypecheckTest, TypecheckArrayInit)
{
    ParseProgram(R"(
        int tab[] = { 1, 2 };
        int main() {
            return tab[0];
        }
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

TEST_F(TypecheckTest, TypecheckStructRedeclared)
{
    ParseProgram(R"(
        struct foo {
            int quz;
        };
        struct foo;
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

//
// Test Case 4: Pointer Arithmetic and Dereference
// Tests array-to-pointer conversion, pointer initialization, and dereference.
//
TEST_F(TypecheckTest, PointerArithmeticDereference)
{
    ParseProgram(R"(
        int arr[5] = {1, 2, 3, 4, 5};
        int *ptr = arr;
        int main() {
            return *(ptr + 1);
        }
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

//
// Test Case 5: Function Call with Parameters
// Tests function declaration, parameter passing, and arithmetic promotion.
//
TEST_F(TypecheckTest, FunctionCallWithParameters)
{
    ParseProgram(R"(
        int add(int a, double b) {
            return a + b;
        }
        int main() {
            return add(1, 2.0);
        }
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

//
// Test Case 6: Conditional Expression
// Tests local variable, conditional expression, and common type resolution.
//
TEST_F(TypecheckTest, ConditionalExpression)
{
    ParseProgram(R"(
        int main() {
            int x = 1;
            return x ? 10 : 20;
        }
    )");
    typecheck_program(program);

    // TODO: check symtab, AST
}

TEST_F(TypecheckTest, GenericExpressionTypeMatch)
{
    ParseProgram(R"(
        int main() {
            int x = 1;
            return _Generic(x, double: 0, int: 42);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, GenericExpressionDefault)
{
    ParseProgram(R"(
        int main() {
            double x = 1.0;
            return _Generic(x, int: 0, default: 42);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, CompoundLiteralArray)
{
    ParseProgram(R"(
        int f(void) {
            return ((int[1]){42})[0];
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, CompoundLiteralStruct)
{
    ParseProgram(R"(
        struct S { int x; int y; };
        int f(void) {
            struct S s = (struct S){1, 2};
            return s.x;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, NestedStruct)
{
    ParseProgram(R"(
        struct foo {
            struct {
                int bar;
            } quz;
        };
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, NestedUnion)
{
    ParseProgram(R"(
        struct foo {
            union {
                int bar;
            } quz;
        };
    )");
    typecheck_program(program);
}

//
// Test Case 7: Error - Duplicate Struct Declaration
// Tests error handling for duplicate struct definitions.
//
TEST_F(TypecheckTest, DuplicateStructDeclaration)
{
    ParseProgram(R"(
        struct S { int x; };
        struct S { int y; };
    )");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "redefinition of 'struct S'");
}

//
// Test Case 8: Static Variable with Compound Initializer
// Tests static variable with struct initializer.
//
TEST_F(TypecheckTest, StaticVariableCompoundInitializer)
{
    ParseProgram(R"(
        struct S { int x; int y; };
        static struct S s = {1, 2};
    )");
    typecheck_program(program);

    // structtab: S with int x@0, int y@4, size=8, align=4
    const StructDef *sd = structtab_find("S");
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->alignment, 4);
    EXPECT_EQ(sd->size, 8);
    ASSERT_NE(sd->members, nullptr);
    EXPECT_STREQ(sd->members->name, "x");
    EXPECT_EQ(sd->members->offset, 0);
    ASSERT_NE(sd->members->next, nullptr);
    EXPECT_STREQ(sd->members->next->name, "y");
    EXPECT_EQ(sd->members->next->offset, 4);

    // symtab: s — static (not global), initialized
    const Symbol *s = symtab_get("s");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->kind, SYM_STATIC);
    EXPECT_FALSE(s->u.static_var.global);
    EXPECT_EQ(s->u.static_var.init_kind, INIT_INITIALIZED);
    ASSERT_NE(s->type, nullptr);
    EXPECT_EQ(s->type->kind, TYPE_STRUCT);

    Tac_StaticInit *init = s->u.static_var.init_list;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(init->u.int_val, 1);
    ASSERT_NE(init->next, nullptr);
    EXPECT_EQ(init->next->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(init->next->u.int_val, 2);
    EXPECT_EQ(init->next->next, nullptr);
}

//
// Test Case 9: Pointer to Struct and Arrow Operator
// Tests struct pointer, address-of operator, and arrow operator.
//
TEST_F(TypecheckTest, PointerStructArrowOperator)
{
    ParseProgram(R"(
        struct S { int x; };
        struct S s = {42};
        int main() {
            struct S *p = &s;
            return p->x;
        }
    )");
    typecheck_program(program);

    // structtab: S with int x@0, size=4, align=4
    const StructDef *sd = structtab_find("S");
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->alignment, 4);
    EXPECT_EQ(sd->size, 4);
    ASSERT_NE(sd->members, nullptr);
    EXPECT_STREQ(sd->members->name, "x");
    EXPECT_EQ(sd->members->offset, 0);
    EXPECT_EQ(sd->members->next, nullptr);

    // symtab: s
    const Symbol *s = symtab_get("s");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->kind, SYM_STATIC);
    EXPECT_TRUE(s->u.static_var.global);
    EXPECT_EQ(s->u.static_var.init_kind, INIT_INITIALIZED);
    Tac_StaticInit *init = s->u.static_var.init_list;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(init->u.int_val, 42);

    // symtab: main
    const Symbol *main_sym = symtab_get("main");
    ASSERT_NE(main_sym, nullptr);
    EXPECT_EQ(main_sym->kind, SYM_FUNC);
    EXPECT_TRUE(main_sym->u.func.defined);

    // AST: arrow expression p->x has type TYPE_INT
    ExternalDecl *ext2 = program->decls->next->next;
    ASSERT_NE(ext2, nullptr);
    EXPECT_EQ(ext2->kind, EXTERNAL_DECL_FUNCTION);
    DeclOrStmt *ds = ext2->u.function.body->u.compound;
    ASSERT_NE(ds, nullptr);       // first: decl for p
    ASSERT_NE(ds->next, nullptr); // second: return stmt
    Stmt *ret = ds->next->u.stmt;
    EXPECT_EQ(ret->kind, STMT_RETURN);
    Expr *arrow = ret->u.expr;
    EXPECT_EQ(arrow->kind, EXPR_PTR_ACCESS);
    ASSERT_NE(arrow->type, nullptr);
    EXPECT_EQ(arrow->type->kind, TYPE_INT);
}

//
// Test Case 10: Error - Invalid Assignment
// Tests error handling for type mismatch in assignment.
//
TEST_F(TypecheckTest, InvalidAssignment)
{
    ParseProgram(R"(
        int main() {
            int x = 1;
            x = "string";
        }
    )");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "cannot convert '.*' to '.*' when");
}

// ---------------------------------------------------------------------------
// Switch semantic validation
// ---------------------------------------------------------------------------

// Float controlling expression is rejected.
TEST_F(TypecheckTest, SwitchFloatExpr)
{
    ParseProgram("double f(double x) { switch (x) {} return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "an integer");
}

// Pointer controlling expression is rejected.
TEST_F(TypecheckTest, SwitchPointerExpr)
{
    ParseProgram("int f(int *p) { switch (p) {} return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "an integer");
}

// Float case expression is rejected.
TEST_F(TypecheckTest, CaseFloatValue)
{
    ParseProgram("int f(int x) { switch (x) { case 1.5: break; } return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "an integer");
}

// Duplicate integer case values are rejected.
TEST_F(TypecheckTest, CaseDuplicate)
{
    ParseProgram("int f(int x) { switch (x) { case 1: break; case 1: break; } return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "duplicate case value");
}

// char 'a' and integer 97 should be the same value — duplicate.
// Disabled: the parser stores all char literals as LITERAL_INT 0 via strtoul,
// so 'a' and 97 are not detected as equal until the parser is fixed.
TEST_F(TypecheckTest, CaseDuplicateCharAndInt)
{
    ParseProgram("int f(int x) { switch (x) { case 'a': break; case 97: break; } return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "duplicate case value");
}

// Two default labels in one switch are rejected.
TEST_F(TypecheckTest, MultipleDefaultLabels)
{
    ParseProgram("int f(int x) { switch (x) { default: break; default: break; } return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1), "multiple 'default'");
}

// Basic switch with distinct integer case values is accepted.
TEST_F(TypecheckTest, SwitchIntBasic)
{
    ParseProgram(
        "int f(int x) { switch (x) { case 0: break; case 1: break; case 1+1: break; } return 0; }");
    typecheck_program(program);
}

// Char controlling expression is accepted (gets integer-promoted).
TEST_F(TypecheckTest, SwitchCharExpr)
{
    ParseProgram("int f(char c) { switch (c) { case 'a': break; } return 0; }");
    typecheck_program(program);
}

// Switch with cases and a default label is accepted.
TEST_F(TypecheckTest, SwitchWithDefault)
{
    ParseProgram("int f(int x) { switch (x) { case 0: break; default: break; } return 0; }");
    typecheck_program(program);
}

// Switch with no cases is accepted.
TEST_F(TypecheckTest, SwitchEmptyBody)
{
    ParseProgram("int f(int x) { switch (x) {} return 0; }");
    typecheck_program(program);
}

// Nested switches each with case 1 are accepted — contexts are independent.
TEST_F(TypecheckTest, SwitchNestedContexts)
{
    ParseProgram(R"(
        int f(int x, int y) {
            switch (x) {
                case 1: switch (y) { case 1: break; } break;
            }
            return 0;
        }
    )");
    typecheck_program(program);
}
