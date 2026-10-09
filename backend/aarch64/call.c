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
// Apple's arm64 ABI (aarch64_darwin) differs in three ways.  Long double is double.  A
// named argument on the stack takes its own size and alignment, a scalar or an HFA,
// not an 8-byte slot (another aggregate is still rounded up to 8 bytes).  A variadic
// argument always goes on the stack, in 8-byte slots, as a named one would once the
// registers ran out, but an HFA as any other aggregate, and inline at any size
// (tac_apple64_class); so va_list is a plain pointer over the stack, and a variadic
// function saves no registers.  The caller extends a
// narrow argument to 32 bits, which a value in its canonical form already is.
//
#include <string.h>

#include "codegen.h"
#include "flow.h"
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

// A named argument on the stack: in an 8-byte slot, or under Apple's ABI at its own
// size and alignment.
static void on_stack_named(ArgState *s, ArgLoc *a, int size, int align)
{
    if (!aarch64_darwin) {
        on_stack(s, a, size, align);
        return;
    }
    s->stack = round_up(s->stack, align);
    a->stack = s->stack;
    s->stack += size;
}

// The HFA count of `t` under the ABI in use (tac_aapcs64_hfa).
static int hfa_of(const Tac_Type *t, int *esize)
{
    return aarch64_darwin ? tac_apple64_hfa(t, esize) : tac_aapcs64_hfa(t, esize);
}

// Where an argument of type `t` goes; `variadic` for one matching the `...` (which
// only Apple's ABI tells from a named one).
static ArgLoc classify(ArgState *s, const Tac_Type *t, bool variadic)
{
    ArgLoc a = { 0 };
    int size = a64_size(t);
    int esize;
    if (aarch64_darwin && variadic) {
        a.by_ref = tac_apple64_class(t) == TAC_AAPCS64_BY_REF;
        if (a.by_ref)
            on_stack(s, &a, 8, 8);
        else
            on_stack(s, &a, size, a64_align(t));
        return a;
    }
    int n = hfa_of(t, &esize);
    if (n) {
        if (s->next_fp + n <= 8) {
            a.nregs = n;
            a.esize = esize;
            for (int i = 0; i < n; i++)
                a.reg[i] = A64_V(s->next_fp++);
        } else {
            s->next_fp = 8;
            on_stack_named(s, &a, size, a64_align(t));
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
    } else if (a.by_ref) {
        on_stack(s, &a, 8, 8);
    } else {
        on_stack_named(s, &a, size, a64_align(t));
    }
    return a;
}

// Whether argument `index` (from 0) of a call through function type `ft` is variadic,
// as far as the ABI tells: only Apple's does.
static bool variadic_arg(const Tac_Type *ft, int index)
{
    if (!aarch64_darwin || !ft || ft->kind != TAC_TYPE_FUN_TYPE || !ft->u.fun_type.variadic)
        return false;
    int named = 0;
    for (const Tac_Type *p = ft->u.fun_type.param_types; p; p = p->next)
        named++;
    return index >= named;
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

// Whether a result of type `t` is written through the address in x8.
static bool indirect_result(const Tac_Type *t)
{
    int esize;
    return t && a64_is_aggregate(t) && a64_size(t) > 16 && !hfa_of(t, &esize);
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
// { __stack, __gr_top, __vr_top, __gr_offs, __vr_offs }; under Apple's ABI the va_list
// is the address of the first variadic argument on the stack.
static void gen_va_start(Gen *g, const Tac_Instruction *in)
{
    if (!g->tl->u.function.variadic)
        fatal_error("aarch64: %s: va_start in a function without ...", gen_name(g));
    if (!in->u.fun_call.args || in->u.fun_call.args->next)
        fatal_error("aarch64: %s: __va_start takes one argument", gen_name(g));
    load_val(g, T0, in->u.fun_call.args);
    if (aarch64_darwin) {
        gen_addr(g, T1, A64_FP, g->va.stack);
        emit2(g, A64_STR, a64_reg(T1, A64_X), a64_mem(T0, 0));
        return;
    }
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

// A move of a value of type `type` between registers of one file, as if at once with
// the others; with `canon`, an integer arrives in the canonical form of its type (a
// parameter: AAPCS64 leaves the upper bits unspecified).
typedef struct {
    int dst, src;
    const Tac_Type *type;
    bool canon;
} Move;

static void emit_move(Gen *g, const Move *m)
{
    if (m->canon && !a64_is_fpreg(m->dst))
        gen_canon(g, m->dst, m->src, m->type);
    else
        move_reg(g, m->dst, m->src, m->type);
}

// Make all moves as if at once: a move goes when no other still reads its
// destination; a cycle is broken through a scratch register.
static void parallel_move(Gen *g, Move *m, int n)
{
    static const Tac_Type wide_int = { .kind = TAC_TYPE_LONG }, wide_fp = { .kind = TAC_TYPE_DOUBLE };
    while (n > 0) {
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++) {
            bool blocked = false;
            for (int j = 0; j < n && !blocked; j++)
                blocked = j != i && m[j].src == m[i].dst;
            if (!blocked)
                pick = i;
        }
        if (pick < 0) {
            bool fp = a64_is_fpreg(m[0].src);
            int tmp = fp ? F0 : T0;
            move_reg(g, tmp, m[0].src, fp ? &wide_fp : &wide_int);
            m[0].src = tmp;
            continue;
        }
        emit_move(g, &m[pick]);
        m[pick] = m[--n];
    }
}

void param_hints(const Gen *g, StringMap *hints)
{
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p && p->type; p = p->next) {
        ArgLoc a = classify(&s, p->type, false);
        if (a.nregs == 1 && !a.by_ref && !a64_is_aggregate(p->type) && !a64_is_ld(p->type))
            map_insert(hints, p->name, a.reg[0], 0);
    }
}

// Each parameter gets a register or a slot: one passed in a register is moved to its
// own, or stored to its slot; one on the stack is loaded, or read where the caller put
// it (above the frame record); one passed by reference is copied in from the caller's
// copy.  The stores come first, then the variadic save areas, then the moves as if at
// once (an allocated register may be another argument register), then the loads.
void gen_params(Gen *g)
{
    Move moves[16];
    int nmoves = 0;
    if (indirect_result(ret_type(g->tl->u.function.type))) {
        g->ret_ptr = alloc_slot(g, NULL, NULL, 8, 8);
        emit2(g, A64_STR, a64_reg(A64_X8, A64_X), mem(g, A64_FP, g->ret_ptr, 8));
    }
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("aarch64: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t, false);
        int preg = assigned_reg(g, p->name);
        if (preg) {
            place_reg(g, p->name, t, preg);
            if (a.nregs && !map_get(&g->dead, p->name, NULL))
                moves[nmoves++] = (Move){ preg, a.reg[0], t, true };
            continue;
        }
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
    if (g->tl->u.function.variadic) {
        if (aarch64_darwin)
            g->va.stack = 16 + round_up(s.stack, 8);
        else
            save_varargs(g, &s);
    }
    parallel_move(g, moves, nmoves);

    s = (ArgState){ 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        ArgLoc a = classify(&s, p->type, false);
        int preg = assigned_reg(g, p->name);
        if (preg && !a.nregs && !map_get(&g->dead, p->name, NULL))
            load_mem(g, preg, p->type, A64_FP, 16 + a.stack);
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

// A move for argument `a` when it is a scalar already in a register, going in one.
static bool arg_move(const Gen *g, const Arg *a, Move *m)
{
    int src = var_reg(g, a->v);
    if (!src || a->loc.by_ref || a->loc.nregs != 1 || a64_is_aggregate(a->type))
        return false;
    *m = (Move){ a->loc.reg[0], src, a->type, false };
    return true;
}

// Load argument `a`'s registers from memory or a constant.
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
    // The callee's address first, in x13: the argument registers are about to change,
    // and nothing that sets up the arguments uses x13.
    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T4, &fp);
    }
    if (!ret && dst)
        ret = val_type(g, dst);
    int nargs = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next)
        nargs++;
    Arg *args  = xalloc((nargs ? nargs : 1) * sizeof(Arg), __func__, __FILE__, __LINE__);
    ArgState s = { 0 };
    int i      = 0;
    // The stack parts and the copies first, through the scratch registers; then the
    // arguments already in registers, moved as if at once; then the rest loaded straight
    // into place.
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        Arg *a  = &args[i];
        a->v    = v;
        a->type = val_type(g, v);
        // A constant takes the declared parameter type, when there is one.
        a->as   = want && !a64_is_fp(a->type) && !a64_is_ld(a->type) && !a64_is_fp(want) &&
                          !a64_is_ld(want) && !a64_is_aggregate(want)
                      ? want
                      : a->type;
        // Placed by its declared type, which a constant may differ from.
        a->loc  = classify(&s, a->as, variadic_arg(ft, i));
        a->copy = 0;
        if (want)
            want = want->next;
        arg_to_stack(g, a);
    }
    Move moves[16];
    int nmoves = 0;
    for (i = 0; i < nargs; i++)
        if (arg_move(g, &args[i], &moves[nmoves]))
            nmoves++;
    parallel_move(g, moves, nmoves);
    for (i = 0; i < nargs; i++) {
        Move m;
        if (!arg_move(g, &args[i], &m))
            arg_to_regs(g, &args[i]);
    }
    uint64_t used = 0;
    for (i = 0; i < nargs; i++)
        for (int k = 0; k < args[i].loc.nregs; k++)
            used |= a64_reg_bit(args[i].loc.reg[k]);
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
        used |= a64_reg_bit(A64_X8);
    }

    A64_Instr *call = in->u.fun_call.indirect ? emit1(g, A64_BLR, a64_reg(T4, A64_X))
                                              : emit1(g, A64_BL, a64_sym(in->u.fun_call.fun_name, 0));
    call->args_known = true;
    call->args       = used;
    if (!dst || indirect_result(ret))
        return;
    const Tac_Type *t = val_type(g, dst);
    int esize;
    int n = hfa_of(t, &esize);
    if (n && a64_is_aggregate(t)) {
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
    if (a64_is_fp(t) || a64_is_ld(t))
        store_val(g, A64_V0, dst);
    else
        store_int(g, A64_X0, dst);
}

void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint)
{
    ArgState s = { 0 };
    int i      = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        int var           = v->kind == TAC_VAL_VAR ? flow_var(f, v->u.var_name) : -1;
        const Tac_Type *t = var >= 0 ? f->types[var] : val_type(g, v);
        if (!t)
            return;
        ArgLoc a = classify(&s, t, variadic_arg(in->u.fun_call.fun_type, i));
        if (var >= 0 && !hint[var] && a.nregs == 1 && !a.by_ref && !a64_is_aggregate(t) &&
            !a64_is_ld(t))
            hint[var] = a.reg[0];
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    int var            = dst ? flow_var(f, dst->u.var_name) : -1;
    const Tac_Type *t  = var >= 0 ? f->types[var] : NULL;
    if (t && !hint[var] && !a64_is_aggregate(t) && !a64_is_ld(t))
        hint[var] = a64_is_fp(t) ? A64_V0 : A64_X0;
}

unsigned result_regs(const Gen *g)
{
    const Tac_Type *rt = ret_type(g->tl->u.function.type);
    int esize;
    if (!rt || rt->kind == TAC_TYPE_VOID || indirect_result(rt))
        return 0;
    int n = hfa_of(rt, &esize);
    if (n && a64_is_aggregate(rt))
        return ((1u << n) - 1) << 2;
    if (a64_is_ld(rt) || a64_is_fp(rt))
        return 1u << 2;
    if (a64_is_aggregate(rt) && a64_size(rt) > 8)
        return 3;
    return 1;
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        int esize;
        int n = hfa_of(t, &esize);
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
            const int regs[2] = { A64_X0, A64_X(1) };
            load_pieces(g, v->u.var_name, t, regs, (a64_size(t) + 7) / 8);
        } else if (a64_is_fp(t)) {
            load_val(g, A64_V0, v);
        } else {
            load_int_as(g, A64_X0, v, rt && !a64_is_fp(rt) ? rt : t);
        }
    }
    gen_epilogue(g);
}
