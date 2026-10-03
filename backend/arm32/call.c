//
// Calls, parameters and return values: AAPCS-VFP.  An integer or pointer goes in the
// next of r0-r3, a long long in the next even pair; once the core registers run out,
// those values go on the stack, and no later one takes a core register.  A float goes
// in the lowest free s register of s0-s15, a double in the lowest free even pair (d0-
// d7), so a float may fill the gap a double's alignment left; once one goes on the
// stack, no later FP value takes a VFP register.  A stack value takes 4 bytes, 8 for
// one of 8-byte alignment, which then is 8-aligned.  The sender extends a narrow value
// to 32 bits.  A variadic callee takes all its arguments, the named ones too, under
// the base standard: a float as an integer, a double as a long long.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Where one argument goes.
typedef struct {
    int nregs; // 0 for the stack, else the core registers (1 or 2) or 1 VFP register
    int reg;   // the first register
    int stack; // byte offset in the argument area
} ArgLoc;

typedef struct {
    bool vfp;       // the VFP variant, not the base standard
    int next_core;  // r0-r3
    unsigned sfree; // the free registers of s0-s15, a bit each
    int stack;
} ArgState;

static ArgState arg_state(bool vfp)
{
    return (ArgState){ vfp, 0, 0xffff, 0 };
}

static int round_up(int n, int to)
{
    return (n + to - 1) / to * to;
}

static void on_stack(ArgState *s, ArgLoc *a, int size, int align)
{
    s->stack = round_up(s->stack, align >= 8 ? 8 : 4);
    a->stack = s->stack;
    s->stack += round_up(size, 4);
}

static ArgLoc classify(ArgState *s, const Tac_Type *t)
{
    ArgLoc a = { 0 };
    int size = a32_size(t);
    if (a32_is_aggregate(t))
        fatal_error("arm32: a structure argument is not implemented yet");
    if (s->vfp && a32_is_fp(t)) {
        // The lowest free single, or even-aligned pair of them.
        int n = a32_is_double(t) ? 2 : 1;
        for (int i = 0; i < 16; i += n) {
            unsigned want = (1u << n) - 1;
            if ((s->sfree >> i & want) == want) {
                s->sfree &= ~(want << i);
                a.nregs = 1;
                a.reg   = A32_SREG(i);
                return a;
            }
        }
        s->sfree = 0;
        on_stack(s, &a, size, size);
        return a;
    }
    int n = size > 4 ? 2 : 1;
    if (n == 2 && s->next_core % 2)
        s->next_core++;
    if (s->next_core + n <= 4) {
        a.nregs = n;
        a.reg   = A32_R0 + s->next_core;
        s->next_core += n;
    } else {
        s->next_core = 4;
        on_stack(s, &a, size, size);
    }
    return a;
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

static bool is_variadic(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE && fun_type->u.fun_type.variadic;
}

// Each parameter gets a slot: one passed in registers is stored there, one on the stack
// is read where the caller put it, above the frame record.
void gen_params(Gen *g)
{
    if (g->tl->u.function.variadic)
        fatal_error("arm32: %s: a variadic function is not implemented yet", gen_name(g));
    ArgState s = arg_state(true);
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("arm32: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t);
        if (!a.nregs) {
            place_slot(g, p->name, t, 8 + a.stack);
            continue;
        }
        int off = alloc_slot(g, p->name, t, a32_size(t), a32_align(t));
        if (a.nregs == 2) {
            emit2(g, A32_STR, a32_reg(a.reg), mem(g, A32_STR, A32_FP, off, T0));
            emit2(g, A32_STR, a32_reg(a.reg + 1), mem(g, A32_STR, A32_FP, off + 4, T0));
        } else {
            store_mem(g, a.reg, t, A32_FP, off, T0);
        }
    }
}

// One argument: its value, the type it is passed as, and where it goes.
typedef struct {
    const Tac_Val *v;
    const Tac_Type *as;
    ArgLoc loc;
} Arg;

// Load argument `a` into its registers.
static void load_arg_regs(Gen *g, const Arg *a)
{
    const ArgLoc *l = &a->loc;
    if (a32_is_vfp(l->reg))
        load_as(g, l->reg, a->v, a->as);
    else if (l->nregs == 2) {
        load_word(g, l->reg, a->v, a->as, 0);
        load_word(g, l->reg + 1, a->v, a->as, 1);
    } else {
        load_as(g, l->reg, a->v, a->as);
    }
}

// Put argument `a` in the outgoing area, through r12 and lr.
static void store_arg(Gen *g, const Arg *a)
{
    int off = a->loc.stack;
    if (a32_size(a->as) == 8) {
        load_word(g, T0, a->v, a->as, 0);
        load_word(g, T1, a->v, a->as, 1);
        emit2(g, A32_STR, a32_reg(T0), mem(g, A32_STR, A32_SP, off, T2));
        emit2(g, A32_STR, a32_reg(T1), mem(g, A32_STR, A32_SP, off + 4, T2));
        return;
    }
    load_as(g, T0, a->v, a->as);
    // A narrow value takes a whole word, extended.
    emit2(g, A32_STR, a32_reg(T0), mem(g, A32_STR, A32_SP, off, T1));
}

// The type argument `v` is passed as: the declared parameter type, when there is one
// and it is a scalar of the same class, else its own.
static const Tac_Type *arg_type(const Gen *g, const Tac_Val *v, const Tac_Type *want)
{
    const Tac_Type *t = val_type(g, v);
    if (want && !a32_is_aggregate(want) && a32_is_fp(want) == a32_is_fp(t) &&
        a32_is_pair(want) == a32_is_pair(t))
        return want;
    return t;
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    const Tac_Val *dst   = in->u.fun_call.dst;
    bool vfp             = !is_variadic(ft);
    int nargs            = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next)
        nargs++;
    Arg *args  = xalloc((nargs ? nargs : 1) * sizeof(Arg), __func__, __FILE__, __LINE__);
    ArgState s = arg_state(vfp);
    int i      = 0;
    // The stack arguments first, through the scratch registers; then the registers,
    // each loaded straight into place.
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        Arg *a = &args[i];
        a->v   = v;
        a->as  = arg_type(g, v, want);
        a->loc = classify(&s, a->as);
        if (want)
            want = want->next;
        if (!a->loc.nregs)
            store_arg(g, a);
    }
    for (i = 0; i < nargs; i++)
        if (args[i].loc.nregs)
            load_arg_regs(g, &args[i]);
    xfree(args);
    if (s.stack > g->outgoing)
        g->outgoing = s.stack;
    if (in->u.fun_call.indirect) {
        // The callee's address last: loading an FP argument goes through r12.
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T0, &fp);
        emit1(g, A32_BLX, a32_reg(T0));
    } else {
        emit1(g, A32_BL, a32_sym(in->u.fun_call.fun_name, 0));
    }
    if (!dst)
        return;
    const Tac_Type *t = val_type(g, dst);
    if (a32_is_aggregate(t))
        fatal_error("arm32: %s: a structure result is not implemented yet", gen_name(g));
    if (a32_is_fp(t) && vfp)
        store_val(g, A32_S0, dst);
    else if (a32_size(t) == 8)
        store_pair(g, dst, A32_R0, A32_R0 + 1);
    else
        store_val(g, A32_R0, dst);
}

// An integer in r0 (the callee extends a narrow one), a long long in r0:r1, a float in
// s0, a double in d0 (a constant's bits through r0 and r1, which need no frame).
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (!rt || rt->kind == TAC_TYPE_VOID)
            rt = t;
        if (a32_is_aggregate(rt))
            fatal_error("arm32: %s: returning a structure is not implemented yet", gen_name(g));
        if (a32_is_fp(rt) && v->kind == TAC_VAL_CONSTANT)
            load_fp_const(g, A32_S0, v->u.constant, rt, A32_R0, A32_R0 + 1);
        else if (a32_is_fp(rt))
            load_val(g, A32_S0, v);
        else if (a32_is_pair(rt)) {
            load_word(g, A32_R0, v, rt, 0);
            load_word(g, A32_R0 + 1, v, rt, 1);
        } else {
            load_as(g, A32_R0, v, rt);
        }
    }
    gen_epilogue(g);
}
