//
// Canonical initializer form produced by normalize_init: brace elision and braced scalars.
//
#include "typecheck.h"
#include "typecheck_fixture.h"

class NormalizeTest : public TypecheckTest {
protected:
    // Parse src, typecheck every external declaration but the last, and normalize the
    // last one's initializer in static mode (leaves stay raw, so no symbols are needed).
    const Initializer *Normalize(const char *src)
    {
        ParseProgram(src);
        ExternalDecl *d = program->decls;
        for (; d->next; d = d->next)
            typecheck_global_decl(d);
        InitDeclarator *id = d->u.declaration->u.var.declarators;
        id->init           = normalize_init(id->type, id->init, INIT_STATIC);
        type               = id->type;
        return id->init;
    }

    const Type *type{};
};

static size_t item_count(const Initializer *init)
{
    size_t n = 0;
    for (const InitItem *item = init->u.items; item; item = item->next)
        n++;
    return n;
}

static const Initializer *item_at(const Initializer *init, size_t i)
{
    const InitItem *item = init->u.items;
    for (; i > 0; i--)
        item = item->next;
    return item->init;
}

static long int_value(const Initializer *init)
{
    EXPECT_EQ(init->kind, INITIALIZER_SINGLE);
    EXPECT_EQ(init->u.expr->kind, EXPR_LITERAL);
    return init->u.expr->u.literal->u.int_val;
}

// A braced scalar is unwrapped (§6.7.9p11).
TEST_F(NormalizeTest, BracedScalar)
{
    const Initializer *init = Normalize("int x = { 5 };");
    EXPECT_EQ(int_value(init), 5);
}

// Missing elements are NULL holes; the node has exactly N items.
TEST_F(NormalizeTest, ArrayHoles)
{
    const Initializer *init = Normalize("int a[4] = { 1, 2 };");
    ASSERT_EQ(init->kind, INITIALIZER_COMPOUND);
    ASSERT_EQ(item_count(init), 4u);
    EXPECT_EQ(int_value(item_at(init, 0)), 1);
    EXPECT_EQ(int_value(item_at(init, 1)), 2);
    EXPECT_EQ(item_at(init, 2), nullptr);
    EXPECT_EQ(item_at(init, 3), nullptr);
}

// Brace elision fills a 2-D array row by row; a partial last row keeps a hole.
TEST_F(NormalizeTest, ElidedMatrix)
{
    const Initializer *init = Normalize("int a[2][2] = { 1, 2, 3 };");
    ASSERT_EQ(item_count(init), 2u);
    const Initializer *row0 = item_at(init, 0);
    const Initializer *row1 = item_at(init, 1);
    ASSERT_EQ(row0->kind, INITIALIZER_COMPOUND);
    ASSERT_EQ(row1->kind, INITIALIZER_COMPOUND);
    EXPECT_EQ(int_value(item_at(row0, 0)), 1);
    EXPECT_EQ(int_value(item_at(row0, 1)), 2);
    EXPECT_EQ(int_value(item_at(row1, 0)), 3);
    EXPECT_EQ(item_at(row1, 1), nullptr);
}

// An elided unsized array of structs gets its length from the rows filled.
TEST_F(NormalizeTest, ElidedStructArray)
{
    const Initializer *init =
        Normalize("struct s { char *name; int v; }; struct s tab[] = { \"AB\", 1, \"CD\" };");
    EXPECT_EQ(get_array_size(type), 2u);
    ASSERT_EQ(item_count(init), 2u);
    const Initializer *second = item_at(init, 1);
    ASSERT_EQ(item_count(second), 2u);
    EXPECT_EQ(item_at(second, 0)->u.expr->u.literal->kind, LITERAL_STRING);
    EXPECT_EQ(item_at(second, 1), nullptr);
}

// Elision stops at the end of the inner array; the next value goes to the next member.
TEST_F(NormalizeTest, ElisionBoundary)
{
    const Initializer *init =
        Normalize("struct s { int a[2]; int b; }; struct s g = { 1, 2, 3 };");
    ASSERT_EQ(item_count(init), 2u);
    const Initializer *a = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(a, 0)), 1);
    EXPECT_EQ(int_value(item_at(a, 1)), 2);
    EXPECT_EQ(int_value(item_at(init, 1)), 3);
}

// A partial inner brace ends that subobject; the next value goes to the next member.
TEST_F(NormalizeTest, PartialInnerBrace)
{
    const Initializer *init =
        Normalize("struct s { int a[2]; int b; }; struct s g = { { 1 }, 2 };");
    const Initializer *a = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(a, 0)), 1);
    EXPECT_EQ(item_at(a, 1), nullptr);
    EXPECT_EQ(int_value(item_at(init, 1)), 2);
}

// A string literal for a char array member is kept whole, not elided into chars.
TEST_F(NormalizeTest, StringForCharArrayMember)
{
    const Initializer *init =
        Normalize("struct s { char n[4]; int v; }; struct s g[] = { \"AB\", 1, \"CD\", 2 };");
    ASSERT_EQ(item_count(init), 2u);
    const Initializer *n = item_at(item_at(init, 1), 0);
    ASSERT_EQ(n->kind, INITIALIZER_SINGLE);
    EXPECT_EQ(n->u.expr->u.literal->kind, LITERAL_STRING);
}

// A braced string for a char array is unwrapped (§6.7.9p14).
TEST_F(NormalizeTest, BracedString)
{
    const Initializer *init = Normalize("char s[4] = { \"AB\" };");
    ASSERT_EQ(init->kind, INITIALIZER_SINGLE);
    EXPECT_EQ(init->u.expr->u.literal->kind, LITERAL_STRING);
}

// A union takes a single item, for its first member.
TEST_F(NormalizeTest, UnionElision)
{
    const Initializer *init =
        Normalize("union u { int i[2]; char *p; }; union u g = { 1, 2 };");
    ASSERT_EQ(item_count(init), 1u);
    const Initializer *i = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(i, 0)), 1);
    EXPECT_EQ(int_value(item_at(i, 1)), 2);
}

TEST_F(NormalizeTest, ExcessElidedDies)
{
    EXPECT_DEATH(Normalize("int a[2][2] = { 1, 2, 3, 4, 5 };"),
                 "Too many elements in array initializer");
}

TEST_F(NormalizeTest, ExcessBracedDies)
{
    EXPECT_DEATH(Normalize("int a[2][2] = { { 1, 2, 3 } };"),
                 "Too many elements in array initializer");
}

TEST_F(NormalizeTest, EmptyScalarDies)
{
    EXPECT_DEATH(Normalize("int x = { };"), "Empty scalar initializer");
}

// An array designator chain; positional initialization continues in the inner row.
TEST_F(NormalizeTest, ArrayDesignatorChain)
{
    const Initializer *init = Normalize("int a[2][2] = { [1][0] = 5, 6 };");
    EXPECT_EQ(item_at(init, 0), nullptr);
    const Initializer *row1 = item_at(init, 1);
    EXPECT_EQ(int_value(item_at(row1, 0)), 5);
    EXPECT_EQ(int_value(item_at(row1, 1)), 6);
}

// Automatic mode typechecks each leaf once, including one first seen at an aggregate
// slot (a struct value vs. brace elision); the fixture checks nothing leaks.
TEST_F(PipelineTest, NormalizeAutomatic)
{
    RunPipeline(R"(struct s { char *name; int v; };
struct t { int a[2]; int b; };
int f(void)
{
    struct s one = { "AB", 1 };
    struct s tab[] = { one, "CD", 2, { "EF" } };
    struct t x = { 1, 2, 3 };
    int m[2][2] = { 1, 2, 3 };
    int y = { 4 };
    char n[4] = { "GH" };
    return tab[1].v + x.b + m[1][0] + y + n[0];
}
)");
}

TEST_F(PipelineTest, NormalizeAutomaticExcessElidedDies)
{
    EXPECT_DEATH(RunPipeline("void f(void) { int a[2][2] = { 1, 2, 3, 4, 5 }; }"),
                 "Too many elements in array initializer");
}

TEST_F(PipelineTest, NormalizeAutomaticExcessStructDies)
{
    EXPECT_DEATH(RunPipeline("struct s { int a; }; void f(void) { struct s x[1] = { { 1, 2 } }; }"),
                 "Too many elements in struct initializer");
}

TEST_F(PipelineTest, NormalizeAutomaticEmptyScalarDies)
{
    EXPECT_DEATH(RunPipeline("void f(void) { int x = { }; }"), "Empty scalar initializer");
}

// --- Field designators -------------------------------------------------------

// Out-of-order field designators land in member order; the gap is a hole.
TEST_F(NormalizeTest, FieldDesignatorsOutOfOrder)
{
    const Initializer *init =
        Normalize("struct s { int a, b, c; }; struct s g = { .c = 3, .a = 1 };");
    ASSERT_EQ(item_count(init), 3u);
    EXPECT_EQ(int_value(item_at(init, 0)), 1);
    EXPECT_EQ(item_at(init, 1), nullptr);
    EXPECT_EQ(int_value(item_at(init, 2)), 3);
    for (const InitItem *item = init->u.items; item; item = item->next)
        EXPECT_EQ(item->designators, nullptr);
}

// Positional initialization resumes after the designated member (§6.7.9p17).
TEST_F(NormalizeTest, PositionalAfterDesignator)
{
    const Initializer *init =
        Normalize("struct s { int a, b, c; }; struct s g = { .b = 1, 2 };");
    EXPECT_EQ(item_at(init, 0), nullptr);
    EXPECT_EQ(int_value(item_at(init, 1)), 1);
    EXPECT_EQ(int_value(item_at(init, 2)), 2);
}

// A repeated member keeps the later initializer (§6.7.9p19).
TEST_F(NormalizeTest, FieldOverride)
{
    const Initializer *init =
        Normalize("struct s { int a, b; }; struct s g = { .a = 1, 2, .a = 3 };");
    EXPECT_EQ(int_value(item_at(init, 0)), 3);
    EXPECT_EQ(int_value(item_at(init, 1)), 2);
}

// A designator ends brace elision: it belongs to the enclosing brace level.
TEST_F(NormalizeTest, DesignatorEndsElision)
{
    const Initializer *init = Normalize(
        "struct s { struct in { int a, b; } in; int c; }; struct s g = { 1, .c = 3 };");
    const Initializer *in = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(in, 0)), 1);
    EXPECT_EQ(item_at(in, 1), nullptr);
    EXPECT_EQ(int_value(item_at(init, 1)), 3);
}

// A union item for a non-first member keeps one designator naming it.
TEST_F(NormalizeTest, UnionFieldDesignator)
{
    const Initializer *init =
        Normalize("union u { int i; char *p; }; union u g = { .p = \"AB\" };");
    ASSERT_EQ(item_count(init), 1u);
    const Designator *d = init->u.items->designators;
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->kind, DESIGNATOR_FIELD);
    EXPECT_STREQ(d->u.name, "p");
    EXPECT_EQ(d->next, nullptr);
    EXPECT_EQ(init->u.items->init->u.expr->u.literal->kind, LITERAL_STRING);
}

// Designating the first member of a union leaves no designator.
TEST_F(NormalizeTest, UnionFirstMemberDesignator)
{
    const Initializer *init =
        Normalize("union u { int i; char *p; }; union u g = { .p = \"AB\", .i = 5 };");
    EXPECT_EQ(init->u.items->designators, nullptr);
    EXPECT_EQ(int_value(init->u.items->init), 5);
}

// Positional re-entry into a union picks the first member again, dropping another's value.
TEST_F(NormalizeTest, UnionPositionalReentry)
{
    const Initializer *init = Normalize("union u { int i; char *p; };"
                                        "struct s { int a; union u x; };"
                                        "struct s g = { .x = { .p = \"AB\" }, .a = 1, 5 };");
    const Initializer *x = item_at(init, 1);
    EXPECT_EQ(x->u.items->designators, nullptr);
    EXPECT_EQ(int_value(x->u.items->init), 5);
}

TEST_F(NormalizeTest, DesignatorAfterLastMemberDies)
{
    EXPECT_DEATH(Normalize("struct s { int a, b; }; struct s g = { .b = 1, 2 };"),
                 "Too many elements in struct initializer");
}

TEST_F(NormalizeTest, UnknownMemberDies)
{
    EXPECT_DEATH(Normalize("struct s { int a; }; struct s g = { .z = 1 };"),
                 "no member named 'z' in 'struct s'");
}

TEST_F(NormalizeTest, UnionExcessAfterDesignatorDies)
{
    EXPECT_DEATH(Normalize("union u { int i; int j; }; union u g = { .j = 1, 2 };"),
                 "Too many elements in union initializer");
}

TEST_F(NormalizeTest, FieldDesignatorOnArrayDies)
{
    EXPECT_DEATH(Normalize("int a[2] = { .x = 1 };"), "Field designator .x in array initializer");
}

TEST_F(NormalizeTest, ArrayDesignatorOnStructDies)
{
    EXPECT_DEATH(Normalize("struct s { int a; }; struct s g = { [0] = 1 };"),
                 "Array designator in struct initializer");
}

TEST_F(NormalizeTest, DesignatorOnScalarDies)
{
    EXPECT_DEATH(Normalize("int x = { .a = 1 };"), "Designator in scalar initializer");
}

// A field designator chain.
TEST_F(NormalizeTest, FieldDesignatorChain)
{
    const Initializer *init = Normalize("struct in { int a, b; }; struct s { struct in x; };"
                                        "struct s g = { .x.b = 1 };");
    const Initializer *x = item_at(init, 0);
    EXPECT_EQ(item_at(x, 0), nullptr);
    EXPECT_EQ(int_value(item_at(x, 1)), 1);
}

// A static union initialized through a non-first member zero-pads from that member's
// size, not the first member's.
TEST_F(PipelineTest, StaticUnionDesignatorPadding)
{
    RunPipeline(R"(union u { int i; int a[3]; };
union u g = { .a = { 1 } };
union u h = { .i = 2 };
)");
    const Symbol *sym = symtab_get("g");
    ASSERT_NE(sym, nullptr);
    const Tac_StaticInit *init = sym->u.static_var.init_list;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(init->u.int_val, 1);
    ASSERT_NE(init->next, nullptr);
    EXPECT_EQ(init->next->kind, TAC_STATIC_INIT_ZERO);
    EXPECT_EQ(init->next->u.zero_bytes, 8u); // x86_64 host sizes: a[1..2]
    EXPECT_EQ(init->next->next, nullptr);

    init = symtab_get("h")->u.static_var.init_list;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->u.int_val, 2);
    ASSERT_NE(init->next, nullptr);
    EXPECT_EQ(init->next->u.zero_bytes, 8u); // the rest of the 12-byte union
}

// Automatic mode: designators into block-scope struct and union tags; nothing leaks.
TEST_F(PipelineTest, DesignatorsAutomatic)
{
    RunPipeline(R"(int f(void)
{
    struct s { int v; char *name; };
    union u { int i; char *p; };
    struct s x = { .name = "AB", .v = 1 };
    union u y = { .p = "CD" };
    struct s z = { .name = "EF", .name = "GH" };
    return x.v + (y.p != 0) + (z.name != 0);
}
)");
}

// --- Array designators -------------------------------------------------------

// Positional initialization resumes after a designated index; a later designator
// overrides (§6.7.9p17, p19).
TEST_F(NormalizeTest, ArrayDesignators)
{
    const Initializer *init = Normalize("int a[5] = { [3] = 7, 8, [1] = 2 };");
    ASSERT_EQ(item_count(init), 5u);
    EXPECT_EQ(item_at(init, 0), nullptr);
    EXPECT_EQ(int_value(item_at(init, 1)), 2);
    EXPECT_EQ(item_at(init, 2), nullptr);
    EXPECT_EQ(int_value(item_at(init, 3)), 7);
    EXPECT_EQ(int_value(item_at(init, 4)), 8);
}

// An unsized array is as long as its highest initialized index plus one (§6.7.9p22).
TEST_F(NormalizeTest, UnsizedArrayDesignator)
{
    const Initializer *init = Normalize("int a[] = { [9] = 1 };");
    EXPECT_EQ(get_array_size(type), 10u);
    ASSERT_EQ(item_count(init), 10u);
    EXPECT_EQ(item_at(init, 0), nullptr);
    EXPECT_EQ(int_value(item_at(init, 9)), 1);
}

// Positional elements after a designator extend an unsized array; an earlier designator
// below the end does not shrink it.
TEST_F(NormalizeTest, UnsizedArrayDesignatorThenPositional)
{
    Normalize("int a[] = { [2] = 1, 2, [0] = 3 };");
    EXPECT_EQ(get_array_size(type), 4u);
}

// An index may be any integer constant expression, including an enumerator.
TEST_F(NormalizeTest, ArrayDesignatorConstExpr)
{
    const Initializer *init =
        Normalize("enum { TWO = 2 }; int a[4] = { [TWO] = 5, [TWO - 1] = 4, [sizeof(char)] = 6 };");
    EXPECT_EQ(int_value(item_at(init, 1)), 6);
    EXPECT_EQ(int_value(item_at(init, 2)), 5);
}

// A designator ends brace elision in a 2-D array.
TEST_F(NormalizeTest, ArrayDesignatorEndsElision)
{
    const Initializer *init = Normalize("int m[2][2] = { 1, [1] = { 3, 4 } };");
    const Initializer *row0 = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(row0, 0)), 1);
    EXPECT_EQ(item_at(row0, 1), nullptr);
    EXPECT_EQ(int_value(item_at(item_at(init, 1), 1)), 4);
}

TEST_F(NormalizeTest, ArrayDesignatorNegativeDies)
{
    EXPECT_DEATH(Normalize("int a[4] = { [-1] = 1 };"), "Array designator index -1 is negative");
}

TEST_F(NormalizeTest, ArrayDesignatorOutOfBoundsDies)
{
    EXPECT_DEATH(Normalize("int a[4] = { [4] = 1 };"),
                 "Array designator index 4 is out of bounds for array of 4");
}

// The parser rejects a non-constant index.
TEST_F(NormalizeTest, ArrayDesignatorNonConstantDies)
{
    EXPECT_DEATH(Normalize("int n; int a[4] = { [n] = 1 };"), "Expected constant expression");
}

TEST_F(NormalizeTest, ArrayDesignatorRealIndexDies)
{
    EXPECT_DEATH(Normalize("int a[4] = { [1.0] = 1 };"),
                 "Array designator index is not an integer constant expression");
}

// Automatic mode: designated char * elements and an unsized array; nothing leaks.
TEST_F(PipelineTest, ArrayDesignatorsAutomatic)
{
    RunPipeline(R"(int f(void)
{
    char *names[] = { [2] = "C", [0] = "A" };
    int a[5] = { [3] = 7, 8, [1] = 2 };
    return sizeof names / sizeof names[0] + a[4];
}
)");
}

// sizeof an automatic unsized array sees the length its initializer gave it.
TEST_F(PipelineTest, AutomaticUnsizedArraySizeof)
{
    RunPipeline(R"(int f(void)
{
    int n[] = { 1, 2 };
    int d[] = { [4] = 1 };
    _Static_assert(sizeof n == 2 * sizeof(int), "n has 2 elements");
    _Static_assert(sizeof d == 5 * sizeof(int), "d has 5 elements");
    return 0;
}
)");
}

// --- Designator chains -------------------------------------------------------

// f10: a chain into an array member, then a plain field designator.
TEST_F(NormalizeTest, ChainIntoArrayMember)
{
    const Initializer *init =
        Normalize("struct s { int a[3]; int b; }; struct s g = { .a[1] = 5, .b = 2 };");
    const Initializer *a = item_at(init, 0);
    EXPECT_EQ(item_at(a, 0), nullptr);
    EXPECT_EQ(int_value(item_at(a, 1)), 5);
    EXPECT_EQ(item_at(a, 2), nullptr);
    EXPECT_EQ(int_value(item_at(init, 1)), 2);
}

// After a chain, positional initialization continues with the next subobject after the
// designated one: inside the inner array, then past it.
TEST_F(NormalizeTest, ChainPositionalContinuation)
{
    const Initializer *init =
        Normalize("struct s { int a[3]; int b; }; struct s g = { .a[1] = 5, 6, 7 };");
    const Initializer *a = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(a, 1)), 5);
    EXPECT_EQ(int_value(item_at(a, 2)), 6);
    EXPECT_EQ(int_value(item_at(init, 1)), 7);
}

// A chain refines an earlier brace-list initializer in place (§6.7.9p19).
TEST_F(NormalizeTest, ChainRefinesInPlace)
{
    const Initializer *init =
        Normalize("struct s { int a[3]; }; struct s g = { .a = { 1, 2, 3 }, .a[1] = 9 };");
    const Initializer *a = item_at(init, 0);
    EXPECT_EQ(int_value(item_at(a, 0)), 1);
    EXPECT_EQ(int_value(item_at(a, 1)), 9);
    EXPECT_EQ(int_value(item_at(a, 2)), 3);
}

// A later whole-element initializer replaces an earlier chained one.
TEST_F(NormalizeTest, ChainThenWholeElement)
{
    const Initializer *init = Normalize("struct s { char *name; int v; };"
                                        "struct s tab[2] = { [1].v = 2, [0] = { \"AB\", 1 } };");
    const Initializer *e0 = item_at(init, 0);
    const Initializer *e1 = item_at(init, 1);
    EXPECT_EQ(item_at(e0, 0)->u.expr->u.literal->kind, LITERAL_STRING);
    EXPECT_EQ(int_value(item_at(e0, 1)), 1);
    EXPECT_EQ(item_at(e1, 0), nullptr);
    EXPECT_EQ(int_value(item_at(e1, 1)), 2);
}

// An unsized array is sized by an index at the head of a chain.
TEST_F(NormalizeTest, ChainSizesUnsizedArray)
{
    Normalize("struct s { int a, v; }; struct s tab[] = { [2].v = 1 };");
    EXPECT_EQ(get_array_size(type), 3u);
}

// A chain through a union member records the member; when that member is full,
// positional initialization continues after the union.
TEST_F(NormalizeTest, ChainThroughUnion)
{
    const Initializer *init = Normalize("struct p { int x, y; };"
                                        "union u { int i; struct p p; };"
                                        "struct s { union u u; int z; };"
                                        "struct s g = { .u.p.y = 2, 3 };");
    const Initializer *u = item_at(init, 0);
    ASSERT_NE(u->u.items->designators, nullptr);
    EXPECT_STREQ(u->u.items->designators->u.name, "p");
    const Initializer *p = u->u.items->init;
    EXPECT_EQ(item_at(p, 0), nullptr);
    EXPECT_EQ(int_value(item_at(p, 1)), 2);
    EXPECT_EQ(int_value(item_at(init, 1)), 3);
}

// Switching a union to another member drops the old member's value.
TEST_F(NormalizeTest, ChainSwitchesUnionMember)
{
    const Initializer *init = Normalize("struct p { int x, y; };"
                                        "union u { struct p q; struct p p; };"
                                        "union u g = { .q.x = 1, .p.y = 2 };");
    EXPECT_STREQ(init->u.items->designators->u.name, "p");
    const Initializer *p = init->u.items->init;
    EXPECT_EQ(item_at(p, 0), nullptr);
    EXPECT_EQ(int_value(item_at(p, 1)), 2);
}

TEST_F(NormalizeTest, ChainIntoScalarDies)
{
    EXPECT_DEATH(Normalize("struct s { int a; }; struct s g = { .a.x = 1 };"),
                 "Designator in scalar initializer");
}

TEST_F(NormalizeTest, ChainIntoStringDies)
{
    EXPECT_DEATH(Normalize("struct s { char n[4]; }; struct s g = { .n = \"AB\", .n[1] = 67 };"),
                 "Designator into a subobject initialized by an expression is not supported");
}

TEST_F(PipelineTest, ChainIntoExpressionDies)
{
    EXPECT_DEATH(RunPipeline(R"(struct in { int a, b; };
struct s { struct in in; };
void f(struct in v) { struct s x = { .in = v, .in.a = 1 }; }
)"),
                 "Designator into a subobject initialized by an expression is not supported");
}

// Automatic mode: chains, in-place refinement and a union member; nothing leaks.
TEST_F(PipelineTest, DesignatorChainsAutomatic)
{
    RunPipeline(R"(int f(void)
{
    struct s { int a[3]; int b; char *name; };
    union u { int i; struct s s; };
    struct s x = { .a = { 1, 2, 3 }, .a[1] = 9, .name = "AB" };
    struct s tab[] = { [1].name = "CD", [0] = { { 1 }, 2 } };
    union u y = { .s.b = 4 };
    return x.a[1] + tab[0].b + y.s.b;
}
)");
}
