//
// Calls, parameters and return values: the LP64D calling convention.  A scalar goes
// in the next a/fa register, or on the stack in 8 bytes; a float or double past fa7,
// or a variadic one, goes where an integer would.  A struct of up to 16 bytes that
// flattens to one or two scalars, at least one of them floating, goes in FP registers
// (or an FP and an integer register) when enough are left.  Any other struct of up to
// 16 bytes goes as one or two doublewords (a register each, or the stack; the second
// may follow on the stack when only a7 is left); a larger one by reference.  A long
// double goes like such a struct.  A value of 16 bytes aligned to 16 starts at a
// 16-byte boundary on the stack, and when variadic in an even register.  Return
// values use the same rules with a0/a1 and fa0/fa1.
//
// On rv32 (ILP32D) the same rules hold with 4-byte registers and stack slots: a struct
// of up to 8 bytes goes in registers, a larger one by reference, and a long long goes
// as the long double does on rv64, in two registers aligned like an 8-byte value; so
// does a double where an integer would go.  A long double is 16 bytes, so it goes by
// reference.  A result that would go by reference is written through a hidden pointer
// the caller passes in a0: for a struct wider than 16 bytes the front end adds it (the
// `.ret` parameter), for a long double or a struct of 9 to 16 bytes on rv32 we do.
//
#include <string.h>

#include "codegen.h"
#include "flow.h"
#include "internal.h"
#include "xalloc.h"

// The type of a register holding a word of a long long, in a move.
static const Tac_Type word = { .kind = TAC_TYPE_UINT };

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
        s->stack += riscv_xlen;
    }
}

typedef struct {
    const Tac_Type *type;
    int offset;
} Field;

// Flatten `t` at `off` into scalar fields; false when there would be more than two,
// or one wider than a register of its class, or `t` holds a union.
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
        if (*n == 2 || rv_size(t) > (rv_is_fp(t) ? 8 : riscv_xlen))
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

// A value of two registers, aligned to their size, in integer registers: an even
// register when variadic, a boundary of its size when on the stack.
static void align_pair(ArgState *s, const Tac_Type *t, bool variadic)
{
    int size = 2 * riscv_xlen;
    if (rv_size(t) != size || rv_align(t) != size)
        return;
    if (variadic && s->next_int % 2)
        s->next_int++;
    if (s->next_int == 8)
        s->stack = (s->stack + size - 1) / size * size;
}

static ArgLoc classify(ArgState *s, const Tac_Type *t, bool variadic)
{
    ArgLoc a = { 0 };
    if (rv_is_pair(t)) {
        align_pair(s, t, variadic);
        a.npieces = 2;
        for (int i = 0; i < 2; i++) {
            a.piece[i] = (Piece){ .offset = riscv_xlen * i, .size = riscv_xlen, .type = t };
            take_int(s, &a.piece[i]);
        }
        return a;
    }
    if (!rv_is_aggregate(t) && !rv_is_ld(t)) {
        a.npieces = 1;
        a.piece[0] = (Piece){ .size = rv_size(t), .type = t };
        if (rv_is_fp(t) && !variadic && s->next_fp < 8) {
            a.piece[0].reg = RV_FA0 + s->next_fp++;
        } else if (rv_size(t) > riscv_xlen) {
            // A double on rv32, in integers: as a long long.
            align_pair(s, t, variadic);
            a.npieces = 2;
            for (int i = 0; i < 2; i++) {
                a.piece[i] = (Piece){ .offset = 4 * i, .size = 4, .type = t };
                take_int(s, &a.piece[i]);
            }
        } else {
            take_int(s, &a.piece[0]);
        }
        return a;
    }
    // FP registers first: on rv32 a flattened struct may be wider than two registers.
    int size = rv_size(t), x = riscv_xlen;
    if (!variadic && classify_fp(s, t, &a))
        return a;
    if (size > 2 * x) {
        a.by_ref   = true;
        a.npieces  = 1;
        a.piece[0] = (Piece){ .size = x };
        take_int(s, &a.piece[0]);
        return a;
    }
    align_pair(s, t, variadic);
    a.npieces = size > x ? 2 : 1;
    for (int i = 0; i < a.npieces; i++) {
        a.piece[i] = (Piece){ .offset = x * i, .size = size - x * i < x ? size - x * i : x };
        take_int(s, &a.piece[i]);
    }
    return a;
}

#define is_freg rv_is_freg

// A piece holding all of a scalar is loaded and stored as one; a part of a value, as
// bytes.
static bool whole(const Piece *pc)
{
    return pc->type && rv_size(pc->type) == pc->size;
}

// Load a piece of aggregate `name` into `reg`, or store `reg` into a piece at base + off.
static void load_piece(Gen *g, int reg, const char *name, const Piece *pc)
{
    int base;
    int64_t off;
    name_addr(g, name, RV_T5, &base, &off);
    if (whole(pc))
        load_mem(g, reg, pc->type, base, off + pc->offset);
    else
        load_bytes(g, reg, base, off + pc->offset, pc->size);
}

static void store_piece(Gen *g, int reg, int base, int64_t off, const Piece *pc)
{
    if (whole(pc))
        store_mem(g, reg, pc->type, base, off + pc->offset);
    else
        store_bytes(g, reg, base, off + pc->offset, pc->size);
}

// A double in a pair of integer registers (rv32), not a struct flattened into fields.
static bool split_fp(const ArgLoc *a)
{
    return a->npieces == 2 && a->piece[0].type && rv_is_fp(a->piece[0].type) && !whole(&a->piece[0]);
}

// No fmv.x.d on rv32: a double goes between the register files through memory.
static void check_fp_move(const Gen *g, const Tac_Type *t)
{
    if (riscv_xlen == 4 && rv_is_double(t))
        fatal_error("riscv: %s: fmv of a double on rv32", gen_name(g));
}

// Move an FP value between an FP register and an integer register.
static void fp_to_int(Gen *g, const Tac_Type *t, int ireg, int freg)
{
    check_fp_move(g, t);
    emit2(g, rv_is_double(t) ? RV_FMVXD : RV_FMVXW, rv_reg(ireg), rv_reg(freg));
}

static void int_to_fp(Gen *g, const Tac_Type *t, int freg, int ireg)
{
    check_fp_move(g, t);
    emit2(g, rv_is_double(t) ? RV_FMVDX : RV_FMVWX, rv_reg(freg), rv_reg(ireg));
}

// Bring an integer in `reg`, of type `have`, to the form of type `want` when that is
// narrower than a word: the psABI extends narrow arguments and results by the declared
// type, which a value of another type (a cast removed by copy propagation) may not be.
static void conform(Gen *g, int reg, const Tac_Type *have, const Tac_Type *want)
{
    if (!want || !have || want->kind == have->kind || rv_is_aggregate(want) ||
        want->kind == TAC_TYPE_VOID || want->kind == TAC_TYPE_LONG_DOUBLE || rv_is_fp(want) ||
        rv_size(want) >= 4)
        return;
    gen_canon(g, reg, reg, want);
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

// Where a value of type `t` is returned.
static ArgLoc classify_result(const Tac_Type *t)
{
    ArgState s = { 0 };
    return classify(&s, t, false);
}

// Whether a result of type `t` goes through a hidden pointer that we pass (see above).
static bool hidden_result(const Tac_Type *t)
{
    if (!t || t->kind == TAC_TYPE_VOID || (rv_is_aggregate(t) && rv_size(t) > 16))
        return false;
    return classify_result(t).by_ref;
}

// The argument registers before the first argument of a function returning `ret`.
static ArgState first_arg(const Tac_Type *ret)
{
    return (ArgState){ .next_int = hidden_result(ret) ? 1 : 0 };
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi)
{
    ArgState s = first_arg(ret_type(g->tl->u.function.type));
    for (const Tac_Param *p = g->tl->u.function.params; p && p->type; p = p->next) {
        ArgLoc a = classify(&s, p->type, false);
        if (rv_is_ll(p->type) && rv_is_pair(p->type)) {
            for (int i = 0; i < 2; i++)
                if (a.piece[i].reg >= 0)
                    map_insert(i ? hints_hi : hints, p->name, a.piece[i].reg, 0);
        } else if (!rv_is_aggregate(p->type) && !rv_is_pair(p->type) && a.piece[0].reg >= 0 &&
            is_freg(a.piece[0].reg) == rv_is_fp(p->type))
            map_insert(hints, p->name, a.piece[0].reg, 0);
    }
}

// A move of a value of type `type` between registers, converted to the form of type
// `want` (or NULL) in an integer register.
typedef struct {
    int dst, src;
    const Tac_Type *type, *want;
} Move;

static void emit_move(Gen *g, const Move *m)
{
    if (is_freg(m->dst) && !is_freg(m->src))
        int_to_fp(g, m->type, m->dst, m->src);
    else if (!is_freg(m->dst) && is_freg(m->src))
        fp_to_int(g, m->type, m->dst, m->src);
    else
        move_reg(g, m->dst, m->src, m->type);
    if (!is_freg(m->dst))
        conform(g, m->dst, m->type, m->want);
}

// Make all moves as if at once: a move goes when no other still reads its
// destination; a cycle is broken through a scratch register.
static void parallel_move(Gen *g, Move *m, int n)
{
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
            int tmp = is_freg(m[0].src) ? RV_F0 : RV_T0;
            if (is_freg(tmp))
                emit2(g, RV_FMVD, rv_reg(tmp), rv_reg(m[0].src));
            else
                emit2(g, RV_MV, rv_reg(tmp), rv_reg(m[0].src));
            m[0].src = tmp;
            continue;
        }
        emit_move(g, &m[pick]);
        m[pick] = m[--n];
    }
}

// A variadic function stores a0-a7 just below the incoming stack arguments, so that
// all arguments passed in integer registers or on the stack are contiguous.  A named
// parameter passed so lives there too: va_start steps on from its address.  The
// incoming registers are stored to memory first, then moved to allocated registers,
// which may be other argument registers; parameters on the stack are loaded last.
void gen_params(Gen *g)
{
    Move moves[32];
    int nmoves = 0;
    struct {
        int reg, slot;
    } fp_loads[8]; // doubles that arrived in integer registers, for FP registers
    int nfp_loads = 0;
    bool variadic = gen_variadic(g);
    int x = riscv_xlen;
    if (variadic)
        for (int i = 0; i < 8; i++)
            emit2(g, xlen_store(), rv_reg(RV_A0 + i), rv_mem(RV_S0, x * (i - 8)));
    const Tac_Type *ret = ret_type(g->tl->u.function.type);
    if (hidden_result(ret)) {
        g->ret_ptr = alloc_slot(g, NULL, NULL, x, x);
        emit2(g, xlen_store(), rv_reg(RV_A0), rv_mem(RV_S0, g->ret_ptr));
    }
    ArgState s = first_arg(ret);
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("riscv: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t, false);
        int preg = assigned_reg(g, p->name);
        if (preg && map_get(&g->dead, p->name, NULL)) {
            place_reg(g, p->name, t, preg, assigned_reg_hi(g, p->name));
            continue;
        }
        if (preg && split_fp(&a) && a.piece[0].reg >= 0) {
            // A double in integer registers, for an FP register: through memory.
            int slot = alloc_slot(g, NULL, NULL, 8, 8);
            for (int i = 0; i < 2; i++) {
                const Piece *pc = &a.piece[i];
                int reg         = pc->reg;
                if (reg < 0) {
                    emit2(g, xlen_load(), rv_reg(RV_T0), mem(g, RV_S0, pc->stack));
                    reg = RV_T0;
                }
                store_piece(g, reg, RV_S0, slot, pc);
            }
            place_reg(g, p->name, t, preg, 0);
            fp_loads[nfp_loads].reg    = preg;
            fp_loads[nfp_loads++].slot = slot;
            continue;
        }
        if (preg) {
            // A scalar, into its register (a long long, into two).
            int hi = assigned_reg_hi(g, p->name);
            place_reg(g, p->name, t, preg, hi);
            for (int i = 0; i < a.npieces; i++)
                if (a.piece[i].reg >= 0)
                    moves[nmoves++] = (Move){ i ? hi : preg, a.piece[i].reg, hi ? &word : t, NULL };
            continue;
        }
        if (!a.by_ref && a.piece[0].reg < 0) {
            place_slot(g, p->name, t, a.piece[0].stack); // entirely on the stack
            continue;
        }
        if (variadic && in_int_regs(&a)) {
            place_slot(g, p->name, t, x * (a.piece[0].reg - RV_A0 - 8));
            continue;
        }
        int size = rv_size(t);
        int off  = alloc_slot(g, p->name, t, size, rv_align(t));
        if (a.by_ref) {
            const Piece *pc = &a.piece[0];
            int src         = pc->reg;
            if (src < 0) {
                emit2(g, xlen_load(), rv_reg(RV_T3), mem(g, RV_S0, pc->stack));
                src = RV_T3;
            }
            gen_memcopy(g, RV_S0, off, src, 0, size, rv_align(t));
        } else if (rv_is_aggregate(t) || rv_is_pair(t) || split_fp(&a)) {
            for (int i = 0; i < a.npieces; i++) {
                const Piece *pc = &a.piece[i];
                int reg         = pc->reg;
                if (reg < 0) {
                    emit2(g, xlen_load(), rv_reg(RV_T0), mem(g, RV_S0, pc->stack));
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
    parallel_move(g, moves, nmoves);
    for (int i = 0; i < nfp_loads; i++)
        emit2(g, RV_FLD, rv_reg(fp_loads[i].reg), mem(g, RV_S0, fp_loads[i].slot));

    s = first_arg(ret);
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        ArgLoc a = classify(&s, p->type, false);
        int preg = assigned_reg(g, p->name), hi = assigned_reg_hi(g, p->name);
        if (map_get(&g->dead, p->name, NULL))
            continue;
        if (preg && hi) {
            for (int i = 0; i < 2; i++)
                if (a.piece[i].reg < 0)
                    emit2(g, RV_LW, rv_reg(i ? hi : preg), mem(g, RV_S0, a.piece[i].stack));
        } else if (preg && a.piece[0].reg < 0) {
            load_mem(g, preg, p->type, RV_S0, a.piece[0].stack);
        }
    }
}

// One argument: its value, where it goes, and its declared type (NULL when unknown
// or variadic).
typedef struct {
    const Tac_Val *v;
    const Tac_Type *type, *want;
    ArgLoc loc;
    int copy;     // frame offset of the copy passed by reference, or of a spilled double
    bool spilled; // a double in integer registers, from an FP register: at `copy`
} Arg;

// Half `i` of a double going in integer registers (rv32) into `reg`.
static void double_half(Gen *g, int reg, const Arg *a, int i)
{
    if (a->spilled) {
        emit2(g, RV_LW, rv_reg(reg), mem(g, RV_S0, a->copy + 4 * i));
    } else if (a->v->kind == TAC_VAL_CONSTANT) {
        uint64_t bits;
        memcpy(&bits, &a->v->u.constant->u.double_val, 8);
        gen_li(g, reg, (int32_t)(bits >> (32 * i)));
    } else {
        int base;
        int64_t off;
        name_addr(g, a->v->u.var_name, RV_T5, &base, &off);
        emit2(g, RV_LW, rv_reg(reg), mem(g, base, off + 4 * i));
    }
}

// Put the parts of argument `a` that go on the stack there.  Only reads registers.
static void arg_to_stack(Gen *g, Arg *a)
{
    const Tac_Type *t = a->type;
    if (a->loc.by_ref) {
        int size = rv_size(t);
        a->copy  = alloc_slot(g, NULL, NULL, size, rv_align(t) > 8 ? rv_align(t) : 8);
        if (rv_is_ld(t)) {
            copy_pair(g, a->v, RV_S0, a->copy); // a variable or a constant
        } else {
            int base;
            int64_t off;
            name_addr(g, a->v->u.var_name, RV_T3, &base, &off);
            gen_memcopy(g, RV_S0, a->copy, base, off, size, rv_align(t));
        }
        const Piece *pc = &a->loc.piece[0];
        if (pc->reg < 0) {
            gen_addr(g, RV_T0, RV_S0, a->copy);
            emit2(g, xlen_store(), rv_reg(RV_T0), mem(g, RV_SP, pc->stack));
        }
        return;
    }
    if (split_fp(&a->loc) && var_reg(g, a->v)) {
        // Before the moves may overwrite its register.
        a->copy    = alloc_slot(g, NULL, NULL, 8, 8);
        a->spilled = true;
        store_mem(g, var_reg(g, a->v), t, RV_S0, a->copy);
    }
    for (int i = 0; i < a->loc.npieces; i++) {
        const Piece *pc = &a->loc.piece[i];
        if (pc->reg >= 0)
            continue;
        if (rv_is_pair(t)) {
            pair_half(g, RV_T0, a->v, i);
        } else if (split_fp(&a->loc)) {
            double_half(g, RV_T0, a, i);
        } else if (rv_is_aggregate(t)) {
            load_piece(g, RV_T0, a->v->u.var_name, pc);
        } else if (rv_is_fp(t)) {
            fp_to_int(g, t, RV_T0, use_val(g, RV_F0, a->v));
        } else {
            load_val(g, RV_T0, a->v);
            conform(g, RV_T0, t, a->want);
        }
        emit2(g, xlen_store(), rv_reg(RV_T0), mem(g, RV_SP, pc->stack));
    }
}

// A scalar argument in a register, already in a register: a move.
// Moves for the parts of argument `a` in a register, already in registers (both words
// of a long long); returns their number, 0 when none.
static int arg_moves(const Gen *g, const Arg *a, Move *m)
{
    int src = var_reg(g, a->v), hi = var_reg_hi(g, a->v);
    if (!src || a->loc.by_ref || rv_is_aggregate(a->type))
        return 0;
    if (hi) {
        int n = 0;
        for (int i = 0; i < 2; i++)
            if (a->loc.piece[i].reg >= 0)
                m[n++] = (Move){ a->loc.piece[i].reg, i ? hi : src, &word, NULL };
        return n;
    }
    if (a->loc.piece[0].reg < 0 || a->loc.npieces != 1)
        return 0;
    *m = (Move){ a->loc.piece[0].reg, src, a->type, a->want };
    return 1;
}

// Load argument `a`'s register parts from memory or a constant.
static void arg_to_regs(Gen *g, const Arg *a)
{
    const Tac_Type *t = a->type;
    if (a->loc.by_ref) {
        if (a->loc.piece[0].reg >= 0)
            gen_addr(g, a->loc.piece[0].reg, RV_S0, a->copy);
        return;
    }
    for (int i = 0; i < a->loc.npieces; i++) {
        const Piece *pc = &a->loc.piece[i];
        int reg         = pc->reg;
        if (reg < 0)
            continue;
        if (rv_is_pair(t)) {
            pair_half(g, reg, a->v, i);
        } else if (split_fp(&a->loc)) {
            double_half(g, reg, a, i);
        } else if (rv_is_aggregate(t)) {
            load_piece(g, reg, a->v->u.var_name, pc);
        } else if (rv_is_fp(t) && !is_freg(reg)) {
            fp_to_int(g, t, reg, use_val(g, RV_F0, a->v));
        } else {
            load_val(g, reg, a->v);
            if (!is_freg(reg))
                conform(g, reg, t, a->want);
        }
    }
}

void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    int nfixed         = 0;
    if (ft)
        for (const Tac_Type *p = ft->u.fun_type.param_types; p; p = p->next)
            nfixed++;
    ArgState s = first_arg(ret_type(ft));
    int i      = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        int var           = v->kind == TAC_VAL_VAR ? flow_var(f, v->u.var_name) : -1;
        const Tac_Type *t = var >= 0 ? f->types[var] : val_type(g, v);
        if (!t)
            return;
        ArgLoc a = classify(&s, t, ft && ft->u.fun_type.variadic && i >= nfixed);
        if (var >= 0 && rv_is_ll(t) && rv_is_pair(t)) {
            for (int k = 0; k < 2; k++)
                if (!hint[var + k * f->nvars] && a.piece[k].reg >= 0)
                    hint[var + k * f->nvars] = a.piece[k].reg;
        } else if (var >= 0 && !hint[var] && !rv_is_aggregate(t) && !rv_is_pair(t) && a.piece[0].reg >= 0 &&
            is_freg(a.piece[0].reg) == rv_is_fp(t))
            hint[var] = a.piece[0].reg;
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    int var            = dst ? flow_var(f, dst->u.var_name) : -1;
    if (var >= 0 && f->types[var] && rv_is_ll(f->types[var]) && rv_is_pair(f->types[var])) {
        if (!hint[var])
            hint[var] = RV_A0;
        if (!hint[var + f->nvars])
            hint[var + f->nvars] = RV_A0 + 1;
    } else if (var >= 0 && !hint[var] && f->types[var] && !rv_is_aggregate(f->types[var]) &&
        !rv_is_pair(f->types[var]) && !rv_is_ld(f->types[var]))
        hint[var] = rv_is_fp(f->types[var]) ? RV_FA0 : RV_A0;
}

// Store a value returned as type `ret` into `dst`.
static void store_result(Gen *g, const Tac_Val *dst, const Tac_Type *ret)
{
    const Tac_Type *t = val_type(g, dst);
    if (var_reg_hi(g, dst)) {
        set_pair(g, dst, RV_A0, RV_A0 + 1);
        return;
    }
    if (!rv_is_aggregate(t) && !rv_is_pair(t)) {
        if (!rv_is_fp(t))
            conform(g, RV_A0, ret, t);
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

    // The callee's address first: an argument register holding it may be overwritten.
    int fpreg = 0;
    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        fpreg      = use_val(g, RV_T1, &fp);
        if (fpreg >= RV_A0 && fpreg <= RV_A7) {
            emit2(g, RV_MV, rv_reg(RV_T1), rv_reg(fpreg));
            fpreg = RV_T1;
        }
    }

    // Arguments may already be in argument registers: the stack parts first, then the
    // register-to-register moves at once, then loads from memory.
    int nargs = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next)
        nargs++;
    Arg *args            = xalloc((nargs ? nargs : 1) * sizeof(Arg), __func__, __FILE__, __LINE__);
    Move moves[32];
    int nmoves           = 0;
    const Tac_Type *ret  = ret_type(ft);
    if (!ret && in->u.fun_call.dst)
        ret = val_type(g, in->u.fun_call.dst);
    bool hidden          = hidden_result(ret);
    ArgState s           = first_arg(ret);
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    int i                = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next, i++) {
        Arg *a  = &args[i];
        a->v    = v;
        a->type = val_type(g, v);
        a->want = want;
        a->loc     = classify(&s, a->type, variadic && i >= nfixed);
        a->copy    = 0;
        a->spilled = false;
        if (want)
            want = want->next;
        arg_to_stack(g, a);
    }
    for (i = 0; i < nargs; i++)
        nmoves += arg_moves(g, &args[i], &moves[nmoves]);
    parallel_move(g, moves, nmoves);
    for (i = 0; i < nargs; i++) {
        Move m[2];
        if (!arg_moves(g, &args[i], m))
            arg_to_regs(g, &args[i]);
    }
    xfree(args);
    if (s.stack > g->outgoing)
        g->outgoing = s.stack;
    if (hidden) {
        // The result's address in a0: the destination, or a slot for an unused one.
        int base;
        int64_t off;
        if (in->u.fun_call.dst) {
            name_addr(g, in->u.fun_call.dst->u.var_name, RV_A0, &base, &off);
        } else {
            base = RV_S0;
            off  = alloc_slot(g, NULL, NULL, rv_size(ret), rv_align(ret));
        }
        gen_addr(g, RV_A0, base, off);
    }

    if (in->u.fun_call.indirect) {
        rv_append(g->fn, RV_JALR)->opnd[0] = rv_reg(fpreg);
    } else {
        rv_append(g->fn, RV_CALL)->opnd[0] = rv_sym(in->u.fun_call.fun_name, 0);
    }
    if (in->u.fun_call.dst && !hidden)
        store_result(g, in->u.fun_call.dst, ret_type(ft));
}

void gen_runtime_call(Gen *g, const char *name, const Tac_Type *ret, const Tac_Val *const *args,
                      const Tac_Type *const *types, int nargs, const Tac_Val *dst)
{
    Tac_Type params[4];
    Tac_Val vals[4];
    Tac_Const consts[4];
    if (nargs > 4)
        fatal_error("riscv: %s: runtime call with %d arguments", gen_name(g), nargs);
    for (int i = 0; i < nargs; i++) {
        params[i]      = types ? *types[i] : *val_type(g, args[i]);
        params[i].next = i + 1 < nargs ? &params[i + 1] : NULL;
        vals[i]        = *args[i];
        vals[i].next   = i + 1 < nargs ? &vals[i + 1] : NULL;
        if (types && rv_is_ll(types[i]) && args[i]->kind == TAC_VAL_CONSTANT &&
            !rv_is_ll(val_type(g, args[i]))) {
            // A narrower constant, as the long long the routine takes.
            consts[i]                 = (Tac_Const){ .kind = TAC_CONST_LONG_LONG };
            consts[i].u.long_long_val = const_value(g, args[i]->u.constant);
            vals[i].u.constant        = &consts[i];
        }
    }
    Tac_Type fun                 = { .kind = TAC_TYPE_FUN_TYPE };
    fun.u.fun_type.param_types   = nargs ? params : NULL;
    fun.u.fun_type.ret_type      = (Tac_Type *)ret;
    Tac_Instruction call         = { .kind = TAC_INSTRUCTION_FUN_CALL };
    call.u.fun_call.fun_name     = (char *)name;
    call.u.fun_call.args         = nargs ? vals : NULL;
    call.u.fun_call.dst          = (Tac_Val *)dst;
    call.u.fun_call.fun_type     = &fun;
    gen_call(g, &call);
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v && g->ret_ptr) {
        // Through the hidden pointer.
        const Tac_Type *t = val_type(g, v);
        emit2(g, xlen_load(), rv_reg(RV_T4), mem(g, RV_S0, g->ret_ptr));
        if (rv_is_ld(t)) {
            copy_pair(g, v, RV_T4, 0);
        } else {
            int base;
            int64_t off;
            name_addr(g, v->u.var_name, RV_T3, &base, &off);
            gen_memcopy(g, RV_T4, 0, base, off, rv_size(t), rv_align(t));
        }
    } else if (v) {
        const Tac_Type *t = val_type(g, v);
        if (var_reg_hi(g, v)) {
            Move m[2] = { { RV_A0, var_reg(g, v), &word, NULL },
                          { RV_A0 + 1, var_reg_hi(g, v), &word, NULL } };
            parallel_move(g, m, 2);
        } else if (rv_is_pair(t)) {
            pair_half(g, RV_A0, v, 0);
            pair_half(g, RV_A0 + 1, v, 1);
        } else if (rv_is_aggregate(t)) {
            ArgLoc a = classify_result(t);
            for (int i = 0; i < a.npieces; i++)
                load_piece(g, a.piece[i].reg, v->u.var_name, &a.piece[i]);
        } else if (rv_is_fp(t)) {
            load_val(g, RV_FA0, v);
        } else {
            const Tac_Type *ft = g->tl->u.function.type;
            load_val(g, RV_A0, v);
            conform(g, RV_A0, t, ft ? ft->u.fun_type.ret_type : NULL);
        }
    }
    gen_epilogue(g);
}
