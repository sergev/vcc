#pragma once
#include <gtest/gtest.h>

#include <initializer_list>

extern "C" {
#include "cfg.h"
#include "optimize.h"
#include "xalloc.h"

// Exposed for direct unit-testing of the folding pass.
Tac_Instruction *constant_fold(Tac_Instruction *body);
void eliminate_unreachable(OptCfg *cfg);

// Process-global optimizer trace switch (defined in optimize.c).
// Set to 1 in a test to enable stdout trace for that test's optimizer calls.
extern int optimize_debug;
}

// RAII guard: enables optimizer tracing for the lifetime of the scope.
// Usage:  OptDebugScope dbg;   // trace on for this test
// The saved value is restored in the destructor, so the setting does not leak
// to sibling tests even if the test fails or exits early.
struct OptDebugScope {
    int saved;
    explicit OptDebugScope(int on = 1) : saved(optimize_debug) { optimize_debug = on; }
    ~OptDebugScope() { optimize_debug = saved; }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class OptimizerTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        xfree_all();
        EXPECT_EQ(xtotal_allocated_size(), 0);
    }

    static std::string capture_instructions(const Tac_Instruction *body)
    {
        FILE *f = tmpfile();
        EXPECT_NE(f, nullptr);
        tac_export_yaml_instruction_list(f, body, 0);
        long len = ftell(f);
        rewind(f);
        std::string yaml(static_cast<size_t>(len), '\0');
        EXPECT_TRUE(fread(&yaml[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return yaml;
    }

    // Link a list of instructions in order and return the head.
    static Tac_Instruction *chain(std::initializer_list<Tac_Instruction *> instrs)
    {
        Tac_Instruction *head = nullptr, *prev = nullptr;
        for (auto *i : instrs) {
            if (!head)
                head = i;
            if (prev)
                prev->next = i;
            prev = i;
        }
        return head;
    }

    // --- Value makers ---

    static Tac_Val *make_const_int(int v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_INT);
        c->u.int_val    = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_float(double v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_FLOAT);
        c->u.float_val  = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_double(double v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_DOUBLE);
        c->u.double_val = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_long_double(long double v)
    {
        Tac_Const *c         = tac_new_const(TAC_CONST_LONG_DOUBLE);
        c->u.long_double_val = f128_from_double((double)v);
        Tac_Val *val         = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant      = c;
        return val;
    }

    static Tac_Val *make_const_char(int v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_SCHAR);
        c->u.char_val   = (int)(int8_t)v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_uchar(unsigned char v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_UCHAR);
        c->u.uchar_val  = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_long(long v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_LONG);
        c->u.long_val   = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_long_long(long long v)
    {
        Tac_Const *c       = tac_new_const(TAC_CONST_LONG_LONG);
        c->u.long_long_val = v;
        Tac_Val *val       = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant    = c;
        return val;
    }

    static Tac_Val *make_const_uint(unsigned v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_UINT);
        c->u.uint_val   = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_ulong(unsigned long v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_ULONG);
        c->u.ulong_val  = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_const_ulong_long(unsigned long long v)
    {
        Tac_Const *c        = tac_new_const(TAC_CONST_ULONG_LONG);
        c->u.ulong_long_val = v;
        Tac_Val *val        = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant     = c;
        return val;
    }

    static Tac_Val *make_var(const char *name)
    {
        Tac_Val *val    = tac_new_val(TAC_VAL_VAR);
        val->u.var_name = xstrdup(name);
        return val;
    }

    // --- Instruction makers ---

    static Tac_Instruction *make_unary(Tac_UnaryOperator op, Tac_Val *src, Tac_Val *dst)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_UNARY);
        i->u.unary.op      = op;
        i->u.unary.src     = src;
        i->u.unary.dst     = dst;
        return i;
    }

    static Tac_Instruction *make_binary(Tac_BinaryOperator op, Tac_Val *src1, Tac_Val *src2,
                                        Tac_Val *dst)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_BINARY);
        i->u.binary.op     = op;
        i->u.binary.src1   = src1;
        i->u.binary.src2   = src2;
        i->u.binary.dst    = dst;
        return i;
    }

    static Tac_Instruction *make_return(Tac_Val *src)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        i->u.return_.src   = src;
        return i;
    }

    // All 21 conversion instructions share the same {src, dst} layout; sign_extend is the proxy.
    static Tac_Instruction *make_conversion(Tac_InstructionKind kind, Tac_Val *src, Tac_Val *dst)
    {
        Tac_Instruction *i   = tac_new_instruction(kind);
        i->u.sign_extend.src = src;
        i->u.sign_extend.dst = dst;
        return i;
    }

    static Tac_Instruction *make_jump_if_zero(Tac_Val *cond, const char *target)
    {
        Tac_Instruction *i          = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_ZERO);
        i->u.jump_if_zero.condition = cond;
        i->u.jump_if_zero.target    = xstrdup(target);
        return i;
    }

    static Tac_Instruction *make_jump_if_not_zero(Tac_Val *cond, const char *target)
    {
        Tac_Instruction *i              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
        i->u.jump_if_not_zero.condition = cond;
        i->u.jump_if_not_zero.target    = xstrdup(target);
        return i;
    }

    static Tac_Instruction *make_label(const char *name)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_LABEL);
        i->u.label.name    = xstrdup(name);
        return i;
    }

    static Tac_Instruction *make_jump(const char *target)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_JUMP);
        i->u.jump.target   = xstrdup(target);
        return i;
    }

    static Tac_Instruction *make_copy(Tac_Val *src, Tac_Val *dst)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_COPY);
        i->u.copy.src      = src;
        i->u.copy.dst      = dst;
        return i;
    }

    static Tac_Instruction *make_fun_call(const char *name)
    {
        Tac_Instruction *i     = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
        i->u.fun_call.fun_name = xstrdup(name);
        return i;
    }

    static Tac_Instruction *make_fun_call_with_arg(const char *name, Tac_Val *arg)
    {
        Tac_Instruction *i     = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
        i->u.fun_call.fun_name = xstrdup(name);
        i->u.fun_call.args     = arg;
        return i;
    }

    static Tac_Instruction *make_get_address(Tac_Val *src, Tac_Val *dst)
    {
        Tac_Instruction *i   = tac_new_instruction(TAC_INSTRUCTION_GET_ADDRESS);
        i->u.get_address.src = src;
        i->u.get_address.dst = dst;
        return i;
    }

    static Tac_Instruction *make_store(Tac_Val *src, Tac_Val *dst_ptr)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_STORE);
        i->u.store.src     = src;
        i->u.store.dst_ptr = dst_ptr;
        return i;
    }

    static Tac_Instruction *make_load(Tac_Val *src_ptr, Tac_Val *dst)
    {
        Tac_Instruction *i = tac_new_instruction(TAC_INSTRUCTION_LOAD);
        i->u.load.src_ptr  = src_ptr;
        i->u.load.dst      = dst;
        return i;
    }

    // Mark an instruction as a volatile memory access (in place) and return it,
    // so any maker can be wrapped: as_volatile(make_load(...)).
    static Tac_Instruction *as_volatile(Tac_Instruction *i)
    {
        i->is_volatile = true;
        return i;
    }

    static Tac_Instruction *make_copy_to_offset(Tac_Val *src, const char *dst_name, int offset)
    {
        Tac_Instruction *i         = tac_new_instruction(TAC_INSTRUCTION_COPY_TO_OFFSET);
        i->u.copy_to_offset.src    = src;
        i->u.copy_to_offset.dst    = xstrdup(dst_name);
        i->u.copy_to_offset.offset = offset;
        return i;
    }

    // A function toplevel carrying an explicit set of automatic-local names. The
    // optimizer treats any non-temporary name that is not a param/local as an
    // observable global; pass {} to mark every named var (e.g. "g") observable.
    static Tac_TopLevel *make_fn_tl(std::initializer_list<const char *> locals)
    {
        Tac_TopLevel *tl    = tac_new_toplevel(TAC_TOPLEVEL_FUNCTION);
        tl->u.function.name = xstrdup("fn");
        Tac_Param **tail    = &tl->u.function.locals;
        for (const char *name : locals) {
            Tac_Param *p = tac_new_param();
            p->name      = xstrdup(name);
            p->next      = nullptr;
            *tail        = p;
            tail         = &p->next;
        }
        return tl;
    }

    // --- Folded-constant assertion helpers ---
    // Each checks that `body` is a COPY of a constant with the expected kind and value.

    static void AssertFoldedInt(Tac_Instruction *body, int expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->kind, TAC_VAL_CONSTANT);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_INT);
        EXPECT_EQ(body->u.copy.src->u.constant->u.int_val, expected);
    }

    static void AssertFoldedLong(Tac_Instruction *body, long expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_LONG);
        EXPECT_EQ(body->u.copy.src->u.constant->u.long_val, expected);
    }

    static void AssertFoldedLongLong(Tac_Instruction *body, long long expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_LONG_LONG);
        EXPECT_EQ(body->u.copy.src->u.constant->u.long_long_val, expected);
    }

    static void AssertFoldedUInt(Tac_Instruction *body, unsigned expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UINT);
        EXPECT_EQ(body->u.copy.src->u.constant->u.uint_val, expected);
    }

    static void AssertFoldedULong(Tac_Instruction *body, unsigned long expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_ULONG);
        EXPECT_EQ(body->u.copy.src->u.constant->u.ulong_val, expected);
    }

    static void AssertFoldedULongLong(Tac_Instruction *body, unsigned long long expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_ULONG_LONG);
        EXPECT_EQ(body->u.copy.src->u.constant->u.ulong_long_val, expected);
    }

    static void AssertFoldedChar(Tac_Instruction *body, int expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_SCHAR);
        EXPECT_EQ(body->u.copy.src->u.constant->u.char_val, expected);
    }

    static void AssertFoldedUChar(Tac_Instruction *body, unsigned char expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_UCHAR);
        EXPECT_EQ(body->u.copy.src->u.constant->u.uchar_val, expected);
    }

    static void AssertFoldedFloat(Tac_Instruction *body, double expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_FLOAT);
        EXPECT_DOUBLE_EQ(body->u.copy.src->u.constant->u.float_val, expected);
    }

    static void AssertFoldedDouble(Tac_Instruction *body, double expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_DOUBLE);
        EXPECT_DOUBLE_EQ(body->u.copy.src->u.constant->u.double_val, expected);
    }

    static void AssertFoldedLongDouble(Tac_Instruction *body, long double expected)
    {
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->kind, TAC_INSTRUCTION_COPY);
        ASSERT_NE(body->u.copy.src, nullptr);
        EXPECT_EQ(body->u.copy.src->u.constant->kind, TAC_CONST_LONG_DOUBLE);
        EXPECT_DOUBLE_EQ(f128_to_double(body->u.copy.src->u.constant->u.long_double_val), (double)expected);
    }
};
