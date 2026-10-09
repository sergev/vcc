//
// Peephole pass over the AArch64 IR, after register allocation and the frame:
//   - an operation on a just-loaded constant takes the immediate form (add/sub/cmp with
//     12 bits, optionally shifted, the constant also when extended or shifted as an
//     operand; and/orr/eor with a bitmask immediate; shifts), and a zero is stored from
//     the zero register;
//   - a move folds into its uses, a result is computed where it is moved, a move to
//     itself goes (a W one when the upper half it clears is not read), and so do a
//     copy moved back, a value never read and the reload of what was just stored;
//   - a constant index register is an offset;
//   - an address computation folds into the load or store that next uses it ([base,
//     #imm] or [base, index, lsl/sxtw #s]), a sign extension into the add that scales
//     it, a pointer step into the access as a post- or pre-index;
//   - a byte or halfword load takes the extension after it, an extension before a
//     narrow store goes;
//   - mul + add is madd (mul + sub, msub); a run of 8-byte copies is ldp/stp of q
//     registers; adjacent ldr/str of one base are ldp/stp, last;
//   - the flags: cmp #0 + b.eq/b.ne is cbz/cbnz, and + a single-bit and, tbz/tbnz; a
//     cset tested again (cmp #0, cbz) is the condition itself, a cset added is cinc; a
//     compare the flags already hold goes; the compare of `n-- > 0` goes ahead of the
//     decrement;
//   - a diamond setting two constants one apart is cset, or mov + cinc;
//   - jump threading: a jump to a jump, a test of a cset or of a constant just set goes
//     where it leads; a jump to the next label goes, a branch over a jump branches the
//     other way, code after a jump or return and code nothing reaches go;
//   - blocks ending alike through a return or a jump share their tail;
//   - the shifts and masks of a bit-field are ubfx/sbfx (a read), bfi (a store) and
//     ubfiz (a value shifted into place); a movz/movk mask that is a bitmask immediate
//     is one, and a zero added or or-ed in is a move.
// Whether a register is read again is decided by its liveness over the blocks (see
// compute_liveness); a return reads only the registers the function's result is in,
// a call only the argument registers call.c recorded on it.  Code selection never
// carries a scratch register (x9-x17, v16-v31) past its block, so a scratch value is
// looked for to the end of the block only, and no block is split where one is live.
// Any register is dead once an instruction writes it.  A move or computation in the W
// view zeroes the upper half, so one is folded only where that cannot matter.  The
// flags never live past a branch or a label.
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
    return op == A64_BCOND || op == A64_CBZ || op == A64_CBNZ || op == A64_TBZ || op == A64_TBNZ;
}

// The operand that is the label of branch or jump `op`.
static int label_slot(A64_Op op)
{
    return op == A64_B ? 0 : op == A64_TBZ || op == A64_TBNZ ? 2 : 1;
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
    case A64_TBZ:
    case A64_TBNZ:
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
    if (is_call(in->op) && (in->args_known ? (in->args & a64_reg_bit(r)) != 0 : is_arg(r)))
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

//
// Liveness of the registers over the function's blocks, computed afresh before each
// round of rewrites.  Within a round a rewrite never makes a register live across a
// block boundary where it was not (it deletes reads, or moves them within the block),
// so the sets stay a safe over-estimate until the next round.
//
// A set of registers: bit r - x0 for x0-x30, bit 31 + k for v<k>; sp and xzr are not
// tracked.
typedef uint64_t Regs;

#define GPRS(lo, hi)  ((~0ull >> (63 - (hi))) & (~0ull << (lo)))
#define VREGS(lo, hi) (GPRS(lo, hi) << 31)
#define CALL_READS    (GPRS(0, 8) | VREGS(0, 7))
#define CALL_WRITES   (GPRS(0, 18) | GPRS(30, 30) | VREGS(0, 7) | VREGS(16, 31))
#define CALLEE_SAVED  (GPRS(19, 29) | VREGS(8, 15))

static Regs bit_of(int r)
{
    return a64_reg_bit(r);
}

// The registers `in` reads, or writes: of the registers its operands name, those
// reads() (writes()) says; a call reads the argument registers and writes those it
// does not keep, a return reads the result, x30 and the callee-saved registers.
static Regs regs_of(const A64_Instr *in, bool (*pred)(const A64_Instr *, int))
{
    Regs s = 0;
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        const A64_Operand *o = &in->opnd[i];
        if (o->kind == A64_OPND_NONE || o->kind == A64_OPND_IMM || o->kind == A64_OPND_COND)
            continue;
        if (o->reg && pred(in, o->reg))
            s |= bit_of(o->reg);
        if (o->kind == A64_OPND_MEM && o->sub == A64_MEM_INDEX && pred(in, o->index))
            s |= bit_of(o->index);
    }
    return s;
}

static Regs ret_regs(void)
{
    Regs s = bit_of(A64_LR) | CALLEE_SAVED;
    if (result & 1)
        s |= bit_of(A64_X0);
    if (result & 2)
        s |= bit_of(A64_X(1));
    for (int k = 0; k < 4; k++)
        if (result & (4u << k))
            s |= bit_of(A64_V(k));
    return s;
}

static Regs uses(const A64_Instr *in)
{
    if (in->op == A64_RET)
        return ret_regs();
    if (is_call(in->op))
        return regs_of(in, reads) | (in->args_known ? in->args : CALL_READS);
    return regs_of(in, reads);
}

static Regs defs(const A64_Instr *in)
{
    return is_call(in->op) ? CALL_WRITES : regs_of(in, writes);
}

static struct {
    int n;
    A64_Block **blocks;
    Regs *in, *out;
} live_info;

static A64_Block *cur_block; // the block being swept
static A64_Func *cur_fn;     // the function being rewritten
static bool live_stale;      // the blocks changed shape: liveness to be computed again

static void compute_liveness(A64_Func *fn);

static void fresh_liveness(void)
{
    if (live_stale)
        compute_liveness(cur_fn);
}

// The registers live at label `l`: all, for one not in the function.
static Regs live_at(const char *l)
{
    fresh_liveness();
    for (int i = 0; i < live_info.n; i++)
        if (live_info.blocks[i]->label && strcmp(live_info.blocks[i]->label, l) == 0)
            return live_info.in[i];
    return ~0ull;
}

static const char *branch_target(const A64_Instr *in)
{
    return in->opnd[label_slot(in->op)].sym;
}

// The registers live before `in`, given those live after it.
static Regs live_through(const A64_Instr *in, Regs live)
{
    if (in->op == A64_RET)
        return uses(in);
    if (in->op == A64_B)
        return live_at(branch_target(in));
    if (is_branch(in->op))
        return live | live_at(branch_target(in)) | uses(in);
    return (live & ~defs(in)) | uses(in);
}

static Regs live_before(const A64_Block *b, Regs live)
{
    int n = 0;
    for (const A64_Instr *in = b->head; in; in = in->next)
        n++;
    const A64_Instr **list = xalloc((n ? n : 1) * sizeof(A64_Instr *), __func__, __FILE__, __LINE__);
    n                      = 0;
    for (const A64_Instr *in = b->head; in; in = in->next)
        list[n++] = in;
    while (n > 0)
        live = live_through(list[--n], live);
    xfree(list);
    return live;
}

static bool falls_through(const A64_Block *b)
{
    const A64_Instr *last = NULL;
    for (const A64_Instr *in = b->head; in; in = in->next)
        last = in;
    return !last || (last->op != A64_B && last->op != A64_RET);
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
static void compute_liveness(A64_Func *fn)
{
    live_stale = false;
    free_liveness();
    for (const A64_Block *b = fn->blocks; b; b = b->next)
        live_info.n++;
    size_t n         = live_info.n ? live_info.n : 1;
    live_info.blocks = xalloc(n * sizeof(A64_Block *), __func__, __FILE__, __LINE__);
    live_info.in     = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    live_info.out    = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    int i            = 0;
    for (A64_Block *b = fn->blocks; b; b = b->next, i++) {
        live_info.blocks[i] = b;
        live_info.in[i] = live_info.out[i] = 0;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (i = live_info.n - 1; i >= 0; i--) {
            const A64_Block *b = live_info.blocks[i];
            Regs out = falls_through(b) ? (i + 1 < live_info.n ? live_info.in[i + 1] : ~0ull) : 0;
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
    fresh_liveness();
    for (int i = 0; i < live_info.n; i++)
        if (live_info.blocks[i] == cur_block)
            return live_info.out[i];
    return ~0ull;
}

// The registers in `live` are not read after `in`, an instruction of the block being
// rewritten: written first, or not live where control goes.
static bool dead_from(const A64_Instr *in, Regs live)
{
    for (const A64_Instr *n = in->next; n && live; n = n->next) {
        if (uses(n) & live)
            return false;
        if (n->op == A64_RET)
            return true;
        if (n->op == A64_B)
            return !(live & live_at(branch_target(n)));
        if (is_branch(n->op) && (live & live_at(branch_target(n))))
            return false;
        live &= ~defs(n);
    }
    return !(live & live_out());
}

// The value `in` leaves in register r is never read: a scratch register is not read
// again in the block, any other is written first or dead where control goes.
static bool dies_after(const A64_Instr *in, int r)
{
    if (is_scratch(r))
        return dead_after(in, r);
    Regs b = bit_of(r);
    return b && dead_from(in, b);
}

// The value r holds before `in` is not read after it: `in` writes r, or it dies.
static bool last_read(const A64_Instr *in, int r)
{
    return writes(in, r) || dies_after(in, r);
}

// Whether register r may be read where branch or jump `in` goes.
static bool live_at_target(const A64_Instr *in, int r)
{
    Regs b = bit_of(r);
    return !b || (live_at(branch_target(in)) & b);
}

static void free_instr(A64_Instr *in)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

static A64_Instr *new_instr(A64_Op op, A64_Instr *next)
{
    A64_Instr *in = xalloc(sizeof(A64_Instr), __func__, __FILE__, __LINE__);
    memset(in, 0, sizeof(*in));
    in->op   = op;
    in->next = next;
    return in;
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
// dead where any branch passed goes, and out of the block when it is not written in it.
static bool forward_move(A64_Instr **link)
{
    A64_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    A64_Width w = mv->opnd[0].width;
    bool scratch = is_scratch(t);
    if (t == r || t == A64_SP || t == A64_ZR || r == A64_SP || r == A64_ZR)
        return false;
    bool clobbered = false, done = false;
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
        if (n->op == A64_RET) {
            done = true;
            break;
        }
        if (!scratch && (is_branch(n->op) || n->op == A64_B) && live_at_target(n, t))
            return false;
        if (writes(n, r))
            clobbered = true;
    }
    if (!done && !scratch && (live_out() & bit_of(t)))
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
// when d is neither read nor written in between, nor live where a branch passed goes
// (nor t, unless a scratch register).  The reads of t in between read d.  A W move
// zeroes the upper half, so it takes a W result only.
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
                if (reads(m, d) || writes(m, d) || !can_substitute(m, t, A64_X) ||
                    ((is_branch(m->op) || m->op == A64_B) &&
                     (live_at_target(m, d) || (!is_scratch(t) && live_at_target(m, t)))))
                    return false;
            for (A64_Instr *m = in->next; m != n; m = m->next)
                substitute(m, t, d);
            in->opnd[0].reg = d;
            delete_at(link);
            return true;
        }
        if (writes(n, t) || is_call(n->op) || n->op == A64_B)
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

// The first instruction after `in` that reads or writes register t, when none before
// it writes register a or b (0 for none) or is a branch, call or return.
static A64_Instr *next_use(const A64_Instr *in, int t, int a, int b)
{
    for (A64_Instr *n = in->next; n; n = n->next) {
        if (reads(n, t) || writes(n, t))
            return n;
        if (is_branch(n->op) || n->op == A64_B || is_call(n->op) || n->op == A64_RET ||
            (a && writes(n, a)) || (b && writes(n, b)))
            return NULL;
    }
    return NULL;
}

// `add t, b, #imm` (or sub), or `add t, a, index, lsl/sxtw #s`, feeding the address of
// the next load or store to use t, at its last read: the address computed by that
// instruction instead.
static bool fold_address(A64_Instr **link)
{
    A64_Instr *add = *link;
    const A64_Operand *o = add->opnd;
    if (o[0].kind != A64_OPND_REG || o[0].width != A64_X || o[1].kind != A64_OPND_REG ||
        o[1].reg == A64_ZR)
        return false;
    int t        = o[0].reg;
    A64_Instr *n = next_use(add, t, o[1].reg, o[2].kind == A64_OPND_IMM ? 0 : o[2].reg);
    if (!n || !is_mem_op(n->op))
        return false;
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

// The signed step of `add p, p, #k` (or sub) on X register p, when it is one.
static bool step_of(const A64_Instr *in, int p, int64_t *k)
{
    const A64_Operand *o = in->opnd;
    if ((in->op != A64_ADD && in->op != A64_SUB) || o[0].kind != A64_OPND_REG ||
        o[0].reg != p || o[0].width != A64_X || o[1].kind != A64_OPND_REG || o[1].reg != p ||
        o[2].kind != A64_OPND_IMM || o[3].kind != A64_OPND_NONE)
        return false;
    *k = in->op == A64_SUB ? -o[2].imm : o[2].imm;
    return *k >= -256 && *k <= 255;
}

// A load or store at [base] that may write register wb back: not volatile, its data
// not wb.
static bool may_index(const A64_Instr *n, int base, int wb)
{
    if (!n || !is_mem_op(n->op) || n->is_volatile)
        return false;
    const A64_Operand *m = &n->opnd[1];
    return m->kind == A64_OPND_MEM && m->sub == A64_MEM_OFFSET && m->reg == base && m->imm == 0 &&
           !(n->opnd[0].kind == A64_OPND_REG && n->opnd[0].reg == wb);
}

// The first instruction after `in` to read or write register r, with no branch, call
// or return before it.
static A64_Instr *next_touch(const A64_Instr *in, int r)
{
    return next_use(in, r, 0, 0);
}

// Post- and pre-indexed addressing:
//   `mov t, p` + `add p, p, #k` + an access at [t], t's last read: [p], #k;
//   an access at [p], and `add p, p, #k` the next use of p: [p], #k;
//   `add p, p, #k`, and an access at [p] the next use of p: [p, #k]!.
static bool fold_index(A64_Instr **link)
{
    A64_Instr *in = *link, *n;
    int64_t k;
    if (is_move(in) && in->op == A64_MOV && in->opnd[0].width == A64_X && in->next) {
        int t = in->opnd[0].reg, p = in->opnd[1].reg;
        A64_Instr *add = in->next;
        if (t == p || p == A64_SP || p == A64_ZR || !step_of(add, p, &k))
            return false;
        n = next_touch(add, t);
        if (!may_index(n, t, p) || !last_read(n, t))
            return false;
        for (const A64_Instr *m = add->next; m != n; m = m->next)
            if (reads(m, p) || writes(m, p))
                return false;
        if (is_store(n->op) && n->opnd[0].reg == t)
            return false;
        n->opnd[1] = a64_mem_post(p, k);
        delete_at(&in->next);
        delete_at(link);
        return true;
    }
    if (is_mem_op(in->op) && in->opnd[1].kind == A64_OPND_MEM) {
        int p = in->opnd[1].reg;
        if (!may_index(in, p, p) || p == A64_SP)
            return false;
        n = next_touch(in, p);
        A64_Instr **nl = &in->next;
        while (*nl && *nl != n)
            nl = &(*nl)->next;
        if (!n || !step_of(n, p, &k))
            return false;
        in->opnd[1] = a64_mem_post(p, k);
        delete_at(nl);
        return true;
    }
    if (step_of(in, in->opnd[0].reg, &k) && in->opnd[0].reg != A64_SP) {
        int p = in->opnd[0].reg;
        n     = next_touch(in, p);
        if (!may_index(n, p, p))
            return false;
        n->opnd[1] = a64_mem_pre(p, k);
        delete_at(link);
        return true;
    }
    return false;
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
// up to its next write is of the lower half (a W register operand), and r is dead
// where control leaves the block.
static bool upper_unread(const A64_Instr *in, int r)
{
    if (!bit_of(r))
        return false;
    for (const A64_Instr *n = in->next; n; n = n->next) {
        if (n->op == A64_RET)
            return !reads(n, r);
        if (is_call(n->op) || reads_as_address(n, r))
            return false;
        for (int i = 0; i < A64_MAX_OPERANDS; i++)
            if (reads_operand(n, i, r) && (n->opnd[i].kind != A64_OPND_REG || n->opnd[i].width != A64_W))
                return false;
        if (writes(n, r))
            return true;
        if (n->op == A64_B)
            return !live_at_target(n, r);
        if (is_branch(n->op) && live_at_target(n, r))
            return false;
    }
    return !(live_out() & bit_of(r));
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
// The bytes an extension keeps: 1 for uxtb/sxtb, 2 for uxth/sxth, else 0.
static int ext_bytes(A64_Op op)
{
    return op == A64_UXTB || op == A64_SXTB ? 1 : op == A64_UXTH || op == A64_SXTH ? 2 : 0;
}

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
        if (!x || ext_bytes(x->op) < access_size(at) || !is_gpr(&x->opnd[1], A64_W) ||
            reads_as_address(at, t))
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
    // A byte or halfword load, and an extension of what it loaded after it.
    A64_Instr *ext = at->next;
    int size       = at->op == A64_LDRB || at->op == A64_LDRSB ? 1 : at->op == A64_LDRH || at->op == A64_LDRSH ? 2 : 0;
    int esize      = ext ? ext_bytes(ext->op) : 0;
    if (!size || !esize || ext->is_volatile || !is_gpr(&at->opnd[0], A64_W) ||
        !is_gpr(&ext->opnd[1], A64_W) || ext->opnd[1].reg != at->opnd[0].reg ||
        ext->opnd[0].kind != A64_OPND_REG)
        return false;
    bool lsigned = at->op == A64_LDRSB || at->op == A64_LDRSH;
    bool esigned = ext->op == A64_SXTB || ext->op == A64_SXTH;
    int t = at->opnd[0].reg, d = ext->opnd[0].reg;
    if (esize > size && !(lsigned && !esigned) && ext->opnd[0].width == A64_W) {
        // A byte is unchanged by a halfword extension of its own kind, or a signed one
        // of a byte loaded zero-extended: a move.
        ext->op = A64_MOV;
        return true;
    }
    if (esize != size || (ext->opnd[0].width == A64_X && !esigned) ||
        (d != t && !dies_after(ext, t)) ||
        (at->opnd[1].sub != A64_MEM_OFFSET && at->opnd[1].sub != A64_MEM_INDEX &&
         at->opnd[1].reg == d))
        return false;
    // The load extends as the extension does, into its register.
    at->op           = size == 1 ? (esigned ? A64_LDRSB : A64_LDRB) : (esigned ? A64_LDRSH : A64_LDRH);
    at->opnd[0]      = ext->opnd[0];
    delete_at(&at->next);
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

static A64_Op invert_branch(A64_Op op)
{
    switch (op) {
    case A64_CBZ:
        return A64_CBNZ;
    case A64_CBNZ:
        return A64_CBZ;
    case A64_TBZ:
        return A64_TBNZ;
    default:
        return A64_TBZ;
    }
}

// The condition operand of an instruction that reads the flags, or NULL.
static A64_Operand *flag_cond(A64_Instr *in)
{
    switch (in->op) {
    case A64_BCOND:
        return &in->opnd[0];
    case A64_CSET:
        return &in->opnd[1];
    case A64_CINC:
        return &in->opnd[2];
    default:
        return NULL;
    }
}

static bool sets_flags(const A64_Instr *in)
{
    return in->op == A64_CMP || in->op == A64_CMN || in->op == A64_FCMP || is_call(in->op);
}

// The flags `in` leaves are not read: set again first, or the block left.  Code
// selection never keeps the flags past a branch or a label.
static bool flags_dead_after(A64_Instr *in)
{
    for (A64_Instr *n = in->next; n; n = n->next) {
        if (flag_cond(n))
            return false;
        if (sets_flags(n) || n->op == A64_B || n->op == A64_RET)
            return true;
    }
    return true;
}

// Whether the value of register w is not read once conditional branch `br` (which
// reads it) has been passed, on either way.
static bool dead_past_branch(const A64_Instr *br, int w)
{
    return (is_scratch(w) || !live_at_target(br, w)) && dies_after(br, w);
}

// Whether `in` is `cmp r, #0` at any width.
static bool is_cmp_zero(const A64_Instr *in)
{
    return in->op == A64_CMP && in->opnd[0].kind == A64_OPND_REG && in->opnd[1].kind == A64_OPND_IMM &&
           in->opnd[1].imm == 0 && in->opnd[2].kind == A64_OPND_NONE;
}

static int instr_count; // of the function: a tbz/tbnz reaches 32 KiB only

// Rewrites on the flags and the 0/1 of a cset at *link, and the single-bit test:
//   `cset w, c` + `cmp w, #0` + a read of the flags for eq/ne: the read of c or not c;
//   `cset w, c` + `cbz`/`cbnz w` at its last read: `b.<not c>`/`b.c`;
//   `cset w, c` + `add d, a, w` at its last read: `cinc d, a, c`;
//   `and t, x, #(1 << k)` + `cbz`/`cbnz t` at its last read: `tbz`/`tbnz x, #k`;
//   `mov t, r` + an instruction writing r + `cmp t, …` at t's last read: the compare of r
//   ahead of that instruction (the test of `n-- > 0`).
static bool fold_flags(A64_Instr **link)
{
    A64_Instr *in = *link, *n = in->next;
    if (!n)
        return false;
    const A64_Operand *o = in->opnd;
    if (in->op == A64_CSET && o[0].kind == A64_OPND_REG) {
        int w      = o[0].reg;
        A64_Cond c = (A64_Cond)o[1].sub;
        A64_Instr *r = n->next;
        A64_Operand *rc;
        if (is_cmp_zero(n) && n->opnd[0].reg == w && r && (rc = flag_cond(r)) &&
            (rc->sub == A64_EQ || rc->sub == A64_NE) && flags_dead_after(r)) {
            rc->sub = rc->sub == A64_NE ? (int)c : (int)(c ^ 1);
            delete_at(&in->next);
            if (!reads(r, w) && (is_branch(r->op) ? dead_past_branch(r, w) : last_read(r, w)))
                delete_at(link);
            return true;
        }
        if ((n->op == A64_CBZ || n->op == A64_CBNZ) && n->opnd[0].reg == w && dead_past_branch(n, w)) {
            n->opnd[0] = a64_cond(n->op == A64_CBNZ ? c : (A64_Cond)(c ^ 1));
            n->op      = A64_BCOND;
            delete_at(link);
            return true;
        }
        A64_Operand *no = n->opnd;
        if (n->op == A64_ADD && no[0].kind == A64_OPND_REG && no[1].kind == A64_OPND_REG &&
            no[2].kind == A64_OPND_REG && no[3].kind == A64_OPND_NONE && no[1].width == no[2].width &&
            (no[1].reg == w) != (no[2].reg == w) && no[1].reg != A64_SP && no[2].reg != A64_SP &&
            last_read(n, w)) {
            A64_Operand a = no[1].reg == w ? no[2] : no[1];
            if (a.reg == A64_ZR)
                return false;
            n->op    = A64_CINC;
            no[1]    = a;
            no[2]    = a64_cond(c);
            delete_at(link);
            return true;
        }
        return false;
    }
    if (in->op == A64_AND && o[0].kind == A64_OPND_REG && o[1].kind == A64_OPND_REG &&
        o[2].kind == A64_OPND_IMM && o[3].kind == A64_OPND_NONE && (n->op == A64_CBZ || n->op == A64_CBNZ) &&
        n->opnd[0].reg == o[0].reg && instr_count < 8000) {
        uint64_t m = (uint64_t)o[2].imm & (o[0].width == A64_W ? 0xffffffffull : ~0ull);
        if (m == 0 || (m & (m - 1)) != 0 || !dead_past_branch(n, o[0].reg))
            return false;
        int k      = 0;
        while (!(m >> k & 1))
            k++;
        n->op      = n->op == A64_CBZ ? A64_TBZ : A64_TBNZ;
        n->opnd[2] = n->opnd[1];
        n->opnd[1] = a64_imm(k);
        n->opnd[0] = a64_reg(o[1].reg, k < 32 ? A64_W : A64_X);
        delete_at(link);
        return true;
    }
    A64_Instr *cmp = n->next;
    if (in->op == A64_MOV && is_move(in) && !a64_is_fpreg(o[0].reg) && cmp && cmp->op == A64_CMP &&
        cmp->opnd[0].kind == A64_OPND_REG && cmp->opnd[0].reg == o[0].reg &&
        (o[0].width == A64_X || cmp->opnd[0].width == A64_W)) {
        int t = o[0].reg, r = o[1].reg;
        if (r == A64_SP || r == A64_ZR || t == r || !writes(n, r) || reads(n, t) || writes(n, t) ||
            sets_flags(n) || flag_cond(n) || is_branch(n->op) || n->op == A64_B || n->op == A64_RET ||
            reads(cmp, r) || reads_operand(cmp, 1, t) || reads_operand(cmp, 2, t) || !dies_after(cmp, t))
            return false;
        for (int i = 1; i < A64_MAX_OPERANDS; i++)
            if (cmp->opnd[i].kind != A64_OPND_IMM && cmp->opnd[i].reg && writes(n, cmp->opnd[i].reg))
                return false;
        // The compare, of r, takes the move's place; r's write follows it.
        in->op      = A64_CMP;
        in->opnd[0] = a64_reg(r, cmp->opnd[0].width);
        for (int i = 1; i < A64_MAX_OPERANDS; i++) {
            in->opnd[i]  = cmp->opnd[i];
            cmp->opnd[i] = (A64_Operand){ 0 };
        }
        delete_at(&n->next);
        return true;
    }
    return false;
}

static bool same_operand(const A64_Operand *x, const A64_Operand *y)
{
    return x->kind == y->kind && x->reg == y->reg && x->width == y->width && x->imm == y->imm &&
           x->sub == y->sub && x->reloc == y->reloc && x->index == y->index &&
           x->index_width == y->index_width && x->ext == y->ext &&
           (x->sym == y->sym || (x->sym && y->sym && strcmp(x->sym, y->sym) == 0));
}

static bool same_instr(const A64_Instr *x, const A64_Instr *y)
{
    if (x->op != y->op || x->is_volatile != y->is_volatile)
        return false;
    for (int i = 0; i < A64_MAX_OPERANDS; i++)
        if (!same_operand(&x->opnd[i], &y->opnd[i]))
            return false;
    return true;
}

// `mov t, r` (an X, S or D one), and a later `mov r, t` in the block with neither
// written in between: r still holds t's value, and the second move goes.  Not a W move:
// it would clear an upper half the first did not.
static bool delete_move_back(A64_Instr *in)
{
    const A64_Operand *o = in->opnd;
    int t = o[0].reg, r = o[1].reg;
    if (!is_move(in) || o[0].width == A64_W || t == r || t == A64_SP || r == A64_SP ||
        t == A64_ZR || r == A64_ZR)
        return false;
    for (A64_Instr **link = &in->next; *link; link = &(*link)->next) {
        A64_Instr *n = *link;
        if (is_move(n) && n->op == in->op && n->opnd[0].reg == r && n->opnd[1].reg == t &&
            n->opnd[0].width == o[0].width) {
            delete_at(link);
            return true;
        }
        if (writes(n, t) || writes(n, r) || n->op == A64_B || n->op == A64_RET)
            return false;
    }
    return false;
}

// A compare that the flags already hold: the same compare earlier in the block, with
// neither its registers nor the flags changed since.
static bool delete_compare_again(A64_Instr *in)
{
    if (in->op != A64_CMP && in->op != A64_CMN && in->op != A64_FCMP)
        return false;
    Regs src = uses(in);
    for (A64_Instr **link = &in->next; *link; link = &(*link)->next) {
        A64_Instr *n = *link;
        if (same_instr(n, in)) {
            delete_at(link);
            return true;
        }
        if (sets_flags(n) || (defs(n) & src) || n->op == A64_B || n->op == A64_RET)
            return false;
    }
    return false;
}

// Whether a q-register access at base + off fits: paired (ldp/stp, a multiple of 16
// within imm7) or alone (ldr/str).
static bool fits_q(int64_t off, bool paired)
{
    return paired ? off % 16 == 0 && off >= -1024 && off <= 1008 : fits_ldst(off, 16);
}

static A64_Instr *mem_instr(A64_Op op, A64_Operand r1, A64_Operand r2, A64_Operand m, A64_Instr *next)
{
    A64_Instr *in = new_instr(op, next);
    in->opnd[0]   = r1;
    if (op == A64_LDP || op == A64_STP) {
        in->opnd[1] = r2;
        in->opnd[2] = m;
    } else {
        in->opnd[1] = m;
    }
    return in;
}

// A run of 8-byte copies, `ldr x, [b, #s + 8k]` and `str x, [b, #d + 8k]` with x dead
// after each, the two ranges of one base apart: 32 bytes at a time by ldp/stp of two q
// scratch registers, 16 by ldr/str of one, 8 as it was.
static bool fold_copy_run(A64_Instr **link)
{
    A64_Instr *in = *link;
    if (in->op != A64_LDR || in->is_volatile || in->opnd[0].kind != A64_OPND_REG ||
        in->opnd[0].width != A64_X || in->opnd[1].kind != A64_OPND_MEM ||
        in->opnd[1].sub != A64_MEM_OFFSET)
        return false;
    int base = in->opnd[1].reg;
    int64_t s0 = in->opnd[1].imm, d0 = 0;
    int n = 0;
    A64_Instr *ld = in, *last = NULL;
    for (;;) {
        A64_Instr *st = ld->next;
        const A64_Operand *lm = &ld->opnd[1];
        if (ld->op != A64_LDR || ld->is_volatile || ld->opnd[0].kind != A64_OPND_REG ||
            ld->opnd[0].width != A64_X || a64_is_fpreg(ld->opnd[0].reg) || lm->kind != A64_OPND_MEM ||
            lm->sub != A64_MEM_OFFSET || lm->reg != base || lm->imm != s0 + 8 * n || !st ||
            st->op != A64_STR || st->is_volatile || st->opnd[0].kind != A64_OPND_REG ||
            st->opnd[0].reg != ld->opnd[0].reg || st->opnd[0].width != A64_X ||
            st->opnd[1].kind != A64_OPND_MEM || st->opnd[1].sub != A64_MEM_OFFSET ||
            st->opnd[1].reg != base || ld->opnd[0].reg == base)
            break;
        if (n == 0)
            d0 = st->opnd[1].imm;
        else if (st->opnd[1].imm != d0 + 8 * n)
            break;
        if (!dies_after(st, ld->opnd[0].reg))
            break;
        last = st;
        n++;
        ld   = st->next;
        if (!ld)
            break;
    }
    int64_t bytes = 8 * (int64_t)n;
    if (n < 2 || (s0 < d0 + bytes && d0 < s0 + bytes) || !dead_after(last, F1) ||
        !dead_after(last, F2))
        return false;
    // The offsets must fit, chunk by chunk.
    for (int64_t i = 0; i < bytes;) {
        if (bytes - i >= 32) {
            bool paired = fits_q(s0 + i, true) && fits_q(d0 + i, true);
            if (!paired && !(fits_q(s0 + i, false) && fits_q(s0 + i + 16, false) &&
                           fits_q(d0 + i, false) && fits_q(d0 + i + 16, false)))
                return false;
            i += 32;
        } else if (bytes - i >= 16) {
            if (!fits_q(s0 + i, false) || !fits_q(d0 + i, false))
                return false;
            i += 16;
        } else {
            i += 8;
        }
    }
    // The new code, in place of the run; an 8-byte rest keeps its last pair.
    A64_Instr *after = last->next, *head = NULL, **tail = &head;
    A64_Instr *keep  = NULL;
    if (n % 2) { // the last pair stays: its ldr is the one before `last`
        for (A64_Instr *x = in; x->next; x = x->next)
            if (x->next->next == last)
                keep = x->next;
    }
    A64_Operand q1 = a64_reg(F1, A64_Q), q2 = a64_reg(F2, A64_Q);
    for (int64_t i = 0; i + 16 <= bytes; ) {
        if (bytes - i >= 32 && fits_q(s0 + i, true) && fits_q(d0 + i, true)) {
            *tail = mem_instr(A64_LDP, q1, q2, a64_mem(base, s0 + i), NULL);
            tail  = &(*tail)->next;
            *tail = mem_instr(A64_STP, q1, q2, a64_mem(base, d0 + i), NULL);
            tail  = &(*tail)->next;
            i += 32;
        } else {
            *tail = mem_instr(A64_LDR, q1, (A64_Operand){ 0 }, a64_mem(base, s0 + i), NULL);
            tail  = &(*tail)->next;
            *tail = mem_instr(A64_STR, q1, (A64_Operand){ 0 }, a64_mem(base, d0 + i), NULL);
            tail  = &(*tail)->next;
            i += 16;
        }
    }
    // Free the run, but for the kept pair.
    for (A64_Instr *x = in; x != after;) {
        A64_Instr *nx = x->next;
        if (x != keep && x != last)
            free_instr(x);
        x = nx;
    }
    if (keep) {
        keep->next = last;
        last->next = after;
        *tail      = keep;
    } else {
        free_instr(last);
        *tail = after;
    }
    *link = head;
    return true;
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
    // A value never read: its computation goes (not a volatile access).
    if (computes(in) && !in->is_volatile && bit_of(o[0].reg) && dies_after(in, o[0].reg)) {
        delete_at(link);
        return true;
    }
    if (!next)
        return false;
    if (in->op == A64_MOV && o[0].kind == A64_OPND_REG && o[1].kind == A64_OPND_IMM &&
        fold_constant(link))
        return true;
    if (fold_index(link) || fold_flags(link) || delete_move_back(in) || delete_compare_again(in) ||
        fold_copy_run(link))
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
    return &in->opnd[label_slot(in->op)];
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
            br->op = invert_branch(br->op);
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

// The number of branches and jumps of function `fn` to label `l`.
static int label_refs(const A64_Func *fn, const char *l)
{
    int n = 0;
    for (const A64_Block *b = fn->blocks; b; b = b->next)
        for (const A64_Instr *in = b->head; in; in = in->next)
            if ((is_branch(in->op) || in->op == A64_B) && strcmp(branch_target(in), l) == 0)
                n++;
    return n;
}

static A64_Block *block_of(const A64_Func *fn, const char *l)
{
    for (A64_Block *b = fn->blocks; b; b = b->next)
        if (b->label && strcmp(b->label, l) == 0)
            return b;
    return NULL;
}

// The condition under which conditional branch `br` is taken, after `cmp r, #0` for
// cbz/cbnz.
static A64_Cond taken_cond(const A64_Instr *br)
{
    return br->op == A64_BCOND ? (A64_Cond)br->opnd[0].sub : br->op == A64_CBZ ? A64_EQ : A64_NE;
}

// Whether the instructions from x on and from y on are the same, ending in a return.
static bool same_return_tail(const A64_Instr *x, const A64_Instr *y)
{
    for (; x && y; x = x->next, y = y->next)
        if (!same_instr(x, y))
            return false;
        else if (x->op == A64_RET)
            return !x->next && !y->next;
    return false;
}

// A constant diamond, ending block b: `br …, L; mov d, #a`, then `b M` with
// `L: mov d, #c` falling into M, or the same code ending in a return after either
// move; L reached by that branch alone: d set by the condition, as `cset d, cond` for
// 0 and 1, `mov d, #lo; cinc d, d, cond` for constants one apart; cbz/cbnz test their
// register by a compare first.
static bool fold_diamond_at(A64_Func *fn, A64_Block *b, A64_Instr **brl)
{
    A64_Instr *br = *brl;
    if ((br->op != A64_BCOND && br->op != A64_CBZ && br->op != A64_CBNZ) || !br->next ||
        !br->next->next)
        return false;
    A64_Instr *mv = br->next, *rest = mv->next;
    if (mv->op != A64_MOV || mv->opnd[0].kind != A64_OPND_REG || mv->opnd[1].kind != A64_OPND_IMM ||
        a64_is_fpreg(mv->opnd[0].reg))
        return false;
    const char *l = branch_target(br);
    A64_Block *lb = block_of(fn, l);
    if (!lb || lb != b->next || !lb->head || label_refs(fn, l) != 1)
        return false;
    const A64_Instr *mc = lb->head;
    bool jump = rest->op == A64_B && !rest->next;
    if (jump ? mc->next || !falls_to(lb->next, rest->opnd[0].sym) : !same_return_tail(rest, mc->next))
        return false;
    A64_Operand d = mv->opnd[0];
    if (mc->op != A64_MOV || mc->opnd[0].kind != A64_OPND_REG || mc->opnd[0].reg != d.reg ||
        mc->opnd[0].width != d.width || mc->opnd[1].kind != A64_OPND_IMM)
        return false;
    int bits  = width_bits(d.width);
    int64_t a = mv->opnd[1].imm, c = mc->opnd[1].imm; // not taken, taken
    if (bits == 32) {
        a = (int32_t)a;
        c = (int32_t)c;
    }
    A64_Cond cond = taken_cond(br);
    int64_t lo;
    if (c == a + 1) {
        lo = a;
    } else if (a == c + 1) {
        lo   = c;
        cond = (A64_Cond)(cond ^ 1);
    } else {
        return false;
    }
    // The new code, in place of br and mv (and the jump).
    A64_Instr *tail = new_instr(A64_CSET, jump ? NULL : rest);
    tail->opnd[0]   = d;
    tail->opnd[1]   = a64_cond(cond);
    if (lo != 0) {
        tail->op      = A64_CINC;
        tail->opnd[1] = d;
        tail->opnd[2] = a64_cond(cond);
        tail          = new_instr(A64_MOV, tail);
        tail->opnd[0] = d;
        tail->opnd[1] = a64_imm(lo);
    }
    if (br->op != A64_BCOND) {
        tail          = new_instr(A64_CMP, tail);
        tail->opnd[0] = br->opnd[0];
        tail->opnd[1] = a64_imm(0);
    }
    *brl = tail;
    if (jump)
        free_instr(rest);
    free_instr(mv);
    free_instr(br);
    while (lb->head)
        delete_at(&lb->head);
    return true;
}

static bool fold_diamond(A64_Func *fn, A64_Block *b)
{
    for (A64_Instr **brl = &b->head; *brl; brl = &(*brl)->next)
        if (is_branch((*brl)->op) && fold_diamond_at(fn, b, brl))
            return true;
    return false;
}

// The first block from b on with code, or NULL.
static A64_Block *first_code(A64_Block *b)
{
    while (b && !b->head)
        b = b->next;
    return b;
}

// Replace the label of branch or jump `in` by `l`.
static void retarget(A64_Instr *in, const char *l)
{
    A64_Operand *t = target(in);
    xfree(t->sym);
    t->sym = xstrdup(l);
}

// The registers live out of block m.
static Regs block_out(const A64_Block *m)
{
    fresh_liveness();
    for (int i = 0; i < live_info.n; i++)
        if (live_info.blocks[i] == m)
            return live_info.out[i];
    return ~0ull;
}

// The registers live before the instructions from `in` on to the end of block m.
static Regs live_from(const A64_Block *m, const A64_Instr *in)
{
    A64_Block rest = { .head = (A64_Instr *)in };
    return live_before(&rest, block_out(m));
}

// Whether the instructions from `in` on read a scratch register they have not written
// first: they cannot begin a block of their own, a scratch value never living past its
// block.
static bool reads_scratch_in(const A64_Instr *in)
{
    Regs written = 0;
    for (; in; in = in->next) {
        for (int r = A64_X(9); r <= A64_X(17); r++)
            if (reads(in, r) && !(written & bit_of(r)))
                return true;
        for (int r = A64_V(16); r < A64_VREG; r++)
            if (reads(in, r) && !(written & bit_of(r)))
                return true;
        written |= defs(in);
    }
    return false;
}

static int split_count; // labels made by splitting a block of the function

// A new label: `<prefix>P<n>_<function>`, apart from those of the TAC and of other
// functions.
static char *split_label(void)
{
    size_t len = strlen(cur_fn->name) + 32;
    char *l    = xalloc(len, __func__, __FILE__, __LINE__);
    snprintf(l, len, "%sP%d_%s", a64_local_prefix(), ++split_count, cur_fn->name);
    return l;
}

// Block m starts with `cbz`/`cbnz w, X`: the test, and where control goes when it is
// not taken: the next block when the test is all of m, else (split) the rest of m moved
// into a new block of its own label.  Sets *after to the registers live there.
static A64_Block *test_block(A64_Block *m, A64_Instr **test, bool split, Regs *after)
{
    A64_Instr *br = m ? m->head : NULL;
    if (!br || (br->op != A64_CBZ && br->op != A64_CBNZ))
        return NULL;
    *test = br;
    if (!br->next) {
        if (!m->next || !m->next->label)
            return NULL;
        *after = live_at(m->next->label);
        return m->next;
    }
    if (reads_scratch_in(br->next))
        return NULL;
    *after = live_from(m, br->next);
    if (!split)
        return m; // not to be used as a target
    A64_Block *n = xalloc(sizeof(A64_Block), __func__, __FILE__, __LINE__);
    memset(n, 0, sizeof(*n));
    n->label   = split_label();
    n->head    = br->next;
    n->next    = m->next;
    m->next    = n;
    br->next   = NULL;
    live_stale = true;
    return n;
}

// Jump threading in block b of fn:
//   a branch or jump to a jump goes where that one does;
//   `cset w, c; b M`, M a lone test of w: the test itself, then a jump past it, so that
//   the test fuses with the cset (w dead either way);
//   `mov w, #k` falling or jumping into a lone test of w: a jump where the test goes;
//   code no branch reaches and nothing falls into goes.
static bool thread_jumps(A64_Func *fn, A64_Block *b, bool fall_in)
{
    for (A64_Instr *in = b->head; in; in = in->next) {
        if (!is_branch(in->op) && in->op != A64_B)
            continue;
        A64_Block *t = first_code(block_of(fn, branch_target(in)));
        if (t && t->head->op == A64_B && strcmp(t->head->opnd[0].sym, branch_target(in)) != 0 &&
            !(t->label && strcmp(t->label, t->head->opnd[0].sym) == 0)) {
            retarget(in, t->head->opnd[0].sym);
            return true;
        }
    }
    A64_Instr **jl = last_link(b, 0), **sl = last_link(b, 1);
    A64_Instr *test;
    if (jl && sl && (*jl)->op == A64_B && (*sl)->op == A64_CSET) {
        A64_Block *m = block_of(fn, (*jl)->opnd[0].sym);
        int w        = (*sl)->opnd[0].reg;
        Regs after;
        if (test_block(m, &test, false, &after) && test->opnd[0].reg == w &&
            !((live_at(branch_target(test)) | after) & bit_of(w))) {
            A64_Block *n = test_block(m, &test, true, &after);
            A64_Instr *jmp  = *jl;
            A64_Instr *copy = new_instr(test->op, jmp);
            copy->opnd[0]   = test->opnd[0];
            copy->opnd[1]   = a64_label(branch_target(test));
            (*sl)->next     = copy;
            retarget(jmp, n->label);
            return true;
        }
    }
    // A constant tested: the jump, or the fall, into the test goes where it goes.
    A64_Instr **ml = jl && (*jl)->op == A64_B ? sl : jl;
    A64_Block *m   = jl && (*jl)->op == A64_B ? block_of(fn, (*jl)->opnd[0].sym)
                     : jl && (*jl)->op != A64_RET ? b->next : NULL;
    Regs after;
    m = first_code(m);
    if (ml && test_block(m, &test, false, &after) && (*ml)->op == A64_MOV &&
        (*ml)->opnd[0].kind == A64_OPND_REG && (*ml)->opnd[1].kind == A64_OPND_IMM &&
        (*ml)->opnd[0].reg == test->opnd[0].reg && !a64_is_fpreg(test->opnd[0].reg)) {
        A64_Block *n  = test_block(m, &test, true, &after);
        A64_Instr *mv = *ml;
        uint64_t k    = (uint64_t)mv->opnd[1].imm;
        if (test->opnd[0].width == A64_W)
            k = (uint32_t)k;
        if (mv->opnd[0].width == A64_W && test->opnd[0].width == A64_X)
            k = (uint32_t)k;
        const char *to = (k == 0) == (test->op == A64_CBZ) ? branch_target(test) : n->label;
        if (mv->next) // the jump to the test
            retarget(mv->next, to);
        else
            mv->next = new_instr(A64_B, NULL), mv->next->opnd[0] = a64_label(to);
        Regs live = to == n->label ? after : live_at(to);
        if (!(live & bit_of(mv->opnd[0].reg)) && bit_of(mv->opnd[0].reg))
            delete_at(ml);
        return true;
    }
    // Unreachable: no branch to it, nothing falling into it.
    if (b->head && !fall_in && (!b->label || label_refs(fn, b->label) == 0)) {
        while (b->head)
            delete_at(&b->head);
        return true;
    }
    return false;
}

// The instructions of block b, into *list (allocated); their number.
static int block_list(const A64_Block *b, A64_Instr ***list)
{
    int n = 0;
    for (const A64_Instr *in = b->head; in; in = in->next)
        n++;
    *list = xalloc((n ? n : 1) * sizeof(A64_Instr *), __func__, __FILE__, __LINE__);
    n     = 0;
    for (A64_Instr *in = b->head; in; in = in->next)
        (*list)[n++] = in;
    return n;
}

// Whether block b ends in a return or a jump.
static bool ends_out(const A64_Block *b)
{
    const A64_Instr *last = NULL;
    for (const A64_Instr *in = b->head; in; in = in->next)
        last = in;
    return last && (last->op == A64_RET || last->op == A64_B);
}

// Tail merging: of two blocks ending in the same instructions, through a return or a
// jump, one keeps them and the other jumps there, when that saves an instruction.  The
// one kept is split where they begin, unless they are all of it and it has a label.
static bool merge_tails(A64_Func *fn)
{
    for (A64_Block *y = fn->blocks; y; y = y->next) {
        if (!ends_out(y))
            continue;
        A64_Instr **ly;
        int ny = block_list(y, &ly);
        for (A64_Block *x = fn->blocks; x; x = x->next) {
            if (x == y || !ends_out(x))
                continue;
            A64_Instr **lx;
            int nx = block_list(x, &lx), k = 0;
            while (k < nx && k < ny && same_instr(lx[nx - 1 - k], ly[ny - 1 - k]))
                k++;
            if (k < 2 || reads_scratch_in(lx[nx - k])) {
                xfree(lx);
                continue;
            }
            // The shared code: in x from index nx - k on.
            const char *l;
            if (k == nx && x->label) {
                l = x->label;
            } else {
                A64_Block *n = xalloc(sizeof(A64_Block), __func__, __FILE__, __LINE__);
                memset(n, 0, sizeof(*n));
                n->label = split_label();
                n->head  = lx[nx - k];
                n->next  = x->next;
                x->next  = n;
                if (k == nx)
                    x->head = NULL;
                else
                    lx[nx - k - 1]->next = NULL;
                l = n->label;
            }
            // y jumps there instead.
            A64_Instr *jmp = new_instr(A64_B, NULL);
            jmp->opnd[0]   = a64_label(l);
            if (k == ny)
                y->head = jmp;
            else
                ly[ny - k - 1]->next = jmp;
            for (int i = ny - k; i < ny; i++)
                free_instr(ly[i]);
            xfree(lx);
            xfree(ly);
            live_stale = true;
            return true;
        }
        xfree(ly);
    }
    return false;
}

// The rewrites to a fixed point, then the pairing of loads and stores, which would
// hide a store from the deletion of its reload.
void a64_peephole(A64_Func *fn, unsigned result_in)
{
    result       = result_in;
    cur_fn       = fn;
    split_count  = 0;
    instr_count  = 0;
    for (const A64_Block *b = fn->blocks; b; b = b->next)
        for (const A64_Instr *in = b->head; in; in = in->next)
            instr_count++;
    bool changed = true;
    while (changed) {
        changed = false;
        compute_liveness(fn);
        bool fall_in = false;
        for (A64_Block *b = fn->blocks; b; b = b->next) {
            cur_block = b;
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
            if (fold_diamond(fn, b))
                changed = true;
            if (b != fn->blocks && thread_jumps(fn, b, fall_in))
                changed = true;
            // Whether control reaches the next block by falling out of this one.
            bool reached = b == fn->blocks || fall_in || (b->label && label_refs(fn, b->label));
            fall_in      = reached && falls_through(b);
        }
        if (!changed && merge_tails(fn))
            changed = true;
    }
    free_liveness();
    cur_block = NULL;
    for (A64_Block *b = fn->blocks; b; b = b->next) {
        for (A64_Instr **link = &b->head; *link; link = &(*link)->next)
            pair(link);
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}
