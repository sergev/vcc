//
// Nested types
//
#include "fixture.h"

//
// Nested Struct with Simple Field
//      struct Outer {
//          int x;
//          struct Inner {
//              int y;
//          } inner;
//      }
//
TEST_F(ParserTest, NestedStructWithSimpleField)
{
    Type *type = TestType("struct Outer { int x; struct Inner { int y; } inner; };");

    //
    // Check struct Outer
    //
    EXPECT_EQ(type->kind, TYPE_STRUCT);
    EXPECT_STREQ(type->u.struct_t.name, "Outer");

    Field *x = type->u.struct_t.fields;
    ASSERT_NE(x, nullptr);
    Field *inner = x->next;
    EXPECT_EQ(inner->next, nullptr);

    //
    // Check field x
    //
    ASSERT_NE(x->u.member.type, nullptr);
    EXPECT_EQ(x->u.member.type->kind, TYPE_INT);

    EXPECT_STREQ(x->u.member.name, "x");
    EXPECT_EQ(x->u.member.bitfield, nullptr);

    //
    // Check field inner
    //
    ASSERT_NE(inner->u.member.type, nullptr);
    EXPECT_EQ(inner->u.member.type->kind, TYPE_STRUCT);
    EXPECT_STREQ(inner->u.member.type->u.struct_t.name, "Inner");

    EXPECT_STREQ(inner->u.member.name, "inner");
    EXPECT_EQ(inner->u.member.bitfield, nullptr);

    //
    // Check struct Inner
    //
    Field *y = inner->u.member.type->u.struct_t.fields;
    EXPECT_EQ(y->next, nullptr);

    //
    // Check field y
    //
    ASSERT_NE(y->u.member.type, nullptr);
    EXPECT_EQ(y->u.member.type->kind, TYPE_INT);

    EXPECT_STREQ(y->u.member.name, "y");
    EXPECT_EQ(y->u.member.bitfield, nullptr);

    free_type(type);
}

//
// Struct with Pointer to Itself
//      struct Node {
//          int data;
//          struct Node *next;
//      }
//
TEST_F(ParserTest, StructWithPointerToItself)
{
    Type *type = TestType("struct Node { int data; struct Node *next; };");

    //
    // Check struct Node
    //
    EXPECT_EQ(type->kind, TYPE_STRUCT);
    EXPECT_STREQ(type->u.struct_t.name, "Node");

    Field *data = type->u.struct_t.fields;
    ASSERT_NE(data, nullptr);
    Field *next = data->next;
    EXPECT_EQ(next->next, nullptr);

    //
    // Check field data
    //
    EXPECT_STREQ(data->u.member.name, "data");
    EXPECT_EQ(data->u.member.bitfield, nullptr);
    ASSERT_NE(data->u.member.type, nullptr);
    EXPECT_EQ(data->u.member.type->kind, TYPE_INT);

    //
    // Check field next
    //
    EXPECT_STREQ(next->u.member.name, "next");
    EXPECT_EQ(next->u.member.bitfield, nullptr);
    ASSERT_NE(next->u.member.type, nullptr);
    EXPECT_EQ(next->u.member.type->kind, TYPE_POINTER);
    EXPECT_EQ(next->u.member.type->qualifiers, nullptr);
    EXPECT_EQ(next->u.member.type->u.pointer.qualifiers, nullptr);

    Type *target = next->u.member.type->u.pointer.target;
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->kind, TYPE_STRUCT);
    EXPECT_STREQ(target->u.struct_t.name, "Node");
    EXPECT_EQ(target->qualifiers, nullptr);

    free_type(type);
}

//
// Function Pointer with Struct Parameter
//      void (*)(struct Pair {
//                  int x;
//                  int y;
//              })
//
TEST_F(ParserTest, FunctionPointerWithStructParameter)
{
    Type *type = TestType("void (*)(struct Pair { int x; int y; });");

    //
    // void (*)(struct Pair { int x; int y; })
    //
    EXPECT_EQ(type->kind, TYPE_POINTER);
    EXPECT_EQ(type->qualifiers, nullptr);
    EXPECT_EQ(type->u.pointer.qualifiers, nullptr);

    Type *fn = type->u.pointer.target;
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->kind, TYPE_FUNCTION);
    EXPECT_EQ(fn->qualifiers, nullptr);
    EXPECT_FALSE(fn->u.function.variadic);

    Type *ret = fn->u.function.return_type;
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->kind, TYPE_VOID);
    EXPECT_EQ(ret->qualifiers, nullptr);

    Param *param = fn->u.function.params;
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->next, nullptr);
    EXPECT_EQ(param->name, nullptr);

    Type *pair_ty = param->type;
    ASSERT_NE(pair_ty, nullptr);
    EXPECT_EQ(pair_ty->kind, TYPE_STRUCT);
    EXPECT_STREQ(pair_ty->u.struct_t.name, "Pair");
    EXPECT_EQ(pair_ty->qualifiers, nullptr);

    Field *fx = pair_ty->u.struct_t.fields;
    ASSERT_NE(fx, nullptr);
    Field *fy = fx->next;
    ASSERT_NE(fy, nullptr);
    EXPECT_EQ(fy->next, nullptr);

    EXPECT_STREQ(fx->u.member.name, "x");
    EXPECT_EQ(fx->u.member.bitfield, nullptr);
    ASSERT_NE(fx->u.member.type, nullptr);
    EXPECT_EQ(fx->u.member.type->kind, TYPE_INT);
    EXPECT_EQ(fx->u.member.type->qualifiers, nullptr);

    EXPECT_STREQ(fy->u.member.name, "y");
    EXPECT_EQ(fy->u.member.bitfield, nullptr);
    ASSERT_NE(fy->u.member.type, nullptr);
    EXPECT_EQ(fy->u.member.type->kind, TYPE_INT);
    EXPECT_EQ(fy->u.member.type->qualifiers, nullptr);

    free_type(type);
}

//
// Nested Struct with Array Field
//      struct Container {
//          struct Item {
//              int value;
//          } items[10];
//      }
//
TEST_F(ParserTest, NestedStructWithArrayField)
{
    Type *type = TestType("struct Container { struct Item { int value; } items[10]; };");

    //
    // struct Container
    //
    EXPECT_EQ(type->kind, TYPE_STRUCT);
    EXPECT_STREQ(type->u.struct_t.name, "Container");
    EXPECT_EQ(type->qualifiers, nullptr);

    Field *items = type->u.struct_t.fields;
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->next, nullptr);

    //
    // Field items: struct Item[10]
    //
    EXPECT_STREQ(items->u.member.name, "items");
    EXPECT_EQ(items->u.member.bitfield, nullptr);

    ASSERT_NE(items->u.member.type, nullptr);
    EXPECT_EQ(items->u.member.type->kind, TYPE_ARRAY);
    EXPECT_EQ(items->u.member.type->qualifiers, nullptr);
    EXPECT_FALSE(items->u.member.type->u.array.is_static);
    EXPECT_EQ(items->u.member.type->u.array.qualifiers, nullptr);

    Expr *size = items->u.member.type->u.array.size;
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->kind, EXPR_LITERAL);
    EXPECT_EQ(size->u.literal->kind, LITERAL_INT);
    EXPECT_EQ(size->u.literal->u.int_val, 10);

    Type *elem = items->u.member.type->u.array.element;
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->kind, TYPE_STRUCT);
    EXPECT_STREQ(elem->u.struct_t.name, "Item");
    EXPECT_EQ(elem->qualifiers, nullptr);

    //
    // struct Item
    //
    Field *value = elem->u.struct_t.fields;
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->next, nullptr);

    EXPECT_STREQ(value->u.member.name, "value");
    EXPECT_EQ(value->u.member.bitfield, nullptr);
    ASSERT_NE(value->u.member.type, nullptr);
    EXPECT_EQ(value->u.member.type->kind, TYPE_INT);
    EXPECT_EQ(value->u.member.type->qualifiers, nullptr);

    free_type(type);
}

//
// Union with Nested Struct and Anonymous Struct
//      union Variant {
//          struct {
//              int a;
//              int b;
//          };
//          struct Named {
//              float x;
//          } named;
//      }
//
TEST_F(ParserTest, UnionWithNestedStructAndAnonymousStruct)
{
    Type *type =
        TestType("union Variant { struct { int a; int b; }; struct Named { float x; } named; };");

    //
    // union Variant
    //
    EXPECT_EQ(type->kind, TYPE_UNION);
    EXPECT_STREQ(type->u.struct_t.name, "Variant");
    EXPECT_EQ(type->qualifiers, nullptr);

    Field *anon_member = type->u.struct_t.fields;
    ASSERT_NE(anon_member, nullptr);
    Field *named_member = anon_member->next;
    ASSERT_NE(named_member, nullptr);
    EXPECT_EQ(named_member->next, nullptr);

    //
    // Anonymous struct { int a; int b; };
    //
    EXPECT_EQ(anon_member->u.member.name, nullptr);
    EXPECT_EQ(anon_member->u.member.bitfield, nullptr);
    Type *anon_struct = anon_member->u.member.type;
    ASSERT_NE(anon_struct, nullptr);
    EXPECT_EQ(anon_struct->kind, TYPE_STRUCT);
    ExpectAnonTag(anon_struct->u.struct_t.name);
    EXPECT_EQ(anon_struct->qualifiers, nullptr);

    Field *fa = anon_struct->u.struct_t.fields;
    ASSERT_NE(fa, nullptr);
    Field *fb = fa->next;
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->next, nullptr);

    EXPECT_STREQ(fa->u.member.name, "a");
    EXPECT_EQ(fa->u.member.bitfield, nullptr);
    ASSERT_NE(fa->u.member.type, nullptr);
    EXPECT_EQ(fa->u.member.type->kind, TYPE_INT);
    EXPECT_EQ(fa->u.member.type->qualifiers, nullptr);

    EXPECT_STREQ(fb->u.member.name, "b");
    EXPECT_EQ(fb->u.member.bitfield, nullptr);
    ASSERT_NE(fb->u.member.type, nullptr);
    EXPECT_EQ(fb->u.member.type->kind, TYPE_INT);
    EXPECT_EQ(fb->u.member.type->qualifiers, nullptr);

    //
    // struct Named { float x; } named;
    //
    EXPECT_STREQ(named_member->u.member.name, "named");
    EXPECT_EQ(named_member->u.member.bitfield, nullptr);
    Type *named_struct = named_member->u.member.type;
    ASSERT_NE(named_struct, nullptr);
    EXPECT_EQ(named_struct->kind, TYPE_STRUCT);
    EXPECT_STREQ(named_struct->u.struct_t.name, "Named");
    EXPECT_EQ(named_struct->qualifiers, nullptr);

    Field *fx = named_struct->u.struct_t.fields;
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(fx->next, nullptr);

    EXPECT_STREQ(fx->u.member.name, "x");
    EXPECT_EQ(fx->u.member.bitfield, nullptr);
    ASSERT_NE(fx->u.member.type, nullptr);
    EXPECT_EQ(fx->u.member.type->kind, TYPE_FLOAT);
    EXPECT_EQ(fx->u.member.type->qualifiers, nullptr);

    free_type(type);
}

//
// Struct with Function Pointer Member
//      struct sysent {
//          int (*sy_call)(void);
//      }
//
TEST_F(ParserTest, StructWithFunctionPointerMember)
{
    Type *type = TestType("struct sysent { int (*sy_call)(void); };");

    EXPECT_EQ(type->kind, TYPE_STRUCT);
    EXPECT_STREQ(type->u.struct_t.name, "sysent");
    EXPECT_EQ(type->qualifiers, nullptr);

    Field *field = type->u.struct_t.fields;
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->next, nullptr);

    EXPECT_STREQ(field->u.member.name, "sy_call");
    EXPECT_EQ(field->u.member.bitfield, nullptr);

    // sy_call must be a pointer (not a function type directly)
    Type *ptr = field->u.member.type;
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->kind, TYPE_POINTER);
    EXPECT_EQ(ptr->qualifiers, nullptr);
    EXPECT_EQ(ptr->u.pointer.qualifiers, nullptr);

    // The pointer target must be a function
    Type *fn = ptr->u.pointer.target;
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->kind, TYPE_FUNCTION);
    EXPECT_EQ(fn->qualifiers, nullptr);
    EXPECT_FALSE(fn->u.function.variadic);

    // Return type is int
    Type *ret = fn->u.function.return_type;
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->kind, TYPE_INT);

    // Single (void) parameter sentinel
    Param *param = fn->u.function.params;
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->next, nullptr);
    EXPECT_EQ(param->name, nullptr);
    ASSERT_NE(param->type, nullptr);
    EXPECT_EQ(param->type->kind, TYPE_VOID);

    free_type(type);
}

//
// Comma-Separated Declarator List Followed by Another Declaration
//      struct s {
//          char *a, *tok_ptr, *c;
//          int p;
//      }
//
// A struct_declaration yields one field per comma-separated declarator, so the
// interior members must survive when a further declaration follows.
//
TEST_F(ParserTest, StructMultipleDeclarators)
{
    Type *type = TestType("struct s { char *a, *tok_ptr, *c; int p; };");

    //
    // Check struct s
    //
    EXPECT_EQ(type->kind, TYPE_STRUCT);
    EXPECT_STREQ(type->u.struct_t.name, "s");

    //
    // All four members must be present, in declaration order
    //
    Field *a = type->u.struct_t.fields;
    ASSERT_NE(a, nullptr);
    Field *tok_ptr = a->next;
    ASSERT_NE(tok_ptr, nullptr);
    Field *c = tok_ptr->next;
    ASSERT_NE(c, nullptr);
    Field *p = c->next;
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->next, nullptr);

    //
    // Check the three char* members
    //
    const char *names[] = { "a", "tok_ptr", "c" };
    Field *ptrs[]       = { a, tok_ptr, c };
    for (int i = 0; i < 3; i++) {
        EXPECT_STREQ(ptrs[i]->u.member.name, names[i]);
        EXPECT_EQ(ptrs[i]->u.member.bitfield, nullptr);
        ASSERT_NE(ptrs[i]->u.member.type, nullptr);
        EXPECT_EQ(ptrs[i]->u.member.type->kind, TYPE_POINTER);

        Type *target = ptrs[i]->u.member.type->u.pointer.target;
        ASSERT_NE(target, nullptr);
        EXPECT_EQ(target->kind, TYPE_CHAR);
    }

    //
    // Check field p
    //
    EXPECT_STREQ(p->u.member.name, "p");
    EXPECT_EQ(p->u.member.bitfield, nullptr);
    ASSERT_NE(p->u.member.type, nullptr);
    EXPECT_EQ(p->u.member.type->kind, TYPE_INT);

    free_type(type);
}

//
// A tagless struct/union definition shared by a comma-separated declarator list is ONE type,
// however many declarators it feeds.  The parser clones the base type per declarator, so the
// synthetic tag has to be minted before the clone; minting it later, per cloned node, gave each
// declarator its own tag and made them mutually incompatible (`a = b` failed to typecheck).
//
TEST_F(ParserTest, AnonStructSharedByDeclaratorList)
{
    Declaration *decl = GetDeclaration("struct { int x; } a, b;");

    InitDeclarator *a = decl->u.var.declarators;
    ASSERT_NE(a, nullptr);
    InitDeclarator *b = a->next;
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(a->name, "a");
    EXPECT_STREQ(b->name, "b");

    EXPECT_EQ(a->type->kind, TYPE_STRUCT);
    EXPECT_EQ(b->type->kind, TYPE_STRUCT);
    ExpectAnonTag(a->type->u.struct_t.name);
    EXPECT_STREQ(a->type->u.struct_t.name, b->type->u.struct_t.name);
}

//
// Same for a union, and through a pointer declarator: the tag rides the base type, so `*p`
// still names the type `a` has.
//
TEST_F(ParserTest, AnonUnionSharedByDeclaratorList)
{
    Declaration *decl = GetDeclaration("union { int x; int y; } a, *p;");

    InitDeclarator *a = decl->u.var.declarators;
    ASSERT_NE(a, nullptr);
    InitDeclarator *p = a->next;
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(a->type->kind, TYPE_UNION);
    ASSERT_EQ(p->type->kind, TYPE_POINTER);
    Type *target = p->type->u.pointer.target;
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->kind, TYPE_UNION);
    ExpectAnonTag(a->type->u.struct_t.name);
    EXPECT_STREQ(a->type->u.struct_t.name, target->u.struct_t.name);
}

//
// Struct *members* take the same path -- parse_struct_declaration clones the member base type
// per declarator -- so `p` and `q` below must also share one tag, or `w.p = w.q` fails.
//
TEST_F(ParserTest, AnonStructMemberSharedByDeclaratorList)
{
    Declaration *decl = GetDeclaration("struct W { struct { int x; } p, q; } w;");

    Type *outer = decl->u.var.declarators->type;
    ASSERT_NE(outer, nullptr);
    EXPECT_STREQ(outer->u.struct_t.name, "W");

    Field *p = outer->u.struct_t.fields;
    ASSERT_NE(p, nullptr);
    Field *q = p->next;
    ASSERT_NE(q, nullptr);
    EXPECT_STREQ(p->u.member.name, "p");
    EXPECT_STREQ(q->u.member.name, "q");

    ASSERT_EQ(p->u.member.type->kind, TYPE_STRUCT);
    ASSERT_EQ(q->u.member.type->kind, TYPE_STRUCT);
    ExpectAnonTag(p->u.member.type->u.struct_t.name);
    EXPECT_STREQ(p->u.member.type->u.struct_t.name, q->u.member.type->u.struct_t.name);
}

//
// Two *separate* tagless definitions stay distinct types (C11 6.7.2.3p5), so they must get
// different tags -- the counter is per definition, not per source shape.
//
TEST_F(ParserTest, DistinctAnonStructDefsGetDistinctTags)
{
    ExternalDecl *first = GetExternalDecl("struct { int x; } a; struct { int x; } b;");

    ASSERT_EQ(first->kind, EXTERNAL_DECL_DECLARATION);
    ExternalDecl *second = first->next;
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->kind, EXTERNAL_DECL_DECLARATION);

    const char *ta = first->u.declaration->u.var.declarators->type->u.struct_t.name;
    const char *tb = second->u.declaration->u.var.declarators->type->u.struct_t.name;
    ExpectAnonTag(ta);
    ExpectAnonTag(tb);
    EXPECT_STRNE(ta, tb);
}

//
// _Alignas on members: each declarator gets its own copy; a clone keeps it, and a
// different one compares unequal.
//      struct A { char c; _Alignas(16) char d, e; _Alignas(long) int f; };
//
TEST_F(ParserTest, StructMemberAlignas)
{
    Type *type = TestType("struct A { char c; _Alignas(16) char d, e; _Alignas(long) int f; };");
    Field *c   = type->u.struct_t.fields;
    Field *d = c->next, *e = d->next, *f = e->next;
    EXPECT_EQ(c->u.member.align_spec, nullptr);
    ASSERT_NE(d->u.member.align_spec, nullptr);
    ASSERT_NE(e->u.member.align_spec, nullptr);
    EXPECT_NE(d->u.member.align_spec, e->u.member.align_spec);
    EXPECT_EQ(d->u.member.align_spec->kind, ALIGN_SPEC_EXPR);
    EXPECT_EQ(d->u.member.align_spec->u.expr->u.literal->u.int_val, 16);
    ASSERT_NE(f->u.member.align_spec, nullptr);
    EXPECT_EQ(f->u.member.align_spec->kind, ALIGN_SPEC_TYPE);
    EXPECT_EQ(f->u.member.align_spec->u.type->kind, TYPE_LONG);

    Type *copy = clone_type(type, __func__, __FILE__, __LINE__);
    EXPECT_TRUE(compare_type(type, copy));
    d->u.member.align_spec->u.expr->u.literal->u.int_val = 32;
    EXPECT_FALSE(compare_type(type, copy));
    free_type(copy);
    free_type(type);
}
