#include "target.h"
#include "typecheck_fixture.h"

namespace {
// Temporarily switch the active target for a test, restoring it on scope exit
// (so a width-specific fold does not leak into later, target-agnostic tests).
struct TargetGuard {
    const Target *saved;
    explicit TargetGuard(const char *name) : saved(target_config)
    {
        target_config = target_lookup(name);
    }
    ~TargetGuard() { target_config = saved; }
};
} // namespace

// Struct definition through the full pipeline.
// Verifies that struct registration does not double-register in structtab.
TEST_F(PipelineTest, StructDecl)
{
    RunPipeline("struct S { int x; double y; };");

    const StructDef *sd = structtab_find("S");
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->alignment, 8);
    EXPECT_EQ(sd->size, 16);
    ASSERT_NE(sd->members, nullptr);
    EXPECT_STREQ(sd->members->name, "x");
    EXPECT_EQ(sd->members->type->kind, TYPE_INT);
    EXPECT_EQ(sd->members->offset, 0);
    ASSERT_NE(sd->members->next, nullptr);
    EXPECT_STREQ(sd->members->next->name, "y");
    EXPECT_EQ(sd->members->next->type->kind, TYPE_DOUBLE);
    EXPECT_EQ(sd->members->next->offset, 8);
    EXPECT_EQ(sd->members->next->next, nullptr);
}

// Struct with a comma-separated declarator list followed by another
// declaration.  Verifies that parse_struct_declaration_list() appends the whole
// chain a struct_declaration returns, instead of clobbering its head's next --
// which used to drop every interior member (`g.tok_ptr` then failed to resolve).
TEST_F(PipelineTest, StructMultipleDeclarators)
{
    RunPipeline(
        "struct S { char *a, *tok_ptr, *c; int p; };"
        " struct S g;"
        " int main(void) { g.tok_ptr = 0; return 0; }");

    const StructDef *sd = structtab_find("S");
    ASSERT_NE(sd, nullptr);

    // All four members must survive, in declaration order and at distinct,
    // increasing offsets (exact offsets are target-dependent).
    const char *names[]    = { "a", "tok_ptr", "c", "p" };
    const TypeKind kinds[] = { TYPE_POINTER, TYPE_POINTER, TYPE_POINTER, TYPE_INT };
    const FieldDef *m      = sd->members;
    int prev_offset        = -1;
    for (int i = 0; i < 4; i++) {
        ASSERT_NE(m, nullptr) << "member " << names[i] << " missing";
        EXPECT_STREQ(m->name, names[i]);
        ASSERT_NE(m->type, nullptr);
        EXPECT_EQ(m->type->kind, kinds[i]);
        if (kinds[i] == TYPE_POINTER) {
            ASSERT_NE(m->type->u.pointer.target, nullptr);
            EXPECT_EQ(m->type->u.pointer.target->kind, TYPE_CHAR);
        }
        EXPECT_GT(m->offset, prev_offset);
        prev_offset = m->offset;
        m           = m->next;
    }
    EXPECT_EQ(m, nullptr);
}

// Struct definition followed by a variable of that struct type.
TEST_F(PipelineTest, StructUsedInVar)
{
    RunPipeline("struct S { int x; }; struct S s;");

    EXPECT_TRUE(structtab_exists("S"));

    const Symbol *s = symtab_get("s");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->kind, SYM_STATIC);
    EXPECT_TRUE(s->u.static_var.global);
    EXPECT_EQ(s->u.static_var.init_kind, INIT_TENTATIVE);
    ASSERT_NE(s->type, nullptr);
    EXPECT_EQ(s->type->kind, TYPE_STRUCT);
}

// Function prototype followed by definition.
// Verifies that symtab_add_fun() sets has_linkage so the redeclaration
// does not fatal with "Duplicate declaration".
TEST_F(PipelineTest, FunctionPrototypeThenDefinition)
{
    RunPipeline("int f(void); int f(void) { return 1; }");

    const Symbol *f = symtab_get("f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, SYM_FUNC);
    EXPECT_TRUE(f->u.func.defined);
    EXPECT_TRUE(f->u.func.global);
    ASSERT_NE(f->type, nullptr);
    EXPECT_EQ(f->type->kind, TYPE_FUNCTION);
}

// File-scope declaration with multiple declarators.
// Verifies that typecheck_file_scope_var_decl() loops over all InitDeclarators.
TEST_F(PipelineTest, FileVarMultipleDeclarators)
{
    RunPipeline("int x = 1, y = 2;");

    const Symbol *x = symtab_get("x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->kind, SYM_STATIC);
    EXPECT_TRUE(x->u.static_var.global);
    EXPECT_EQ(x->u.static_var.init_kind, INIT_INITIALIZED);
    ASSERT_NE(x->u.static_var.init_list, nullptr);
    EXPECT_EQ(x->u.static_var.init_list->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(x->u.static_var.init_list->u.int_val, 1);

    const Symbol *y = symtab_get("y");
    ASSERT_NE(y, nullptr);
    EXPECT_EQ(y->kind, SYM_STATIC);
    EXPECT_TRUE(y->u.static_var.global);
    EXPECT_EQ(y->u.static_var.init_kind, INIT_INITIALIZED);
    ASSERT_NE(y->u.static_var.init_list, nullptr);
    EXPECT_EQ(y->u.static_var.init_list->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(y->u.static_var.init_list->u.int_val, 2);
}

// Static and extern file-scope variables: linkage and init_kind.
TEST_F(PipelineTest, StaticAndExternVars)
{
    RunPipeline("static int a; extern int b;");

    const Symbol *a = symtab_get("a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->kind, SYM_STATIC);
    EXPECT_FALSE(a->u.static_var.global);
    EXPECT_EQ(a->u.static_var.init_kind, INIT_TENTATIVE);

    const Symbol *b = symtab_get("b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->kind, SYM_STATIC);
    EXPECT_TRUE(b->u.static_var.global);
    EXPECT_EQ(b->u.static_var.init_kind, INIT_NONE);
}

// _Static_assert with a true condition inside a struct is accepted.
TEST_F(PipelineTest, StaticAssertInStructPasses)
{
    RunPipeline("struct S { _Static_assert(1, \"ok\"); int x; };");

    const StructDef *sd = structtab_find("S");
    ASSERT_NE(sd, nullptr);
    ASSERT_NE(sd->members, nullptr);
    EXPECT_STREQ(sd->members->name, "x");
    EXPECT_EQ(sd->members->type->kind, TYPE_INT);
}

// _Static_assert with a false condition inside a struct is a compile-time error.
TEST_F(PipelineTest, StaticAssertInStructFails)
{
    ParseProgram("struct S { _Static_assert(0, \"bad\"); int x; };");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "static assertion failed: bad");
}

// _Static_assert with a true condition inside a union is accepted.
TEST_F(PipelineTest, StaticAssertInUnionPasses)
{
    RunPipeline("union U { _Static_assert(1, \"ok\"); int x; };");

    const StructDef *sd = structtab_find("U");
    ASSERT_NE(sd, nullptr);
    ASSERT_NE(sd->members, nullptr);
    EXPECT_STREQ(sd->members->name, "x");
}

// A file-scope _Static_assert is evaluated, not merely type-checked.
TEST_F(PipelineTest, StaticAssertAtFileScopePasses)
{
    RunPipeline("_Static_assert(1, \"ok\"); int x;");

    EXPECT_NE(symtab_get("x"), nullptr);
}

// A long double constant folds at the target's precision: 0.1L and a value 1e-22 away
// are one x87 value (64-bit significand), but two binary128 ones.
TEST_F(PipelineTest, LongDoubleFoldsAtX87Precision)
{
    const Target *saved = target_config;
    target_config       = target_lookup("x86_64");
    RunPipeline("_Static_assert(0.1L == 0.1000000000000000000001L, \"x87\"); int x;");
    target_config = saved;
}

TEST_F(PipelineTest, LongDoubleFoldsAtBinary128Precision)
{
    const Target *saved = target_config;
    target_config       = target_lookup("riscv64");
    RunPipeline("_Static_assert(0.1L != 0.1000000000000000000001L, \"binary128\"); int x;");
    target_config = saved;
}

// A file-scope _Static_assert with a false condition is a compile-time error.
TEST_F(PipelineTest, StaticAssertAtFileScopeFails)
{
    ParseProgram("_Static_assert(0, \"bad\"); int x;");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "static assertion failed: bad");
}

// A block-scope _Static_assert is evaluated too; it used to be an "unsupported
// local declaration kind" error.
TEST_F(PipelineTest, StaticAssertInBlockPasses)
{
    RunPipeline("int f(void) { _Static_assert(1, \"ok\"); return 0; }");

    EXPECT_NE(symtab_get("f"), nullptr);
}

// A block-scope _Static_assert with a false condition is a compile-time error.
TEST_F(PipelineTest, StaticAssertInBlockFails)
{
    ParseProgram("int f(void) { _Static_assert(0, \"bad\"); return 0; }");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "static assertion failed: bad");
}

// --- Missing-return diagnostic & main's implicit return 0 -------------------

// A non-void, non-main function whose body can fall off the end is rejected.
TEST_F(PipelineTest, NonVoidFallsOffEnd_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int f(void) {
    int x = 1;
}
)"),
                 "non-void function 'f' may reach its end without returning a value");
}

// A non-void body ending in a call to a user-defined `_Noreturn` function does
// not fall off the end — accepted (the noreturn flag is threaded onto the symbol).
TEST_F(PipelineTest, NonVoidEndsInNoreturnCall_Ok)
{
    RunPipeline(R"(_Noreturn void die(void);
int f(int x) {
    die();
})");
    EXPECT_NE(program, nullptr);
}

// The same body calling a function that is NOT _Noreturn is still rejected.
TEST_F(PipelineTest, NonVoidEndsInPlainCall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(void g(void);
int f(int x) {
    g();
}
)"),
                 "non-void function 'f' may reach its end without returning a value");
}

// A function that returns on every path is accepted.
TEST_F(PipelineTest, NonVoidAllPathsReturn_Ok)
{
    RunPipeline(R"(int f(int x) {
    if (x)
        return 1;
    else
        return 2;
})");
    EXPECT_NE(program, nullptr);
}

// An infinite loop with no exit makes the end unreachable — accepted.
TEST_F(PipelineTest, NonVoidInfiniteLoop_Ok)
{
    RunPipeline(R"(int f(int x) {
    for (;;) {
        if (x)
            return x;
    }
})");
    EXPECT_NE(program, nullptr);
}

// A switch is conservatively treated as not falling through, so an exhaustive
// switch is never flagged (matches the runtime library's strerror()).
TEST_F(PipelineTest, ExhaustiveSwitch_Ok)
{
    RunPipeline(R"(int f(int x) {
    switch (x) {
    case 1:
        return 1;
    default:
        return 0;
    }
})");
    EXPECT_NE(program, nullptr);
}

// A void function may fall off the end with no diagnostic.
TEST_F(PipelineTest, VoidFallsOffEnd_Ok)
{
    RunPipeline(R"(void f(void) {
    int x = 1;
})");
    EXPECT_NE(program, nullptr);
}

// main() falling off the end is not an error; the typechecker appends an
// implicit `return 0;` (C11 §5.1.2.2.3).
TEST_F(PipelineTest, MainFallsOffEndGetsImplicitReturnZero)
{
    RunPipeline("int main(void) { }");

    ExternalDecl *fn = program->decls;
    ASSERT_EQ(fn->kind, EXTERNAL_DECL_FUNCTION);
    DeclOrStmt *it = fn->u.function.body->u.compound;
    ASSERT_NE(it, nullptr);
    while (it->next) {
        it = it->next;
    }
    ASSERT_EQ(it->kind, DECL_OR_STMT_STMT);
    Stmt *last = it->u.stmt;
    ASSERT_EQ(last->kind, STMT_RETURN);
    ASSERT_NE(last->u.expr, nullptr);
    EXPECT_EQ(last->u.expr->kind, EXPR_LITERAL);
    EXPECT_EQ(last->u.expr->u.literal->kind, LITERAL_INT);
    EXPECT_EQ(last->u.expr->u.literal->u.int_val, 0);
}

// --- Constant folding of real and mixed expressions --------------------------

// Returns the sole static initializer of a file-scope variable.
static const Tac_StaticInit *sole_init(const char *name)
{
    const Symbol *sym = symtab_get(name);
    EXPECT_NE(sym, nullptr);
    EXPECT_EQ(sym->u.static_var.init_kind, INIT_INITIALIZED);
    return sym->u.static_var.init_list;
}

// A static void * from a string literal is a fat pointer, as for char *.
TEST_F(PipelineTest, StaticVoidPointerFromString)
{
    RunPipeline(R"(void *p = "AB";
const void *cp = "CD";
struct s { void *p; } g = { "EF" };
int main(void) { static void *q = "GH"; return 0; }
)");

    for (const char *name : { "p", "cp", "g" }) {
        const Tac_StaticInit *init = sole_init(name);
        ASSERT_NE(init, nullptr) << name;
        EXPECT_EQ(init->kind, TAC_STATIC_INIT_FAT_POINTER) << name;
        EXPECT_EQ(init->u.pointer.byte_offset, 0) << name;
        EXPECT_EQ(init->next, nullptr) << name;
    }
}

// unsigned char * from a string stays a pointer-sign error on the static path.
TEST_F(PipelineTest, StaticUcharPointerFromStringDies)
{
    EXPECT_DEATH(RunPipeline(R"(unsigned char *p = "AB";)"),
                 "cannot initialize 'unsigned char \\*' with a string literal");
}

// A real static initializer that is not a bare literal must still fold.  Each of these
// parses as EXPR_UNARY_OP / EXPR_BINARY_OP / EXPR_CAST over a literal, none of which the
// folder used to reach for a floating target.
TEST_F(PipelineTest, RealStaticInitConstExpr)
{
    RunPipeline(R"(double neg = -0.5;
double neg_int = -1;
double plus = +2.5;
double quotient = 1.0 / 4.0;
double mixed = 2.5 * 2;
double compare = (1.5 < 2.0);
double lognot = !0.0;
double cast = (double)-1;
float negf = -0.5f;
)");

    ASSERT_NE(sole_init("neg"), nullptr);
    EXPECT_EQ(sole_init("neg")->kind, TAC_STATIC_INIT_DOUBLE);
    EXPECT_EQ(sole_init("neg")->u.double_val, -0.5);
    EXPECT_EQ(sole_init("neg_int")->u.double_val, -1.0);
    EXPECT_EQ(sole_init("plus")->u.double_val, 2.5);
    EXPECT_EQ(sole_init("quotient")->u.double_val, 0.25);
    EXPECT_EQ(sole_init("mixed")->u.double_val, 5.0);
    // A comparison and a logical NOT yield an int, which converts to the real target.
    EXPECT_EQ(sole_init("compare")->u.double_val, 1.0);
    EXPECT_EQ(sole_init("lognot")->u.double_val, 1.0);
    EXPECT_EQ(sole_init("cast")->u.double_val, -1.0);

    ASSERT_NE(sole_init("negf"), nullptr);
    EXPECT_EQ(sole_init("negf")->kind, TAC_STATIC_INIT_FLOAT);
    EXPECT_EQ(sole_init("negf")->u.float_val, -0.5);
}

// A real element inside an aggregate initializer folds through the same recursion.
TEST_F(PipelineTest, RealArrayStaticInitConstExpr)
{
    RunPipeline("double a[] = { -1.5, 2.5 };");

    const Tac_StaticInit *first = sole_init("a");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->kind, TAC_STATIC_INIT_DOUBLE);
    EXPECT_EQ(first->u.double_val, -1.5);
    ASSERT_NE(first->next, nullptr);
    EXPECT_EQ(first->next->u.double_val, 2.5);
}

// A real constant expression converts to an integer target by truncation toward zero.
TEST_F(PipelineTest, IntStaticInitFromRealConstExpr)
{
    RunPipeline("int trunc = -1.5; int cast = (int)2.9;");

    EXPECT_EQ(sole_init("trunc")->u.int_val, -1);
    EXPECT_EQ(sole_init("cast")->u.int_val, 2);
}

// A cast wraps to the cast type's own width and signedness, not the target's.  Both
// initializers are wider than the cast, so an unnarrowed fold would survive downstream
// truncation and show up here: 300 instead of 44, and -1 instead of 4294967295.
TEST_F(PipelineTest, ConstExprCastNarrowsToCastType)
{
    RunPipeline("int narrowed = (char)300; long widened = (unsigned)-1;");

    EXPECT_EQ(sole_init("narrowed")->u.int_val, 44);
    EXPECT_EQ(sole_init("widened")->u.long_val, 4294967295L); // x86_64: unsigned is 32-bit
}

// A cast and a real operand in a constant expression.  A _Static_assert is the vehicle:
// its condition goes through try_eval_const_int, so a wrong fold either fails the assert
// or reports "not a constant expression".
TEST_F(PipelineTest, ConstExprFoldsCastsAndReals)
{
    RunPipeline(R"(struct S {
    _Static_assert((char)300 == 44, "cast narrows to char");
    _Static_assert((int)1.5 == 1, "real converts to int");
    _Static_assert((int)-1.9 == -1, "real truncates toward zero");
    _Static_assert(1.5 < 2.0, "real comparison yields int");
    _Static_assert(2.5 * 2.0 == 5.0, "real arithmetic");
    _Static_assert(!0, "logical not on int");
    _Static_assert(!1.5 == 0, "logical not on real");
    _Static_assert(!0.0 == 1, "logical not yields int");
    int x;
};
)");
    EXPECT_TRUE(structtab_exists("S"));
}

// A cast to an unsigned type, an enum, or a typedef name is a constant expression, and it
// folds to the cast type's own width and signedness.  Widths are the x86_64 fixture
// target's.  (Full-width 64-bit unsigned values are covered separately below.)
TEST_F(PipelineTest, ConstExprFoldsUnsignedAndEnumCasts)
{
    RunPipeline(R"(typedef unsigned int uint_t;
enum E { A = 3 };
_Static_assert((unsigned char)-1 == 255, "wraps to 8 bits, unsigned");
_Static_assert((signed char)-1 == -1, "sign-extends from 8 bits");
_Static_assert((unsigned short)-1 == 65535, "wraps to 16 bits");
_Static_assert((unsigned)-1 == 4294967295, "wraps to 32 bits");
_Static_assert((enum E)3 == A, "cast to enum");
_Static_assert((uint_t)-1 == 4294967295, "cast through a typedef name");
_Static_assert((unsigned char)300 == 44, "wraps like (char)300");
int x;
)");
    EXPECT_NE(symtab_get("x"), nullptr);
}

// The unsigned cast is really evaluated: a false one fails.
TEST_F(PipelineTest, ConstExprUnsignedCastFalseAssertFails)
{
    ParseProgram("_Static_assert((unsigned char)-1 == 254, \"bad\");");
    ASSERT_EXIT(typecheck_program(program), ::testing::ExitedWithCode(1),
                "static assertion failed: bad");
}

// The folder carries a value's signedness, so an unsigned value at the host's full
// 64 bits still compares, divides and shifts as unsigned, and the usual arithmetic
// conversions make a mixed signed/unsigned operator unsigned.  Widths are the x86_64
// fixture target's (unsigned = 32 bits, unsigned long = 64).
TEST_F(PipelineTest, ConstExprFoldsFullWidthUnsigned)
{
    RunPipeline(R"(_Static_assert((unsigned long)-1 > 0, "full-width unsigned compares unsigned");
_Static_assert((unsigned long)-1 == 18446744073709551615UL, "converts modulo 2^64");
_Static_assert((unsigned long long)-1 > 0, "same at unsigned long long");
_Static_assert(~0u == 4294967295u, "~ wraps to the unsigned operand type");
_Static_assert(-1u == 4294967295u, "unary minus wraps unsigned");
_Static_assert((-1 < 1u) == 0, "usual conversions make the compare unsigned");
_Static_assert((unsigned)-1 / 2 == 2147483647, "unsigned division");
_Static_assert((unsigned)-1 % 10 == 5, "unsigned remainder");
_Static_assert((unsigned)-2 >> 1 == 2147483647, "unsigned >> is logical");
_Static_assert(sizeof(int) - 8 > 0, "sizeof yields unsigned size_t");
long w = ((unsigned long)-1 > 0);
)");
    EXPECT_EQ(sole_init("w")->u.long_val, 1);
}

// The same folds on the BESM-6 target, where a signed value is a 41-bit pattern inside
// the 48-bit word whose upper bits hold zeros: converting a negative signed value to an
// unsigned type is a plain word copy, so (unsigned long)-1 is 2^41-1 (not 2^48-1) --
// matching const_to_uint64 in optimize/const_fold.c -- and a signed right shift is
// logical over that pattern (Target.right_shift_is_logical), matching the value the
// optimizer folds in BinaryFoldRightShiftNegativeBesm6Logical.
TEST_F(PipelineTest, ConstExprFoldsUnsignedBesm6)
{
    TargetGuard besm6("besm6");
    RunPipeline(R"(_Static_assert((unsigned long)-1 > 0, "unsigned compare");
_Static_assert((unsigned long)-1 == 2199023255551UL, "signed word pattern, 2^41-1");
_Static_assert((-1 < 1u) == 0, "mixed compare is unsigned");
_Static_assert((-8160 >> 5) == 68719476481, "signed >> is logical on BESM-6");
long w = ((unsigned long)-1 > 0);
)");
    EXPECT_EQ(sole_init("w")->u.long_val, 1);
}

// `!` folds, so it is usable where an integer constant expression is required.
TEST_F(PipelineTest, LogicalNotFoldsAsArraySize)
{
    RunPipeline("int a[!0];");

    const Symbol *a = symtab_get("a");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->type->kind, TYPE_ARRAY);
    ASSERT_NE(a->type->u.array.size, nullptr);
    ASSERT_EQ(a->type->u.array.size->kind, EXPR_LITERAL);
    EXPECT_EQ(a->type->u.array.size->u.literal->u.int_val, 1);
}

// A bare real is not an *integer* constant expression (C11 §6.6p6); only a cast makes
// it one.  Guards the `is_real` rejection in the try_eval_const_int wrapper.
TEST_F(PipelineTest, BareRealIsNotIntegerConstExpr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(struct S {
    _Static_assert(1.5, "not an integer constant expression");
    int x;
};
)"),
                 "'_Static_assert' condition is not a constant expression");
}

// The new floating-scalar path must not accept a non-constant initializer.
TEST_F(PipelineTest, RealStaticInitFromVariable_Neg)
{
    EXPECT_DEATH(RunPipeline("double a; double b = -a;"),
                 "initializer element is not a constant expression");
}

// Division by zero is not a constant expression: reject rather than fold an infinity.
TEST_F(PipelineTest, RealStaticInitDivideByZero_Neg)
{
    EXPECT_DEATH(RunPipeline("double z = 1.0 / 0.0;"),
                 "initializer element is not a constant expression");
}

//
// A tagless struct/union definition shared by a comma-separated declarator list.
//
// The parser clones the base type per declarator, so the synthetic tag must be minted once, at
// the definition, before that clone.  While it was minted per cloned node instead, each
// declarator got its own `__anon_N` and compatible_type's tag strcmp rejected every use that
// needs the declarators to share a type.  One test per row of backend/besm6/tmp/BUG2.md.
//

TEST_F(PipelineTest, AnonStructDeclaratorListAssign)
{
    RunPipeline("struct { int x; } a, b;  int f(void) { a = b; return a.x; }");

    // One definition, so exactly one tag -- not one per declarator.  The counter is reset by
    // parse(), so the number is deterministic regardless of test order.
    EXPECT_TRUE(structtab_exists("__anon_1"));
    EXPECT_FALSE(structtab_exists("__anon_2"));
}

TEST_F(PipelineTest, AnonStructDeclaratorListPointer)
{
    RunPipeline("struct { int x; } a, *p;  int f(void) { p = &a; return p->x; }");

    EXPECT_TRUE(structtab_exists("__anon_1"));
    EXPECT_FALSE(structtab_exists("__anon_2"));
}

TEST_F(PipelineTest, AnonStructDeclaratorListArray)
{
    RunPipeline("struct { int x; } a, arr[2];  int f(void) { arr[0] = a; return arr[0].x; }");

    EXPECT_TRUE(structtab_exists("__anon_1"));
    EXPECT_FALSE(structtab_exists("__anon_2"));
}

TEST_F(PipelineTest, AnonUnionDeclaratorList)
{
    RunPipeline("union { int x; int y; } a, b;  int f(void) { a = b; return a.x; }");

    const StructDef *sd = structtab_find("__anon_1");
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->kind, TYPE_UNION);
    EXPECT_FALSE(structtab_exists("__anon_2"));
}

TEST_F(PipelineTest, AnonStructBlockScopeDeclaratorList)
{
    RunPipeline("int f(void) { struct { int a, b, c; } m, n;  n = m;  return n.a; }");
}

//
// Struct *members* take the same path: parse_struct_declaration clones the member base type per
// declarator, so `p` and `q` must share one tag too.  Not listed in BUG2.md, but it is the same
// defect and it failed the same way.
//
TEST_F(PipelineTest, AnonStructMembersShareType)
{
    RunPipeline(
        "struct W { struct { int x; } p, q; };  struct W w;"
        " int f(void) { w.p = w.q; return w.p.x; }");
}

//
// `typedef struct { int x; } T1, T2;` used to make T1 and T2 incompatible for the same reason.
//
TEST_F(PipelineTest, AnonTypedefDeclaratorList)
{
    RunPipeline(
        "typedef struct { int x; } T1, T2;  T1 a;  T2 b;"
        " int f(void) { a = b; return a.x; }");
}

//
// The other half of the invariant: two *separate* tagless definitions are distinct types
// (C11 §6.7.2.3p5), so they must keep distinct tags.  Guards against a future "merge by shape"
// refactor over-merging them.
//
TEST_F(PipelineTest, DistinctAnonStructsStayIncompatible_Neg)
{
    EXPECT_DEATH(RunPipeline("struct { int x; } a;  struct { int x; } b;"
                             " int f(void) { a = b; return 0; }"),
                 "cannot convert '.*' to '.*' when");
}

//
// A compound literal is an lvalue (C11 6.5.2.5p4): its address may be taken, and it may be
// assigned, incremented and modified by a compound assignment.
//
TEST_F(PipelineTest, CompoundLiteralIsLvalue)
{
    RunPipeline(R"(struct s { int a, b; };
int f(void)
{
    int *p = &(int){ 5 };
    struct s *q = &(struct s){ 1 };
    int *b = &(struct s){ 1, 2 }.b;
    (int){ 1 } = 2;
    (struct s){ 1 } = *q;
    (int){ 3 } += 4;
    return *p + q->a + *b + ++(int){ 0 } + (int){ 0 }--;
}
)");
}

TEST_F(PipelineTest, ArrayCompoundLiteralNotAssignable_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(int *p) { (int[2]){ 1, 2 } = p; }"),
                 "array type '.*' is not assignable");
}

//
// A struct/union defined in a type name (cast, sizeof, _Alignof, _Generic, compound literal)
// is registered like one defined in a declaration, and its tag stays in scope.
//
TEST_F(PipelineTest, StructDefinedInTypeName)
{
    RunPipeline(R"(int f(void)
{
    int n = sizeof(struct r { int a, b; }) + _Alignof(struct r2 { int a; });
    int g = _Generic(0, struct g { int a; }: 1, int: 2);
    struct r *p = (struct r *)0;
    return n + g + (struct { int a, b; }){ 1 }.b + ((union { int i; char *s; }){ 3 }).i +
           ((struct w { int a, b; }){ 1 }).b + (struct w){ 2 }.a + (p != 0);
}
)");
}

// A block-scope compound literal has automatic storage, so its address is not a constant.
TEST_F(PipelineTest, BlockScopeLiteralInStaticInit_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { static int *p = (int[]){ 1 }; }"),
                 "initializer element is not a constant expression");
}

TEST_F(PipelineTest, FileScopeLiteralNonConstant_Neg)
{
    EXPECT_DEATH(RunPipeline("int x; int *p = (int[]){ x };"),
                 "initializer element is not a constant expression");
}

// &c of a scalar char points at its byte: offset 0 on a byte-addressed target, the low
// byte (#5) of the char's one-word cell on BESM-6.
TEST_F(PipelineTest, StaticAddressOfScalarChar)
{
    RunPipeline("char c = 1; char *p = &c;");
    EXPECT_EQ(sole_init("p")->kind, TAC_STATIC_INIT_FAT_POINTER);
    EXPECT_EQ(sole_init("p")->u.pointer.byte_offset, 0);
}

TEST_F(PipelineTest, StaticAddressOfScalarCharBesm6)
{
    TargetGuard besm6("besm6");
    RunPipeline("char c = 1; char *p = &c;");
    EXPECT_EQ(sole_init("p")->u.pointer.byte_offset, 5);
}

// A 4-byte int gets the 32-bit init slot; a BESM-6 word int the 64-bit one.
TEST_F(PipelineTest, StaticIntSlotFollowsTargetSize)
{
    RunPipeline("int i = 5; unsigned u = 7; long l = 9;");
    EXPECT_EQ(sole_init("i")->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(sole_init("u")->kind, TAC_STATIC_INIT_U32);
    EXPECT_EQ(sole_init("l")->kind, TAC_STATIC_INIT_I64);
}

TEST_F(PipelineTest, StaticIntSlotFollowsTargetSizeBesm6)
{
    TargetGuard besm6("besm6");
    RunPipeline("int i = 5; unsigned u = 7;");
    EXPECT_EQ(sole_init("i")->kind, TAC_STATIC_INIT_I64);
    EXPECT_EQ(sole_init("u")->kind, TAC_STATIC_INIT_U64);
}

// A multi-character constant must fit the target's int: five bytes do on BESM-6
// (41-bit int), not on x86_64.
TEST_F(PipelineTest, CharConstantWidthBesm6)
{
    TargetGuard besm6("besm6");
    RunPipeline("int f(void) { return 'abcde'; } unsigned g(void) { return 'abcdef'; }");
}

TEST_F(PipelineTest, CharConstantTooWideForInt_Neg)
{
    EXPECT_DEATH(RunPipeline("int f(void) { return 'abcde'; }"),
                 "character constant too long for type int");
}

TEST_F(PipelineTest, CharConstantTooWideForUnsignedBesm6_Neg)
{
    TargetGuard besm6("besm6");
    EXPECT_DEATH(RunPipeline("unsigned g(void) { return 'abcdefg'; }"),
                 "character constant too long for type unsigned int");
}

TEST_F(PipelineTest, StaticCharConstantTooWideForInt_Neg)
{
    EXPECT_DEATH(RunPipeline("int x = 'abcde';"), "character constant too long for type int");
}

// An enum defined in a typedef, a variable or a member declaration declares its
// constants as a bare `enum { ... };` does.
TEST_F(PipelineTest, EnumConstantsFromTypedefVarAndMember)
{
    RunPipeline(R"(typedef enum { K_A, K_B } Kind;
enum { V_A = 3, V_B } v, w;
struct S { enum { M_A, M_B = 7 } m; };
int f(Kind k)
{
    enum { L_A = 5 } l = L_A;
    switch (k) {
    case K_B:
        return V_B + M_B + l;
    default:
        return K_A;
    }
}
)");
    const Symbol *b = symtab_get_opt("K_B");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->u.enum_val, 1);
    ASSERT_NE(symtab_get_opt("V_B"), nullptr);
    EXPECT_EQ(symtab_get_opt("V_B")->u.enum_val, 4);
    ASSERT_NE(symtab_get_opt("M_B"), nullptr);
    EXPECT_EQ(symtab_get_opt("M_B")->u.enum_val, 7);
}

// A tag spelled like a typedef name, and a typedef repeated with the same type (C11 §6.7p3).
TEST_F(PipelineTest, TypedefRepeatedWithSameType)
{
    RunPipeline(R"(typedef struct W W;
struct W { int x; };
typedef struct W W;
typedef enum E E;
enum E { E_A };
typedef unsigned long Size, *SizeP;
typedef unsigned long Size;
int f(W *w, SizeP p) { return w->x + (int)*p + E_A; }
)");
    EXPECT_NE(structtab_find("W"), nullptr);
}

TEST_F(PipelineTest, TypedefRedefinedWithOtherTypeDies)
{
    EXPECT_DEATH(RunPipeline("typedef int T; typedef long T;"), "redefinition of typedef 'T'");
}

TEST_F(PipelineTest, TypedefRepeatedInInnerScopeDies)
{
    EXPECT_DEATH(RunPipeline("typedef int T; void f(void) { typedef int T; }"),
                 "redefinition of typedef 'T'");
}

// An automatic aggregate zero-fills its enum and long double members.
TEST_F(PipelineTest, ZeroFillEnumAndLongDoubleMembers)
{
    RunPipeline(R"(enum E { A, B };
struct S { int x; enum E e; long double d; };
int f(void) { struct S s = { 1 }; return s.e + (int)s.d; }
)");
}
