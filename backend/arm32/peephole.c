//
// Peephole pass over the ARM32 IR, after register allocation and the frame:
//   - a move folds into its uses within the block, a result is computed where it is
//     moved, a move to itself goes, and so does the reload of what was just stored (or
//     it becomes a move);
//   - an address computation folds into the load or store it feeds ([base, #imm] or
//     [base, index, lsl #s]), a shift by a constant into the operand2 it feeds, a mask
//     into a tst, mul + add into mla (mul + sub, mls);
//   - a jump to the next label goes, a branch over a jump branches the other way, code
//     after a jump or return goes;
//   - a short diamond or triangle of a conditional branch becomes conditional
//     instructions, when they set no flags and make no call;
//   - adjacent ldr/str of a frame slot pair, of an even register and the next, are
//     ldrd/strd, last.
// Whether a value is read again is decided by the liveness of the registers over the
// function's blocks, computed afresh after each round of rewrites.  Registers are
// tracked as sets, s registers apart, so a d register is the two s registers it holds.
// A conditional instruction may leave its destination as it was: it reads it too.
//
#include <string.h>

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
            if (!def_operand(in, i) || in->cond != A32_AL || in->op == A32_MOVT)
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
    const A32_Block *cur;
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
    for (A32_Block *b = fn->blocks; b; b = b->next)
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
                if (in->cond != A32_AL || in->op == A32_MOVT)
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

// `mov t, r` with t scratch: the reads of t up to its next write read r instead, if r is
// not written before the last of them and each can.
static bool forward_move(A32_Instr **link)
{
    A32_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    A32_Width w = mv->opnd[0].width;
    Regs tb = reg_set(t, w), rb = reg_set(r, w);
    if ((tb & ~SCRATCH) || t == r || r == A32_SP || r == A32_PC)
        return false;
    bool clobbered = false;
    A32_Instr *end = NULL;
    for (A32_Instr *n = mv->next; n; n = n->next) {
        if ((uses(n) & tb) && (clobbered || !can_substitute(n, t, w)))
            return false;
        Regs d = defs(n);
        if (n->cond == A32_AL && (d & tb) == tb) {
            end = n->next;
            break;
        }
        if (d & tb)
            return false;
        if (d & rb)
            clobbered = true;
    }
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
        in->op == A32_LDRD || in->op == A32_MOVT)
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
            for (A32_Instr *m = in->next; m != n; m = m->next)
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
        A32_Instr *n = *link;
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
static bool delete_reload(A32_Instr *st)
{
    if ((st->op != A32_STR && st->op != A32_VSTR) || st->cond != A32_AL)
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

// One rewrite at *link; true when something changed.
static bool rewrite(A32_Instr **link)
{
    A32_Instr *in = *link;
    A32_Operand *o = in->opnd;
    // A move to itself.
    if (is_move(in) && o[0].reg == o[1].reg) {
        delete_at(link);
        return true;
    }
    // An add or sub of zero: a move.
    if ((in->op == A32_ADD || in->op == A32_SUB) && in->cond == A32_AL && !in->set_flags &&
        is_reg(&o[0]) && is_reg(&o[1]) && o[2].kind == A32_OPND_IMM && o[2].imm == 0 &&
        o[3].kind == A32_OPND_NONE) {
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
    for (A32_Instr *in = b->head; in; in = in->next)
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
static bool predicate(A32_Func *fn, A32_Block *b)
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
// not, and ldr does not mind).
static bool pair(A32_Instr **link)
{
    A32_Instr *x = *link, *y = x->next;
    if (!y || y->op != x->op || (x->op != A32_LDR && x->op != A32_STR) || x->cond != A32_AL ||
        y->cond != A32_AL)
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

// The rewrites to a fixed point, then the conditional instructions, then the pairing of
// loads and stores, which would hide a store from the deletion of its reload.
void a32_peephole(A32_Func *fn, uint64_t result)
{
    live_info.result = result;
    bool changed     = true;
    while (changed) {
        changed = false;
        compute_liveness(fn);
        for (A32_Block *b = fn->blocks; b; b = b->next) {
            live_info.cur = b;
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
    for (A32_Block *b = fn->blocks; b; b = b->next) {
        for (A32_Instr **link = &b->head; *link; link = &(*link)->next)
            pair(link);
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}
