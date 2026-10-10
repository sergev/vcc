#include <gtest/gtest.h>

#include <string>

#include "ast.h"
#include "xalloc.h"

class TypeToC : public ::testing::Test {
protected:
    void TearDown() override
    {
        xfree_all();
    }

    static Type *T(TypeKind kind)
    {
        return new_type(kind, __func__, __FILE__, __LINE__);
    }

    static Type *Ptr(Type *target, TypeQualifier *quals = nullptr)
    {
        Type *t                 = T(TYPE_POINTER);
        t->u.pointer.target     = target;
        t->u.pointer.qualifiers = quals;
        return t;
    }

    static Type *Array(Type *element, long n)
    {
        Type *t            = T(TYPE_ARRAY);
        t->u.array.element = element;
        if (n >= 0) {
            Expr *size                    = new_expression(EXPR_LITERAL);
            size->u.literal               = new_literal(LITERAL_INT);
            size->u.literal->u.int_val    = n;
            t->u.array.size               = size;
        }
        return t;
    }

    static Type *Func(Type *ret, std::initializer_list<Type *> params, bool variadic = false)
    {
        Type *t                  = T(TYPE_FUNCTION);
        t->u.function.return_type = ret;
        t->u.function.variadic    = variadic;
        Param **tail              = &t->u.function.params;
        for (Type *p : params) {
            *tail         = new_param();
            (*tail)->type = p;
            tail          = &(*tail)->next;
        }
        return t;
    }

    static Type *Tagged(TypeKind kind, const char *name)
    {
        Type *t = T(kind);
        if (kind == TYPE_ENUM)
            t->u.enum_t.name = xstrdup(name);
        else
            t->u.struct_t.name = xstrdup(name);
        return t;
    }

    static TypeQualifier *Const()
    {
        return new_type_qualifier(TYPE_QUALIFIER_CONST);
    }

    static std::string C(const Type *t)
    {
        char *s = type_to_c(t);
        std::string result(s);
        xfree(s);
        return result;
    }
};

TEST_F(TypeToC, Scalars)
{
    EXPECT_EQ(C(T(TYPE_INT)), "int");
    EXPECT_EQ(C(T(TYPE_ULONG)), "unsigned long");
    EXPECT_EQ(C(T(TYPE_LONG_DOUBLE)), "long double");
}

TEST_F(TypeToC, Qualified)
{
    Type *c       = T(TYPE_CHAR);
    c->qualifiers = Const();
    EXPECT_EQ(C(c), "const char");
    EXPECT_EQ(C(Ptr(c)), "const char *");
    EXPECT_EQ(C(Ptr(T(TYPE_CHAR), Const())), "char *const");
}

TEST_F(TypeToC, Declarators)
{
    EXPECT_EQ(C(Ptr(Ptr(T(TYPE_INT)))), "int **");
    EXPECT_EQ(C(Array(T(TYPE_INT), 3)), "int [3]");
    EXPECT_EQ(C(Array(T(TYPE_INT), -1)), "int []");
    EXPECT_EQ(C(Ptr(Array(T(TYPE_INT), 4))), "int (*)[4]");
    EXPECT_EQ(C(Array(Ptr(T(TYPE_INT)), 2)), "int *[2]");
    EXPECT_EQ(C(Func(T(TYPE_INT), { T(TYPE_INT), Ptr(T(TYPE_CHAR)) })), "int (int, char *)");
    EXPECT_EQ(C(Ptr(Func(T(TYPE_VOID), { T(TYPE_INT) }, true))), "void (*)(int, ...)");
    // A function returning a pointer to a function: int (*(void))(int).
    EXPECT_EQ(C(Func(Ptr(Func(T(TYPE_INT), { T(TYPE_INT) })), { T(TYPE_VOID) })),
              "int (*(void))(int)");
}

TEST_F(TypeToC, Tags)
{
    EXPECT_EQ(C(Tagged(TYPE_STRUCT, "S")), "struct S");
    EXPECT_EQ(C(Ptr(Tagged(TYPE_UNION, "U"))), "union U *");
    EXPECT_EQ(C(Tagged(TYPE_ENUM, "E")), "enum E");
    EXPECT_EQ(C(Tagged(TYPE_STRUCT, "__anon_7")), "struct <anonymous>");
}
