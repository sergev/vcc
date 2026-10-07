//
// Peephole pass over the AArch64 IR, after register allocation and the frame:
//   - an operation on a just-loaded constant takes the immediate form (add/sub/cmp with
//     12 bits, optionally shifted, the constant also when extended or shifted as an
//     operand; and/orr/eor with a bitmask immediate; shifts), and a zero is stored from
//     the zero register;
//   - a move folds into its uses within the block, a result is computed where it is
//     moved, a move to itself goes (a W one when the upper half it clears is not
//     read), and so does the reload of what was just stored;
//   - a constant index register is an offset;
//   - an address computation folds into the load or store it feeds ([base, #imm] or
//     [base, index, lsl/sxtw #s]), and a sign extension into the add that scales it;
//   - mul + add is madd (mul + sub, msub); adjacent ldr/str of one base are ldp/stp,
//     last;
//   - cmp #0 + b.eq/b.ne is cbz/cbnz; a jump to the next label goes, a branch over a
//     jump branches the other way, and code after a jump or return goes;
//   - the shifts and masks of a bit-field are ubfx/sbfx (a read), bfi (a store) and
//     ubfiz (a value shifted into place); a movz/movk mask that is a bitmask immediate
//     is one, a zero added or or-ed in is a move, and a uxtb/uxth after an ldrb/ldrh or
//     before an strb/strh goes.
// A return reads only the registers the function's result is in.
// Code selection never carries a scratch register (x9-x17, v16-v31) past its block, so
// whether a scratch value is read again is decided by looking to the end of the block;
// any register is dead once an instruction writes it.  A move or computation in the W
// view zeroes the upper half, so one is folded only where that cannot matter.
//
#include <string.h>

#include "bitops.h"
#include "internal.h"
#include "xalloc.h"

static unsigned result; // the registers a return reads: bit 0 x0, 1 x1, 2 + k v<k>

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

// Whether `in` reads the register it writes: movk and bfi keep the bits they do not set.
static bool reads_dest(A64_Op op)
{
    return op == A64_MOVK || op == A64_BFI;
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
    return no_dest(in->op) || reads_dest(in->op);
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
        return r == A64_LR || (r == A64_X0 && (result & 1)) || (r == A64_X(1) && (result & 2)) ||
               (r >= A64_V0 && r < A64_V(4) && (result & (4u << (r - A64_V0))));
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
        int64_t index;
        if (m->index_width == A64_W)
            index = m->ext == A64_EXT_SXTW ? (int64_t)(int32_t)v : (int64_t)(uint32_t)v;
        else if (bits == 32)
            index = (int64_t)(uint32_t)v; // a W write cleared the upper half
        else
            index = v;
        int64_t off = index * (1 << m->imm);
        if (!fits_ldst(off, access_size(n)))
            return false;
        *m = a64_mem(m->reg, off);
        delete_at(link);
        return true;
    }
    if (reads_as_address(n, t))
        return false;
    // A zero added, or-ed or xor-ed in, as a register, shifted or extended: a move.
    if (v == 0 && (n->op == A64_ADD || n->op == A64_SUB || n->op == A64_ORR || n->op == A64_EOR) &&
        o[0].kind == A64_OPND_REG && o[3].kind == A64_OPND_NONE) {
        int keep = -1;
        if (o[1].kind == A64_OPND_REG && o[1].reg != t && o[1].width == o[0].width &&
            (o[2].kind == A64_OPND_REG || o[2].kind == A64_OPND_SHIFT ||
             o[2].kind == A64_OPND_EXT) &&
            o[2].reg == t)
            keep = 1;
        else if (n->op != A64_SUB && o[2].kind == A64_OPND_REG && o[2].reg != t &&
                 o[2].width == o[0].width && o[1].kind == A64_OPND_REG && o[1].reg == t)
            keep = 2;
        if (keep > 0 && o[keep].reg != A64_ZR && (o[keep].reg != A64_SP || n->op == A64_ADD)) {
            n->op = A64_MOV;
            o[1]  = o[keep];
            o[2]  = (A64_Operand){ 0 };
            delete_at(link);
            return true;
        }
    }
    A64_Operand imm, shift;
    // A constant added or subtracted extended or shifted (a member's offset, an index):
    // its value, extended and shifted, as an immediate.
    if ((n->op == A64_ADD || n->op == A64_SUB) && o[0].kind == A64_OPND_REG &&
        o[1].kind == A64_OPND_REG && o[1].reg != t && o[1].reg != A64_ZR &&
        o[1].width == o[0].width && o[2].reg == t && o[3].kind == A64_OPND_NONE &&
        ((o[2].kind == A64_OPND_EXT && o[2].imm <= 4) ||
         (o[2].kind == A64_OPND_SHIFT && o[2].sub == A64_SHIFT_LSL))) {
        uint64_t u = bits == 32 ? (uint32_t)v : (uint64_t)v; // as the register holds it
        int64_t x;
        if (o[2].kind == A64_OPND_SHIFT) {
            x = (int64_t)u;
        } else {
            switch ((A64_Extend)o[2].sub) {
            case A64_EXT_UXTB:
                x = (uint8_t)u;
                break;
            case A64_EXT_UXTH:
                x = (uint16_t)u;
                break;
            case A64_EXT_UXTW:
                x = (uint32_t)u;
                break;
            case A64_EXT_SXTB:
                x = (int8_t)u;
                break;
            case A64_EXT_SXTH:
                x = (int16_t)u;
                break;
            case A64_EXT_SXTW:
                x = (int32_t)u;
                break;
            default:
                x = (int64_t)u;
                break;
            }
        }
        x = (int64_t)((uint64_t)x << o[2].imm);
        if (o[0].width == A64_W)
            x = (int32_t)x;
        bool neg   = x < 0;
        uint64_t a = neg ? -(uint64_t)x : (uint64_t)x;
        if (!arith_imm(a, &imm, &shift))
            return false;
        if (neg)
            n->op = n->op == A64_ADD ? A64_SUB : A64_ADD;
        set_imm(n, imm, shift);
        delete_at(link);
        return true;
    }
    bool regs3 = o[0].kind == A64_OPND_REG && o[1].kind == A64_OPND_REG &&
                 o[2].kind == A64_OPND_REG && o[3].kind == A64_OPND_NONE;
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
        bool is_mem          = o->kind == A64_OPND_MEM && (o->reg == t || (o->sub == A64_MEM_INDEX && o->index == t));
        if (is_mem && w != A64_X)
            return false;
        if (!reads_operand(in, i, t))
            continue;
        if (i == 0 && reads_dest(in->op))
            return false; // the destination would move with it
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

// `mov t, r`: the reads of t up to its next write read r instead, if r is not written
// before the last of them and each can.  A t other than a scratch register must be
// written before any branch, or reach a return that does not read it.
static bool forward_move(A64_Instr **link)
{
    A64_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    A64_Width w = mv->opnd[0].width;
    bool scratch = is_scratch(t);
    if (t == r || t == A64_SP || t == A64_ZR || r == A64_SP || r == A64_ZR)
        return false;
    bool clobbered = false, done = scratch;
    const A64_Instr *end = NULL;
    for (const A64_Instr *n = mv->next; n; n = n->next) {
        if (reads(n, t) &&
            (clobbered || is_call(n->op) || n->op == A64_RET || !can_substitute(n, t, w)))
            return false;
        if (writes(n, t)) {
            end  = n->next;
            done = true;
            break;
        }
        if (!scratch && n->op == A64_RET) {
            done = true;
            break;
        }
        if (!scratch && (is_branch(n->op) || n->op == A64_B))
            return false;
        if (writes(n, r))
            clobbered = true;
    }
    if (!done)
        return false;
    for (A64_Instr *n = mv->next; n != end; n = n->next)
        substitute(n, t, r);
    delete_at(link);
    return true;
}

// Whether `in` writes register operand 0 alone, from its other operands: the result
// could go to another register.
static bool computes(const A64_Instr *in)
{
    if (no_dest(in->op) || is_call(in->op) || reads_dest(in->op) || in->op == A64_LDP ||
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
            for (const A64_Instr *m = in->next; m != n; m = m->next)
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
        if (n->op == A64_RET)
            return !reads(n, r);
        if (is_call(n->op) || is_branch(n->op) || n->op == A64_B || reads_as_address(n, r))
            return false;
        for (int i = 0; i < A64_MAX_OPERANDS; i++)
            if (reads_operand(n, i, r) && (n->opnd[i].kind != A64_OPND_REG || n->opnd[i].width != A64_W))
                return false;
        if (writes(n, r))
            return true;
    }
    return false;
}

// Bit-field instructions.  A rule rewrites a consumer `at` into ubfx/sbfx/bfi/ubfiz and
// deletes the group of instructions of its block that fed it: shifts, masks, and the
// mov/movz/movk of a mask that is no immediate.  The rewritten `at` reads the group's
// sources where `at` is, so each must still hold there the value the group read; the
// group's own writes go with it, so a source one of them overwrote after reading it
// survives.  All at the width of `at`.
enum { MAX_GROUP = 8, MAX_SOURCES = 2 };

typedef struct {
    A64_Instr *in[MAX_GROUP];
    int n;
    struct {
        int reg;
        const A64_Instr *reader; // the instruction of the group (or `at`) that read it
    } src[MAX_SOURCES];
    int nsrc;
} Group;

static A64_Block *cur_block; // the block being swept

static bool in_group(const Group *g, const A64_Instr *in)
{
    for (int i = 0; i < g->n; i++)
        if (g->in[i] == in)
            return true;
    return false;
}

static bool group_add(Group *g, A64_Instr *in)
{
    if (in_group(g, in))
        return true;
    if (g->n == MAX_GROUP)
        return false;
    g->in[g->n++] = in;
    return true;
}

static void group_source(Group *g, int reg, const A64_Instr *reader)
{
    g->src[g->nsrc].reg    = reg;
    g->src[g->nsrc].reader = reader;
    g->nsrc++;
}

static bool is_gpr(const A64_Operand *o, A64_Width w)
{
    return o->kind == A64_OPND_REG && o->width == w && !a64_is_fpreg(o->reg) && o->reg != A64_SP;
}

// The last instruction of the block before `at` to write register `reg`, when it is a
// plain computation of it at width `w`; NULL otherwise.
static A64_Instr *last_def(const A64_Instr *at, int reg, A64_Width w)
{
    A64_Instr *d = NULL;
    for (A64_Instr *in = cur_block->head; in && in != at; in = in->next)
        if (writes(in, reg))
            d = in;
    if (!d || d->is_volatile || !computes(d) || d->opnd[0].reg != reg || d->opnd[0].width != w ||
        is_mem_op(d->op))
        return NULL;
    return d;
}

// The value of operand `o` of an instruction of `g` (or `at`), of `bits` bits: an
// immediate, or a register `at` sees loaded with a constant by mov, movz or movn and
// any movk, added to `g`.
static bool operand_value(const A64_Instr *at, const A64_Operand *o, int bits, uint64_t *v,
                          Group *g)
{
    uint64_t mask = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
    if (o->kind == A64_OPND_IMM) {
        *v = (uint64_t)o->imm & mask;
        return true;
    }
    if (o->kind != A64_OPND_REG || a64_is_fpreg(o->reg) || o->reg == A64_SP || o->reg == A64_ZR)
        return false;
    uint64_t value = 0, known = 0; // the bits the movk seen so far set
    for (const A64_Instr *stop = at;;) {
        A64_Instr *d = NULL;
        for (A64_Instr *in = cur_block->head; in && in != stop; in = in->next)
            if (writes(in, o->reg))
                d = in;
        if (!d || d->is_volatile || d->opnd[0].kind != A64_OPND_REG || d->opnd[0].reg != o->reg ||
            d->opnd[1].kind != A64_OPND_IMM || !group_add(g, d))
            return false;
        int shift = d->opnd[2].kind == A64_OPND_LSL ? (int)d->opnd[2].imm : 0;
        if (d->opnd[2].kind != A64_OPND_NONE && d->opnd[2].kind != A64_OPND_LSL)
            return false;
        uint64_t part = (uint64_t)d->opnd[1].imm << shift;
        switch (d->op) {
        case A64_MOVK: {
            uint64_t field = 0xffffULL << shift;
            if (!(known & field))
                value |= part & field;
            known |= field;
            stop = d;
            continue;
        }
        case A64_MOV:
        case A64_MOVZ:
            value = (value & known) | (part & ~known);
            break;
        case A64_MOVN:
            value = (value & known) | (~part & ~known);
            break;
        default:
            return false;
        }
        *v = value & mask;
        return true;
    }
}

// Whether `at` may read the sources of `g` and the group go: every instruction of the
// group comes before `at` in its block; what lies among them neither branches nor calls,
// reads or writes what the group writes, nor writes a source; no member writes a source
// before that source is read; and of what the group writes, nothing but `k1`/`k2` (what
// the rewritten `at` writes; 0 for none) is read after `at`.
static bool group_ok(const Group *g, const A64_Instr *at, int k1, int k2)
{
    for (int i = 0; i < g->n; i++)
        if (g->in[i]->is_volatile)
            return false;
    for (int i = 0; i < g->nsrc; i++)
        if (g->src[i].reg == A64_SP)
            return false;
    int found              = 0;
    bool read[MAX_SOURCES] = { false };
    for (const A64_Instr *in = cur_block->head; in != at; in = in->next) {
        if (!in)
            return false;
        bool member = in_group(g, in);
        if (member)
            found++;
        else if (found == 0)
            continue;
        if (!member) {
            if (is_call(in->op) || is_branch(in->op) || in->op == A64_B || in->op == A64_RET)
                return false;
            for (int i = 0; i < g->n; i++) {
                int r = g->in[i]->opnd[0].reg;
                if (reads(in, r) || writes(in, r))
                    return false;
            }
            for (int i = 0; i < g->nsrc; i++)
                if (writes(in, g->src[i].reg))
                    return false;
            continue;
        }
        for (int i = 0; i < g->nsrc; i++)
            if (g->src[i].reader == in)
                read[i] = true;
        for (int i = 0; i < g->nsrc; i++)
            if (!read[i] && writes(in, g->src[i].reg))
                return false;
    }
    if (found != g->n)
        return false;
    for (int i = 0; i < g->n; i++) {
        int r = g->in[i]->opnd[0].reg;
        if (r != k1 && r != k2 && !dies_after(at, r))
            return false;
    }
    return true;
}

// Unlink and free the instructions of `g`.
static void group_delete(const Group *g)
{
    for (A64_Instr **link = &cur_block->head; *link;)
        if (in_group(g, *link))
            delete_at(link);
        else
            link = &(*link)->next;
}

static void set_bitfield(A64_Instr *in, A64_Op op, int d, int s, A64_Width w, int lsb, int width)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    memset(in->opnd, 0, sizeof in->opnd);
    in->op      = op;
    in->opnd[0] = a64_reg(d, w);
    in->opnd[1] = a64_reg(s, w);
    in->opnd[2] = a64_imm(lsb);
    in->opnd[3] = a64_imm(width);
}

// A new `mov d, s` at width w before *link.
static void insert_move(A64_Instr **link, int d, int s, A64_Width w)
{
    A64_Instr *mv = xalloc(sizeof(A64_Instr), __func__, __FILE__, __LINE__);
    mv->op        = A64_MOV;
    mv->opnd[0]   = a64_reg(d, w);
    mv->opnd[1]   = a64_reg(s, w);
    mv->next      = *link;
    *link         = mv;
}

static A64_Instr **link_of(const A64_Instr *at)
{
    A64_Instr **link = &cur_block->head;
    while (*link != at)
        link = &(*link)->next;
    return link;
}

// The width of mask `m` = 2^w - 1, or 0 when it is not one.
static int low_mask_width(uint64_t m)
{
    if (m == 0 || (m & (m + 1)))
        return 0;
    return popcount64(m);
}

// `op d, a, b` at width w on general registers.
static bool is_op3(const A64_Instr *in, A64_Op op, A64_Width w)
{
    return in->op == op && !in->is_volatile && is_gpr(&in->opnd[0], w) && is_gpr(&in->opnd[1], w) &&
           in->opnd[3].kind == A64_OPND_NONE;
}

// `lsl/lsr/asr d, a, #n` at width w, the amount maybe a constant in a register (added
// to `g`): the register and the amount.
static bool shift_by(const A64_Instr *in, A64_Op op, A64_Width w, int *a, int *n, Group *g)
{
    uint64_t v;
    Group save = *g;
    if (!is_op3(in, op, w) || in->opnd[1].reg == A64_ZR ||
        (in->opnd[2].kind == A64_OPND_REG && in->opnd[2].reg == in->opnd[1].reg) ||
        !operand_value(in, &in->opnd[2], 64, &v, g) || v == 0 || v >= (uint64_t)width_bits(w)) {
        *g = save;
        return false;
    }
    *a = in->opnd[1].reg;
    *n = (int)v;
    return true;
}

// A mask `and d, a, M` (either order) at width w, or uxtb/uxth (W): a and M.
static bool mask_of(const A64_Instr *in, A64_Width w, int *a, uint64_t *m, Group *g)
{
    if ((in->op == A64_UXTB || in->op == A64_UXTH) && w == A64_W && is_gpr(&in->opnd[0], w) &&
        is_gpr(&in->opnd[1], w) && in->opnd[2].kind == A64_OPND_NONE) {
        *a = in->opnd[1].reg;
        *m = in->op == A64_UXTB ? 0xff : 0xffff;
        return true;
    }
    if (!is_op3(in, A64_AND, w))
        return false;
    Group save = *g;
    if (operand_value(in, &in->opnd[2], width_bits(w), m, g)) {
        *a = in->opnd[1].reg;
        return true;
    }
    *g = save;
    if (is_gpr(&in->opnd[2], w) && operand_value(in, &in->opnd[1], width_bits(w), m, g)) {
        *a = in->opnd[2].reg;
        return true;
    }
    *g = save;
    return false;
}

// `at` = `and d, t, #(2^w - 1)` fed by `lsr t, a, #p`: `ubfx d, a, #p, #w`.  `at` =
// `lsr`/`asr d, t, #r` fed by `lsl t, a, #l`: r >= l, `ubfx`/`sbfx d, a, #(r - l),
// #(bits - r)`; r < l (lsr), `ubfiz d, a, #(l - r), #(bits - l)`.  `at` = `lsl d, t, #p`
// fed by a mask `and t, a, #(2^w - 1)`: `ubfiz d, a, #p, #w`.
static bool fold_extract(A64_Instr *at)
{
    A64_Width w = at->opnd[0].width;
    if ((w != A64_W && w != A64_X) || !is_gpr(&at->opnd[0], w))
        return false;
    int bits = width_bits(w), d = at->opnd[0].reg, a, t, n, s;
    Group g = { 0 };
    uint64_t m;
    if (at->op == A64_AND && mask_of(at, w, &t, &m, &g)) {
        int fw = low_mask_width(m);
        A64_Instr *f;
        if (!fw || !(f = last_def(at, t, w)) || !shift_by(f, A64_LSR, w, &a, &n, &g) ||
            !group_add(&g, f))
            return false;
        group_source(&g, a, f);
        if (!group_ok(&g, at, d, 0))
            return false;
        set_bitfield(at, A64_UBFX, d, a, w, n, fw < bits - n ? fw : bits - n);
        group_delete(&g);
        return true;
    }
    if (shift_by(at, A64_LSL, w, &t, &n, &g)) {
        A64_Instr *f = last_def(at, t, w);
        if (!f || !mask_of(f, w, &a, &m, &g) || !low_mask_width(m) || !group_add(&g, f))
            return false;
        int fw = low_mask_width(m);
        group_source(&g, a, f);
        if (!group_ok(&g, at, d, 0))
            return false;
        set_bitfield(at, A64_UBFIZ, d, a, w, n, fw < bits - n ? fw : bits - n);
        group_delete(&g);
        return true;
    }
    bool arith = at->op == A64_ASR;
    if (!shift_by(at, arith ? A64_ASR : A64_LSR, w, &t, &n, &g))
        return false;
    A64_Instr *f = last_def(at, t, w);
    if (!f || !shift_by(f, A64_LSL, w, &a, &s, &g) || (arith && n < s))
        return false;
    group_add(&g, f);
    group_source(&g, a, f);
    if (!group_ok(&g, at, d, 0))
        return false;
    if (n >= s)
        set_bitfield(at, arith ? A64_SBFX : A64_UBFX, d, a, w, n - s, bits - n);
    else
        set_bitfield(at, A64_UBFIZ, d, a, w, s - n, bits - s);
    group_delete(&g);
    return true;
}

// The field value `v` placed at bit `p` as operand `o` of `at`: `lsl y, z, #p` of a mask
// `and z, v, #(2^fw - 1)`, or the mask alone (p = 0), or the shift alone when it drops
// the bits above the field (fw = bits - p).  Its instructions join `g`.
static bool placed_field(const A64_Instr *at, const A64_Operand *o, A64_Width w, int *v, int *p,
                         int *fw, Group *g)
{
    int bits                = width_bits(w), z;
    *p                      = 0;
    const A64_Instr *reader = at;
    if (!is_gpr(o, w))
        return false;
    A64_Instr *y = last_def(at, o->reg, w);
    if (!y)
        return false;
    if (shift_by(y, A64_LSL, w, &z, p, g)) {
        if (!group_add(g, y))
            return false;
        reader = y;
    } else {
        z = o->reg; // the mask, unshifted
    }
    A64_Instr *mk = last_def(reader, z, w);
    uint64_t m;
    Group save = *g;
    if (mk && group_add(g, mk) && mask_of(mk, w, v, &m, g) && (*fw = low_mask_width(m)) != 0) {
        if (*p + *fw > bits)
            *fw = bits - *p;
        group_source(g, *v, mk);
        return true;
    }
    *g = save;
    if (*p == 0)
        return false;
    *v  = z; // no mask: the shift drops the bits above
    *fw = bits - *p;
    group_source(g, z, reader);
    return true;
}

// `at` = `orr r, x, y`, x = `and x, u, #keep` clearing a field, y the value placed
// there: bfi into r after `mov r, u` (which the computation of u may then take over), or
// into u when r is v.
static bool fold_insert(A64_Instr *at)
{
    A64_Width w = at->opnd[0].width;
    if ((w != A64_W && w != A64_X) || !is_op3(at, A64_ORR, w) || !is_gpr(&at->opnd[2], w))
        return false;
    int bits = width_bits(w), r = at->opnd[0].reg;
    uint64_t all = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
    for (int side = 1; side <= 2; side++) {
        const A64_Operand *xo = &at->opnd[side], *yo = &at->opnd[3 - side];
        Group g      = { 0 };
        A64_Instr *x = last_def(at, xo->reg, w);
        uint64_t keep;
        if (!x || !is_op3(x, A64_AND, w) || !group_add(&g, x) ||
            !operand_value(x, &x->opnd[2], bits, &keep, &g))
            continue;
        int u = x->opnd[1].reg, v, p, fw;
        if (!placed_field(at, yo, w, &v, &p, &fw, &g))
            continue;
        uint64_t field = (fw == 64 ? ~0ULL : ((1ULL << fw) - 1)) << p;
        if (keep != (~field & all) || u == v || u == A64_ZR || v == A64_SP)
            continue;
        group_source(&g, u, x);
        bool in_u = r == v;
        if (in_u && !dies_after(at, u))
            continue;
        if (!group_ok(&g, at, r, in_u ? u : 0))
            continue;
        group_delete(&g);
        if (in_u) {
            set_bitfield(at, A64_BFI, u, v, w, p, fw);
            insert_move(&at->next, r, u, w);
        } else {
            A64_Instr **link = link_of(at);
            set_bitfield(at, A64_BFI, r, v, w, p, fw);
            if (r != u)
                insert_move(link, r, u, w);
        }
        return true;
    }
    return false;
}

// `at` = `and`/`orr`/`eor d, a, t`, t a constant from movz/movk (or mov/movn) that is a
// bitmask immediate: the immediate form (fold_constant takes only a single mov).
static bool fold_wide_constant(A64_Instr *at)
{
    A64_Width w = at->opnd[0].width;
    if ((at->op != A64_AND && at->op != A64_ORR && at->op != A64_EOR) ||
        (w != A64_W && w != A64_X) || !is_op3(at, at->op, w) || !is_gpr(&at->opnd[2], w))
        return false;
    Group g = { 0 };
    uint64_t v;
    int bits = width_bits(w);
    if (!operand_value(at, &at->opnd[2], bits, &v, &g) || g.n < 2 || !bitmask_imm(v, bits) ||
        at->opnd[1].reg == at->opnd[2].reg)
        return false;
    group_source(&g, at->opnd[1].reg, at);
    if (!group_ok(&g, at, at->opnd[0].reg, 0))
        return false;
    at->opnd[2] = a64_imm(bits == 32 ? (int64_t)(int32_t)v : (int64_t)v);
    group_delete(&g);
    return true;
}

// `uxtb`/`uxth t, a` stored by `strb`/`strh t` at its last read: a stored instead.  And
// a uxtb/uxth (or uxtb of a halfword) of what an ldrb/ldrh just loaded, already zero
// extended: gone, or a move.
static bool fold_narrow(A64_Instr **link)
{
    A64_Instr *at = *link;
    if (at->is_volatile)
        return false;
    if ((at->op == A64_STRB || at->op == A64_STRH) && is_gpr(&at->opnd[0], A64_W) &&
        at->opnd[0].reg != A64_ZR) {
        int t        = at->opnd[0].reg;
        A64_Instr *x = NULL;
        for (A64_Instr *in = cur_block->head; in != at; in = in->next)
            if (writes(in, t))
                x = in;
        if (!x || x->op != (at->op == A64_STRB ? A64_UXTB : A64_UXTH) ||
            !is_gpr(&x->opnd[1], A64_W) || reads_as_address(at, t))
            return false;
        Group g = { 0 };
        group_add(&g, x);
        group_source(&g, x->opnd[1].reg, x);
        if (!group_ok(&g, at, 0, 0))
            return false;
        at->opnd[0].reg = x->opnd[1].reg;
        group_delete(&g);
        return true;
    }
    A64_Instr *ext = at->next;
    if ((at->op != A64_LDRB && at->op != A64_LDRH) || !ext ||
        (ext->op != A64_UXTB && ext->op != A64_UXTH) || ext->is_volatile ||
        (at->op == A64_LDRH && ext->op == A64_UXTB) || !is_gpr(&at->opnd[0], A64_W) ||
        !is_gpr(&ext->opnd[0], A64_W) || !is_gpr(&ext->opnd[1], A64_W) ||
        ext->opnd[1].reg != at->opnd[0].reg)
        return false;
    if (ext->opnd[0].reg == ext->opnd[1].reg) {
        delete_at(&at->next);
    } else {
        ext->op = A64_MOV;
    }
    return true;
}

// One bit-field rewrite in block `b`; true when something changed (the block is then
// to be swept again: instructions before the one rewritten may be gone).  Inserts
// first: an extract would take the masks out of their shape.
static bool fold_bitfields(A64_Block *b)
{
    cur_block = b;
    for (A64_Instr *in = b->head; in; in = in->next)
        if (fold_insert(in))
            return true;
    for (A64_Instr **link = &b->head; *link; link = &(*link)->next)
        if (fold_extract(*link) || fold_wide_constant(*link) || fold_narrow(link))
            return true;
    return false;
}

// One rewrite at *link; true when something changed.
static bool rewrite(A64_Instr **link)
{
    A64_Instr *in = *link, *next = in->next;
    const A64_Operand *o = in->opnd;

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
    for (const A64_Instr *in = b->head; in; in = in->next)
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
void a64_peephole(A64_Func *fn, unsigned result_in)
{
    result       = result_in;
    bool changed = true;
    while (changed) {
        changed = false;
        for (A64_Block *b = fn->blocks; b; b = b->next) {
            while (fold_bitfields(b))
                changed = true;
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
