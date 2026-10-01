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
