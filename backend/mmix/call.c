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

// A scalar value into register `reg` as the ABI passes it: a float as its binary32 bits.
static void load_abi(Gen *g, const Tac_Val *v, int reg)
{
    const Tac_Type *t = val_type(g, v);
    if (!mmix_is_float(t)) {
        load_val(g, v, reg);
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
        load_abi(g, v, ret_reg(g));
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

void gen_call(Gen *g, const Tac_Instruction *in)
{
    (void)in;
    fatal_error("mmix: %s: calls are not implemented yet", gen_name(g));
}
