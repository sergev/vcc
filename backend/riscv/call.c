//
// Calls, parameters and return values: the LP64D integer and FP calling convention.
// A scalar goes in the next a/fa register, or on the stack in 8 bytes.  A float or
// double past fa7, or a variadic one, goes where an integer would.  A struct of up to
// 16 bytes goes as one or two doublewords (a register each, or the stack; the second
// may follow on the stack when only a7 is left); a larger one by reference.  Floating
// point members do not yet use FP registers.
//
#include "internal.h"

// Where one doubleword of an argument goes: an integer or FP register, or the stack.
typedef struct {
    int reg;   // register, or -1 for the stack
    int stack; // byte offset in the argument area
} Piece;

typedef struct {
    int npieces;
    Piece piece[2];
    bool by_ref; // the piece holds the address of a copy
} ArgLoc;

typedef struct {
    int next_int, next_fp, stack;
} ArgState;

static Piece int_piece(ArgState *s)
{
    if (s->next_int < 8)
        return (Piece){ RV_A0 + s->next_int++, 0 };
    Piece p = { -1, s->stack };
    s->stack += 8;
    return p;
}

static ArgLoc classify(ArgState *s, const Tac_Type *t, bool variadic)
{
    ArgLoc a = { 0 };
    if (rv_is_aggregate(t)) {
        int size = rv_size(t);
        if (size > 16) {
            a.by_ref   = true;
            a.npieces  = 1;
            a.piece[0] = int_piece(s);
        } else {
            a.npieces = size > 8 ? 2 : 1;
            for (int i = 0; i < a.npieces; i++)
                a.piece[i] = int_piece(s);
        }
    } else if (rv_is_fp(t) && !variadic && s->next_fp < 8) {
        a.npieces  = 1;
        a.piece[0] = (Piece){ RV_FA0 + s->next_fp++, 0 };
    } else {
        a.npieces  = 1;
        a.piece[0] = int_piece(s);
    }
    return a;
}

static bool is_freg(int reg)
{
    return reg >= RV_F0;
}

// Move an FP value between an FP register and an integer register.
static void fp_to_int(Gen *g, const Tac_Type *t, int ireg, int freg)
{
    emit2(g, rv_is_double(t) ? RV_FMVXD : RV_FMVXW, rv_reg(ireg), rv_reg(freg));
}

static void int_to_fp(Gen *g, const Tac_Type *t, int freg, int ireg)
{
    emit2(g, rv_is_double(t) ? RV_FMVDX : RV_FMVWX, rv_reg(freg), rv_reg(ireg));
}

void gen_params(Gen *g)
{
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("riscv: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t, false);
        if (!a.by_ref && a.piece[0].reg < 0) {
            place_slot(g, p->name, t, a.piece[0].stack); // entirely on the stack
            continue;
        }
        int size = rv_size(t);
        int off  = alloc_slot(g, p->name, t, size, rv_align(t));
        if (a.by_ref) {
            const Piece *pc = &a.piece[0];
            int src         = pc->reg;
            if (src < 0) {
                emit2(g, RV_LD, rv_reg(RV_T3), mem(g, RV_S0, pc->stack));
                src = RV_T3;
            }
            gen_memcopy(g, RV_S0, off, src, 0, size, rv_align(t));
        } else if (rv_is_aggregate(t)) {
            for (int i = 0; i < a.npieces; i++) {
                const Piece *pc = &a.piece[i];
                int n           = size - 8 * i < 8 ? size - 8 * i : 8;
                int reg         = pc->reg;
                if (reg < 0) {
                    emit2(g, RV_LD, rv_reg(RV_T0), mem(g, RV_S0, pc->stack));
                    reg = RV_T0;
                }
                store_bytes(g, reg, RV_S0, off + 8 * i, n);
            }
        } else if (rv_is_fp(t) && !is_freg(a.piece[0].reg)) {
            int_to_fp(g, t, RV_F0, a.piece[0].reg);
            store_mem(g, RV_F0, t, RV_S0, off);
        } else {
            store_mem(g, a.piece[0].reg, t, RV_S0, off);
        }
    }
}

// Load one doubleword of aggregate `name` (at index `i`, `size` bytes in all) into reg.
static void load_agg_piece(Gen *g, int reg, const char *name, int i, int size)
{
    int base;
    int64_t off;
    name_addr(g, name, RV_T5, &base, &off);
    int n = size - 8 * i < 8 ? size - 8 * i : 8;
    load_bytes(g, reg, base, off + 8 * i, n);
}

static void gen_arg(Gen *g, const Tac_Val *v, const ArgLoc *a)
{
    const Tac_Type *t = val_type(g, v);
    if (a->by_ref) {
        int size = rv_size(t);
        int copy = alloc_slot(g, NULL, NULL, size, rv_align(t) > 8 ? rv_align(t) : 8);
        int base;
        int64_t off;
        name_addr(g, v->u.var_name, RV_T3, &base, &off);
        gen_memcopy(g, RV_S0, copy, base, off, size, rv_align(t));
        const Piece *pc = &a->piece[0];
        int reg         = pc->reg >= 0 ? pc->reg : RV_T0;
        gen_addr(g, reg, RV_S0, copy);
        if (pc->reg < 0)
            emit2(g, RV_SD, rv_reg(reg), mem(g, RV_SP, pc->stack));
        return;
    }
    for (int i = 0; i < a->npieces; i++) {
        const Piece *pc = &a->piece[i];
        int reg         = pc->reg >= 0 ? pc->reg : RV_T0;
        if (rv_is_aggregate(t)) {
            load_agg_piece(g, reg, v->u.var_name, i, rv_size(t));
        } else if (rv_is_fp(t) && !is_freg(reg)) {
            load_val(g, RV_F0, v);
            fp_to_int(g, t, reg, RV_F0);
        } else {
            load_val(g, reg, v);
        }
        if (pc->reg < 0)
            emit2(g, RV_SD, rv_reg(reg), mem(g, RV_SP, pc->stack));
    }
}

// Store a returned value in a0/a1 or fa0 into `dst`.
static void store_result(Gen *g, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (rv_is_aggregate(t)) {
        int size = rv_size(t);
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, RV_T5, &base, &off);
        store_bytes(g, RV_A0, base, off, size < 8 ? size : 8);
        if (size > 8)
            store_bytes(g, RV_A0 + 1, base, off + 8, size - 8);
        return;
    }
    store_val(g, rv_is_fp(t) ? RV_FA0 : RV_A0, dst);
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    int nfixed         = 0;
    bool variadic      = ft && ft->u.fun_type.variadic;
    if (ft)
        for (const Tac_Type *p = ft->u.fun_type.param_types; p; p = p->next)
            nfixed++;

    ArgState s = { 0 };
    int i      = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        ArgLoc a = classify(&s, val_type(g, v), variadic && i >= nfixed);
        gen_arg(g, v, &a);
    }
    if (s.stack > g->outgoing)
        g->outgoing = s.stack;

    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, RV_T1, &fp);
        rv_append(g->fn, RV_JALR)->opnd[0] = rv_reg(RV_T1);
    } else {
        rv_append(g->fn, RV_CALL)->opnd[0] = rv_sym(in->u.fun_call.fun_name, 0);
    }
    if (in->u.fun_call.dst)
        store_result(g, in->u.fun_call.dst);
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (rv_is_aggregate(t)) {
            int size = rv_size(t);
            load_agg_piece(g, RV_A0, v->u.var_name, 0, size);
            if (size > 8)
                load_agg_piece(g, RV_A0 + 1, v->u.var_name, 1, size);
        } else {
            load_val(g, rv_is_fp(t) ? RV_FA0 : RV_A0, v);
        }
    }
    gen_epilogue(g);
}
