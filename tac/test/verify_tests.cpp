#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "tac.h"
#include "xalloc.h"

// tac_verify on hand-built TAC: each rule accepts a well-typed function and reports the
// one problem planted in an ill-typed one.  Sizes are LP64.

class TacVerifyTest : public ::testing::Test {
protected:
    Tac_Layout layout{};
    Tac_TopLevel *fn{};

    void SetUp() override
    {
        const int sizes[] = { 1, 1, 2, 4, 8, 8, 2, 4, 8, 8, 4, 8, 16 };
        for (int k = 0; k < TAC_TYPE_VOID; k++)
            layout.scalar[k] = sizes[k];
        layout.pointer = 8;

        fn                  = tac_new_toplevel(TAC_TOPLEVEL_FUNCTION);
        fn->u.function.name = xstrdup("f");
    }

    void TearDown() override
    {
        tac_free_toplevel(fn);
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    static Tac_Type *Type(Tac_TypeKind k) { return tac_new_type(k); }

    static Tac_Type *Ptr(Tac_TypeKind k)
    {
        Tac_Type *p              = tac_new_type(TAC_TYPE_POINTER);
        p->u.pointer.target_type = tac_new_type(k);
        return p;
    }

    void Local(const char *name, Tac_Type *type)
    {
        Tac_Param *p          = tac_new_param();
        p->name               = xstrdup(name);
        p->type               = type;
        p->next               = fn->u.function.locals;
        fn->u.function.locals = p;
    }

    static Tac_Val *Var(const char *name)
    {
        Tac_Val *v    = tac_new_val(TAC_VAL_VAR);
        v->u.var_name = xstrdup(name);
        return v;
    }

    static Tac_Val *Const(Tac_ConstKind k)
    {
        Tac_Val *v    = tac_new_val(TAC_VAL_CONSTANT);
        v->u.constant = tac_new_const(k);
        return v;
    }

    void Emit(Tac_Instruction *in)
    {
        Tac_Instruction **pp = &fn->u.function.body;
        while (*pp)
            pp = &(*pp)->next;
        *pp = in;
    }

    void Unary2(Tac_InstructionKind k, Tac_Val *src, Tac_Val *dst)
    {
        Tac_Instruction *in = tac_new_instruction(k);
        in->u.copy.src      = src;
        in->u.copy.dst      = dst;
        Emit(in);
    }

    void Binary(Tac_BinaryOperator op, Tac_Val *a, Tac_Val *b, Tac_Val *d)
    {
        Tac_Instruction *in = tac_new_instruction(TAC_INSTRUCTION_BINARY);
        in->u.binary.op     = op;
        in->u.binary.src1   = a;
        in->u.binary.src2   = b;
        in->u.binary.dst    = d;
        Emit(in);
    }

    // The problems found, one per line.
    std::string Verify()
    {
        FILE *f = tmpfile();
        if (!f)
            return "tmpfile failed";
        tac_verify_function(fn, &layout, nullptr, nullptr, f);
        long len = ftell(f);
        rewind(f);
        std::string out(static_cast<size_t>(len), '\0');
        if (len) {
            EXPECT_TRUE(fread(&out[0], 1, static_cast<size_t>(len), f));
        }
        fclose(f);
        return out;
    }
};

TEST_F(TacVerifyTest, WellTyped)
{
    Local("%i", Type(TAC_TYPE_INT));
    Local("%l", Type(TAC_TYPE_LONG));
    Local("%p", Ptr(TAC_TYPE_INT));
    Local("%d", Type(TAC_TYPE_DOUBLE));
    Unary2(TAC_INSTRUCTION_SIGN_EXTEND, Var("%i"), Var("%l"));
    Binary(TAC_BINARY_ADD, Var("%l"), Const(TAC_CONST_INT), Var("%l"));
    Unary2(TAC_INSTRUCTION_COPY, Var("%l"), Var("%p")); // integer <-> pointer, same size
    Unary2(TAC_INSTRUCTION_INT_TO_DOUBLE, Var("%i"), Var("%d"));
    Binary(TAC_BINARY_LESS_THAN_DOUBLE, Var("%d"), Const(TAC_CONST_DOUBLE), Var("%i"));
    Unary2(TAC_INSTRUCTION_CHAR_PTR_TO_PTR, Const(TAC_CONST_INT), Var("%p")); // a null
    EXPECT_EQ(Verify(), "");
}

TEST_F(TacVerifyTest, UntypedName)
{
    Local("%i", Type(TAC_TYPE_INT));
    Unary2(TAC_INSTRUCTION_COPY, Var("%i"), Var("%x"));
    Unary2(TAC_INSTRUCTION_COPY, Var("%i"), Var("g")); // no resolver: globals unknown too
    EXPECT_EQ(Verify(), "verify: f: #1 copy: %x has no type\n"
                        "verify: f: #2 copy: g has no type\n");
}

TEST_F(TacVerifyTest, WidthMismatch)
{
    Local("%i", Type(TAC_TYPE_INT));
    Local("%l", Type(TAC_TYPE_LONG));
    Binary(TAC_BINARY_ADD, Var("%i"), Var("%l"), Var("%l"));
    Unary2(TAC_INSTRUCTION_SIGN_EXTEND, Var("%l"), Var("%i"));
    EXPECT_EQ(Verify(), "verify: f: #1 binary: src1 has 4 bytes but dst has 8\n"
                        "verify: f: #2 sign_extend: extends 8 bytes to 4\n");
}

TEST_F(TacVerifyTest, FloatingKindMismatch)
{
    Local("%d", Type(TAC_TYPE_DOUBLE));
    Local("%f", Type(TAC_TYPE_FLOAT));
    Local("%i", Type(TAC_TYPE_INT));
    Binary(TAC_BINARY_ADD_DOUBLE, Var("%d"), Var("%f"), Var("%d"));
    Binary(TAC_BINARY_NOT_EQUAL, Var("%d"), Const(TAC_CONST_INT), Var("%i"));
    Unary2(TAC_INSTRUCTION_FLOAT_TO_DOUBLE, Var("%d"), Var("%d"));
    EXPECT_EQ(Verify(), "verify: f: #1 binary: src2 is float but dst is double\n"
                        "verify: f: #2 binary: src1 is double but src2 is integer\n"
                        "verify: f: #3 float_to_double: src is double\n");
}

TEST_F(TacVerifyTest, MemoryThroughNonPointer)
{
    Local("%i", Type(TAC_TYPE_INT));
    Local("%l", Type(TAC_TYPE_LONG));
    Unary2(TAC_INSTRUCTION_LOAD, Var("%l"), Var("%i"));
    EXPECT_EQ(Verify(), "verify: f: #1 load: src_ptr is integer\n");
}

TEST_F(TacVerifyTest, OffsetOutsideAggregate)
{
    Tac_Type *s              = Type(TAC_TYPE_STRUCTURE);
    s->u.structure.tag       = xstrdup("S");
    s->u.structure.size      = 12;
    s->u.structure.alignment = 4;
    Local("%s", s);
    Local("%l", Type(TAC_TYPE_ULONG));
    Tac_Instruction *in            = tac_new_instruction(TAC_INSTRUCTION_COPY_FROM_OFFSET);
    in->u.copy_from_offset.src     = xstrdup("%s");
    in->u.copy_from_offset.offset  = 8;
    in->u.copy_from_offset.dst     = Var("%l");
    Emit(in);
    EXPECT_EQ(Verify(), "verify: f: #1 copy_from_offset: 8 bytes at offset 8 of %s, which has 12\n");
}

// tac_verify_program resolves globals against the chain: definitions and externs.
TEST_F(TacVerifyTest, ProgramResolvesGlobals)
{
    Local("%i", Type(TAC_TYPE_INT));
    Unary2(TAC_INSTRUCTION_COPY, Var("g"), Var("%i"));
    Unary2(TAC_INSTRUCTION_COPY, Var("e"), Var("%i"));
    Tac_TopLevel *g           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    g->u.static_variable.name = xstrdup("g");
    g->u.static_variable.type = Type(TAC_TYPE_INT);
    Tac_TopLevel *e           = tac_new_toplevel(TAC_TOPLEVEL_EXTERN);
    e->u.extern_.name         = xstrdup("e");
    e->u.extern_.type         = Type(TAC_TYPE_LONG);
    fn->next                  = g;
    g->next                   = e;
    EXPECT_EQ(tac_verify_program(fn, &layout, nullptr), 1); // e is 8 bytes, %i 4
}
