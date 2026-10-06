//
// Parameters, calls and returns: the MMIXware ABI as GCC implements it.
//
// The first 16 arguments go in registers, one each, and the rest on the stack, 8 bytes
// each.  A narrow integer goes extended to 64 bits; a float goes as its binary32 bits in
// the low half, sign-extended (GCC's stsf, then ldt); a double as itself.
//
#include <string.h>

#include "internal.h"

static bool is_va_start(const Tac_Instruction *in)
{
    return !in->u.fun_call.indirect && strcmp(in->u.fun_call.fun_name, "__va_start") == 0;
}

bool makes_call(const Tac_TopLevel *tl)
{
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        if ((in->kind == TAC_INSTRUCTION_FUN_CALL && !is_va_start(in)) ||
            in->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN)
            return true;
    return false;
}

int param_count(const Gen *g)
{
    int n = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next)
        n++;
    return n;
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

// The register parameters into their slots: of a structure over 8 bytes, the address of
// the caller's object, in the first octa of the slot that copy_byref_params fills.
void store_params(Gen *g)
{
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p && i < MAX_REG_ARGS; p = p->next, i++) {
        if (mmix_is_scalar(p->type))
            store_abi(g, i, p->name, p->type);
        else if (struct_in_reg(p->type))
            store_small_struct(g, i, p->name, p->type);
        else
            mem_op(g, MMIX_STO, i, p->name, 0);
    }
    // A variadic function's save area: every argument register after the named ones,
    // whether the caller passed it or not (a register above rL reads as 0).
    if (g->tl->u.function.variadic)
        for (int r = param_count(g); r < MAX_REG_ARGS; r++)
            mem_op_at(g, MMIX_STO, r, MMIX_SP, g->va_off + 8 * (r - param_count(g)));
}

void copy_byref_params(Gen *g)
{
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        if (!param_byref(p->type))
            continue;
        if (i < MAX_REG_ARGS)
            mem_op(g, MMIX_LDO, REG_B, p->name, 0);
        else
            mem_op_at(g, MMIX_LDO, REG_B, MMIX_SP, stack_param_off(g, i));
        address_of(g, REG_C, p->name, 0);
        copy_bytes(g, mmix_type_size(p->type), mmix_type_align(p->type));
    }
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (mmix_is_scalar(t)) {
            const Tac_Type *ft = g->tl->u.function.type;
            load_abi(g, v, ret_reg(g), ft ? ft->u.fun_type.ret_type : NULL);
        } else {
            // A structure goes to the address the caller put in $251.
            mem_op(g, MMIX_LDO, REG_C, SRET_SLOT, 0);
            address_of(g, REG_B, v->u.var_name, 0);
            copy_bytes(g, mmix_type_size(t), mmix_type_align(t));
        }
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

enum { MAX_ARGS = 256 };

// Argument `a` into register `reg` as the ABI passes it: a scalar extended, a small
// structure right-justified (`tmp` for its pieces), a large one as the address of its
// copy at `copy` in the frame.
static void load_arg(Gen *g, const Tac_Val *a, int reg, int tmp, const Tac_Type *pt, int copy)
{
    const Tac_Type *t = val_type(g, a);
    if (mmix_is_scalar(t))
        load_abi(g, a, reg, pt);
    else if (struct_in_reg(t))
        load_small_struct(g, a->u.var_name, t, reg, tmp);
    else
        add_offset(g, reg, MMIX_SP, copy);
}

// pushj $1: rJ is in $0, which the call keeps; the arguments go in $2..$17 and on the
// stack at 0($254) up, the result comes back in $1.  First the copies of the large
// structure arguments, through $1-$3 (the callee copies too, as GCC's does, but ours keep
// an argument apart from the result's destination in x = f(x)); then the stack arguments, through $1 and $2; then
// the register arguments, while no argument register holds anything yet.  A structure
// result goes where $251 points: the destination, or a scratch copy when there is none.
// va_start(ap), a call of __va_start(&ap): ap = the first variable argument's slot.
static void gen_va_start(Gen *g, const Tac_Instruction *in)
{
    if (!g->tl->u.function.variadic)
        fatal_error("mmix: %s: va_start in a function without ...", gen_name(g));
    if (!in->u.fun_call.args || in->u.fun_call.args->next)
        fatal_error("mmix: %s: __va_start takes one argument", gen_name(g));
    load_val(g, in->u.fun_call.args, REG_A);
    add_offset(g, REG_B, MMIX_SP, g->va_off);
    mem_op_at(g, MMIX_STO, REG_B, REG_A, 0);
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    if (is_va_start(in)) {
        gen_va_start(g, in);
        return;
    }
    const Tac_Type *ft = in->u.fun_call.fun_type;
    int copy[MAX_ARGS];
    int cursor = g->copy_off, i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        if (i >= MAX_ARGS)
            fatal_error("mmix: %s: more than %d arguments", gen_name(g), MAX_ARGS);
        const Tac_Type *t = val_type(g, a);
        copy[i]           = 0;
        if (mmix_is_scalar(t) || struct_in_reg(t))
            continue;
        copy[i] = cursor;
        address_of(g, REG_B, a->u.var_name, 0);
        add_offset(g, REG_C, MMIX_SP, cursor);
        copy_bytes(g, mmix_type_size(t), mmix_type_align(t));
        cursor += (mmix_type_size(t) + 7) & ~7;
    }
    i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        if (i < MAX_REG_ARGS)
            continue;
        load_arg(g, a, REG_A, REG_B, param_type(ft, i), copy[i]);
        mem_op_at(g, MMIX_STO, REG_A, MMIX_SP, 8 * (i - MAX_REG_ARGS));
    }
    i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a && i < MAX_REG_ARGS; a = a->next, i++)
        load_arg(g, a, REG_ARG0 + i, REG_A, param_type(ft, i), copy[i]);

    const Tac_Val *dst = in->u.fun_call.dst;
    bool sret          = dst ? !mmix_is_scalar(val_type(g, dst))
                             : ft && ft->kind == TAC_TYPE_FUN_TYPE && ft->u.fun_type.ret_type &&
                          !mmix_is_scalar(ft->u.fun_type.ret_type);
    if (sret) {
        if (dst)
            address_of(g, MMIX_SRET, dst->u.var_name, 0);
        else
            add_offset(g, MMIX_SRET, MMIX_SP, cursor);
    }

    if (in->u.fun_call.indirect) {
        mem_op(g, MMIX_LDO, MMIX_TMP, in->u.fun_call.fun_name, 0);
        emit3(g, MMIX_PUSHGO, mmix_reg(REG_A), mmix_reg(MMIX_TMP), mmix_imm(0));
    } else {
        emit2(g, MMIX_PUSHJ, mmix_reg(REG_A), mmix_sym(in->u.fun_call.fun_name, 0));
    }

    if (dst && !sret)
        store_abi(g, REG_A, dst->u.var_name, val_type(g, dst));
}
