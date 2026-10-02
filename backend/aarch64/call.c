//
// Calls, parameters and return values: AAPCS64.  An integer or pointer goes in the next
// of x0-x7, a float or double in the next of v0-v7; once a class runs out, its values
// go on the stack, each in an 8-byte slot, in order.  A variadic argument goes as a
// named one would.  Neither side extends a narrow value: the receiver does (a store to
// a slot truncates, a load extends by type).
//
// A struct or union of up to 16 bytes goes in one or two X registers, the pair starting
// at an even register when it is 16-byte aligned; when there are not enough left, it
// goes whole on the stack (rounded up to 8 bytes, aligned to 8 or 16) and no later
// argument takes an X register.  A larger one is copied by the caller and passed as a
// pointer to the copy, as an integer is.  A result of up to 16 bytes comes back in
// x0/x1; a larger one is written through the address the caller passes in x8.
//
#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Where one argument goes.
typedef struct {
    int nregs;   // 1 or 2 registers, or 0 for the stack
    int reg[2];  // registers
    int stack;   // byte offset in the argument area
    bool by_ref; // a pointer to a copy goes there instead
} ArgLoc;

typedef struct {
    int next_int, next_fp, stack;
} ArgState;

static int round_up(int n, int to)
{
    return (n + to - 1) / to * to;
}

static void on_stack(ArgState *s, ArgLoc *a, int size, int align)
{
    s->stack = round_up(s->stack, align > 8 ? align : 8);
    a->stack = s->stack;
    s->stack += round_up(size, 8);
}

static ArgLoc classify(Gen *g, ArgState *s, const Tac_Type *t)
{
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: passing a long double is not implemented yet", gen_name(g));
    ArgLoc a = { 0 };
    int size = a64_size(t);
    if (a64_is_aggregate(t) && size <= 16) {
        int n = (size + 7) / 8;
        if (a64_align(t) == 16 && s->next_int % 2)
            s->next_int++;
        if (s->next_int + n <= 8) {
            a.nregs = n;
            for (int i = 0; i < n; i++)
                a.reg[i] = A64_X(s->next_int++);
        } else {
            s->next_int = 8;
            on_stack(s, &a, size, a64_align(t));
        }
        return a;
    }
    a.by_ref = a64_is_aggregate(t);
    if (a64_is_fp(t) && s->next_fp < 8) {
        a.nregs  = 1;
        a.reg[0] = A64_V(s->next_fp++);
    } else if (!a64_is_fp(t) && s->next_int < 8) {
        a.nregs  = 1;
        a.reg[0] = A64_X(s->next_int++);
    } else {
        on_stack(s, &a, 8, 8);
    }
    return a;
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

// Whether a result of type `t` is written through the address in x8.
static bool indirect_result(const Tac_Type *t)
{
    return t && a64_is_aggregate(t) && a64_size(t) > 16;
}

// The bytes of doubleword `i` of an aggregate of `size` bytes.
static int piece_size(int size, int i)
{
    return size - 8 * i < 8 ? size - 8 * i : 8;
}

// Each parameter gets a slot: one passed in registers is stored there, one on the
// stack is read where the caller put it (above the frame record), one passed by
// reference is copied in from the caller's copy.
void gen_params(Gen *g)
{
    if (indirect_result(ret_type(g->tl->u.function.type))) {
        g->ret_ptr = alloc_slot(g, NULL, NULL, 8, 8);
        emit2(g, A64_STR, a64_reg(A64_X8, A64_X), mem(g, A64_FP, g->ret_ptr, 8));
    }
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("aarch64: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(g, &s, t);
        if (!a.nregs && !a.by_ref) {
            place_slot(g, p->name, t, 16 + a.stack);
            continue;
        }
        int size = a64_size(t);
        int off  = alloc_slot(g, p->name, t, size, a64_align(t));
        if (a.by_ref) {
            int src = T3;
            if (a.nregs)
                src = a.reg[0];
            else
                emit2(g, A64_LDR, a64_reg(T3, A64_X), mem(g, A64_FP, 16 + a.stack, 8));
            gen_memcopy(g, A64_FP, off, src, 0, size, a64_align(t));
        } else if (a64_is_aggregate(t)) {
            for (int i = 0; i < a.nregs; i++)
                store_bytes(g, a.reg[i], A64_FP, off + 8 * i, piece_size(size, i));
        } else {
            store_mem(g, a.reg[0], t, A64_FP, off);
        }
    }
}

// Load the doublewords of aggregate `name` into registers `reg`.
static void load_pieces(Gen *g, const char *name, const Tac_Type *t, const int *reg, int n)
{
    int base;
    int64_t off;
    name_addr(g, name, T5, &base, &off);
    for (int i = 0; i < n; i++)
        load_bytes(g, reg[i], base, off + 8 * i, piece_size(a64_size(t), i));
}

// One argument: its value, its type, the type it is converted to, and where it goes.
typedef struct {
    const Tac_Val *v;
    const Tac_Type *type, *as;
    ArgLoc loc;
    int copy; // frame offset of the copy passed by reference
} Arg;

// Put argument `a` on the stack, or make its copy; only the scratch registers change.
static void arg_to_stack(Gen *g, Arg *a)
{
    const Tac_Type *t = a->type;
    if (a->loc.by_ref) {
        int base;
        int64_t off;
        a->copy = alloc_slot(g, NULL, NULL, a64_size(t), a64_align(t) > 8 ? a64_align(t) : 8);
        name_addr(g, a->v->u.var_name, T3, &base, &off);
        gen_memcopy(g, A64_FP, a->copy, base, off, a64_size(t), a64_align(t));
        if (!a->loc.nregs) {
            gen_addr(g, T0, A64_FP, a->copy);
            emit2(g, A64_STR, a64_reg(T0, A64_X), mem(g, A64_SP, a->loc.stack, 8));
        }
        return;
    }
    if (a->loc.nregs)
        return;
    if (a64_is_aggregate(t)) {
        int base;
        int64_t off;
        name_addr(g, a->v->u.var_name, T3, &base, &off);
        gen_memcopy(g, A64_SP, a->loc.stack, base, off, a64_size(t), a64_align(t));
        return;
    }
    int r = a64_is_fp(t) ? F0 : T0;
    if (a64_is_fp(t))
        load_val(g, r, a->v);
    else
        load_int_as(g, r, a->v, a->as);
    store_mem(g, r, a->as, A64_SP, a->loc.stack);
}

// Load argument `a`'s registers.
static void arg_to_regs(Gen *g, const Arg *a)
{
    const ArgLoc *l = &a->loc;
    if (!l->nregs)
        return;
    if (l->by_ref)
        gen_addr(g, l->reg[0], A64_FP, a->copy);
    else if (a64_is_aggregate(a->type))
        load_pieces(g, a->v->u.var_name, a->type, l->reg, l->nregs);
    else if (a64_is_fp(a->type))
        load_val(g, l->reg[0], a->v);
    else
        load_int_as(g, l->reg[0], a->v, a->as);
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    const Tac_Val *dst   = in->u.fun_call.dst;
    const Tac_Type *ret  = ret_type(ft);
    if (!ret && dst)
        ret = val_type(g, dst);
    int nargs = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next)
        nargs++;
    Arg *args  = xalloc((nargs ? nargs : 1) * sizeof(Arg), __func__, __FILE__, __LINE__);
    ArgState s = { 0 };
    int i      = 0;
    // Every argument is in memory or a constant: the stack ones and the copies first,
    // through the scratch registers, then the register ones straight into place.
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        Arg *a  = &args[i];
        a->v    = v;
        a->type = val_type(g, v);
        // A constant takes the declared parameter type, when there is one.
        a->as   = want && !a64_is_fp(a->type) && !a64_is_fp(want) && !a64_is_aggregate(want)
                      ? want
                      : a->type;
        a->loc  = classify(g, &s, a->type);
        a->copy = 0;
        if (want)
            want = want->next;
        arg_to_stack(g, a);
    }
    for (i = 0; i < nargs; i++)
        arg_to_regs(g, &args[i]);
    xfree(args);
    if (s.stack > g->outgoing)
        g->outgoing = s.stack;
    if (indirect_result(ret)) {
        // The result's address in x8: the destination, or a slot for an unused one.
        int base;
        int64_t off;
        if (dst) {
            name_addr(g, dst->u.var_name, A64_X8, &base, &off);
        } else {
            base = A64_FP;
            off  = alloc_slot(g, NULL, NULL, a64_size(ret), a64_align(ret));
        }
        gen_addr(g, A64_X8, base, off);
    }

    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T0, &fp);
        emit1(g, A64_BLR, a64_reg(T0, A64_X));
    } else {
        emit1(g, A64_BL, a64_sym(in->u.fun_call.fun_name, 0));
    }
    if (!dst || indirect_result(ret))
        return;
    const Tac_Type *t = val_type(g, dst);
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: a long double result is not implemented yet", gen_name(g));
    if (a64_is_aggregate(t)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, T5, &base, &off);
        int size = a64_size(t);
        for (int k = 0; k * 8 < size; k++)
            store_bytes(g, A64_X(k), base, off + 8 * k, piece_size(size, k));
        return;
    }
    store_val(g, a64_is_fp(t) ? A64_V0 : A64_X0, dst);
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (a64_is_ld(t))
            fatal_error("aarch64: %s: returning a long double is not implemented yet", gen_name(g));
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (a64_is_aggregate(t) && g->ret_ptr) {
            int base;
            int64_t off;
            emit2(g, A64_LDR, a64_reg(T4, A64_X), mem(g, A64_FP, g->ret_ptr, 8));
            name_addr(g, v->u.var_name, T3, &base, &off);
            gen_memcopy(g, T4, 0, base, off, a64_size(t), a64_align(t));
        } else if (a64_is_aggregate(t)) {
            int regs[2] = { A64_X0, A64_X(1) };
            load_pieces(g, v->u.var_name, t, regs, (a64_size(t) + 7) / 8);
        } else if (a64_is_fp(t)) {
            load_val(g, A64_V0, v);
        } else {
            load_int_as(g, A64_X0, v, rt && !a64_is_fp(rt) ? rt : t);
        }
    }
    gen_epilogue(g);
}
