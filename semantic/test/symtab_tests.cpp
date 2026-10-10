#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "internal.h"
#include "semantic.h"
#include "symtab.h"
#include "xalloc.h"

// Helper functions for type creation
Type *new_function_type(Type *return_type, Param *params)
{
    Type *func                   = new_type(TYPE_FUNCTION, __func__, __FILE__, __LINE__);
    func->u.function.return_type = return_type;
    func->u.function.params      = params;
    return func;
}

// Helper to create a parameter list
Param *create_param(const char *name, Type *type)
{
    Param *p = new_param();
    p->name  = xstrdup(name);
    p->type  = type;
    return p;
}

// Test fixture for symtab tests
class SymtabTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Enable to debug allocations:
        // xalloc_debug = 1;

        symtab_init();
    }

    void TearDown() override
    {
        symtab_destroy();
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }
};

// Test symtab_init
TEST_F(SymtabTest, InitEmptyTable)
{
    ASSERT_EQ(symtab_get_opt("x"), nullptr);
}

// Test symtab_destroy
TEST_F(SymtabTest, DestroyEmptyTable)
{
    symtab_destroy();
    symtab_init(); // Re-init to ensure no corruption
}

// Test symtab_add_automatic_var
TEST_F(SymtabTest, AddAutomaticVar1)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    symtab_add_automatic_var_type("x", int_type, 0);

    Symbol *sym = symtab_get("x");
    ASSERT_STREQ(sym->name, "x");
    ASSERT_TRUE(compare_type(sym->type, int_type));
    ASSERT_EQ(sym->kind, SYM_LOCAL);

    free_type(int_type);
}

// Test symtab_add_automatic_var overwrite
TEST_F(SymtabTest, AddAutomaticVarOverwrite)
{
    Type *int_type  = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    Type *char_type = new_type(TYPE_CHAR, __func__, __FILE__, __LINE__);

    symtab_add_automatic_var_type("x", int_type, 0);
    symtab_add_automatic_var_type("x", char_type, 0);

    Symbol *sym = symtab_get("x");
    ASSERT_STREQ(sym->name, "x");
    ASSERT_TRUE(compare_type(sym->type, char_type));
    ASSERT_EQ(sym->kind, SYM_LOCAL);

    free_type(int_type);
    free_type(char_type);
}

// Test symtab_add_static_var
TEST_F(SymtabTest, AddStaticVarWithInitializer)
{
    Type *int_type       = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    Tac_StaticInit *init = tac_new_static_init(TAC_STATIC_INIT_I32);
    init->u.int_val      = 42;

    symtab_add_static_var("x", int_type, true, INIT_INITIALIZED, init);

    Symbol *sym = symtab_get("x");
    ASSERT_STREQ(sym->name, "x");
    ASSERT_TRUE(compare_type(sym->type, int_type));
    ASSERT_EQ(sym->kind, SYM_STATIC);
    ASSERT_TRUE(sym->u.static_var.global);
    ASSERT_EQ(sym->u.static_var.init_kind, INIT_INITIALIZED);

    // Check initializer
    Tac_StaticInit *check = sym->u.static_var.init_list;
    ASSERT_NE(check, nullptr);
    EXPECT_EQ(check->kind, TAC_STATIC_INIT_I32);
    EXPECT_EQ(check->u.int_val, 42);
    EXPECT_EQ(check->next, nullptr);

    free_type(int_type);
}

// Test symtab_add_static_var with no initializer
TEST_F(SymtabTest, AddStaticVarNoInitializer)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);

    symtab_add_static_var("y", int_type, false, INIT_TENTATIVE, NULL);

    Symbol *sym = symtab_get("y");
    ASSERT_STREQ(sym->name, "y");
    ASSERT_TRUE(compare_type(sym->type, int_type));
    ASSERT_EQ(sym->kind, SYM_STATIC);
    ASSERT_FALSE(sym->u.static_var.global);
    ASSERT_EQ(sym->u.static_var.init_kind, INIT_TENTATIVE);
    ASSERT_EQ(sym->u.static_var.init_list, nullptr);

    free_type(int_type);
}

// Test symtab_add_fun
TEST_F(SymtabTest, AddFunction)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    Param *param   = create_param("a", clone_type(int_type, __func__, __FILE__, __LINE__));
    Type *fun_type = new_function_type(clone_type(int_type, __func__, __FILE__, __LINE__), param);

    symtab_add_fun("f", fun_type, true, true, false);

    Symbol *sym = symtab_get("f");
    ASSERT_STREQ(sym->name, "f");
    ASSERT_TRUE(compare_type(sym->type, fun_type));
    ASSERT_EQ(sym->kind, SYM_FUNC);
    ASSERT_TRUE(sym->u.func.global);
    ASSERT_TRUE(sym->u.func.defined);
    ASSERT_FALSE(sym->u.func.noret);

    free_type(int_type);
    free_type(fun_type);
}

// Test symtab_add_string
TEST_F(SymtabTest, AddStringLiteral)
{
    const char *str = "hello";
    char *str_id    = symtab_add_string(str, strlen(str));

    // Check symbol
    Symbol *sym = symtab_get(str_id);
    ASSERT_EQ(sym->kind, SYM_CONST);
    ASSERT_STREQ(sym->name, str_id);

    // Check symbol type
    ASSERT_NE(sym->type, nullptr);
    EXPECT_EQ(sym->type->kind, TYPE_ARRAY);
    ASSERT_NE(sym->type->u.array.element, nullptr);
    EXPECT_EQ(sym->type->qualifiers, nullptr);
    EXPECT_EQ(sym->type->u.array.element->kind, TYPE_CHAR);
    EXPECT_EQ(sym->type->u.array.element->qualifiers, nullptr);
    ASSERT_NE(sym->type->u.array.size, nullptr);
    EXPECT_EQ(sym->type->u.array.size->kind, EXPR_LITERAL);
    EXPECT_EQ(sym->type->u.array.size->u.literal->kind, LITERAL_INT);
    EXPECT_EQ(sym->type->u.array.size->u.literal->u.int_val, strlen(str) + 1);
    EXPECT_EQ(sym->type->u.array.qualifiers, nullptr);

    // Check initializer
    Tac_StaticInit *init = sym->u.const_init;
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->kind, TAC_STATIC_INIT_STRING);
    EXPECT_TRUE(init->u.string.null_terminated);
    EXPECT_EQ(init->u.string.len, strlen(str));
    EXPECT_STREQ(init->u.string.val, str);
    EXPECT_EQ(init->next, nullptr);

    xfree(str_id);
}

// Test symtab_get
TEST_F(SymtabTest, GetNonExistentSymbol)
{
    ASSERT_EXIT(symtab_get("x"), ::testing::ExitedWithCode(1), "use of undeclared identifier 'x'");
}

// Test symtab_get_opt
TEST_F(SymtabTest, GetOptExistentAndNonExistent)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    symtab_add_automatic_var_type("x", int_type, 0);

    Symbol *sym = symtab_get_opt("x");
    ASSERT_NE(sym, nullptr);
    ASSERT_STREQ(sym->name, "x");
    ASSERT_TRUE(compare_type(sym->type, int_type));

    ASSERT_EQ(symtab_get_opt("y"), nullptr);

    free_type(int_type);
}

// Test symtab_is_global
TEST_F(SymtabTest, IsGlobal)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);

    symtab_add_static_var("x", int_type, true, INIT_NONE, NULL);
    symtab_add_static_var("y", int_type, false, INIT_NONE, NULL);
    symtab_add_automatic_var_type("z", int_type, 0);

    ASSERT_TRUE(symtab_is_global("x"));
    ASSERT_FALSE(symtab_is_global("y"));
    ASSERT_FALSE(symtab_is_global("z"));

    free_type(int_type);
}

// Test that symtab_purge frees Symbol memory for block-scope entries.
// TearDown asserts xtotal_allocated_size() == 0, so any leaked Symbol
// would cause the test to fail even without an explicit EXPECT here.
TEST_F(SymtabTest, PurgeFreesLocalSymbols)
{
    Type *int_type = new_type(TYPE_INT, __func__, __FILE__, __LINE__);
    symtab_add_automatic_var_type("n", int_type, 1);
    free_type(int_type);

    symtab_purge(0); // purges entries with level > 0

    EXPECT_EQ(symtab_get_opt("n"), nullptr);
}

// Test symtab_add_string unique IDs
TEST_F(SymtabTest, AddStringUniqueIDs)
{
    char *id1 = symtab_add_string("str1", 4);
    char *id2 = symtab_add_string("str2", 4);

    ASSERT_STRNE(id1, id2);
    ASSERT_TRUE(symtab_get_opt(id1) != nullptr);
    ASSERT_TRUE(symtab_get_opt(id2) != nullptr);
    xfree(id1);
    xfree(id2);
}
