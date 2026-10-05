//
// Parameters, calls and returns: the MMIXware ABI as GCC implements it.
//
// The first 16 arguments go in registers, one each, and the rest on the stack, 8 bytes
// each.  A narrow integer goes extended to 64 bits; a float goes as its binary32 bits in
// the low half, sign-extended (GCC's stsf, then ldt); a double as itself.
//
#include <string.h>

#include "internal.h"

bool makes_call(const Tac_TopLevel *tl)
{
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_FUN_CALL || in->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN)
            return true;
    return false;
}

// A scalar in register `reg` as the ABI passes it, into its slot: a float's binary32
// bits as they are, anything else in its own width.
static void store_abi(Gen *g, int reg, const char *name, const Tac_Type *t)
{
    mem_op(g, mmix_is_float(t) ? MMIX_STTU : store_op(t), reg, name, 0);
}

// A scalar value into register `reg` as the ABI passes it, in type `t` (its own when
// NULL): extended to 64 bits, a float as its binary32 bits.
static void load_abi(Gen *g, const Tac_Val *v, int reg, const Tac_Type *t)
{
    if (!t)
        t = val_type(g, v);
    if (!mmix_is_float(t)) {
        load_val_as(g, v, reg, t);
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT)
        gen_const(g, reg, (uint64_t)(int64_t)(int32_t)const_mem_bits(v->u.constant));
    else
        mem_op(g, MMIX_LDT, reg, v->u.var_name, 0);
}

void store_params(Gen *g)
{
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p && i < MAX_REG_ARGS; p = p->next, i++) {
        if (!mmix_is_scalar(p->type))
            fatal_error("mmix: %s: a structure parameter is not implemented yet", gen_name(g));
        store_abi(g, i, p->name, p->type);
    }
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (!mmix_is_scalar(t))
            fatal_error("mmix: %s: a structure result is not implemented yet", gen_name(g));
        const Tac_Type *ft = g->tl->u.function.type;
        load_abi(g, v, ret_reg(g), ft ? ft->u.fun_type.ret_type : NULL);
    }
    if (!last)
        emit1(g, MMIX_JMP, mmix_label(g->exit));
}

int call_stack_size(const Gen *g, const Tac_Instruction *in)
{
    (void)g;
    int n = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        n++;
    return n > MAX_REG_ARGS ? 8 * (n - MAX_REG_ARGS) : 0;
}

// The type of parameter `i` of function type `ft`, or NULL past the named ones.
static const Tac_Type *param_type(const Tac_Type *ft, int i)
{
    if (!ft || ft->kind != TAC_TYPE_FUN_TYPE)
        return NULL;
    const Tac_Type *p = ft->u.fun_type.param_types;
    for (; p && i > 0; i--)
        p = p->next;
    return p;
}

// pushj $1: rJ is in $0, which the call keeps; the arguments go in $2..$17 and on the
// stack at 0($254) up, the result comes back in $1.  The stack arguments go first, through
// $1, while no argument register holds anything yet.
void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    int i              = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        if (!mmix_is_scalar(val_type(g, a)))
            fatal_error("mmix: %s: a structure argument is not implemented yet", gen_name(g));
        if (i < MAX_REG_ARGS)
            continue;
        load_abi(g, a, REG_A, param_type(ft, i));
        mem_op_at(g, MMIX_STO, REG_A, MMIX_SP, 8 * (i - MAX_REG_ARGS));
    }
    i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a && i < MAX_REG_ARGS; a = a->next, i++)
        load_abi(g, a, REG_ARG0 + i, param_type(ft, i));

    if (in->u.fun_call.indirect) {
        mem_op(g, MMIX_LDO, MMIX_TMP, in->u.fun_call.fun_name, 0);
        emit3(g, MMIX_PUSHGO, mmix_reg(REG_A), mmix_reg(MMIX_TMP), mmix_imm(0));
    } else {
        emit2(g, MMIX_PUSHJ, mmix_reg(REG_A), mmix_sym(in->u.fun_call.fun_name, 0));
    }

    const Tac_Val *dst = in->u.fun_call.dst;
    if (dst) {
        const Tac_Type *t = val_type(g, dst);
        if (!mmix_is_scalar(t))
            fatal_error("mmix: %s: a structure result is not implemented yet", gen_name(g));
        store_abi(g, REG_A, dst->u.var_name, t);
    }
}
