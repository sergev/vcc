//
// Peephole pass over the AArch64 IR, after register allocation and the frame:
//   - an operation on a just-loaded constant takes the immediate form (add/sub/cmp with
//     12 bits, optionally shifted; and/orr/eor with a bitmask immediate; shifts), and a
//     zero is stored from the zero register;
//   - a move folds into its uses within the block, a result is computed where it is
//     moved, a move to itself goes (a W one when the upper half it clears is not
//     read), and so does the reload of what was just stored;
//   - a constant index register is an offset;
//   - an address computation folds into the load or store it feeds ([base, #imm] or
//     [base, index, lsl/sxtw #s]), and a sign extension into the add that scales it;
//   - mul + add is madd (mul + sub, msub); adjacent ldr/str of one base are ldp/stp,
//     last;
//   - cmp #0 + b.eq/b.ne is cbz/cbnz; a jump to the next label goes, a branch over a
//     jump branches the other way, and code after a jump or return goes.
// Code selection never carries a scratch register (x9-x17, v16-v31) past its block, so
// whether a scratch value is read again is decided by looking to the end of the block;
// any register is dead once an instruction writes it.  A move or computation in the W
// view zeroes the upper half, so one is folded only where that cannot matter.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

static bool is_call(A64_Op op)
{
    return op == A64_BL || op == A64_BLR;
}

static bool is_branch(A64_Op op)
{
    return op == A64_BCOND || op == A64_CBZ || op == A64_CBNZ;
}

// Operand 0 is read, not written.
static bool no_dest(A64_Op op)
{
    switch (op) {
    case A64_STR:
    case A64_STRB:
    case A64_STRH:
    case A64_STP:
    case A64_CMP:
    case A64_CMN:
    case A64_FCMP:
    case A64_CBZ:
    case A64_CBNZ:
    case A64_B:
    case A64_BCOND:
    case A64_BL:
    case A64_BLR:
    case A64_RET:
        return true;
    default:
        return false;
    }
}

static bool is_scratch(int r)
{
    return (r >= A64_X(9) && r <= A64_X(17)) || (r >= A64_V(16) && r < A64_VREG);
}

// The registers a call reads: arguments and the indirect result address.
static bool is_arg(int r)
{
    return (r >= A64_X0 && r <= A64_X8) || (r >= A64_V0 && r < A64_V(8));
}

static bool is_store(A64_Op op)
{
    return op == A64_STR || op == A64_STRB || op == A64_STRH || op == A64_STP;
}

static bool is_mem_op(A64_Op op)
{
    switch (op) {
    case A64_LDR:
    case A64_LDRB:
    case A64_LDRSB:
    case A64_LDRH:
    case A64_LDRSH:
    case A64_LDRSW:
    case A64_STR:
    case A64_STRB:
    case A64_STRH:
        return true;
    default:
        return false;
    }
}

// Whether operand `i` of `in` reads register r as a register (not as an address).
static bool reads_operand(const A64_Instr *in, int i, int r)
{
    const A64_Operand *o = &in->opnd[i];
    if ((o->kind != A64_OPND_REG && o->kind != A64_OPND_SHIFT && o->kind != A64_OPND_EXT) ||
        o->reg != r)
        return false;
    if (i > 0)
        return !(in->op == A64_LDP && i == 1);
    return no_dest(in->op) || in->op == A64_MOVK;
}

static bool reads_as_address(const A64_Instr *in, int r)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        const A64_Operand *o = &in->opnd[i];
        if (o->kind == A64_OPND_MEM && (o->reg == r || (o->sub == A64_MEM_INDEX && o->index == r)))
            return true;
    }
    return false;
}

static bool reads(const A64_Instr *in, int r)
{
    if (is_call(in->op) && is_arg(r))
        return true;
    if (in->op == A64_RET)
        return r == A64_X0 || r == A64_X(1) || (r >= A64_V0 && r < A64_V(4)) || r == A64_LR;
    for (int i = 0; i < A64_MAX_OPERANDS; i++)
        if (reads_operand(in, i, r))
            return true;
    return reads_as_address(in, r);
}

static bool writes(const A64_Instr *in, int r)
{
    if (is_call(in->op))
        return is_scratch(r) || is_arg(r) || r == A64_LR || (r >= A64_X(10) && r <= A64_X(18));
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        const A64_Operand *o = &in->opnd[i];
        if (o->kind == A64_OPND_MEM && o->sub != A64_MEM_OFFSET && o->sub != A64_MEM_INDEX &&
            o->reg == r)
            return true; // writeback
    }
    if (no_dest(in->op) || in->opnd[0].kind != A64_OPND_REG)
        return false;
    return in->opnd[0].reg == r || (in->op == A64_LDP && in->opnd[1].reg == r);
}

// The value `in` leaves in scratch register r is never read.
static bool dead_after(const A64_Instr *in, int r)
{
    if (!is_scratch(r))
        return false;
    for (const A64_Instr *n = in->next; n; n = n->next) {
        if (reads(n, r))
            return false;
        if (writes(n, r))
            return true;
    }
    return true;
}

// The value `in` leaves in register r is never read: as dead_after, and for any other
// register, written again before a branch, or an argument register at a return that
// does not read it.
static bool dies_after(const A64_Instr *in, int r)
{
    if (is_scratch(r))
        return dead_after(in, r);
    for (const A64_Instr *n = in->next; n; n = n->next) {
        if (reads(n, r))
            return false;
        if (writes(n, r))
            return true;
        if (n->op == A64_RET)
            return is_arg(r);
        if (is_branch(n->op) || n->op == A64_B)
            return false;
    }
    return false;
}

// The value r holds before `in` is not read after it: `in` writes r, or r is a scratch
// register not read again.
static bool last_read(const A64_Instr *in, int r)
{
    return writes(in, r) || dead_after(in, r);
}

static void free_instr(A64_Instr *in)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

// Unlink and free *link.
static void delete_at(A64_Instr **link)
{
    A64_Instr *in = *link;
    *link         = in->next;
    free_instr(in);
}

static int width_bits(A64_Width w)
{
    return w == A64_W || w == A64_S ? 32 : w == A64_Q ? 128 : 64;
}

// The bytes a load or store accesses.
static int access_size(const A64_Instr *in)
{
    switch (in->op) {
    case A64_LDRB:
    case A64_LDRSB:
    case A64_STRB:
        return 1;
    case A64_LDRH:
    case A64_LDRSH:
    case A64_STRH:
        return 2;
    case A64_LDRSW:
        return 4;
    default:
        return width_bits(in->opnd[0].width) / 8;
    }
}

// An offset an ldr/str of `size` bytes takes: scaled unsigned 12 bits, or signed 9.
static bool fits_ldst(int64_t off, int size)
{
    return (off >= -256 && off <= 255) || (off >= 0 && off % size == 0 && off / size <= 4095);
}

// A value an add/sub immediate takes: 12 bits, optionally shifted left by 12; sets the
// operands.
static bool arith_imm(uint64_t v, A64_Operand *imm, A64_Operand *shift)
{
    if (v <= 4095) {
        *imm   = a64_imm((int64_t)v);
        *shift = (A64_Operand){ 0 };
        return true;
    }
    if ((v & 0xfff) == 0 && (v >> 12) <= 4095) {
        *imm   = a64_imm((int64_t)(v >> 12));
        *shift = a64_lsl(12);
        return true;
    }
    return false;
}

// Whether `v` (of `bits` bits) is a logical (bitmask) immediate: a repetition of an
// element of 2..64 bits that is a rotated run of ones, neither all zeros nor all ones.
static bool bitmask_imm(uint64_t v, int bits)
{
    uint64_t mask = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
    v &= mask;
    if (v == 0 || v == mask)
        return false;
    for (int e = 2; e <= bits; e *= 2) {
        uint64_t emask = e == 64 ? ~0ULL : (1ULL << e) - 1;
        uint64_t elem  = v & emask;
        bool repeats   = true;
        for (int i = e; i < bits && repeats; i += e)
            repeats = ((v >> i) & emask) == elem;
        if (!repeats)
            continue;
        for (int r = 0; r < e; r++) {
            uint64_t rot = r ? ((elem >> r) | (elem << (e - r))) & emask : elem;
            if (rot && rot != emask && (rot & (rot + 1)) == 0)
                return true;
        }
        return false;
    }
    return false;
}

static void set_imm(A64_Instr *in, A64_Operand imm, A64_Operand shift)
{
    in->opnd[2] = imm;
    in->opnd[3] = shift;
}

// `mov t, #imm` followed by an instruction reading t once, at its last read: the
// immediate form, or the zero register for a stored zero.
static bool fold_constant(A64_Instr **link)
{
    A64_Instr *mv = *link, *n = mv->next;
    int t         = mv->opnd[0].reg;
    if (!n || !is_scratch(t) || !reads(n, t) || !last_read(n, t))
        return false;
    A64_Operand *o = n->opnd;
    A64_Width w    = o[0].width;
    int bits       = width_bits(mv->opnd[0].width);
    int64_t v      = mv->opnd[1].imm;
    if (bits == 32)
        v = (int32_t)v;
    // A constant index: an offset.
    A64_Operand *m = &o[1];
    if (is_mem_op(n->op) && m->kind == A64_OPND_MEM && m->sub == A64_MEM_INDEX &&
        m->index == t && m->reg != t && !(o[0].kind == A64_OPND_REG && o[0].reg == t)) {
        int64_t index = v;
        if (m->index_width == A64_W)
            index = m->ext == A64_EXT_SXTW ? (int64_t)(int32_t)v : (int64_t)(uint32_t)v;
        else if (bits == 32)
            index = (int64_t)(uint32_t)v; // a W write cleared the upper half
        int64_t off = index * (1 << m->imm);
        if (!fits_ldst(off, access_size(n)))
            return false;
        *m = a64_mem(m->reg, off);
        delete_at(link);
        return true;
    }
    if (reads_as_address(n, t))
        return false;
    bool regs3 = o[0].kind == A64_OPND_REG && o[1].kind == A64_OPND_REG &&
                 o[2].kind == A64_OPND_REG && o[3].kind == A64_OPND_NONE;
    A64_Operand imm, shift;
    switch (n->op) {
    case A64_ADD:
    case A64_AND:
    case A64_ORR:
    case A64_EOR:
        // Commutative: the constant second.
        if (regs3 && o[1].reg == t && o[2].reg != t) {
            o[1].reg = o[2].reg;
            o[2].reg = t;
        }
        // fall through
    case A64_SUB:
        if (!regs3 || o[2].reg != t || o[1].reg == t || o[1].reg == A64_ZR || o[0].width != w)
            return false;
        if (n->op == A64_ADD || n->op == A64_SUB) {
            bool neg = v < 0 && bits == width_bits(w);
            uint64_t a = neg ? -(uint64_t)v : (uint64_t)v;
            if (bits != width_bits(w) && v < 0)
                return false;
            if (!arith_imm(a, &imm, &shift))
                return false;
            if (neg)
                n->op = n->op == A64_ADD ? A64_SUB : A64_ADD;
            set_imm(n, imm, shift);
        } else {
            if (bits != width_bits(w) || !bitmask_imm((uint64_t)v, bits))
                return false;
            set_imm(n, a64_imm(v), (A64_Operand){ 0 });
        }
        break;
    case A64_CMP:
        if (o[0].kind != A64_OPND_REG || o[1].kind != A64_OPND_REG || o[1].reg != t ||
            o[0].reg == t || bits != width_bits(o[0].width))
            return false;
        {
            bool neg   = v < 0;
            uint64_t a = neg ? -(uint64_t)v : (uint64_t)v;
            if (!arith_imm(a, &imm, &shift))
                return false;
            if (neg)
                n->op = A64_CMN;
            n->opnd[1] = imm;
            n->opnd[2] = shift;
        }
        break;
    case A64_LSL:
    case A64_LSR:
    case A64_ASR:
        if (!regs3 || o[2].reg != t || o[1].reg == t)
            return false;
        set_imm(n, a64_imm(v & (width_bits(w) - 1)), (A64_Operand){ 0 });
        break;
    case A64_STR:
    case A64_STRB:
    case A64_STRH:
        if (v != 0 || o[0].kind != A64_OPND_REG || o[0].reg != t || a64_is_fpreg(t))
            return false;
        o[0].reg = A64_ZR;
        break;
    default:
        return false;
    }
    delete_at(link);
    return true;
}

static bool is_move(const A64_Instr *in)
{
    return (in->op == A64_MOV || in->op == A64_FMOV) && in->opnd[0].kind == A64_OPND_REG &&
           in->opnd[1].kind == A64_OPND_REG && in->opnd[2].kind == A64_OPND_NONE &&
           a64_is_fpreg(in->opnd[0].reg) == a64_is_fpreg(in->opnd[1].reg) &&
           in->opnd[0].width == in->opnd[1].width;
}

// Whether every read of t in `in` can read r instead, after `mov t, r` at width w: an
// X move anywhere r may stand, a W or FP move only where t is read at that width.
static bool can_substitute(const A64_Instr *in, int t, A64_Width w)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        const A64_Operand *o = &in->opnd[i];
        bool mem             = o->kind == A64_OPND_MEM && (o->reg == t || (o->sub == A64_MEM_INDEX && o->index == t));
        if (mem && w != A64_X)
            return false;
        if (!reads_operand(in, i, t))
            continue;
        if (w != A64_X && (o->kind != A64_OPND_REG || o->width != w))
            return false;
    }
    return true;
}

static void substitute(A64_Instr *in, int t, int r)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        A64_Operand *o = &in->opnd[i];
        if (reads_operand(in, i, t))
            o->reg = r;
        if (o->kind == A64_OPND_MEM && o->reg == t)
            o->reg = r;
        if (o->kind == A64_OPND_MEM && o->sub == A64_MEM_INDEX && o->index == t)
            o->index = r;
    }
}

// `mov t, r` with t scratch: the reads of t up to its next write read r instead, if r is
// not written before the last of them and each can.
static bool forward_move(A64_Instr **link)
{
    A64_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    A64_Width w = mv->opnd[0].width;
    if (!is_scratch(t) || t == r || r == A64_SP || r == A64_ZR)
        return false;
    bool clobbered = false;
    A64_Instr *end = NULL;
    for (A64_Instr *n = mv->next; n; n = n->next) {
        if (reads(n, t) && (clobbered || is_call(n->op) || !can_substitute(n, t, w)))
            return false;
        if (writes(n, t)) {
            end = n->next;
            break;
        }
        if (writes(n, r))
            clobbered = true;
    }
    for (A64_Instr *n = mv->next; n != end; n = n->next)
        substitute(n, t, r);
    delete_at(link);
    return true;
}

// Whether `in` writes register operand 0 alone, from its other operands: the result
// could go to another register.
static bool computes(const A64_Instr *in)
{
    if (no_dest(in->op) || is_call(in->op) || in->op == A64_MOVK || in->op == A64_LDP ||
        in->opnd[0].kind != A64_OPND_REG || in->opnd[0].reg == A64_SP)
        return false;
    for (int i = 1; i < A64_MAX_OPERANDS; i++)
        if (in->opnd[i].kind == A64_OPND_MEM && in->opnd[i].sub != A64_MEM_OFFSET &&
            in->opnd[i].sub != A64_MEM_INDEX)
            return false;
    return true;
}

// `in` computes into t, and a later `mov d, t` is the last read of it: compute into d,
// when d is neither read nor written in between, nor (t not scratch) a branch passed.  The reads of t in between read d.  A
// W move zeroes the upper half, so it takes a W result only.
static bool compute_in_place(A64_Instr *in)
{
    int t       = in->opnd[0].reg;
    A64_Width w = in->opnd[0].width;
    for (A64_Instr **link = &in->next; *link; link = &(*link)->next) {
        A64_Instr *n = *link;
        if (is_move(n) && n->opnd[1].reg == t && n->opnd[0].reg != t &&
            (n->opnd[0].width == w || (n->opnd[0].width == A64_X && w == A64_W)) &&
            n->opnd[0].reg != A64_SP && n->opnd[0].reg != A64_ZR && dies_after(n, t)) {
            int d = n->opnd[0].reg;
            for (A64_Instr *m = in->next; m != n; m = m->next)
                if (reads(m, d) || writes(m, d) || !can_substitute(m, t, A64_X))
                    return false;
            for (A64_Instr *m = in->next; m != n; m = m->next)
                substitute(m, t, d);
            in->opnd[0].reg = d;
            delete_at(link);
            return true;
        }
        // Beyond a branch, a register other than a scratch one may still be read.
        if (writes(n, t) || is_call(n->op) ||
            (!is_scratch(t) && (is_branch(n->op) || n->op == A64_B)))
            return false;
    }
    return false;
}

// Delete a later load of what store `st` wrote, into the same register at the same
// width, when nothing between changes the register, the base or memory that may
// overlap.  Only a frame slot (sp or x29) is followed past other instructions.  Not a W
// store: a W load would clear an upper half the register may not have had clear.  A
// volatile access is neither of the two.
static bool delete_reload(A64_Instr *st)
{
    const A64_Operand *v = &st->opnd[0], *m = &st->opnd[1];
    if (st->op != A64_STR || st->is_volatile || v->kind != A64_OPND_REG || v->width == A64_W || v->width == A64_S ||
        v->reg == A64_ZR || m->kind != A64_OPND_MEM || m->sub != A64_MEM_OFFSET || m->reg == v->reg)
        return false;
    int size = width_bits(v->width) / 8;
    for (A64_Instr **link = &st->next; *link; link = &(*link)->next) {
        A64_Instr *n = *link;
        const A64_Operand *nm = &n->opnd[1];
        if (n->op == A64_LDR && n->opnd[0].reg == v->reg && n->opnd[0].width == v->width &&
            nm->kind == A64_OPND_MEM && nm->sub == A64_MEM_OFFSET && nm->reg == m->reg &&
            nm->imm == m->imm) {
            if (n->is_volatile)
                return false;
            delete_at(link);
            return true;
        }
        if (m->reg != A64_SP && m->reg != A64_FP)
            return false;
        if (is_call(n->op) || writes(n, v->reg) || writes(n, m->reg))
            return false;
        if (is_store(n->op)) {
            const A64_Operand *sm = &n->opnd[n->op == A64_STP ? 2 : 1];
            int nsize = n->op == A64_STP ? 2 * (width_bits(n->opnd[0].width) / 8) : access_size(n);
            if (sm->kind != A64_OPND_MEM || sm->sub != A64_MEM_OFFSET || sm->reg != m->reg ||
                (sm->imm < m->imm + size && m->imm < sm->imm + nsize))
                return false;
        }
    }
    return false;
}

static int log2_of(int n)
{
    int s = 0;
    while ((1 << s) < n)
        s++;
    return s;
}

// `add t, b, #imm` (or sub), or `add t, a, index, lsl/sxtw #s`, feeding the address of
// the load or store after it, at its last read: the address computed by that
// instruction instead.
static bool fold_address(A64_Instr **link)
{
    A64_Instr *add = *link, *n = add->next;
    const A64_Operand *o = add->opnd;
    if (!n || !is_mem_op(n->op) || o[0].kind != A64_OPND_REG || o[0].width != A64_X ||
        o[1].kind != A64_OPND_REG || o[1].reg == A64_ZR)
        return false;
    int t          = o[0].reg;
    A64_Operand *m = &n->opnd[1];
    if (m->kind != A64_OPND_MEM || m->sub != A64_MEM_OFFSET || m->reg != t ||
        (n->opnd[0].kind == A64_OPND_REG && n->opnd[0].reg == t && no_dest(n->op)) ||
        !last_read(n, t))
        return false;
    int size = access_size(n);
    if (o[2].kind == A64_OPND_IMM && o[3].kind == A64_OPND_NONE) {
        int64_t off = m->imm + (add->op == A64_SUB ? -o[2].imm : o[2].imm);
        if ((add->op != A64_ADD && add->op != A64_SUB) || !fits_ldst(off, size))
            return false;
        m->reg = o[1].reg;
        m->imm = off;
    } else if (add->op == A64_ADD && m->imm == 0 && o[3].kind == A64_OPND_NONE &&
               (o[2].kind == A64_OPND_REG || o[2].kind == A64_OPND_SHIFT ||
                o[2].kind == A64_OPND_EXT)) {
        int shift = o[2].kind == A64_OPND_REG ? 0 : (int)o[2].imm;
        if ((o[2].kind == A64_OPND_SHIFT && o[2].sub != A64_SHIFT_LSL) ||
            (o[2].kind == A64_OPND_EXT && o[2].sub != A64_EXT_SXTW && o[2].sub != A64_EXT_UXTW) ||
            (o[2].kind != A64_OPND_EXT && o[2].width != A64_X) ||
            (shift != 0 && shift != log2_of(size)) || o[2].reg == A64_SP)
            return false;
        *m = a64_mem_index(o[1].reg, o[2].reg, o[2].width,
                           o[2].kind == A64_OPND_EXT ? (A64_Extend)o[2].sub : A64_EXT_UXTX, shift);
    } else {
        return false;
    }
    delete_at(link);
    return true;
}

// `sxtw t, w` and `add d, a, t, lsl #s` at its last read: `add d, a, w, sxtw #s`.
static bool fold_extend(A64_Instr **link)
{
    A64_Instr *ext = *link, *n = ext->next;
    if (!n || n->op != A64_ADD || ext->opnd[0].width != A64_X)
        return false;
    int t = ext->opnd[0].reg;
    A64_Operand *o = n->opnd;
    bool shifted   = o[2].kind == A64_OPND_SHIFT && o[2].sub == A64_SHIFT_LSL && o[2].imm <= 4;
    if (o[0].width != A64_X || o[1].kind != A64_OPND_REG || o[1].reg == t ||
        (!shifted && o[2].kind != A64_OPND_REG) || o[2].reg != t || o[2].width != A64_X ||
        o[3].kind != A64_OPND_NONE || !last_read(n, t))
        return false;
    o[2] = a64_ext(ext->opnd[1].reg, A64_W, A64_EXT_SXTW, shifted ? (int)o[2].imm : 0);
    delete_at(link);
    return true;
}

// `mul t, a, b` and `add d, c, t` (or `sub d, c, t`) at its last read: madd (msub).
static bool fold_multiply(A64_Instr **link)
{
    A64_Instr *mul = *link, *n = mul->next;
    if (!n || (n->op != A64_ADD && n->op != A64_SUB))
        return false;
    int t = mul->opnd[0].reg;
    A64_Operand *o = n->opnd;
    if (o[0].kind != A64_OPND_REG || o[1].kind != A64_OPND_REG || o[2].kind != A64_OPND_REG ||
        o[3].kind != A64_OPND_NONE || o[0].width != mul->opnd[0].width || !last_read(n, t))
        return false;
    int c;
    if (o[2].reg == t && o[1].reg != t)
        c = o[1].reg;
    else if (n->op == A64_ADD && o[1].reg == t && o[2].reg != t)
        c = o[2].reg;
    else
        return false;
    if (c == A64_SP)
        return false;
    n->op      = n->op == A64_ADD ? A64_MADD : A64_MSUB;
    n->opnd[1] = mul->opnd[1];
    n->opnd[2] = mul->opnd[2];
    n->opnd[3] = a64_reg(c, o[0].width);
    mul->opnd[1].sym = mul->opnd[2].sym = NULL;
    delete_at(link);
    return true;
}

// Adjacent `ldr a, [b, #o]` and `ldr c, [b, #o + size]` (or str), in either order:
// ldp (stp) of the lower address's register first.  Not a volatile access: it is made
// as it was written.
static bool pair(A64_Instr **link)
{
    A64_Instr *x = *link, *y = x->next;
    if (!y || y->op != x->op || (x->op != A64_LDR && x->op != A64_STR) || x->is_volatile ||
        y->is_volatile)
        return false;
    const A64_Operand *xm = &x->opnd[1], *ym = &y->opnd[1];
    A64_Width w = x->opnd[0].width;
    int size    = width_bits(w) / 8;
    if (x->opnd[0].kind != A64_OPND_REG || y->opnd[0].kind != A64_OPND_REG ||
        y->opnd[0].width != w || a64_is_fpreg(x->opnd[0].reg) != a64_is_fpreg(y->opnd[0].reg) ||
        xm->kind != A64_OPND_MEM || ym->kind != A64_OPND_MEM || xm->sub != A64_MEM_OFFSET ||
        ym->sub != A64_MEM_OFFSET || xm->reg != ym->reg)
        return false;
    bool up            = ym->imm == xm->imm + size;
    const A64_Operand *lo = up ? xm : ym;
    if ((!up && ym->imm != xm->imm - size) || lo->imm % size != 0 || lo->imm < -64 * size ||
        lo->imm > 63 * size)
        return false;
    if (x->op == A64_LDR &&
        (x->opnd[0].reg == xm->reg || x->opnd[0].reg == y->opnd[0].reg || y->opnd[0].reg == xm->reg))
        return false;
    A64_Operand first = up ? x->opnd[0] : y->opnd[0], second = up ? y->opnd[0] : x->opnd[0];
    A64_Operand addr  = *lo;
    x->op             = x->op == A64_LDR ? A64_LDP : A64_STP;
    x->opnd[0]        = first;
    x->opnd[1]        = second;
    x->opnd[2]        = addr;
    delete_at(&x->next);
    return true;
}

// `mov w, w` of register r: it clears the upper half, which goes when every read of r
// up to its next write, within the block, is of the lower half (a W register operand).
static bool upper_unread(const A64_Instr *in, int r)
{
    for (const A64_Instr *n = in->next; n; n = n->next) {
        if (is_call(n->op) || n->op == A64_RET || is_branch(n->op) || n->op == A64_B ||
            reads_as_address(n, r))
            return false;
        for (int i = 0; i < A64_MAX_OPERANDS; i++)
            if (reads_operand(n, i, r) && (n->opnd[i].kind != A64_OPND_REG || n->opnd[i].width != A64_W))
                return false;
        if (writes(n, r))
            return true;
    }
    return false;
}

// One rewrite at *link; true when something changed.
static bool rewrite(A64_Instr **link)
{
    A64_Instr *in = *link, *next = in->next;
    A64_Operand *o = in->opnd;

    // A move to itself: of a whole register (a W move clears the upper half).
    if (is_move(in) && o[0].reg == o[1].reg &&
        (in->op == A64_FMOV || o[0].width == A64_X || upper_unread(in, o[0].reg))) {
        delete_at(link);
        return true;
    }
    if (!next)
        return false;
    if (in->op == A64_MOV && o[0].kind == A64_OPND_REG && o[1].kind == A64_OPND_IMM &&
        fold_constant(link))
        return true;
    if (is_move(in) && forward_move(link))
        return true;
    if (computes(in) && compute_in_place(in))
        return true;
    if ((in->op == A64_ADD || in->op == A64_SUB) && fold_address(link))
        return true;
    if (in->op == A64_SXTW && fold_extend(link))
        return true;
    if (in->op == A64_MUL && fold_multiply(link))
        return true;
    if (delete_reload(in))
        return true;
    // cmp r, #0 and b.eq/b.ne: cbz/cbnz.
    if (in->op == A64_CMP && o[1].kind == A64_OPND_IMM && o[1].imm == 0 &&
        o[2].kind == A64_OPND_NONE && next->op == A64_BCOND &&
        (next->opnd[0].sub == A64_EQ || next->opnd[0].sub == A64_NE)) {
        next->op      = next->opnd[0].sub == A64_EQ ? A64_CBZ : A64_CBNZ;
        next->opnd[0] = o[0];
        delete_at(link);
        return true;
    }
    return false;
}

// The label a branch or jump goes to.
static A64_Operand *target(A64_Instr *in)
{
    return &in->opnd[in->op == A64_B ? 0 : 1];
}

// Whether label `l` is on `b` or on an empty block between `b` and the next code.
static bool falls_to(const A64_Block *b, const char *l)
{
    for (; b; b = b->next) {
        if (b->label && strcmp(b->label, l) == 0)
            return true;
        if (b->head)
            return false;
    }
    return false;
}

static A64_Instr **last_link(A64_Block *b, int back)
{
    A64_Instr **link = &b->head;
    int n            = 0;
    for (A64_Instr *in = b->head; in; in = in->next)
        n++;
    if (n <= back)
        return NULL;
    for (int i = 0; i < n - 1 - back; i++)
        link = &(*link)->next;
    return link;
}

static bool rewrite_block_end(A64_Block *b)
{
    // Nothing runs after a jump or return.
    for (A64_Instr *in = b->head; in; in = in->next) {
        if ((in->op == A64_B || in->op == A64_RET) && in->next) {
            while (in->next)
                delete_at(&in->next);
            return true;
        }
    }
    A64_Instr **jl = last_link(b, 0);
    if (!jl || (*jl)->op != A64_B)
        return false;
    // A branch over a jump: branch the other way to the jump's target.
    A64_Instr **bl = last_link(b, 1);
    if (bl && is_branch((*bl)->op) && falls_to(b->next, target(*bl)->sym)) {
        A64_Instr *br = *bl;
        if (br->op == A64_BCOND)
            br->opnd[0].sub ^= 1;
        else
            br->op = br->op == A64_CBZ ? A64_CBNZ : A64_CBZ;
        xfree(target(br)->sym);
        *target(br)          = (*jl)->opnd[0];
        (*jl)->opnd[0].sym   = NULL;
        delete_at(&br->next);
        return true;
    }
    // A jump to the next label.
    if (falls_to(b->next, (*jl)->opnd[0].sym)) {
        delete_at(jl);
        return true;
    }
    return false;
}

// The rewrites to a fixed point, then the pairing of loads and stores, which would
// hide a store from the deletion of its reload.
void a64_peephole(A64_Func *fn)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (A64_Block *b = fn->blocks; b; b = b->next) {
            for (A64_Instr **link = &b->head; *link;) {
                if (rewrite(link))
                    changed = true;
                else
                    link = &(*link)->next;
            }
            while (rewrite_block_end(b))
                changed = true;
        }
    }
    for (A64_Block *b = fn->blocks; b; b = b->next) {
        for (A64_Instr **link = &b->head; *link; link = &(*link)->next)
            pair(link);
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}
