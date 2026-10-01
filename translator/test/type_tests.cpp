#include "translate_test.h"

// Typed TAC: every parameter, local and temporary carries its type, functions and
// calls carry their function types, and a unit lists the names it uses but does not
// define.  x86_64 widths make int, long and char distinguishable.

static bool Has(const std::string &yaml, const char *text)
{
    return yaml.find(text) != std::string::npos;
}

TEST_F(TranslateTestX86, TypedSymbols)
{
    std::string yaml =
        CompileUnitToTypedYaml("long f(int a, char *p) { long x = a; return x + p[1]; }");
    EXPECT_TRUE(Has(yaml, R"(
  type: fn(int, *schar) -> long
  params:
    - param: %a
      type: int
    - param: %p
      type: *schar
  locals:
    - local: %x
      type: long
    - local: %0
      type: long
    - local: %1
      type: long
    - local: %2
      type: *schar
    - local: %3
      type: schar
    - local: %4
      type: long
    - local: %5
      type: long
  body:
)")) << yaml;
}

TEST_F(TranslateTestX86, CallCarriesCalleeType)
{
    std::string yaml = CompileUnitToTypedYaml(R"(
        int printf(const char *fmt, ...);
        double (*fp)(float);
        void g(void) { printf("x", 1.0); fp(2.0f); }
    )");
    EXPECT_TRUE(Has(yaml, "      fun_type: fn(*schar, ...) -> int\n")) << yaml;
    EXPECT_TRUE(Has(yaml, "      indirect: true\n")) << yaml;
    EXPECT_TRUE(Has(yaml, "      fun_type: fn(float) -> double\n")) << yaml;
    EXPECT_TRUE(Has(yaml, R"(
  kind: extern
  name: printf
  type:
    kind: fun_type
    param_types:
      - type:
        kind: pointer
        target:
          kind: schar
    ret_type:
      kind: int
    variadic: true
)")) << yaml;
}

// Referenced but undefined: listed, in name order, with the declared type (an
// incomplete struct has size 0).  Unreferenced or defined later in the unit: not.
TEST_F(TranslateTestX86, ExternListsUsedUndefinedNames)
{
    std::string yaml = CompileUnitToTypedYaml(R"(
        extern int used, unused;
        int defined_later(void);
        int helper(int);
        struct S;
        extern struct S opaque;
        int f(void) { return used + helper(1) + defined_later() + (&opaque != 0); }
        int defined_later(void) { return 2; }
    )");
    std::string externs = yaml.substr(yaml.find("  kind: extern"));
    EXPECT_EQ(externs, R"(  kind: extern
  name: helper
  type:
    kind: fun_type
    param_types:
      - type:
        kind: int
    ret_type:
      kind: int
- toplevel:
  kind: extern
  name: opaque
  type:
    kind: structure
    tag: S
    size: 0
    alignment: 0
- toplevel:
  kind: extern
  name: used
  type:
    kind: int
)");
}

// ---------------------------------------------------------------------------
// Struct layout: size, alignment and members, enough for psABI classification
// ---------------------------------------------------------------------------

TEST_F(TranslateTestX86, StructLayout)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct P { char c; double d; int a[3]; };
        double f(struct P p) { return p.d; }
    )");
    const Tac_Type *t = SymbolType(tac, "f", "%p");
    EXPECT_EQ(TypeStr(t), "struct P(32,8)");
    EXPECT_FALSE(t->u.structure.is_union);
    EXPECT_EQ(Members(t), "c@0:schar d@8:double a@16:[3]int");
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestX86, UnionLayout)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        union U { float f; long l; };
        long f(union U u) { return u.l; }
    )");
    const Tac_Type *t = SymbolType(tac, "f", "%u");
    EXPECT_EQ(TypeStr(t), "union U(8,8)");
    EXPECT_TRUE(t->u.structure.is_union);
    EXPECT_EQ(Members(t), "f@0:float l@0:long");
    tac_free_toplevel(tac);
}

// A struct is expanded by value, not behind a pointer, so a self-referential struct
// terminates; a nested struct member is expanded in turn.
TEST_F(TranslateTestX86, NestedAndSelfReferentialStruct)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct In { short s; float x; };
        struct N { int v; struct In in; struct N *next; };
        int f(struct N n) { return n.v; }
    )");
    const Tac_Type *t = SymbolType(tac, "f", "%n");
    EXPECT_EQ(Members(t), "v@0:int in@4:struct In(8,4) next@16:*struct N(24,8)");
    const Tac_Member *in = t->u.structure.members->next;
    EXPECT_EQ(Members(in->type), "s@0:short x@4:float");
    const Tac_Type *next = in->next->type->u.pointer.target_type;
    EXPECT_EQ(next->u.structure.members, nullptr);
    tac_free_toplevel(tac);
}

// Sibling block scopes may reuse a tag; each object keeps its own definition, even
// though both are purged from the struct table before lowering.
TEST_F(TranslateTestX86, SiblingBlockScopeTags)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        long f(void) {
            { struct Q { int a; } u; u.a = 1; }
            { struct Q { long b; char c; } v; v.b = 2; return v.b; }
        }
    )");
    EXPECT_EQ(Members(SymbolType(tac, "f", "%u")), "a@0:int");
    EXPECT_EQ(TypeStr(SymbolType(tac, "f", "%v")), "struct Q(16,8)");
    EXPECT_EQ(Members(SymbolType(tac, "f", "%v")), "b@0:long c@8:schar");
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestX86, NestedBlockScopeStruct)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        int g(void) { struct A { int x; }; struct B { struct A a; char c; } b; b.c = 1; return b.c; }
    )");
    const Tac_Type *t = SymbolType(tac, "g", "%b");
    EXPECT_EQ(Members(t), "a@0:struct A(4,4) c@4:schar");
    EXPECT_EQ(Members(t->u.structure.members->type), "x@0:int");
    tac_free_toplevel(tac);
}

// ---------------------------------------------------------------------------
// Aggregate copies go in chunks of the aggregate's alignment, at most one word
// ---------------------------------------------------------------------------

// "kind@offset:type" of each chunk store into an aggregate, in order: COPY_*_TO_OFFSET
// gives its offset, STORE/STORE_BYTE through a pointer gives "ptr".
static std::string ChunkStores(const Tac_TopLevel *tac, const char *fn)
{
    std::string out;
    for (; tac; tac = tac->next) {
        if (tac->kind != TAC_TOPLEVEL_FUNCTION || strcmp(tac->u.function.name, fn) != 0)
            continue;
        for (const Tac_Instruction *in = tac->u.function.body; in; in = in->next) {
            const Tac_Val *src;
            std::string where;
            if (in->kind == TAC_INSTRUCTION_COPY_TO_OFFSET ||
                in->kind == TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET) {
                src   = in->u.copy_to_offset.src;
                where = std::to_string(in->u.copy_to_offset.offset);
            } else if (in->kind == TAC_INSTRUCTION_STORE || in->kind == TAC_INSTRUCTION_STORE_BYTE) {
                src   = in->u.store.src;
                where = "ptr";
            } else {
                continue;
            }
            if (src->kind != TAC_VAL_VAR)
                continue;
            const Tac_Type *t = nullptr;
            for (const Tac_Param *p = tac->u.function.locals; p; p = p->next)
                if (strcmp(p->name, src->u.var_name) == 0)
                    t = p->type;
            char *ts = tac_type_str(t);
            out += std::string(out.empty() ? "" : " ") + where + ":" + ts;
            xfree(ts);
        }
    }
    return out;
}

TEST_F(TranslateTestX86, AggregateCopyByAlignment)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct I { int a, b, c; } gi;
        void f(void) { struct I x; x = gi; }
    )");
    EXPECT_EQ(ChunkStores(tac, "f"), "0:uint 4:uint 8:uint");
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestX86, AggregateCopyThroughPointerByBytes)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct C { char c[3]; };
        void f(struct C *p, struct C *q) { *p = *q; }
    )");
    EXPECT_EQ(ChunkStores(tac, "f"), "ptr:uchar ptr:uchar ptr:uchar");
    tac_free_toplevel(tac);
}

// An alignment above one word still copies by words.
TEST_F(TranslateTestX86, AggregateCopyCappedAtWord)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct L { long double x; } gl;
        void f(void) { struct L y = gl; }
    )");
    EXPECT_EQ(ChunkStores(tac, "f"), "0:ulong 8:ulong");
    tac_free_toplevel(tac);
}

// ---------------------------------------------------------------------------
// Struct by value follows the target: RV64 returns up to 16 bytes by value, passes
// struct arguments whole, and merges a struct `?:` in a slot
// ---------------------------------------------------------------------------

static const Tac_TopLevel *Function(const Tac_TopLevel *tac, const char *name)
{
    for (; tac; tac = tac->next)
        if (tac->kind == TAC_TOPLEVEL_FUNCTION && strcmp(tac->u.function.name, name) == 0)
            return tac;
    return nullptr;
}

static const Tac_Instruction *FirstCall(const Tac_TopLevel *fn)
{
    for (const Tac_Instruction *in = fn->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_FUN_CALL)
            return in;
    return nullptr;
}

static int CountArgs(const Tac_Instruction *call)
{
    int n = 0;
    for (const Tac_Val *a = call->u.fun_call.args; a; a = a->next)
        n++;
    return n;
}

TEST_F(TranslateTestRiscv, SixteenByteStructByValue)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct P { long a, b; };
        struct P make(long a) { struct P p = { a, a }; return p; }
        long use(void) { struct P q = make(1); return q.b; }
    )");
    const Tac_TopLevel *make = Function(tac, "make");
    EXPECT_STREQ(make->u.function.params->name, "%a"); // no hidden pointer
    const Tac_Instruction *call = FirstCall(Function(tac, "use"));
    EXPECT_EQ(CountArgs(call), 1);
    ASSERT_NE(call->u.fun_call.dst, nullptr);
    EXPECT_EQ(TypeStr(SymbolType(tac, "use", call->u.fun_call.dst->u.var_name)), "struct P(16,8)");
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestRiscv, WideStructReturnThroughHiddenPointer)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct T { long a, b, c; };
        struct T make(long a) { struct T t = { a, a, a }; return t; }
        long use(void) { struct T q = make(1); return q.c; }
    )");
    const Tac_TopLevel *make = Function(tac, "make");
    EXPECT_STREQ(make->u.function.params->name, "%.ret");
    EXPECT_EQ(TypeStr(make->u.function.params->type), "*struct T(24,8)");
    const Tac_Instruction *call = FirstCall(Function(tac, "use"));
    EXPECT_EQ(CountArgs(call), 2); // the result slot's address, then a
    EXPECT_EQ(call->u.fun_call.dst, nullptr);
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestRiscv, StructArgumentPassedWhole)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct T { long a, b, c; };
        long get(struct T t) { return t.c; }
        long use(struct T *p) { return get(*p); }
    )");
    const Tac_TopLevel *get = Function(tac, "get");
    EXPECT_STREQ(get->u.function.params->name, "%t");
    EXPECT_EQ(get->u.function.params->next, nullptr); // no per-word fillers
    const Tac_Instruction *call = FirstCall(Function(tac, "use"));
    ASSERT_EQ(CountArgs(call), 1);
    EXPECT_EQ(TypeStr(SymbolType(tac, "use", call->u.fun_call.args->u.var_name)),
              "struct T(24,8)");
    tac_free_toplevel(tac);
}

// BESM-6 splits a struct argument wider than a word into words.
TEST_F(TranslateTest, StructArgumentSplitIntoWords)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct T { long a, b, c; };
        long get(struct T t) { return t.c; }
        long use(struct T *p) { return get(*p); }
    )");
    int nparams = 0;
    for (const Tac_Param *p = Function(tac, "get")->u.function.params; p; p = p->next)
        nparams++;
    EXPECT_EQ(nparams, 3);
    EXPECT_EQ(CountArgs(FirstCall(Function(tac, "use"))), 3);
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestRiscv, StructConditionalMergedInSlot)
{
    std::string yaml = CompileToYaml(R"(
        struct S { short a, b, c; };
        short f(int k, struct S x, struct S y) { return (k ? x : y).b; }
    )");
    EXPECT_NE(yaml.find("kind: allocate_local"), std::string::npos) << yaml;
    EXPECT_EQ(yaml.find("kind: copy\n"), std::string::npos) << yaml;
}

// ---------------------------------------------------------------------------
// On a byte-addressed target pointer arithmetic scales by the pointee size
// ---------------------------------------------------------------------------

// "kind[:scale|:divisor]" of each ADD_PTR, SUBTRACT and DIVIDE in function `fn`.
static std::string PointerOps(const Tac_TopLevel *tac, const char *fn)
{
    std::string out;
    for (const Tac_Instruction *in = Function(tac, fn)->u.function.body; in; in = in->next) {
        std::string op;
        if (in->kind == TAC_INSTRUCTION_ADD_PTR)
            op = "add_ptr:" + std::to_string(in->u.add_ptr.scale);
        else if (in->kind == TAC_INSTRUCTION_BINARY && in->u.binary.op == TAC_BINARY_SUBTRACT)
            op = "sub";
        else if (in->kind == TAC_INSTRUCTION_BINARY && in->u.binary.op == TAC_BINARY_DIVIDE)
            op = "div:" + std::to_string(in->u.binary.src2->u.constant->u.int_val);
        else if (in->kind == TAC_INSTRUCTION_BINARY && in->u.binary.op == TAC_BINARY_ADD)
            op = "add";
        if (!op.empty())
            out += (out.empty() ? "" : " ") + op;
    }
    return out;
}

TEST_F(TranslateTestRiscv, PointerArithmeticScales)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        int *step(int *p, long n) { p = p + n; return p - 1; }
        void inc(short *s, long **pp) { ++s; s += 2; (*pp)++; }
        long diff(int *p, int *q) { return q - p; }
    )");
    EXPECT_EQ(PointerOps(tac, "step"), "add_ptr:4 add_ptr:4");
    EXPECT_EQ(PointerOps(tac, "inc"), "add_ptr:2 add_ptr:2 add_ptr:8");
    EXPECT_EQ(PointerOps(tac, "diff"), "sub div:4");
    tac_free_toplevel(tac);
}

// BESM-6 is word-addressed: a one-word pointee needs no scaling.
TEST_F(TranslateTest, WordPointerArithmeticUnscaled)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        int *step(int *p, long n) { return p + n; }
        long diff(int *p, int *q) { return q - p; }
    )");
    EXPECT_EQ(PointerOps(tac, "step"), "add");
    EXPECT_EQ(PointerOps(tac, "diff"), "sub");
    tac_free_toplevel(tac);
}

// A block-scope extern object or function declaration is purged with its block, so it
// travels as an EXTERN toplevel ahead of the function; once per unit.
TEST_F(TranslateTestRiscv, BlockScopeExterns)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        long f(void) { extern long q; int g(int); return q + g(1); }
        long h(void) { extern long q; return q; }
    )");
    std::string order;
    for (const Tac_TopLevel *t = tac; t; t = t->next) {
        if (t->kind == TAC_TOPLEVEL_EXTERN)
            order += std::string(t->u.extern_.name) + ":" + TypeStr(t->u.extern_.type) + " ";
        else if (t->kind == TAC_TOPLEVEL_FUNCTION)
            order += std::string(t->u.function.name) + " ";
    }
    EXPECT_EQ(order, "q:long g:fn(int) -> int f h ");
    tac_free_toplevel(tac);
}
