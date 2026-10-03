//
// Calls, parameters and return values: AAPCS-VFP.  An integer or pointer goes in the
// next of r0-r3, a long long in the next even pair; once the core registers run out,
// those values go on the stack, and no later one takes a core register.  A float goes
// in the lowest free s register of s0-s15, a double in the lowest free even pair (d0-
// d7), so a float may fill the gap a double's alignment left; once one goes on the
// stack, no later FP value takes a VFP register.  A stack value takes 4 bytes, 8 for
// one of 8-byte alignment, which then is 8-aligned.  The sender extends a narrow value
// to 32 bits.  A variadic function takes all its arguments, the named ones too, under
// the base standard: a float as an integer, a double as a long long; and so returns
// its result, a float in r0, a double in r0:r1.  It saves r0-r3 below its stack
// arguments (gen_prologue), so they are read in place, all in one area.
//
// A structure or union goes by value in the next core registers, a word each, from
// an even one when it is 8-byte aligned; one that does not fit is split between the
// last of them and the stack while nothing is on the stack yet, else goes wholly on the
// stack.  A result of up to 4 bytes comes back in r0; a larger one is written to the
// address the caller passes in r0, ahead of the arguments.
//
// A homogeneous FP aggregate (1-4 members of one FP type, tac_aapcs32_class) goes, as
// an FP scalar does, in the lowest run of free consecutive s registers (d registers
// for doubles) that holds it, else on the stack, closing the VFP registers; it comes
// back in s0-s3 (d0-d3).  Under the base standard it is an ordinary aggregate.
//
#include <string.h>

#include "codegen.h"
#include "flow.h"
#include "internal.h"
#include "xalloc.h"

// Where one argument goes.
typedef struct {
    int nregs; // the core registers (1-4) or VFP elements (1-4); 0 for the stack
    int reg;   // the first register
    int esize; // the bytes of a VFP element (4 or 8); 0 for core registers
    int stack; // byte offset in the argument area of what is on the stack
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

// The AAPCS class of `t`: an FP scalar or a homogeneous FP aggregate in VFP registers
// under the VFP variant, else TAC_AAPCS32_CORE.
static int vfp_class(const Tac_Type *t, bool vfp)
{
    return vfp ? tac_aapcs32_class(t) : TAC_AAPCS32_CORE;
}

static ArgLoc classify(ArgState *s, const Tac_Type *t)
{
    ArgLoc a = { 0 };
    int size = a32_size(t);
    int cls  = vfp_class(t, s->vfp);
    if (cls != TAC_AAPCS32_CORE) {
        // The lowest free run of singles, or of even-aligned pairs of them.
        int count = cls % 8, esize = cls / 8, step = esize / 4;
        unsigned want = (1u << (count * step)) - 1;
        for (int i = 0; i + count * step <= 16; i += step) {
            if ((s->sfree >> i & want) == want) {
                s->sfree &= ~(want << i);
                a.nregs = count;
                a.reg   = A32_SREG(i);
                a.esize = esize;
                return a;
            }
        }
        s->sfree = 0;
        on_stack(s, &a, size, a32_align(t));
        return a;
    }
    if (a32_is_aggregate(t)) {
        int n = (size + 3) / 4;
        if (a32_align(t) >= 8 && s->next_core % 2)
            s->next_core++;
        if (s->next_core + n <= 4) {
            a.nregs = n;
            a.reg   = A32_R0 + s->next_core;
            s->next_core += n;
        } else if (s->next_core < 4 && s->stack == 0) {
            a.nregs      = 4 - s->next_core;
            a.reg        = A32_R0 + s->next_core;
            a.stack      = 0;
            s->stack     = round_up(size - 4 * a.nregs, 4);
            s->next_core = 4;
        } else {
            s->next_core = 4;
            on_stack(s, &a, size, a32_align(t));
        }
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

// Whether a result of type `t` is written through the address passed in r0.
static bool indirect_result(const Tac_Type *t, bool vfp)
{
    return t && a32_is_aggregate(t) && a32_size(t) > 4 &&
           vfp_class(t, vfp) == TAC_AAPCS32_CORE;
}

// Whether `t` is an aggregate in VFP registers, as an argument or a result.
static bool vfp_aggregate(const Tac_Type *t, bool vfp)
{
    return a32_is_aggregate(t) && vfp_class(t, vfp) != TAC_AAPCS32_CORE;
}

// The type of a VFP element of `esize` bytes.
static const Tac_Type *elem_type(int esize)
{
    static const Tac_Type f = { .kind = TAC_TYPE_FLOAT }, d = { .kind = TAC_TYPE_DOUBLE };
    return esize == 4 ? &f : &d;
}

// The VFP register of element `k` of an aggregate starting at `reg`.
static int elem_reg(int reg, int esize, int k)
{
    return reg + k * esize / 4;
}

// The element size of homogeneous FP aggregate `t`.
static int elem_size(const Tac_Type *t)
{
    return tac_aapcs32_class(t) / 8;
}

// Load `size` (1..4) bytes at base + off into core register `reg`: a word at once,
// fewer byte by byte (an object may end there), r12 the temporary.  Or store them.
static void load_piece(Gen *g, int reg, int base, int64_t off, int size)
{
    if (size == 4) {
        emit2(g, A32_LDR, a32_reg(reg), mem(g, A32_LDR, base, off, reg));
        return;
    }
    emit2(g, A32_LDRB, a32_reg(reg), a32_mem(base, off + size - 1));
    for (int i = size - 2; i >= 0; i--) {
        emit2(g, A32_LDRB, a32_reg(T0), a32_mem(base, off + i));
        emit3(g, A32_ORR, a32_reg(reg), a32_reg(T0), a32_shift(reg, A32_SHIFT_LSL, 8));
    }
}

static void store_piece(Gen *g, int reg, int base, int64_t off, int size)
{
    if (size == 4) {
        emit2(g, A32_STR, a32_reg(reg), mem(g, A32_STR, base, off, T0));
        return;
    }
    emit2(g, A32_STRB, a32_reg(reg), a32_mem(base, off));
    for (int i = 1; i < size; i++) {
        emit3(g, A32_LSR, a32_reg(T0), a32_reg(reg), a32_imm(8 * i));
        emit2(g, A32_STRB, a32_reg(T0), a32_mem(base, off + i));
    }
}

// The bytes of word `i` of an aggregate of `size` bytes.
static int piece_size(int size, int i)
{
    return size - 4 * i < 4 ? size - 4 * i : 4;
}

// The address of variable `name` + `off`, in a register (`scratch`, or r11 for a near
// slot): an aggregate's pieces are addressed from it.
static int piece_base(Gen *g, const char *name, int scratch, int64_t *off)
{
    int base;
    name_addr(g, name, scratch, &base, off);
    if (*off < -4000 || *off > 4000) {
        gen_addr(g, scratch, base, *off);
        base = scratch;
        *off = 0;
    }
    return base;
}

static bool is_variadic(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE && fun_type->u.fun_type.variadic;
}

// The move of a scalar of type `t` from argument register `from` (a pair from `from`
// and the next) to its allocated register `reg` (`hi`); returns how many.
static int param_moves(Move *m, const Tac_Type *t, int from, int reg, int hi)
{
    if (hi >= 0) {
        m[0] = (Move){ reg, from, MOVE_CORE };
        m[1] = (Move){ hi, from + 1, MOVE_CORE };
        return 2;
    }
    if (a32_is_fp(t))
        m[0] = (Move){ reg, from, a32_is_double(t) ? MOVE_D : MOVE_S };
    else
        m[0] = (Move){ reg, from, MOVE_CORE };
    return 1;
}

void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi)
{
    if (g->tl->u.function.variadic)
        return;
    ArgState s = arg_state(true);
    if (indirect_result(ret_type(g->tl->u.function.type), true))
        s.next_core = 1;
    for (const Tac_Param *p = g->tl->u.function.params; p && p->type; p = p->next) {
        ArgLoc a = classify(&s, p->type);
        if (!a.nregs || a32_is_aggregate(p->type))
            continue;
        map_insert(hints, p->name, a.reg + 1, 0);
        if (a.nregs == 2 && !a.esize)
            map_insert(hints_hi, p->name, a.reg + 2, 0);
    }
}

// Each parameter gets a register or a slot.  One passed in registers is stored to its
// slot, or moved to its register; one on the stack is read where the caller put it,
// above the frame record, or loaded.  A variadic function's are all in the area of its
// saved r0-r3 and its stack arguments.  The stores come first, then the moves as if
// at once (an allocated register may be another argument register), then the loads.
void gen_params(Gen *g)
{
    bool variadic = g->tl->u.function.variadic;
    ArgState s    = arg_state(!variadic);
    Move moves[32];
    int nmoves = 0;
    if (indirect_result(ret_type(g->tl->u.function.type), s.vfp)) {
        if (variadic) {
            g->ret_ptr = 8;
        } else {
            g->ret_ptr = alloc_slot(g, NULL, NULL, 4, 4);
            emit2(g, A32_STR, a32_reg(A32_R0), mem(g, A32_STR, FB, g->ret_ptr, T0));
        }
        s.next_core = 1;
    }
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("arm32: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t);
        int hi, reg = assigned_reg(g, p->name, &hi);
        if (reg >= 0) {
            place_reg(g, p->name, t, reg, hi);
            if (a.nregs && !variadic)
                nmoves += param_moves(moves + nmoves, t, a.reg, reg, hi);
            continue;
        }
        if (variadic) {
            place_slot(g, p->name, t, 8 + (a.nregs ? 4 * (a.reg - A32_R0) : 16 + a.stack));
            continue;
        }
        if (!a.nregs) {
            place_slot(g, p->name, t, 8 + a.stack);
            continue;
        }
        if (a.esize && a32_is_aggregate(t)) {
            int off = alloc_slot(g, p->name, t, a32_size(t), a32_align(t));
            for (int k = 0; k < a.nregs; k++)
                store_mem(g, elem_reg(a.reg, a.esize, k), elem_type(a.esize), FB,
                          off + k * a.esize, T0);
            continue;
        }
        if (a32_is_aggregate(t)) {
            // Its words stored, then any part the caller put on the stack copied after.
            int size = a32_size(t);
            int off  = alloc_slot(g, p->name, t, round_up(size, 4), a32_align(t));
            for (int i = 0; i < a.nregs; i++)
                emit2(g, A32_STR, a32_reg(a.reg + i), mem(g, A32_STR, FB, off + 4 * i, T0));
            if (size > 4 * a.nregs)
                gen_memcopy(g, FB, off + 4 * a.nregs, FB, 8 + a.stack, size - 4 * a.nregs,
                            4);
            continue;
        }
        int off = alloc_slot(g, p->name, t, a32_size(t), a32_align(t));
        if (a.nregs == 2) {
            emit2(g, A32_STR, a32_reg(a.reg), mem(g, A32_STR, FB, off, T0));
            emit2(g, A32_STR, a32_reg(a.reg + 1), mem(g, A32_STR, FB, off + 4, T0));
        } else {
            store_mem(g, a.reg, t, FB, off, T0);
        }
    }
    parallel_move(g, moves, nmoves);

    s = arg_state(!variadic);
    if (indirect_result(ret_type(g->tl->u.function.type), s.vfp))
        s.next_core = 1;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        ArgLoc a = classify(&s, p->type);
        int hi, reg = assigned_reg(g, p->name, &hi);
        if (reg < 0 || (a.nregs && !variadic))
            continue;
        int off = 8 + (!variadic ? a.stack : a.nregs ? 4 * (a.reg - A32_R0) : 16 + a.stack);
        if (hi >= 0) {
            emit2(g, A32_LDR, a32_reg(reg), mem(g, A32_LDR, FB, off, reg));
            emit2(g, A32_LDR, a32_reg(hi), mem(g, A32_LDR, FB, off + 4, hi));
        } else {
            load_mem(g, reg, p->type, FB, off);
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
    if (l->esize && a32_is_aggregate(a->as)) {
        int64_t off;
        int base = piece_base(g, a->v->u.var_name, T1, &off);
        for (int k = 0; k < l->nregs; k++)
            load_mem(g, elem_reg(l->reg, l->esize, k), elem_type(l->esize), base,
                     off + k * l->esize);
    } else if (a32_is_aggregate(a->as)) {
        int64_t off;
        int base = piece_base(g, a->v->u.var_name, T1, &off);
        for (int i = 0; i < l->nregs; i++)
            load_piece(g, l->reg + i, base, off + 4 * i, piece_size(a32_size(a->as), i));
    } else if (a32_is_vfp(l->reg))
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
    if (a32_is_aggregate(a->as)) {
        // What does not go in registers.
        int size = a32_size(a->as), skip = 4 * a->loc.nregs;
        int base;
        int64_t soff;
        name_addr(g, a->v->u.var_name, T1, &base, &soff);
        gen_memcopy(g, A32_SP, off, base, soff + skip, size - skip, skip ? 4 : a32_align(a->as));
        return;
    }
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

// The moves for argument `a` when it is a scalar in registers, going in registers;
// returns how many (0: it is loaded instead).
static int arg_moves(const Gen *g, const Arg *a, Move *m)
{
    const ArgLoc *l = &a->loc;
    int r           = var_reg(g, a->v);
    if (r < 0 || !l->nregs || a32_is_aggregate(a->as))
        return 0;
    const Tac_Type *t = val_type(g, a->v);
    int hi            = var_reg_hi(g, a->v);
    if (a32_is_vfp(l->reg)) {
        m[0] = (Move){ l->reg, r, a32_is_double(t) ? MOVE_D : MOVE_S };
        return 1;
    }
    if (l->nregs == 1) {
        m[0] = (Move){ l->reg, r, a32_is_vfp(r) ? MOVE_S_TO_CORE : MOVE_CORE };
        return 1;
    }
    if (hi >= 0) {
        m[0] = (Move){ l->reg, r, MOVE_CORE };
        m[1] = (Move){ l->reg + 1, hi, MOVE_CORE };
        return 2;
    }
    if (a32_is_vfp(r)) { // a double under the base standard
        m[0] = (Move){ l->reg, r, MOVE_S_TO_CORE };
        m[1] = (Move){ l->reg + 1, r + 1, MOVE_S_TO_CORE };
        return 2;
    }
    // An int widened to a long long: its high word the sign, or zero (loaded after).
    m[0] = (Move){ l->reg, r, MOVE_CORE };
    if (a32_is_unsigned(t))
        return 1;
    m[1] = (Move){ l->reg + 1, r, MOVE_HI_SIGN };
    return 2;
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

void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    const Tac_Type *ret  = ret_type(ft);
    bool vfp             = !is_variadic(ft);
    ArgState s           = arg_state(vfp);
    const Tac_Val *dst   = in->u.fun_call.dst;
    int dvar             = dst ? flow_var(f, dst->u.var_name) : -1;
    if (!ret && dvar >= 0)
        ret = f->types[dvar];
    if (indirect_result(ret, vfp))
        s.next_core = 1;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next) {
        int var           = v->kind == TAC_VAL_VAR ? flow_var(f, v->u.var_name) : -1;
        const Tac_Type *t = var >= 0 ? f->types[var] : val_type(g, v);
        if (!t)
            return;
        if (want && !a32_is_aggregate(want) && a32_is_fp(want) == a32_is_fp(t) &&
            a32_is_pair(want) == a32_is_pair(t))
            t = want;
        if (want)
            want = want->next;
        ArgLoc a = classify(&s, t);
        if (var < 0 || !a.nregs || a32_is_aggregate(t) || a32_is_fp(t) != a32_is_vfp(a.reg))
            continue;
        if (!hint[var])
            hint[var] = a.reg + 1;
        if (a.nregs == 2 && !hint[var + f->nvars])
            hint[var + f->nvars] = a.reg + 2;
    }
    const Tac_Type *t = dvar >= 0 ? f->types[dvar] : NULL;
    if (!t || a32_is_aggregate(t) || (a32_is_fp(t) && !vfp))
        return;
    if (!hint[dvar])
        hint[dvar] = (a32_is_fp(t) ? A32_S0 : A32_R0) + 1;
    if (a32_is_pair(t) && !hint[dvar + f->nvars])
        hint[dvar + f->nvars] = A32_R0 + 2;
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    const Tac_Val *dst   = in->u.fun_call.dst;
    const Tac_Type *ret  = ret_type(ft);
    bool vfp             = !is_variadic(ft);
    int nargs            = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next)
        nargs++;
    Arg *args  = xalloc((nargs ? nargs : 1) * sizeof(Arg), __func__, __FILE__, __LINE__);
    ArgState s = arg_state(vfp);
    int i      = 0;
    if (!ret && dst)
        ret = val_type(g, dst);
    if (indirect_result(ret, vfp))
        s.next_core = 1;
    // The stack arguments first, through the scratch registers; then the registers,
    // each loaded straight into place.
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        Arg *a = &args[i];
        a->v   = v;
        a->as  = arg_type(g, v, want);
        a->loc = classify(&s, a->as);
        if (want)
            want = want->next;
        if (!a->loc.nregs ||
            (!a->loc.esize && a32_is_aggregate(a->as) && 4 * a->loc.nregs < a32_size(a->as)))
            store_arg(g, a);
    }
    // The callee's address, when in an argument register, goes to r10 with the moves:
    // nothing that loads the argument registers uses r10.
    Move moves[32];
    int nmoves = 0, fn_reg = -1;
    if (in->u.fun_call.indirect) {
        Tac_Val fv = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        fn_reg     = var_reg(g, &fv);
        if (fn_reg >= A32_R0 && fn_reg < A32_R4) {
            moves[nmoves++] = (Move){ T2, fn_reg, MOVE_CORE };
            fn_reg          = T2;
        }
    }
    for (i = 0; i < nargs; i++)
        nmoves += arg_moves(g, &args[i], moves + nmoves);
    parallel_move(g, moves, nmoves);
    for (i = 0; i < nargs; i++) {
        Move m[2];
        int k = args[i].loc.nregs ? arg_moves(g, &args[i], m) : -1;
        if (k == 0)
            load_arg_regs(g, &args[i]);
        else if (k == 1 && args[i].loc.nregs == 2)
            gen_li(g, args[i].loc.reg + 1, 0); // an unsigned int widened
    }
    xfree(args);
    if (s.stack > g->outgoing)
        g->outgoing = s.stack;
    if (indirect_result(ret, vfp)) {
        // The result's address in r0: the destination, or a slot for an unused one.
        int base;
        int64_t off;
        if (dst) {
            name_addr(g, dst->u.var_name, A32_R0, &base, &off);
        } else {
            base = FB;
            off  = alloc_slot(g, NULL, NULL, a32_size(ret), a32_align(ret));
        }
        gen_addr(g, A32_R0, base, off);
    }
    if (in->u.fun_call.indirect && fn_reg >= 0) {
        emit1(g, A32_BLX, a32_reg(fn_reg));
    } else if (in->u.fun_call.indirect) {
        // From memory, the callee's address last: loading an FP argument goes through r12.
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T0, &fp);
        emit1(g, A32_BLX, a32_reg(T0));
    } else {
        emit1(g, A32_BL, a32_sym(in->u.fun_call.fun_name, 0));
    }
    if (!dst || indirect_result(ret, vfp))
        return;
    const Tac_Type *t = val_type(g, dst);
    if (vfp_aggregate(t, vfp)) {
        int64_t off;
        int base = piece_base(g, dst->u.var_name, T1, &off);
        int esize = elem_size(t);
        for (int k = 0; k < a32_size(t) / esize; k++)
            store_mem(g, elem_reg(A32_S0, esize, k), elem_type(esize), base, off + k * esize,
                      T0);
        return;
    }
    if (a32_is_aggregate(t)) {
        int64_t off;
        int base = piece_base(g, dst->u.var_name, T1, &off);
        store_piece(g, A32_R0, base, off, a32_size(t));
        return;
    }
    if (a32_is_fp(t) && vfp)
        store_val(g, A32_S0, dst);
    else if (a32_size(t) == 8)
        store_pair(g, dst, A32_R0, A32_R0 + 1);
    else
        store_val(g, A32_R0, dst);
}

uint64_t result_regs(const Gen *g)
{
    const Tac_Type *rt = ret_type(g->tl->u.function.type);
    bool vfp           = !g->tl->u.function.variadic;
    if (!rt || rt->kind == TAC_TYPE_VOID)
        return 0;
    if (vfp_aggregate(rt, vfp))
        return ((1ull << a32_size(rt) / 4) - 1) << A32_S0;
    if (a32_is_fp(rt) && vfp)
        return (a32_is_double(rt) ? 3ull : 1ull) << A32_S0;
    if (!a32_is_aggregate(rt) && a32_size(rt) == 8)
        return 3;
    return 1; // r0: a scalar, a small aggregate, or the result's address
}

// An integer in r0 (the callee extends a narrow one), a long long in r0:r1, a float in
// s0, a double in d0 (a constant's bits through r0 and r1, which need no frame); an
// aggregate as the comment at the top says.
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        bool vfp           = !g->tl->u.function.variadic;
        if (!rt || rt->kind == TAC_TYPE_VOID)
            rt = t;
        if (vfp_aggregate(rt, vfp)) {
            int64_t off;
            int base  = piece_base(g, v->u.var_name, T1, &off);
            int esize = elem_size(rt);
            for (int k = 0; k < a32_size(rt) / esize; k++)
                load_mem(g, elem_reg(A32_S0, esize, k), elem_type(esize), base, off + k * esize);
        } else if (a32_is_aggregate(rt) && g->ret_ptr) {
            int base;
            int64_t off;
            emit2(g, A32_LDR, a32_reg(T1), mem(g, A32_LDR, FB, g->ret_ptr, T1));
            name_addr(g, v->u.var_name, T2, &base, &off);
            gen_memcopy(g, T1, 0, base, off, a32_size(rt), a32_align(rt));
        } else if (a32_is_aggregate(rt)) {
            int64_t off;
            int base = piece_base(g, v->u.var_name, T1, &off);
            load_piece(g, A32_R0, base, off, a32_size(rt));
        } else if (a32_is_fp(rt) && vfp && v->kind == TAC_VAL_CONSTANT)
            load_fp_const(g, A32_S0, v->u.constant, rt, A32_R0, A32_R0 + 1);
        else if (a32_is_fp(rt) && vfp)
            load_val(g, A32_S0, v);
        else if (a32_size(rt) == 8) {
            WordLoad w[2] = { { A32_R0, v, rt, 0 }, { A32_R0 + 1, v, rt, 1 } };
            load_words(g, w, 2);
        } else {
            load_as(g, A32_R0, v, rt);
        }
    }
    gen_epilogue(g);
}
