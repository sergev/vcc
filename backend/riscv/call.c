//
// Calls, parameters and return values: the LP64D calling convention.  A scalar goes
// in the next a/fa register, or on the stack in 8 bytes; a float or double past fa7,
// or a variadic one, goes where an integer would.  A struct of up to 16 bytes that
// flattens to one or two scalars, at least one of them floating, goes in FP registers
// (or an FP and an integer register) when enough are left.  Any other struct of up to
// 16 bytes goes as one or two doublewords (a register each, or the stack; the second
// may follow on the stack when only a7 is left); a larger one by reference.  Return
// values use the same rules with a0/a1 and fa0/fa1.
//
#include "internal.h"

// One register-sized part of an argument: where it goes, and what it holds.
typedef struct {
    int reg;              // register, or -1 for the stack
    int stack;            // byte offset in the argument area
    int offset;           // within the argument
    int size;             // bytes
    const Tac_Type *type; // a scalar; NULL for raw bytes of an aggregate
} Piece;

typedef struct {
    int npieces;
    Piece piece[2];
    bool by_ref; // the piece holds the address of a copy
} ArgLoc;

typedef struct {
    int next_int, next_fp, stack;
} ArgState;

static void take_int(ArgState *s, Piece *p)
{
    if (s->next_int < 8) {
        p->reg = RV_A0 + s->next_int++;
    } else {
        p->reg   = -1;
        p->stack = s->stack;
        s->stack += 8;
    }
}

typedef struct {
    const Tac_Type *type;
    int offset;
} Field;

// Flatten `t` at `off` into scalar fields; false when there would be more than two,
// or one wider than 8 bytes, or `t` holds a union.
static bool flatten(const Tac_Type *t, int off, Field *f, int *n)
{
    switch (t->kind) {
    case TAC_TYPE_STRUCTURE:
        if (t->u.structure.is_union)
            return false;
        for (const Tac_Member *m = t->u.structure.members; m; m = m->next)
            if (!flatten(m->type, off + m->offset, f, n))
                return false;
        return true;
    case TAC_TYPE_ARRAY:
        for (int i = 0; i < t->u.array.size; i++)
            if (!flatten(t->u.array.elem_type, off + i * rv_size(t->u.array.elem_type), f, n))
                return false;
        return true;
    default:
        if (*n == 2 || rv_size(t) > 8)
            return false;
        f[(*n)++] = (Field){ t, off };
        return true;
    }
}

// Pass a small struct in FP registers, if it flattens to fit and they are free.
static bool classify_fp(ArgState *s, const Tac_Type *t, ArgLoc *a)
{
    Field f[2];
    int n = 0;
    if (!flatten(t, 0, f, &n) || n == 0)
        return false;
    int nfp = 0;
    for (int i = 0; i < n; i++)
        nfp += rv_is_fp(f[i].type);
    if (nfp == 0 || s->next_fp + nfp > 8 || s->next_int + n - nfp > 8)
        return false;
    a->npieces = n;
    for (int i = 0; i < n; i++) {
        Piece *p  = &a->piece[i];
        p->offset = f[i].offset;
        p->size   = rv_size(f[i].type);
        p->type   = f[i].type;
        p->reg    = rv_is_fp(f[i].type) ? RV_FA0 + s->next_fp++ : RV_A0 + s->next_int++;
    }
    return true;
}

static ArgLoc classify(ArgState *s, const Tac_Type *t, bool variadic)
{
    ArgLoc a = { 0 };
    if (!rv_is_aggregate(t)) {
        a.npieces = 1;
        a.piece[0] = (Piece){ .size = rv_size(t), .type = t };
        if (rv_is_fp(t) && !variadic && s->next_fp < 8)
            a.piece[0].reg = RV_FA0 + s->next_fp++;
        else
            take_int(s, &a.piece[0]);
        return a;
    }
    int size = rv_size(t);
    if (size > 16) {
        a.by_ref   = true;
        a.npieces  = 1;
        a.piece[0] = (Piece){ .size = 8 };
        take_int(s, &a.piece[0]);
        return a;
    }
    if (!variadic && classify_fp(s, t, &a))
        return a;
    a.npieces = size > 8 ? 2 : 1;
    for (int i = 0; i < a.npieces; i++) {
        a.piece[i] = (Piece){ .offset = 8 * i, .size = size - 8 * i < 8 ? size - 8 * i : 8 };
        take_int(s, &a.piece[i]);
    }
    return a;
}

static bool is_freg(int reg)
{
    return reg >= RV_F0;
}

// Load a piece of aggregate `name` into `reg`, or store `reg` into a piece at base + off.
static void load_piece(Gen *g, int reg, const char *name, const Piece *pc)
{
    int base;
    int64_t off;
    name_addr(g, name, RV_T5, &base, &off);
    if (pc->type)
        load_mem(g, reg, pc->type, base, off + pc->offset);
    else
        load_bytes(g, reg, base, off + pc->offset, pc->size);
}

static void store_piece(Gen *g, int reg, int base, int64_t off, const Piece *pc)
{
    if (pc->type)
        store_mem(g, reg, pc->type, base, off + pc->offset);
    else
        store_bytes(g, reg, base, off + pc->offset, pc->size);
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

// Whether a value is passed whole in integer registers, maybe ending on the stack.
static bool in_int_regs(const ArgLoc *a)
{
    if (a->by_ref)
        return false;
    for (int i = 0; i < a->npieces; i++)
        if (is_freg(a->piece[i].reg))
            return false;
    return true;
}

// A variadic function stores a0-a7 just below the incoming stack arguments, so that
// all arguments passed in integer registers or on the stack are contiguous.  A named
// parameter passed so lives there too: va_start steps on from its address.
void gen_params(Gen *g)
{
    bool variadic = gen_variadic(g);
    if (variadic)
        for (int i = 0; i < 8; i++)
            emit2(g, RV_SD, rv_reg(RV_A0 + i), rv_mem(RV_S0, -64 + 8 * i));
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
        if (variadic && in_int_regs(&a)) {
            place_slot(g, p->name, t, -64 + 8 * (a.piece[0].reg - RV_A0));
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
                int reg         = pc->reg;
                if (reg < 0) {
                    emit2(g, RV_LD, rv_reg(RV_T0), mem(g, RV_S0, pc->stack));
                    reg = RV_T0;
                }
                store_piece(g, reg, RV_S0, off, pc);
            }
        } else if (rv_is_fp(t) && !is_freg(a.piece[0].reg)) {
            int_to_fp(g, t, RV_F0, a.piece[0].reg);
            store_mem(g, RV_F0, t, RV_S0, off);
        } else {
            store_mem(g, a.piece[0].reg, t, RV_S0, off);
        }
    }
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
            load_piece(g, reg, v->u.var_name, pc);
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

// Where a value of type `t` is returned.
static ArgLoc classify_result(const Tac_Type *t)
{
    ArgState s = { 0 };
    return classify(&s, t, false);
}

// Store a returned value into `dst`.
static void store_result(Gen *g, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!rv_is_aggregate(t)) {
        store_val(g, rv_is_fp(t) ? RV_FA0 : RV_A0, dst);
        return;
    }
    ArgLoc a = classify_result(t);
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, RV_T5, &base, &off);
    for (int i = 0; i < a.npieces; i++)
        store_piece(g, a.piece[i].reg, base, off, &a.piece[i]);
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
            ArgLoc a = classify_result(t);
            for (int i = 0; i < a.npieces; i++)
                load_piece(g, a.piece[i].reg, v->u.var_name, &a.piece[i]);
        } else {
            load_val(g, rv_is_fp(t) ? RV_FA0 : RV_A0, v);
        }
    }
    gen_epilogue(g);
}
