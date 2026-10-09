//
// Peephole pass over the ARM32 IR, after register allocation and the frame:
//   - a move folds into its uses within the block, a result is computed where it is
//     moved, a move to itself goes, and so does the reload of what was just stored (or
//     it becomes a move);
//   - an address computation folds into the load or store it feeds ([base, #imm] or
//     [base, index, lsl #s]), a shift by a constant into the operand2 it feeds, a mask
//     into a tst, mul + add into mla (mul + sub, mls);
//   - the shifts and masks of a bit-field are ubfx/sbfx (a read), bfi (a store) and bfc
//     (a store of zero), the movw/movt of the masks gone with them, and a uxtb/uxth
//     before a strb/strh goes;
//   - a jump to the next label goes, a branch over a jump branches the other way, code
//     after a jump or return goes;
//   - a short diamond or triangle of a conditional branch becomes conditional
//     instructions, when they set no flags and make no call;
//   - adjacent ldr/str of a frame slot pair, of an even register and the next, are
//     ldrd/strd, last;
//   - and a frame of only `push {lr}`, for an lr the rewrites no longer use, goes.
// Whether a value is read again is decided by the liveness of the registers over the
// function's blocks, computed afresh after each round of rewrites.  Registers are
// tracked as sets, s registers apart, so a d register is the two s registers it holds.
// A conditional instruction may leave its destination as it was: it reads it too.
//
#include <string.h>

#include "bitops.h"
#include "internal.h"
#include "xalloc.h"

// A set of registers: bit r for r0-r15, bit 16 + k for s<k>.
typedef uint64_t Regs;

#define BIT(r)    (1ull << (r))
#define CORE_ARGS 0xfull                     // r0-r3
#define VFP_ARGS  (0xffffull << A32_S0)      // s0-s15
#define SCRATCH   (BIT(T0) | BIT(T1) | BIT(T2) | 0xfull << F0)

static Regs reg_set(int reg, A32_Width w)
{
    if (reg < 0 || reg >= A32_VREG)
        return 0;
    return (w == A32_D ? 3ull : 1ull) << reg;
}

static Regs list_set(const A32_Operand *o)
{
    if (o->width != A32_D)
        return (Regs)(o->imm & 0xffff);
    Regs r = 0;
    for (int k = 0; k < 16; k++)
        if (o->imm & (1 << k))
            r |= 3ull << (A32_S0 + 2 * k);
    return r;
}

static bool is_call(const A32_Instr *in)
{
    return in->op == A32_BL || in->op == A32_BLX;
}

static bool is_return(const A32_Instr *in)
{
    return in->cond == A32_AL &&
           (in->op == A32_BX || (in->op == A32_POP && (in->opnd[0].imm & (1 << A32_PC))));
}

static bool is_jump(const A32_Instr *in)
{
    return in->op == A32_B && in->cond == A32_AL;
}

static bool is_branch(const A32_Instr *in)
{
    return in->op == A32_B && in->cond != A32_AL;
}

// Operand 0 is read, not written.
static bool no_dest(A32_Op op)
{
    switch (op) {
    case A32_STR:
    case A32_STRB:
    case A32_STRH:
    case A32_STRD:
    case A32_VSTR:
    case A32_CMP:
    case A32_CMN:
    case A32_TST:
    case A32_VCMP_F32:
    case A32_VCMP_F64:
    case A32_VMRS:
    case A32_B:
    case A32_BL:
    case A32_BLX:
    case A32_BX:
    case A32_PUSH:
    case A32_POP:
    case A32_VPUSH:
    case A32_VPOP:
    case A32_EPILOGUE:
        return true;
    default:
        return false;
    }
}

// Whether operand `i` of `in` is a register it writes.
static bool def_operand(const A32_Instr *in, int i)
{
    if (in->opnd[i].kind != A32_OPND_REG)
        return false;
    return (i == 0 && !no_dest(in->op)) || (i == 1 && (in->op == A32_UMULL || in->op == A32_LDRD));
}

// Whether `in` reads the register it writes: movt and bfi/bfc keep the bits they do not
// set.
static bool reads_dest(const A32_Instr *in)
{
    return in->op == A32_MOVT || in->op == A32_BFI || in->op == A32_BFC;
}

static Regs return_regs(void);

static Regs uses(const A32_Instr *in)
{
    Regs u = 0;
    if (is_call(in))
        u |= CORE_ARGS | VFP_ARGS;
    if (is_return(in))
        u |= return_regs() | BIT(A32_SP);
    if (in->op == A32_PUSH || in->op == A32_POP || in->op == A32_VPUSH || in->op == A32_VPOP)
        u |= BIT(A32_SP);
    for (int i = 0; i < A32_MAX_OPERANDS; i++) {
        const A32_Operand *o = &in->opnd[i];
        switch (o->kind) {
        case A32_OPND_REG:
            if (!def_operand(in, i) || in->cond != A32_AL || reads_dest(in))
                u |= reg_set(o->reg, o->width);
            break;
        case A32_OPND_MEM:
        case A32_OPND_SHIFT:
            u |= reg_set(o->reg, A32_CORE) | reg_set(o->reg2, A32_CORE);
            break;
        case A32_OPND_REGLIST:
            if (in->op == A32_PUSH || in->op == A32_VPUSH)
                u |= list_set(o);
            break;
        default:
            break;
        }
    }
    return u;
}

static Regs defs(const A32_Instr *in)
{
    Regs d = 0;
    if (is_call(in))
        d |= CORE_ARGS | BIT(T0) | BIT(A32_LR) | VFP_ARGS;
    if (in->op == A32_PUSH || in->op == A32_POP || in->op == A32_VPUSH || in->op == A32_VPOP)
        d |= BIT(A32_SP);
    for (int i = 0; i < A32_MAX_OPERANDS; i++) {
        const A32_Operand *o = &in->opnd[i];
        if (def_operand(in, i))
            d |= reg_set(o->reg, o->width);
        else if (o->kind == A32_OPND_REGLIST && (in->op == A32_POP || in->op == A32_VPOP))
            d |= list_set(o);
    }
    return d;
}

static bool sets_flags(const A32_Instr *in)
{
    return in->set_flags || in->op == A32_CMP || in->op == A32_CMN || in->op == A32_TST ||
           in->op == A32_VMRS || is_call(in);
}

// The label a branch or jump goes to.
static const char *target(const A32_Instr *in)
{
    return in->opnd[0].sym;
}

// The registers live into and out of each block, and the block being rewritten; the
// registers a return reads.
static struct {
    Regs result;
    int n;
    A32_Block **blocks;
    Regs *in, *out;
    A32_Block *cur;
} live_info;

static Regs return_regs(void)
{
    return live_info.result;
}

// The registers live at label `l`: all, for one not in the function.
static Regs live_at(const char *l)
{
    for (int i = 0; i < live_info.n; i++)
        if (live_info.blocks[i]->label && strcmp(live_info.blocks[i]->label, l) == 0)
            return live_info.in[i];
    return ~0ull;
}

// The registers live before instruction `in`, given those live after it.
static Regs live_through(const A32_Instr *in, Regs live)
{
    if (is_return(in))
        return uses(in);
    if (is_jump(in))
        return live_at(target(in));
    if (is_branch(in))
        return live | live_at(target(in));
    if (in->cond == A32_AL)
        live &= ~defs(in);
    return live | uses(in);
}

// The registers live on entry to block b, given those live out of it.
static Regs live_before(const A32_Block *b, Regs live)
{
    int n = 0;
    for (const A32_Instr *in = b->head; in; in = in->next)
        n++;
    const A32_Instr **list = xalloc((n ? n : 1) * sizeof(A32_Instr *), __func__, __FILE__, __LINE__);
    n                      = 0;
    for (const A32_Instr *in = b->head; in; in = in->next)
        list[n++] = in;
    while (n > 0)
        live = live_through(list[--n], live);
    xfree(list);
    return live;
}

// Whether block b's code falls through to the next block.
static bool falls_through(const A32_Block *b)
{
    const A32_Instr *last = b->tail;
    for (const A32_Instr *in = b->head; in; in = in->next)
        last = in;
    return !last || (!is_jump(last) && !is_return(last));
}

static void free_liveness(void)
{
    xfree(live_info.blocks);
    xfree(live_info.in);
    xfree(live_info.out);
    live_info.blocks = NULL;
    live_info.in = live_info.out = NULL;
    live_info.n                  = 0;
}

// Liveness over the blocks, to a fixed point, backwards.
static void compute_liveness(A32_Func *fn)
{
    free_liveness();
    for (const A32_Block *b = fn->blocks; b; b = b->next)
        live_info.n++;
    size_t n           = live_info.n ? live_info.n : 1;
    live_info.blocks   = xalloc(n * sizeof(A32_Block *), __func__, __FILE__, __LINE__);
    live_info.in       = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    live_info.out      = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    int i              = 0;
    for (A32_Block *b = fn->blocks; b; b = b->next, i++) {
        live_info.blocks[i] = b;
        live_info.in[i] = live_info.out[i] = 0;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (i = live_info.n - 1; i >= 0; i--) {
            const A32_Block *b = live_info.blocks[i];
            Regs out = falls_through(b) && i + 1 < live_info.n ? live_info.in[i + 1] : 0;
            Regs in  = live_before(b, out);
            if (in != live_info.in[i] || out != live_info.out[i])
                changed = true;
            live_info.in[i]  = in;
            live_info.out[i] = out;
        }
    }
}

// The registers live out of the block being rewritten.
static Regs live_out(void)
{
    for (int i = 0; i < live_info.n; i++)
        if (live_info.blocks[i] == live_info.cur)
            return live_info.out[i];
    return ~0ull;
}

// The registers in `live` are not read after `in`, an instruction of the block being
// rewritten: written first, or not live where control goes.
static bool dead_after(const A32_Instr *in, Regs live)
{
    for (const A32_Instr *n = in->next; n; n = n->next) {
        if (uses(n) & live)
            return false;
        if (is_return(n))
            return true;
        if (is_jump(n))
            return !(live & live_at(target(n)));
        if (is_branch(n) && (live & live_at(target(n))))
            return false;
        if (n->cond == A32_AL)
            live &= ~defs(n);
        if (!live)
            return true;
    }
    return !(live & live_out());
}

// The value of `live` before `in` is not read after it.
static bool last_read(const A32_Instr *in, Regs live)
{
    if (in->cond == A32_AL)
        live &= ~defs(in);
    return !live || dead_after(in, live);
}

static void free_instr(A32_Instr *in)
{
    for (int i = 0; i < A32_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

// Unlink and free *link.
static void delete_at(A32_Instr **link)
{
    A32_Instr *in = *link;
    *link         = in->next;
    free_instr(in);
}

// A plain register operand.
static bool is_reg(const A32_Operand *o)
{
    return o->kind == A32_OPND_REG;
}

// `mov t, r` between core registers, or `vmov.f32`/`vmov.f64` between VFP ones.
static bool is_move(const A32_Instr *in)
{
    if (in->cond != A32_AL || in->set_flags || !is_reg(&in->opnd[0]) || !is_reg(&in->opnd[1]) ||
        in->opnd[2].kind != A32_OPND_NONE)
        return false;
    if (in->op == A32_MOV)
        return in->opnd[0].width == A32_CORE && in->opnd[1].width == A32_CORE;
    return (in->op == A32_VMOV_F32 || in->op == A32_VMOV_F64) &&
           in->opnd[0].width == in->opnd[1].width;
}

// Whether every read of register t (at width w) in `in` can read another register of
// the file instead: as a register operand of the same width, or a core one as an
// address or shift; not in a register list, nor as a destination it also reads.
static bool can_substitute(const A32_Instr *in, int t, A32_Width w)
{
    Regs tb = reg_set(t, w);
    if ((is_call(in) || is_return(in)) && (tb & (CORE_ARGS | VFP_ARGS)))
        return false;
    for (int i = 0; i < A32_MAX_OPERANDS; i++) {
        const A32_Operand *o = &in->opnd[i];
        switch (o->kind) {
        case A32_OPND_REG:
            if (!(reg_set(o->reg, o->width) & tb))
                break;
            if (def_operand(in, i)) {
                if (in->cond != A32_AL || reads_dest(in))
                    return false;
                break;
            }
            if (o->reg != t || o->width != w)
                return false;
            break;
        case A32_OPND_MEM:
        case A32_OPND_SHIFT:
            if ((o->reg == t || o->reg2 == t) && w != A32_CORE)
                return false;
            break;
        case A32_OPND_REGLIST:
            if (list_set(o) & tb)
                return false;
            break;
        default:
            break;
        }
    }
    return true;
}

static void substitute(A32_Instr *in, int t, int r)
{
    for (int i = 0; i < A32_MAX_OPERANDS; i++) {
        A32_Operand *o = &in->opnd[i];
        if (o->kind == A32_OPND_REG && !def_operand(in, i) && o->reg == t)
            o->reg = r;
        if (o->kind == A32_OPND_MEM || o->kind == A32_OPND_SHIFT) {
            if (o->reg == t)
                o->reg = r;
            if (o->reg2 == t)
                o->reg2 = r;
        }
    }
}

// `mov t, r`: the reads of t up to its next write read r instead, if r is not written
// before the last of them and each can.  A t other than a scratch register must be dead
// where a branch passed goes, and at the end of the block if nothing writes it first.
static bool forward_move(A32_Instr **link)
{
    A32_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    A32_Width w = mv->opnd[0].width;
    Regs tb = reg_set(t, w), rb = reg_set(r, w);
    bool scratch = !(tb & ~SCRATCH);
    if (t == r || t == A32_SP || t == A32_PC || r == A32_SP || r == A32_PC)
        return false;
    bool clobbered = false, written = false;
    const A32_Instr *end = NULL;
    for (const A32_Instr *n = mv->next; n; n = n->next) {
        if ((uses(n) & tb) && (clobbered || !can_substitute(n, t, w)))
            return false;
        Regs d = defs(n);
        if (n->cond == A32_AL && (d & tb) == tb) {
            end     = n->next;
            written = true;
            break;
        }
        if (d & tb)
            return false;
        if (!scratch && n->op == A32_B && (tb & live_at(target(n))))
            return false;
        if (d & rb)
            clobbered = true;
    }
    if (!scratch && !written && (tb & live_out()))
        return false;
    for (A32_Instr *n = mv->next; n != end; n = n->next)
        substitute(n, t, r);
    delete_at(link);
    return true;
}

// Whether `in` writes register operand 0 alone, from its other operands: the result
// could go to another register.
static bool computes(const A32_Instr *in)
{
    if (!def_operand(in, 0) || in->cond != A32_AL || in->op == A32_UMULL ||
        in->op == A32_LDRD || reads_dest(in))
        return false;
    int r = in->opnd[0].reg;
    return r != A32_SP && r != A32_PC && r < A32_VREG;
}

// `in` computes into t, and a later move of t is the last read of it: compute into the
// move's destination d, when d is neither read nor written in between, nor (t not
// scratch) a branch passed.  The reads of t in between read d.
static bool compute_in_place(A32_Instr *in)
{
    int t       = in->opnd[0].reg;
    A32_Width w = in->opnd[0].width;
    Regs tb     = reg_set(t, w);
    for (A32_Instr **link = &in->next; *link; link = &(*link)->next) {
        A32_Instr *n = *link;
        if (is_move(n) && n->opnd[1].reg == t && n->opnd[1].width == w && n->opnd[0].reg != t &&
            n->opnd[0].reg != A32_SP && n->opnd[0].reg != A32_PC && dead_after(n, tb)) {
            int d   = n->opnd[0].reg;
            Regs db = reg_set(d, w);
            for (const A32_Instr *m = in->next; m != n; m = m->next)
                if ((uses(m) & db) || (defs(m) & db) || !can_substitute(m, t, w))
                    return false;
            for (A32_Instr *m = in->next; m != n; m = m->next)
                substitute(m, t, d);
            in->opnd[0].reg = d;
            delete_at(link);
            return true;
        }
        if ((defs(n) & tb) || is_call(n) || n->op == A32_B)
            return false;
    }
    return false;
}

// `mov x, y`, and later, before either changes, `mov y, x`: that one goes.
static bool delete_move_back(A32_Instr *mv)
{
    int x = mv->opnd[0].reg, y = mv->opnd[1].reg;
    A32_Width w = mv->opnd[0].width;
    Regs both   = reg_set(x, w) | reg_set(y, w);
    for (A32_Instr **link = &mv->next; *link; link = &(*link)->next) {
        const A32_Instr *n = *link;
        if (is_move(n) && n->op == mv->op && n->opnd[0].reg == y && n->opnd[1].reg == x) {
            delete_at(link);
            return true;
        }
        if (defs(n) & both)
            return false;
    }
    return false;
}

// The offset reach of a memory instruction, as frame.c has it.
static bool fits(A32_Op op, int64_t off)
{
    switch (op) {
    case A32_LDRH:
    case A32_LDRSH:
    case A32_LDRSB:
    case A32_STRH:
    case A32_LDRD:
    case A32_STRD:
        return off >= -255 && off <= 255;
    case A32_VLDR:
    case A32_VSTR:
        return off >= -1020 && off <= 1020 && off % 4 == 0;
    default:
        return off >= -4095 && off <= 4095;
    }
}

static bool is_mem_op(A32_Op op)
{
    switch (op) {
    case A32_LDR:
    case A32_LDRB:
    case A32_LDRSB:
    case A32_LDRH:
    case A32_LDRSH:
    case A32_STR:
    case A32_STRB:
    case A32_STRH:
    case A32_VLDR:
    case A32_VSTR:
        return true;
    default:
        return false;
    }
}

static bool is_store(A32_Op op)
{
    return op == A32_STR || op == A32_STRB || op == A32_STRH || op == A32_STRD || op == A32_VSTR;
}

// The bytes a load or store accesses.
static int access_size(const A32_Instr *in)
{
    switch (in->op) {
    case A32_LDRB:
    case A32_LDRSB:
    case A32_STRB:
        return 1;
    case A32_LDRH:
    case A32_LDRSH:
    case A32_STRH:
        return 2;
    case A32_LDRD:
    case A32_STRD:
        return 8;
    case A32_VLDR:
    case A32_VSTR:
        return in->opnd[0].width == A32_D ? 8 : 4;
    default:
        return 4;
    }
}

// The memory operand of a load or store.
static A32_Operand *mem_operand(A32_Instr *in)
{
    return &in->opnd[in->op == A32_LDRD || in->op == A32_STRD ? 2 : 1];
}

// Delete a later load of what store `st` wrote, at the same width, when nothing between
// changes the register, the base or memory that may overlap; into another register it
// becomes a move.  Only a frame slot (sp or r11) is followed past other instructions.
// A volatile access is neither of the two.
static bool delete_reload(A32_Instr *st)
{
    if ((st->op != A32_STR && st->op != A32_VSTR) || st->cond != A32_AL || st->is_volatile)
        return false;
    const A32_Operand *v = &st->opnd[0], *m = &st->opnd[1];
    if (m->kind != A32_OPND_MEM || m->sub != A32_MEM_OFFSET || m->reg2 >= 0 || m->reg == v->reg)
        return false;
    A32_Op load = st->op == A32_STR ? A32_LDR : A32_VLDR;
    Regs vb     = reg_set(v->reg, v->width);
    int size    = access_size(st);
    for (A32_Instr **link = &st->next; *link; link = &(*link)->next) {
        A32_Instr *n          = *link;
        const A32_Operand *nm = &n->opnd[1];
        if (n->op == load && n->cond == A32_AL && n->opnd[0].width == v->width &&
            nm->kind == A32_OPND_MEM && nm->sub == A32_MEM_OFFSET && nm->reg2 < 0 &&
            nm->reg == m->reg && nm->imm == m->imm) {
            if (n->is_volatile)
                return false;
            if (n->opnd[0].reg == v->reg) {
                delete_at(link);
            } else {
                n->op = load == A32_LDR ? A32_MOV : v->width == A32_D ? A32_VMOV_F64 : A32_VMOV_F32;
                n->opnd[1] = *v;
                n->opnd[1].sym = NULL;
            }
            return true;
        }
        if (m->reg != A32_SP && m->reg != A32_FP)
            return false;
        if (is_call(n) || n->op == A32_B || is_return(n) || (defs(n) & (vb | BIT(m->reg))))
            return false;
        if (is_store(n->op)) {
            const A32_Operand *sm = mem_operand(n);
            int nsize             = access_size(n);
            if (sm->kind != A32_OPND_MEM || sm->sub != A32_MEM_OFFSET || sm->reg2 >= 0 ||
                sm->reg != m->reg || (sm->imm < m->imm + size && m->imm < sm->imm + nsize))
                return false;
        }
    }
    return false;
}

// `add t, b, #imm` (or sub), or `add t, a, index{, lsl #s}`, feeding the address of the
// load or store after it, at its last read: the address computed by that instruction
// instead.
static bool fold_address(A32_Instr **link)
{
    A32_Instr *add = *link, *n = add->next;
    const A32_Operand *o = add->opnd;
    if (!n || !is_mem_op(n->op) || add->cond != A32_AL || add->set_flags || !is_reg(&o[0]) ||
        !is_reg(&o[1]) || o[3].kind != A32_OPND_NONE)
        return false;
    int t          = o[0].reg;
    A32_Operand *m = &n->opnd[1];
    if (m->kind != A32_OPND_MEM || m->sub != A32_MEM_OFFSET || m->reg != t || m->reg2 >= 0 ||
        (is_store(n->op) && n->opnd[0].reg == t && n->opnd[0].width == A32_CORE) ||
        !last_read(n, BIT(t)))
        return false;
    if (o[2].kind == A32_OPND_IMM) {
        int64_t off = m->imm + (add->op == A32_SUB ? -o[2].imm : o[2].imm);
        if ((add->op != A32_ADD && add->op != A32_SUB) || !fits(n->op, off))
            return false;
        m->reg = o[1].reg;
        m->imm = off;
    } else if (add->op == A32_ADD && m->imm == 0 && n->op != A32_VLDR && n->op != A32_VSTR) {
        bool word  = n->op == A32_LDR || n->op == A32_STR || n->op == A32_LDRB || n->op == A32_STRB;
        int shift  = 0;
        if (o[2].kind == A32_OPND_SHIFT) {
            if (o[2].sub != A32_SHIFT_LSL || o[2].reg2 >= 0 || !word)
                return false;
            shift = (int)o[2].imm;
        } else if (!is_reg(&o[2])) {
            return false;
        }
        if (o[2].reg == A32_PC || o[2].reg == A32_SP)
            return false;
        *m = a32_mem_index(o[1].reg, o[2].reg, false, shift);
    } else {
        return false;
    }
    delete_at(link);
    return true;
}

// The operand2 a shift by a constant could become, for `mov t, r, <shift>` or
// `lsl/lsr/asr t, r, #n`; false when `in` is neither.
static bool shift_operand(const A32_Instr *in, A32_Operand *sh)
{
    const A32_Operand *o = in->opnd;
    if (in->cond != A32_AL || in->set_flags || !is_reg(&o[0]))
        return false;
    if (in->op == A32_MOV && o[1].kind == A32_OPND_SHIFT && o[2].kind == A32_OPND_NONE) {
        *sh = o[1];
        return true;
    }
    if ((in->op != A32_LSL && in->op != A32_LSR && in->op != A32_ASR) || !is_reg(&o[1]) ||
        o[2].kind != A32_OPND_IMM || o[2].imm < 1 || o[2].imm > 31)
        return false;
    *sh = a32_shift(o[1].reg, in->op == A32_LSL   ? A32_SHIFT_LSL
                              : in->op == A32_LSR ? A32_SHIFT_LSR
                                                  : A32_SHIFT_ASR,
                    (int)o[2].imm);
    return true;
}

// A shift into t and the instruction after it reading t as its operand2, at its last
// read: the shifted register operand2 there.
static bool fold_shift(A32_Instr **link)
{
    A32_Instr *sh = *link, *n = sh->next;
    A32_Operand shifted;
    if (!n || !shift_operand(sh, &shifted) || n->cond != A32_AL)
        return false;
    int t          = sh->opnd[0].reg;
    A32_Operand *o = n->opnd;
    int at         = -1;
    switch (n->op) {
    case A32_ADD:
    case A32_AND:
    case A32_ORR:
    case A32_EOR:
    case A32_ADC:
        // Commutative: the shifted one second.
        if (is_reg(&o[1]) && o[1].reg == t && is_reg(&o[2]) && o[2].reg != t &&
            o[3].kind == A32_OPND_NONE) {
            o[1].reg = o[2].reg;
            o[2].reg = t;
        }
        // fall through
    case A32_SUB:
    case A32_RSB:
    case A32_SBC:
    case A32_RSC:
    case A32_BIC:
        if (is_reg(&o[1]) && o[1].reg != t && is_reg(&o[2]) && o[2].reg == t &&
            o[3].kind == A32_OPND_NONE)
            at = 2;
        break;
    case A32_CMP:
    case A32_CMN:
    case A32_TST:
    case A32_MOV:
    case A32_MVN:
        if (is_reg(&o[0]) && o[0].reg != t && is_reg(&o[1]) && o[1].reg == t &&
            o[2].kind == A32_OPND_NONE && o[0].width == A32_CORE)
            at = 1;
        break;
    default:
        break;
    }
    if (at < 0 || !last_read(n, BIT(t)))
        return false;
    o[at] = shifted;
    delete_at(link);
    return true;
}

// `mul t, a, b` and `add d, c, t` (or `sub d, c, t`) at its last read: mla (mls).
static bool fold_multiply(A32_Instr **link)
{
    A32_Instr *mul = *link, *n = mul->next;
    if (!n || (n->op != A32_ADD && n->op != A32_SUB) || mul->cond != A32_AL || mul->set_flags ||
        n->cond != A32_AL || n->set_flags)
        return false;
    int t          = mul->opnd[0].reg;
    A32_Operand *o = n->opnd;
    if (!is_reg(&o[0]) || !is_reg(&o[1]) || !is_reg(&o[2]) || o[3].kind != A32_OPND_NONE ||
        !last_read(n, BIT(t)))
        return false;
    int c;
    if (o[2].reg == t && o[1].reg != t)
        c = o[1].reg;
    else if (n->op == A32_ADD && o[1].reg == t && o[2].reg != t)
        c = o[2].reg;
    else
        return false;
    if (c == A32_SP || c == A32_PC)
        return false;
    n->op      = n->op == A32_ADD ? A32_MLA : A32_MLS;
    n->opnd[1] = mul->opnd[1];
    n->opnd[2] = mul->opnd[2];
    n->opnd[3] = a32_reg(c);
    delete_at(link);
    return true;
}

// `and t, a, x` and `cmp t, #0` at its last read: `tst a, x`.
static bool fold_test(A32_Instr **link)
{
    A32_Instr *mask = *link, *n = mask->next;
    if (!n || n->op != A32_CMP || n->cond != A32_AL || mask->cond != A32_AL || mask->set_flags ||
        !is_reg(&n->opnd[0]) || n->opnd[0].reg != mask->opnd[0].reg ||
        n->opnd[1].kind != A32_OPND_IMM || n->opnd[1].imm != 0 || n->opnd[2].kind != A32_OPND_NONE ||
        mask->opnd[3].kind != A32_OPND_NONE || !last_read(n, BIT(n->opnd[0].reg)))
        return false;
    n->op      = A32_TST;
    n->opnd[0] = mask->opnd[1];
    n->opnd[1] = mask->opnd[2];
    mask->opnd[1].sym = mask->opnd[2].sym = NULL;
    delete_at(link);
    return true;
}

// Bit-field instructions.  A rule rewrites a consumer `at` into ubfx/sbfx/bfi/bfc and
// deletes the group of instructions of its block that fed it: shifts, masks, and the
// movw/movt of a mask that is no immediate.  The rewritten `at` reads the group's sources
// where `at` is, so each must still hold there the value the group read; the group's own
// writes go with it, so a source one of them overwrote after reading it survives.
enum { MAX_GROUP = 8, MAX_SOURCES = 2 };

typedef struct {
    A32_Instr *in[MAX_GROUP];
    int n;
    struct {
        int reg;
        const A32_Instr *reader; // the instruction of the group (or `at`) that read it
    } src[MAX_SOURCES];
    int nsrc;
} Group;

static bool in_group(const Group *g, const A32_Instr *in)
{
    for (int i = 0; i < g->n; i++)
        if (g->in[i] == in)
            return true;
    return false;
}

static bool group_add(Group *g, A32_Instr *in)
{
    if (in_group(g, in))
        return true;
    if (g->n == MAX_GROUP)
        return false;
    g->in[g->n++] = in;
    return true;
}

static void group_source(Group *g, int reg, const A32_Instr *reader)
{
    g->src[g->nsrc].reg    = reg;
    g->src[g->nsrc].reader = reader;
    g->nsrc++;
}

// The last instruction of the current block before `at` to write core register `reg`,
// when it writes that alone, always, and sets no flags; NULL otherwise.
static A32_Instr *last_def(const A32_Instr *at, int reg)
{
    A32_Instr *d = NULL;
    for (A32_Instr *in = live_info.cur->head; in && in != at; in = in->next)
        if (defs(in) & BIT(reg))
            d = in;
    if (!d || d->cond != A32_AL || d->set_flags || d->is_volatile || defs(d) != BIT(reg) ||
        reads_dest(d))
        return NULL;
    return d;
}

// The value of operand `o` of an instruction of `g` (or `at`): an immediate, or a core
// register `at` sees loaded with a constant by movw[+movt], mov or mvn, added to `g`.
static bool operand_value(const A32_Instr *at, const A32_Operand *o, uint32_t *v, Group *g)
{
    if (o->kind == A32_OPND_IMM) {
        *v = (uint32_t)o->imm;
        return true;
    }
    if (!is_reg(o) || o->width != A32_CORE || o->reg == A32_SP || o->reg == A32_PC)
        return false;
    A32_Instr *d = NULL;
    for (A32_Instr *in = live_info.cur->head; in && in != at; in = in->next)
        if (defs(in) & BIT(o->reg))
            d = in;
    if (!d || d->cond != A32_AL || d->set_flags || d->opnd[1].kind != A32_OPND_IMM ||
        d->opnd[2].kind != A32_OPND_NONE)
        return false;
    uint32_t high = 0;
    if (d->op == A32_MOVT) {
        high = (uint32_t)d->opnd[1].imm << 16;
        if (!group_add(g, d))
            return false;
        A32_Instr *lo = NULL;
        for (A32_Instr *in = live_info.cur->head; in && in != d; in = in->next)
            if (defs(in) & BIT(o->reg))
                lo = in;
        if (!lo || lo->op != A32_MOVW || lo->cond != A32_AL || lo->opnd[1].kind != A32_OPND_IMM)
            return false;
        d = lo;
    }
    switch (d->op) {
    case A32_MOVW:
        *v = high | (uint32_t)(d->opnd[1].imm & 0xffff);
        break;
    case A32_MOV:
        *v = (uint32_t)d->opnd[1].imm;
        break;
    case A32_MVN:
        *v = ~(uint32_t)d->opnd[1].imm;
        break;
    default:
        return false;
    }
    return group_add(g, d);
}

// Whether `at` may read the sources of `g` and the group go: every instruction of the
// group comes before `at` in its block; what lies among them neither branches nor calls,
// reads or writes what the group writes, nor writes a source; no member writes a source
// before that source is read; and of what the group writes, nothing but `keep` (what the
// rewritten `at` writes) is read after `at`.
static bool group_ok(const Group *g, const A32_Instr *at, Regs keep)
{
    Regs wr = 0, src = 0;
    for (int i = 0; i < g->n; i++) {
        if (g->in[i]->cond != A32_AL || g->in[i]->set_flags || g->in[i]->is_volatile)
            return false;
        wr |= defs(g->in[i]);
    }
    for (int i = 0; i < g->nsrc; i++)
        src |= BIT(g->src[i].reg);
    int found   = 0;
    Regs unread = src; // sources whose reader is still to come
    for (const A32_Instr *in = live_info.cur->head; in != at; in = in->next) {
        if (!in)
            return false;
        bool member = in_group(g, in);
        if (member)
            found++;
        else if (found == 0)
            continue;
        if (!member) {
            if (is_call(in) || in->op == A32_B || (uses(in) & wr) || (defs(in) & (wr | src)))
                return false;
            continue;
        }
        for (int i = 0; i < g->nsrc; i++)
            if (g->src[i].reader == in)
                unread &= ~BIT(g->src[i].reg);
        if (defs(in) & unread)
            return false;
    }
    if (found != g->n)
        return false;
    for (int i = 0; i < g->nsrc; i++)
        if (g->src[i].reg == A32_SP || g->src[i].reg == A32_PC)
            return false;
    Regs dead = wr & ~keep;
    return !dead || dead_after(at, dead);
}

// Unlink and free the instructions of `g`.
static void group_delete(const Group *g)
{
    for (A32_Instr **link = &live_info.cur->head; *link;)
        if (in_group(g, *link))
            delete_at(link);
        else
            link = &(*link)->next;
}

static void set_bitfield(A32_Instr *in, A32_Op op, int d, int s, int lsb, int width)
{
    for (int i = 0; i < A32_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    memset(in->opnd, 0, sizeof in->opnd);
    for (int i = 0; i < A32_MAX_OPERANDS; i++)
        in->opnd[i].reg2 = -1;
    in->op      = op;
    in->opnd[0] = a32_reg(d);
    int k       = 1;
    if (s >= 0)
        in->opnd[k++] = a32_reg(s);
    in->opnd[k++] = a32_imm(lsb);
    in->opnd[k]   = a32_imm(width);
}

// A new `mov d, s` before *link.
static void insert_move(A32_Instr **link, int d, int s)
{
    A32_Instr *mv    = xalloc(sizeof(A32_Instr), __func__, __FILE__, __LINE__);
    mv->op           = A32_MOV;
    mv->opnd[0]      = a32_reg(d);
    mv->opnd[1]      = a32_reg(s);
    mv->opnd[2].reg2 = mv->opnd[3].reg2 = -1;
    mv->next                            = *link;
    *link                               = mv;
}

// The width of mask `m` = 2^w - 1, or 0 when it is not one.
static int low_mask_width(uint32_t m)
{
    if (m == 0 || (m & (m + 1)))
        return 0;
    return 32 - clz32(m);
}

// The position and width of a field, the one run of ones in `f`; false if there is none.
static bool field_of(uint32_t f, int *pos, int *width)
{
    if (f == 0)
        return false;
    int p = ctz32(f);
    int w = low_mask_width(f >> p);
    if (!w)
        return false;
    *pos   = p;
    *width = w;
    return true;
}

// A plain unconditional core operation `op d, a, b` setting no flags.
static bool is_alu3(const A32_Instr *in, A32_Op op)
{
    return in->op == op && in->cond == A32_AL && !in->set_flags && !in->is_volatile &&
           is_reg(&in->opnd[0]) && in->opnd[0].width == A32_CORE && is_reg(&in->opnd[1]) &&
           in->opnd[3].kind == A32_OPND_NONE;
}

// `and d, a, M` as an operand of the group: the register a and the mask M, either
// order; uxtb/uxth as masks 0xff/0xffff.
static bool mask_of(A32_Instr *in, int *a, uint32_t *m, Group *g)
{
    if ((in->op == A32_UXTB || in->op == A32_UXTH) && in->cond == A32_AL && is_reg(&in->opnd[1]) &&
        in->opnd[2].kind == A32_OPND_NONE) {
        *a = in->opnd[1].reg;
        *m = in->op == A32_UXTB ? 0xff : 0xffff;
        return true;
    }
    if (!is_alu3(in, A32_AND))
        return false;
    Group save = *g;
    if (operand_value(in, &in->opnd[2], m, g)) {
        *a = in->opnd[1].reg;
        return true;
    }
    *g = save;
    if (is_reg(&in->opnd[2]) && operand_value(in, &in->opnd[1], m, g)) {
        *a = in->opnd[2].reg;
        return true;
    }
    *g = save;
    return false;
}

// `at` = `and d, t, #(2^w - 1)` fed by `lsr t, a, #p`: `ubfx d, a, #p, #w`; with no
// shift, and a mask that took a movw, `ubfx d, a, #0, #w`.  `at` = `lsr`/`asr d, t, #r`
// fed by `lsl t, a, #l`, r >= l: `ubfx`/`sbfx d, a, #(r - l), #(32 - r)`.
static bool fold_extract(A32_Instr *at)
{
    Group g = { 0 };
    int d   = at->opnd[0].reg, a;
    uint32_t m;
    A32_Operand sh;
    if (mask_of(at, &a, &m, &g) && at->op == A32_AND) {
        int w = low_mask_width(m);
        if (!w)
            return false;
        A32_Instr *f = last_def(at, a);
        if (f && shift_operand(f, &sh) && sh.sub == A32_SHIFT_LSR && sh.reg2 < 0) {
            Group with = g;
            int p      = (int)sh.imm;
            if (group_add(&with, f)) {
                group_source(&with, sh.reg, f);
                if (group_ok(&with, at, BIT(d))) {
                    set_bitfield(at, A32_UBFX, d, sh.reg, p, w < 32 - p ? w : 32 - p);
                    group_delete(&with);
                    return true;
                }
            }
        }
        if (g.n == 0 || a32_operand2_imm(m))
            return false; // an immediate mask: and is as good
        group_source(&g, a, at);
        if (!group_ok(&g, at, BIT(d)))
            return false;
        set_bitfield(at, A32_UBFX, d, a, 0, w);
        group_delete(&g);
        return true;
    }
    A32_Operand r;
    if (!shift_operand(at, &r) || r.sub == A32_SHIFT_LSL || r.reg2 >= 0)
        return false;
    A32_Instr *f = last_def(at, r.reg);
    if (!f || !shift_operand(f, &sh) || sh.sub != A32_SHIFT_LSL || sh.reg2 >= 0 || r.imm < sh.imm)
        return false;
    g = (Group){ 0 };
    group_add(&g, f);
    group_source(&g, sh.reg, f);
    if (!group_ok(&g, at, BIT(d)))
        return false;
    set_bitfield(at, r.sub == A32_SHIFT_ASR ? A32_SBFX : A32_UBFX, d, sh.reg, (int)(r.imm - sh.imm),
                 32 - (int)r.imm);
    group_delete(&g);
    return true;
}

// The field value `v` placed at bit `p` as operand `o` of `at`: `lsl y, z, #p` of a mask
// `and z, v, #(2^w - 1)`, or the mask alone (p = 0), or the shift alone when it drops
// the bits above the field (w = 32 - p).  Its instructions join `g`.
static bool placed_field(const A32_Instr *at, const A32_Operand *o, int *v, int *p, int *w,
                         Group *g)
{
    int z                   = -1;
    *p                      = 0;
    const A32_Instr *reader = at;
    A32_Operand sh;
    if (o->kind == A32_OPND_SHIFT) {
        if (o->sub != A32_SHIFT_LSL || o->reg2 >= 0)
            return false;
        z  = o->reg;
        *p = (int)o->imm;
    } else if (is_reg(o)) {
        A32_Instr *y = last_def(at, o->reg);
        if (!y)
            return false;
        if (shift_operand(y, &sh)) {
            if (sh.sub != A32_SHIFT_LSL || sh.reg2 >= 0 || !group_add(g, y))
                return false;
            z      = sh.reg;
            *p     = (int)sh.imm;
            reader = y;
        } else {
            z = o->reg; // the mask, unshifted
        }
    } else {
        return false;
    }
    A32_Instr *mk = last_def((A32_Instr *)reader, z);
    uint32_t m;
    Group save = *g;
    if (mk && group_add(g, mk) && mask_of(mk, v, &m, g) && (*w = low_mask_width(m)) != 0) {
        if (*p + *w > 32)
            *w = 32 - *p;
        group_source(g, *v, mk);
        return true;
    }
    *g = save;
    if (*p == 0)
        return false;
    *v = z; // no mask: the shift drops the bits above
    *w = 32 - *p;
    group_source(g, z, reader);
    return true;
}

// `at` = `orr r, x, y`, x = `and x, u, #keep` (or `bic x, u, #f`) clearing a field, y
// the value placed there: bfi into r after a move of u, or into u.
static bool fold_insert(A32_Instr **link)
{
    A32_Instr *at = *link;
    if (!is_alu3(at, A32_ORR))
        return false;
    int r = at->opnd[0].reg;
    for (int side = 1; side <= 2; side++) {
        const A32_Operand *xo = &at->opnd[side], *yo = &at->opnd[3 - side];
        if (!is_reg(xo))
            continue;
        Group g      = { 0 };
        A32_Instr *x = last_def(at, xo->reg);
        uint32_t keep;
        if (!x || !group_add(&g, x))
            continue;
        if (is_alu3(x, A32_AND) && operand_value(x, &x->opnd[2], &keep, &g)) {
        } else if (is_alu3(x, A32_BIC) && x->opnd[2].kind == A32_OPND_IMM) {
            keep = ~(uint32_t)x->opnd[2].imm;
        } else {
            continue;
        }
        int u = x->opnd[1].reg, v, p, w;
        if (!placed_field(at, yo, &v, &p, &w, &g))
            continue;
        uint32_t field = (w == 32 ? ~0u : ((1u << w) - 1)) << p;
        if (keep != ~field || u == v)
            continue;
        group_source(&g, u, x);
        // Into r after `mov r, u`, which the computation of u may then take over; into
        // u when r is v.
        bool in_u = r == v;
        if (in_u && !last_read(at, BIT(u)))
            continue;
        if (!group_ok(&g, at, BIT(r) | (in_u ? BIT(u) : 0)))
            continue;
        group_delete(&g);
        for (link = &live_info.cur->head; *link != at; link = &(*link)->next)
            ;
        if (in_u) {
            set_bitfield(at, A32_BFI, u, v, p, w);
            if (r != u)
                insert_move(&at->next, r, u);
        } else {
            set_bitfield(at, A32_BFI, r, v, p, w);
            if (r != u)
                insert_move(link, r, u);
        }
        return true;
    }
    return false;
}

// `at` = `and d, u, M` with M from a movw (and movt) clearing one field: bfc in place,
// after a move of u when only that is no longer.
static bool fold_clear(A32_Instr **link)
{
    A32_Instr *at = *link;
    if (!is_alu3(at, A32_AND) || !is_reg(&at->opnd[2]))
        return false;
    Group g = { 0 };
    uint32_t keep;
    if (!operand_value(at, &at->opnd[2], &keep, &g) || a32_operand2_imm(keep) ||
        a32_operand2_imm(~keep))
        return false;
    int d = at->opnd[0].reg, u = at->opnd[1].reg, p, w;
    if (!field_of(~keep, &p, &w) || p + w == 32 || (d != u && g.n < 2))
        return false; // a field to the top is the ubfx of the bits below it
    group_source(&g, u, at);
    if (!group_ok(&g, at, BIT(d)))
        return false;
    group_delete(&g);
    for (link = &live_info.cur->head; *link != at; link = &(*link)->next)
        ;
    set_bitfield(at, A32_BFC, d, -1, p, w);
    if (d != u)
        insert_move(link, d, u);
    return true;
}

// `uxtb`/`uxth t, a` stored by `strb`/`strh t` at its last read: a stored instead.
static bool fold_narrow_store(A32_Instr *at)
{
    if ((at->op != A32_STRB && at->op != A32_STRH) || at->cond != A32_AL || at->is_volatile)
        return false;
    int t        = at->opnd[0].reg;
    A32_Instr *x = last_def(at, t);
    if (!x || x->op != (at->op == A32_STRB ? A32_UXTB : A32_UXTH) || !is_reg(&x->opnd[1]) ||
        x->opnd[2].kind != A32_OPND_NONE)
        return false;
    const A32_Operand *m = &at->opnd[1];
    if (m->kind != A32_OPND_MEM || m->reg == t || m->reg2 == t)
        return false;
    Group g = { 0 };
    group_add(&g, x);
    group_source(&g, x->opnd[1].reg, x);
    if (!group_ok(&g, at, 0))
        return false;
    at->opnd[0].reg = x->opnd[1].reg;
    group_delete(&g);
    return true;
}

// One bit-field rewrite in block `b`; true when something changed (the block is then
// to be swept again: instructions before the one rewritten may be gone).  Inserts
// first: an extract or clear would take the masks out of their shape.
static bool fold_bitfields(A32_Block *b)
{
    for (A32_Instr **link = &b->head; *link; link = &(*link)->next)
        if (fold_insert(link))
            return true;
    for (A32_Instr **link = &b->head; *link; link = &(*link)->next) {
        A32_Instr *in = *link;
        if (in->cond != A32_AL || in->set_flags || in->is_volatile)
            continue;
        if (fold_extract(in) || fold_clear(link) || fold_narrow_store(in))
            return true;
    }
    return false;
}

// One rewrite at *link; true when something changed.
static bool rewrite(A32_Instr **link)
{
    A32_Instr *in  = *link;
    A32_Operand *o = in->opnd;
    // A move to itself.
    if (is_move(in) && o[0].reg == o[1].reg) {
        delete_at(link);
        return true;
    }
    // An add, sub, orr, eor or bic of zero: a move.
    if ((in->op == A32_ADD || in->op == A32_SUB || in->op == A32_ORR || in->op == A32_EOR ||
         in->op == A32_BIC) &&
        in->cond == A32_AL && !in->set_flags && is_reg(&o[0]) && is_reg(&o[1]) &&
        o[2].kind == A32_OPND_IMM && o[2].imm == 0 && o[3].kind == A32_OPND_NONE) {
        in->op      = A32_MOV;
        o[2]        = (A32_Operand){ 0 };
        o[2].reg2   = -1;
        return true;
    }
    if (!in->next)
        return false;
    if (is_move(in) && (delete_move_back(in) || forward_move(link)))
        return true;
    if (computes(in) && compute_in_place(in))
        return true;
    if ((in->op == A32_ADD || in->op == A32_SUB) && fold_address(link))
        return true;
    if (fold_shift(link))
        return true;
    if (in->op == A32_MUL && fold_multiply(link))
        return true;
    if (in->op == A32_AND && fold_test(link))
        return true;
    if (delete_reload(in))
        return true;
    return false;
}

// Whether label `l` is on `b` or on an empty block between `b` and the next code.
static bool falls_to(const A32_Block *b, const char *l)
{
    for (; b; b = b->next) {
        if (b->label && strcmp(b->label, l) == 0)
            return true;
        if (b->head)
            return false;
    }
    return false;
}

static A32_Instr **last_link(A32_Block *b, int back)
{
    A32_Instr **link = &b->head;
    int n            = 0;
    for (const A32_Instr *in = b->head; in; in = in->next)
        n++;
    if (n <= back)
        return NULL;
    for (int i = 0; i < n - 1 - back; i++)
        link = &(*link)->next;
    return link;
}

static int invert(int cond)
{
    return cond % 2 ? cond + 1 : cond - 1;
}

static bool rewrite_block_end(A32_Block *b)
{
    // Nothing runs after a jump or return.
    for (A32_Instr *in = b->head; in; in = in->next) {
        if ((is_jump(in) || is_return(in)) && in->next) {
            while (in->next)
                delete_at(&in->next);
            return true;
        }
    }
    A32_Instr **jl = last_link(b, 0);
    if (!jl || !is_jump(*jl))
        return false;
    // A branch over a jump: branch the other way to the jump's target.
    A32_Instr **bl = last_link(b, 1);
    if (bl && is_branch(*bl) && falls_to(b->next, target(*bl))) {
        A32_Instr *br = *bl;
        br->cond      = invert(br->cond);
        xfree(br->opnd[0].sym);
        br->opnd[0]        = (*jl)->opnd[0];
        (*jl)->opnd[0].sym = NULL;
        delete_at(&br->next);
        return true;
    }
    // A jump to the next label.
    if (falls_to(b->next, target(*jl))) {
        delete_at(jl);
        return true;
    }
    return false;
}

// How many branches and jumps of `fn` go to label `l`.
static int references(const A32_Func *fn, const char *l)
{
    int n = 0;
    for (const A32_Block *b = fn->blocks; b; b = b->next)
        for (const A32_Instr *in = b->head; in; in = in->next)
            if (in->op == A32_B && strcmp(target(in), l) == 0)
                n++;
    return n;
}

enum { MAX_ARM = 4 }; // instructions on each side of a predicated diamond

// Whether instruction `in` may run under a condition instead of being branched around:
// not conditional already, not setting or reading the flags, not a call, branch or
// stack change.
static bool predicable(const A32_Instr *in)
{
    return in->cond == A32_AL && !sets_flags(in) && in->op != A32_B && !is_return(in) &&
           in->op != A32_ADC && in->op != A32_SBC && in->op != A32_RSC &&
           in->op != A32_VCMP_F32 && in->op != A32_VCMP_F64 && !(defs(in) & BIT(A32_SP));
}

// The code from `in` (in block `b`) on, to the block labelled `stop`: past only blocks
// that no branch goes to, at most MAX_ARM instructions, each predicable but a jump at
// the end of a block.  Returns how many, or -1; the jump, if any, in *jump (the code
// then goes on at the block after it), with its link.
typedef struct {
    A32_Instr *instr[MAX_ARM];
    int n;
    A32_Instr **jump; // the jump's link, or NULL
    A32_Block *after; // the block after the code
} Arm;

static bool walk_arm(const A32_Func *fn, A32_Block *b, A32_Instr **link, const char *stop, Arm *arm)
{
    arm->n    = 0;
    arm->jump = NULL;
    for (;;) {
        for (; *link; link = &(*link)->next) {
            A32_Instr *in = *link;
            if (is_jump(in) && !in->next) {
                arm->jump  = link;
                arm->after = b->next;
                return arm->n > 0;
            }
            if (arm->n == MAX_ARM || !predicable(in))
                return false;
            arm->instr[arm->n++] = in;
        }
        b = b->next;
        if (!b)
            return false;
        if (b->label && strcmp(b->label, stop) == 0) {
            arm->after = b;
            return arm->n > 0;
        }
        if (b->label && references(fn, b->label) > 0)
            return false;
        link = &b->head;
    }
}

// A conditional branch around a few instructions to its label (a triangle), or with a
// jump at their end past a few more (a diamond): the branch and jump go, and the
// instructions run under the conditions, when no other branch goes to the label.
static bool predicate(const A32_Func *fn, A32_Block *b)
{
    A32_Instr **bl = NULL;
    for (A32_Instr **link = &b->head; *link; link = &(*link)->next)
        if (is_branch(*link))
            bl = link;
    if (!bl)
        return false;
    A32_Instr *br = *bl;
    const char *l1 = target(br);
    if (references(fn, l1) != 1)
        return false;
    Arm a, c;
    if (!walk_arm(fn, b, &br->next, l1, &a))
        return false;
    if (a.jump) {
        // A diamond: the code after the jump must be the branch's label, then the other
        // side, to the jump's target.
        if (!a.after || !a.after->label || strcmp(a.after->label, l1) != 0 ||
            !walk_arm(fn, a.after, &a.after->head, target(*a.jump), &c) || c.jump)
            return false;
        for (int i = 0; i < c.n; i++)
            c.instr[i]->cond = br->cond;
        delete_at(a.jump);
    }
    for (int i = 0; i < a.n; i++)
        a.instr[i]->cond = invert(br->cond);
    delete_at(bl);
    return true;
}

// Adjacent `ldr a, [b, #o]` and `ldr a + 1, [b, #o + 4]` (or str) of a frame slot, a
// even and not lr, in either order: ldrd (strd).  sp and r11 are 8-byte aligned, so a
// word-aligned offset is a word-aligned address, as ldrd needs (a slot of bytes is
// not, and ldr does not mind).  Not a volatile access: it is made as it was written.
static bool pair(A32_Instr **link)
{
    A32_Instr *x = *link, *y = x->next;
    if (!y || y->op != x->op || (x->op != A32_LDR && x->op != A32_STR) || x->cond != A32_AL ||
        y->cond != A32_AL || x->is_volatile || y->is_volatile)
        return false;
    const A32_Operand *xm = &x->opnd[1], *ym = &y->opnd[1];
    if (!is_reg(&x->opnd[0]) || !is_reg(&y->opnd[0]) || xm->kind != A32_OPND_MEM ||
        ym->kind != A32_OPND_MEM || xm->sub != A32_MEM_OFFSET || ym->sub != A32_MEM_OFFSET ||
        xm->reg2 >= 0 || ym->reg2 >= 0 || xm->reg != ym->reg ||
        (xm->reg != A32_SP && xm->reg != A32_FP))
        return false;
    bool up                  = ym->imm == xm->imm + 4;
    const A32_Instr *lo      = up ? x : y, *hi = up ? y : x;
    int r                    = lo->opnd[0].reg;
    const A32_Operand addr   = lo->opnd[1];
    if ((!up && ym->imm != xm->imm - 4) || r % 2 || r >= A32_LR || hi->opnd[0].reg != r + 1 ||
        !fits(A32_LDRD, addr.imm) || addr.imm % 4)
        return false;
    if (x->op == A32_LDR && (r == xm->reg || r + 1 == xm->reg))
        return false;
    x->op      = x->op == A32_LDR ? A32_LDRD : A32_STRD;
    x->opnd[0] = a32_reg(r);
    x->opnd[1] = a32_reg(r + 1);
    x->opnd[2] = addr;
    delete_at(&x->next);
    return true;
}

// A frame that is only `push {lr}`, laid out because lr was in use as a scratch
// register, which the rewrites have since freed: with no call, no other mention of lr,
// and no use of sp, the push goes and each `pop {pc}` is `bx lr`.
static void drop_lr_save(A32_Func *fn)
{
    A32_Instr **push = NULL;
    for (A32_Block *b = fn->blocks; b; b = b->next) {
        for (A32_Instr **link = &b->head; *link; link = &(*link)->next) {
            const A32_Instr *in = *link;
            if (in->op == A32_PUSH && in->opnd[0].kind == A32_OPND_REGLIST &&
                in->opnd[0].width == A32_CORE && in->opnd[0].imm == 1 << A32_LR && !push &&
                in->cond == A32_AL) {
                push = link;
                continue;
            }
            if (in->op == A32_POP && in->opnd[0].kind == A32_OPND_REGLIST &&
                in->opnd[0].width == A32_CORE && in->opnd[0].imm == 1 << A32_PC)
                continue;
            if (is_call(in) || in->op == A32_PUSH || in->op == A32_POP || in->op == A32_VPUSH ||
                in->op == A32_VPOP || ((uses(in) | defs(in)) & (BIT(A32_LR) | BIT(A32_SP))))
                return;
        }
    }
    if (!push)
        return;
    delete_at(push);
    for (A32_Block *b = fn->blocks; b; b = b->next) {
        for (A32_Instr *in = b->head; in; in = in->next) {
            if (in->op == A32_POP) {
                in->op      = A32_BX;
                in->opnd[0] = a32_reg(A32_LR);
            }
        }
    }
}

// The rewrites to a fixed point, then the conditional instructions, then the pairing of
// loads and stores, which would hide a store from the deletion of its reload; last, an
// lr no longer in use is not saved.
void a32_peephole(A32_Func *fn, uint64_t result)
{
    live_info.result = result;
    bool changed     = true;
    while (changed) {
        changed = false;
        compute_liveness(fn);
        for (A32_Block *b = fn->blocks; b; b = b->next) {
            live_info.cur = b;
            while (fold_bitfields(b))
                changed = true;
            for (A32_Instr **link = &b->head; *link;) {
                if (rewrite(link))
                    changed = true;
                else
                    link = &(*link)->next;
            }
        }
        for (A32_Block *b = fn->blocks; b; b = b->next)
            while (rewrite_block_end(b))
                changed = true;
        for (A32_Block *b = fn->blocks; b && !changed; b = b->next)
            changed = predicate(fn, b);
    }
    free_liveness();
    for (A32_Block *b = fn->blocks; b; b = b->next)
        for (A32_Instr **link = &b->head; *link; link = &(*link)->next)
            pair(link);
    drop_lr_save(fn);
    for (A32_Block *b = fn->blocks; b; b = b->next) {
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}
