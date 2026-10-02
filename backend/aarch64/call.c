//
// Calls, parameters and return values: AAPCS64.  An integer or pointer goes in the next
// of x0-x7, a float or double in the next of v0-v7; once a class runs out, its values
// go on the stack, each in an 8-byte slot, in order.  A variadic argument goes as a
// named one would.  The result comes back in x0 or v0.  Neither side extends a narrow
// value: the receiver does (a store to a slot truncates, a load extends by type).
//
#include "codegen.h"
#include "internal.h"

// Where one scalar argument goes.
typedef struct {
    int reg;   // register, or 0 for the stack
    int stack; // byte offset in the argument area
} ArgLoc;

typedef struct {
    int next_int, next_fp, stack;
} ArgState;

static ArgLoc classify(Gen *g, ArgState *s, const Tac_Type *t)
{
    if (a64_is_aggregate(t) || a64_is_ld(t))
        fatal_error("aarch64: %s: passing a %d-byte value is not implemented yet", gen_name(g),
                    a64_size(t));
    ArgLoc a = { 0, 0 };
    if (a64_is_fp(t) && s->next_fp < 8) {
        a.reg = A64_V(s->next_fp++);
    } else if (!a64_is_fp(t) && s->next_int < 8) {
        a.reg = A64_X(s->next_int++);
    } else {
        a.stack = s->stack;
        s->stack += 8;
    }
    return a;
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

// Each parameter gets a slot: one passed in a register is stored there, one on the
// stack is read where the caller put it, above the frame record.
void gen_params(Gen *g)
{
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("aarch64: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(g, &s, t);
        if (!a.reg) {
            place_slot(g, p->name, t, 16 + a.stack);
            continue;
        }
        int off = alloc_slot(g, p->name, t, a64_size(t), a64_align(t));
        store_mem(g, a.reg, t, A64_FP, off);
    }
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    int stack            = 0;
    // Every argument is in memory or a constant: the stack ones first, through the
    // scratch registers, then the register ones straight into place.
    for (int pass = 0; pass < 2; pass++) {
        ArgState state    = { 0 };
        const Tac_Type *w = want;
        for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next) {
            const Tac_Type *t = val_type(g, v);
            // A constant takes the declared parameter type, when there is one.
            const Tac_Type *as = w && !a64_is_fp(t) && !a64_is_fp(w) ? w : t;
            ArgLoc a           = classify(g, &state, t);
            if (w)
                w = w->next;
            if (pass == 0 && !a.reg) {
                int r = a64_is_fp(t) ? F0 : T0;
                if (a64_is_fp(t))
                    load_val(g, r, v);
                else
                    load_int_as(g, r, v, as);
                store_mem(g, r, as, A64_SP, a.stack);
            } else if (pass == 1 && a.reg) {
                if (a64_is_fp(t))
                    load_val(g, a.reg, v);
                else
                    load_int_as(g, a.reg, v, as);
            }
        }
        stack = state.stack;
    }
    if (stack > g->outgoing)
        g->outgoing = stack;

    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T0, &fp);
        emit1(g, A64_BLR, a64_reg(T0, A64_X));
    } else {
        emit1(g, A64_BL, a64_sym(in->u.fun_call.fun_name, 0));
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    if (dst) {
        const Tac_Type *t = val_type(g, dst);
        if (a64_is_aggregate(t) || a64_is_ld(t))
            fatal_error("aarch64: %s: a %d-byte result is not implemented yet", gen_name(g),
                        a64_size(t));
        store_val(g, a64_is_fp(t) ? A64_V0 : A64_X0, dst);
    }
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (a64_is_aggregate(t) || a64_is_ld(t))
            fatal_error("aarch64: %s: returning a %d-byte value is not implemented yet",
                        gen_name(g), a64_size(t));
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (a64_is_fp(t))
            load_val(g, A64_V0, v);
        else
            load_int_as(g, A64_X0, v, rt && !a64_is_fp(rt) ? rt : t);
    }
    gen_epilogue(g);
}
