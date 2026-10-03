//
// Calls, parameters and return values: AAPCS64.  An integer or pointer goes in the next
// of x0-x7, a float, double or long double in the next of v0-v7; once a class runs
// out, its values go on the stack, each in an 8-byte slot (16-byte aligned for a long
// double), in order.  A variadic argument goes as a named one would.  Neither side
// extends a narrow value: the receiver does (a store to a slot truncates, a load
// extends by type).
//
// A homogeneous float aggregate (1-4 members of one FP type, tac_aapcs64_hfa) goes in
// that many consecutive v registers, a member in each, or whole on the stack when they
// do not all fit, and then no later argument takes a v register.  Another struct or
// union of up to 16 bytes goes in one or two X registers, the pair starting at an even
// register when it is 16-byte aligned; when there are not enough left, it goes whole on
// the stack (rounded up to 8 bytes, aligned to 8 or 16) and no later argument takes an
// X register.  A larger one is copied by the caller and passed as a pointer to the
// copy, as an integer is.  A result comes back the way the first argument would go: an
// HFA in v0-v3, up to 16 bytes in x0/x1; a larger one is written through the address
// the caller passes in x8.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Where one argument goes.
typedef struct {
    int nregs;   // 1-4 registers, or 0 for the stack
    int reg[4];  // registers
    int esize;   // v registers: the bytes of the element each holds; 0 for X registers
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

static ArgLoc classify(ArgState *s, const Tac_Type *t)
{
    ArgLoc a = { 0 };
    int size = a64_size(t);
    int esize;
    int n = tac_aapcs64_hfa(t, &esize);
    if (n) {
        if (s->next_fp + n <= 8) {
            a.nregs = n;
            a.esize = esize;
            for (int i = 0; i < n; i++)
                a.reg[i] = A64_V(s->next_fp++);
        } else {
            s->next_fp = 8;
            on_stack(s, &a, size, a64_align(t));
        }
        return a;
    }
    if (a64_is_aggregate(t) && size <= 16) {
        n = (size + 7) / 8;
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
    if (s->next_int < 8) {
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
    return t && tac_aapcs64_class(t) == TAC_AAPCS64_BY_REF;
}

// The register view of an FP element of `esize` bytes.
static A64_Width elem_width(int esize)
{
    return esize == 4 ? A64_S : esize == 8 ? A64_D : A64_Q;
}

// Load the elements of an HFA at base + off into v registers `reg`, or store them.
static void load_elems(Gen *g, const int *reg, int n, int esize, int base, int64_t off)
{
    for (int i = 0; i < n; i++)
        emit2(g, A64_LDR, a64_reg(reg[i], elem_width(esize)), mem(g, base, off + i * esize, esize));
}

static void store_elems(Gen *g, const int *reg, int n, int esize, int base, int64_t off)
{
    for (int i = 0; i < n; i++)
        emit2(g, A64_STR, a64_reg(reg[i], elem_width(esize)), mem(g, base, off + i * esize, esize));
}

// The bytes of doubleword `i` of an aggregate of `size` bytes.
static int piece_size(int size, int i)
{
    return size - 8 * i < 8 ? size - 8 * i : 8;
}

// A variadic function saves the argument registers the named parameters left, x<n>-x7
// and q<m>-q7, at the ends of two save areas, where va_arg finds them (AAPCS64 B.4).
static void save_varargs(Gen *g, const ArgState *s)
{
    int gr = alloc_slot(g, NULL, NULL, 64, 16);
    int vr = alloc_slot(g, NULL, NULL, 128, 16);
    for (int i = s->next_int; i < 8; i++)
        emit2(g, A64_STR, a64_reg(A64_X(i), A64_X), mem(g, A64_FP, gr + 8 * i, 8));
    for (int i = s->next_fp; i < 8; i++)
        emit2(g, A64_STR, a64_reg(A64_V(i), A64_Q), mem(g, A64_FP, vr + 16 * i, 16));
    g->va.stack   = 16 + s->stack;
    g->va.gr_top  = gr + 64;
    g->va.vr_top  = vr + 128;
    g->va.gr_offs = -8 * (8 - s->next_int);
    g->va.vr_offs = -16 * (8 - s->next_fp);
}

// va_start(ap), a call of __va_start(&ap): fill the va_list
// { __stack, __gr_top, __vr_top, __gr_offs, __vr_offs }.
static void gen_va_start(Gen *g, const Tac_Instruction *in)
{
    if (!g->tl->u.function.variadic)
        fatal_error("aarch64: %s: va_start in a function without ...", gen_name(g));
    if (!in->u.fun_call.args || in->u.fun_call.args->next)
        fatal_error("aarch64: %s: __va_start takes one argument", gen_name(g));
    load_val(g, T0, in->u.fun_call.args);
    const int field[3] = { g->va.stack, g->va.gr_top, g->va.vr_top };
    for (int i = 0; i < 3; i++) {
        gen_addr(g, T1, A64_FP, field[i]);
        emit2(g, A64_STR, a64_reg(T1, A64_X), a64_mem(T0, 8 * i));
    }
    gen_li(g, T1, A64_W, g->va.gr_offs);
    emit2(g, A64_STR, a64_reg(T1, A64_W), a64_mem(T0, 24));
    gen_li(g, T1, A64_W, g->va.vr_offs);
    emit2(g, A64_STR, a64_reg(T1, A64_W), a64_mem(T0, 28));
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
        ArgLoc a = classify(&s, t);
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
        } else if (a.esize) {
            store_elems(g, a.reg, a.nregs, a.esize, A64_FP, off);
        } else if (a64_is_aggregate(t)) {
            for (int i = 0; i < a.nregs; i++)
                store_bytes(g, a.reg[i], A64_FP, off + 8 * i, piece_size(size, i));
        } else {
            store_mem(g, a.reg[0], t, A64_FP, off);
        }
    }
    if (g->tl->u.function.variadic)
        save_varargs(g, &s);
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
    if (a64_is_aggregate(t) || (a64_is_ld(t) && a->v->kind == TAC_VAL_VAR)) {
        int base;
        int64_t off;
        name_addr(g, a->v->u.var_name, T3, &base, &off);
        gen_memcopy(g, A64_SP, a->loc.stack, base, off, a64_size(t), a64_align(t));
        return;
    }
    bool fp = a64_is_fp(t) || a64_is_ld(t);
    int r   = fp ? F0 : T0;
    if (fp)
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
    if (l->by_ref) {
        gen_addr(g, l->reg[0], A64_FP, a->copy);
    } else if (l->esize && a64_is_aggregate(a->type)) {
        int base;
        int64_t off;
        name_addr(g, a->v->u.var_name, T5, &base, &off);
        load_elems(g, l->reg, l->nregs, l->esize, base, off);
    } else if (a64_is_aggregate(a->type)) {
        load_pieces(g, a->v->u.var_name, a->type, l->reg, l->nregs);
    } else if (l->esize) {
        load_val(g, l->reg[0], a->v);
    } else {
        load_int_as(g, l->reg[0], a->v, a->as);
    }
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    if (!in->u.fun_call.indirect && strcmp(in->u.fun_call.fun_name, "__va_start") == 0) {
        gen_va_start(g, in);
        return;
    }
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
        a->as   = want && !a64_is_fp(a->type) && !a64_is_ld(a->type) && !a64_is_fp(want) &&
                          !a64_is_ld(want) && !a64_is_aggregate(want)
                      ? want
                      : a->type;
        a->loc  = classify(&s, a->type);
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
    int esize;
    int n = tac_aapcs64_hfa(t, &esize);
    if (n) {
        static const int vregs[4] = { A64_V(0), A64_V(1), A64_V(2), A64_V(3) };
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, T5, &base, &off);
        store_elems(g, vregs, n, esize, base, off);
        return;
    }
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
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        int esize;
        int n = tac_aapcs64_hfa(t, &esize);
        if (n && a64_is_aggregate(t)) {
            static const int vregs[4] = { A64_V(0), A64_V(1), A64_V(2), A64_V(3) };
            int base;
            int64_t off;
            name_addr(g, v->u.var_name, T3, &base, &off);
            load_elems(g, vregs, n, esize, base, off);
        } else if (a64_is_ld(t)) {
            load_val(g, A64_V0, v);
        } else if (a64_is_aggregate(t) && g->ret_ptr) {
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
