//
// Peephole pass over the RISC-V IR, after register allocation: an operation on a
// just-loaded constant takes the immediate form (or the zero register), a move folds
// into its uses within the block and a result is computed where it is moved, a move
// back goes, identities and a store's reload go, and so do a jump to the
// next label, a branch over a jump, and code after a jump.  Code selection never
// carries a scratch register (t0-t6, ft0-ft11) past its block, so whether a scratch
// value is read again is decided by looking to the end of the block.
//
#include <string.h>

#include "codegen.h"
#include "rv.h"
#include "xalloc.h"

static bool is_branch(Rv_Op op)
{
    return op == RV_BEQZ || op == RV_BNEZ;
}

static bool is_call(Rv_Op op)
{
    return op == RV_CALL || op == RV_JALR;
}

// Operand 0 is read, not written.
static bool no_dest(Rv_Op op)
{
    switch (op) {
    case RV_SB:
    case RV_SH:
    case RV_SW:
    case RV_SD:
    case RV_FSW:
    case RV_FSD:
    case RV_BEQZ:
    case RV_BNEZ:
    case RV_J:
    case RV_CALL:
    case RV_JALR:
    case RV_RET:
        return true;
    default:
        return false;
    }
}

static bool is_scratch(int r)
{
    return (r >= RV_T0 && r <= RV_T2) || (r >= RV_T3 && r <= RV_T6) ||
           (r >= RV_F0 && r < RV_F0 + 8) || (r >= RV_F0 + 28 && r < RV_VREG);
}

static bool is_arg(int r)
{
    return (r >= RV_A0 && r <= RV_A7) || (r >= RV_FA0 && r < RV_FA0 + 8);
}

static bool reads(const Rv_Instr *in, int r)
{
    if (is_call(in->op) && is_arg(r))
        return true;
    if (in->op == RV_RET)
        return r == RV_A0 || r == RV_A0 + 1 || r == RV_FA0 || r == RV_FA0 + 1;
    for (int i = 0; i < 3; i++) {
        const Rv_Operand *o = &in->opnd[i];
        if (o->kind == RV_OPND_MEM && o->reg == r)
            return true;
        if (o->kind == RV_OPND_REG && o->reg == r && (i > 0 || no_dest(in->op)))
            return true;
    }
    return false;
}

static bool writes(const Rv_Instr *in, int r)
{
    if (is_call(in->op))
        return is_scratch(r) || is_arg(r) || r == RV_RA;
    return !no_dest(in->op) && in->opnd[0].kind == RV_OPND_REG && in->opnd[0].reg == r;
}

// The value `in` leaves in scratch register r is never read.
static bool dead_after(const Rv_Instr *in, int r)
{
    if (!is_scratch(r))
        return false;
    for (const Rv_Instr *n = in->next; n; n = n->next) {
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
static bool dies_after(const Rv_Instr *in, int r)
{
    if (is_scratch(r))
        return dead_after(in, r);
    for (const Rv_Instr *n = in->next; n; n = n->next) {
        if (reads(n, r))
            return false;
        if (writes(n, r))
            return true;
        if (n->op == RV_RET)
            return is_arg(r);
        if (is_branch(n->op) || n->op == RV_J)
            return false;
    }
    return false;
}

// The value r holds before `in` is not read after it.
static bool last_read(const Rv_Instr *in, int r)
{
    return is_scratch(r) && (writes(in, r) || dead_after(in, r));
}

// Replace reads of r in `in` by register `by`; memory bases too when `mem`.
static void replace_reads(Rv_Instr *in, int r, int by, bool mem)
{
    for (int i = no_dest(in->op) ? 0 : 1; i < 3; i++) {
        Rv_Operand *o = &in->opnd[i];
        if ((o->kind == RV_OPND_REG || (mem && o->kind == RV_OPND_MEM)) && o->reg == r)
            o->reg = by;
    }
}

static bool reads_as_base(const Rv_Instr *in, int r)
{
    for (int i = 0; i < 3; i++)
        if (in->opnd[i].kind == RV_OPND_MEM && in->opnd[i].reg == r)
            return true;
    return false;
}

static void free_instr(Rv_Instr *in)
{
    for (int i = 0; i < 3; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

// Unlink and free *link.
static void delete_at(Rv_Instr **link)
{
    Rv_Instr *in = *link;
    *link        = in->next;
    free_instr(in);
}

static bool fits12(int64_t v)
{
    return v >= -2048 && v <= 2047;
}

static bool commutes(Rv_Op op)
{
    return op == RV_ADD || op == RV_ADDW || op == RV_AND || op == RV_OR || op == RV_XOR;
}

// `op d, a, imm` for `op d, a, <imm in a register>`; false when there is none.
static bool immediate_form(Rv_Instr *in, int64_t imm)
{
    Rv_Op op;
    switch (in->op) {
    case RV_ADD:
        op = RV_ADDI;
        break;
    case RV_ADDW:
        op = RV_ADDIW;
        break;
    case RV_SUB:
        op  = RV_ADDI;
        imm = -imm;
        break;
    case RV_SUBW:
        op  = RV_ADDIW;
        imm = -imm;
        break;
    case RV_AND:
        op = RV_ANDI;
        break;
    case RV_OR:
        op = RV_ORI;
        break;
    case RV_XOR:
        op = RV_XORI;
        break;
    case RV_SLT:
        op = RV_SLTI;
        break;
    case RV_SLTU:
        op = RV_SLTIU;
        break;
    case RV_SLL:
        op  = RV_SLLI;
        imm &= 8 * riscv_xlen - 1;
        break;
    case RV_SRL:
        op  = RV_SRLI;
        imm &= 8 * riscv_xlen - 1;
        break;
    case RV_SRA:
        op  = RV_SRAI;
        imm &= 8 * riscv_xlen - 1;
        break;
    case RV_SLLW:
        op  = RV_SLLIW;
        imm &= 31;
        break;
    case RV_SRLW:
        op  = RV_SRLIW;
        imm &= 31;
        break;
    case RV_SRAW:
        op  = RV_SRAIW;
        imm &= 31;
        break;
    default:
        return false;
    }
    if (!fits12(imm))
        return false;
    in->op      = op;
    in->opnd[2] = rv_imm(imm);
    return true;
}

// `li r, imm` followed by its only use.
static bool fold_li(Rv_Instr **link)
{
    Rv_Instr *li = *link, *next = li->next;
    int r        = li->opnd[0].reg;
    if (!next || !reads(next, r) || !last_read(next, r) || reads_as_base(next, r))
        return false;
    int64_t imm = li->opnd[1].imm;
    Rv_Operand *o = next->opnd;
    if (o[0].kind == RV_OPND_REG && o[1].kind == RV_OPND_REG && o[2].kind == RV_OPND_REG) {
        if (commutes(next->op) && o[1].reg == r && o[2].reg != r) {
            o[1].reg = o[2].reg;
            o[2].reg = r;
        }
        if (o[2].reg == r && o[1].reg != r && immediate_form(next, imm)) {
            delete_at(link);
            return true;
        }
    }
    if (imm == 0 && !is_call(next->op)) {
        replace_reads(next, r, RV_ZERO, false);
        delete_at(link);
        return true;
    }
    return false;
}

static bool is_move(Rv_Op op)
{
    return op == RV_MV || op == RV_FMVD || op == RV_FMVS;
}

static bool is_store(Rv_Op op)
{
    return op == RV_SB || op == RV_SH || op == RV_SW || op == RV_SD || op == RV_FSW ||
           op == RV_FSD;
}

// Delete a later `load` of what store `st` wrote, into the same register, when nothing
// between them changes the register, the base, or memory that may overlap.  Only a
// frame slot is followed past other instructions: a store through another base, or a
// call, may write it.
static bool delete_reload(Rv_Instr *st, Rv_Op load)
{
    int r = st->opnd[0].reg, base = st->opnd[1].reg;
    int64_t off = st->opnd[1].imm;
    for (Rv_Instr **link = &st->next; *link; link = &(*link)->next) {
        Rv_Instr *n = *link;
        if (n->op == load && n->opnd[0].reg == r && n->opnd[1].reg == base &&
            n->opnd[1].imm == off) {
            delete_at(link);
            return true;
        }
        if (base != RV_SP && base != RV_S0)
            return false;
        if (is_call(n->op) || writes(n, r) || writes(n, base))
            return false;
        if (is_store(n->op) &&
            (n->opnd[1].reg != base || (n->opnd[1].imm < off + 8 && off < n->opnd[1].imm + 8)))
            return false;
    }
    return false;
}

// `mv t, r`: the reads of t up to its next write read r instead, if r is not written
// before the last of them.  A scratch t may reach the end of the block instead, and an
// argument register a return; any other register must be written again before a
// branch, or its value may be read beyond.  This takes the operand and result copies around a register pair, which come
// four together, not one by one.
static bool forward_move(Rv_Instr **link)
{
    Rv_Instr *mv = *link;
    int t = mv->opnd[0].reg, r = mv->opnd[1].reg;
    bool clobbered = false;
    Rv_Instr *end  = NULL;
    for (Rv_Instr *n = mv->next;; n = n->next) {
        if (!n) {
            if (!is_scratch(t))
                return false;
            break;
        }
        if (reads(n, t)) {
            if (clobbered || is_call(n->op) || (mv->op != RV_MV && reads_as_base(n, t)))
                return false;
        }
        if (writes(n, t)) {
            end = n->next;
            break;
        }
        // An argument register the return does not read is dead there: the
        // epilogue is already in place.
        if (n->op == RV_RET && is_arg(t) && !reads(n, t))
            break;
        if (!is_scratch(t) && (is_branch(n->op) || n->op == RV_J || n->op == RV_RET))
            return false;
        if (writes(n, r))
            clobbered = true;
    }
    for (Rv_Instr *n = mv->next; n != end; n = n->next)
        if (reads(n, t))
            replace_reads(n, t, r, true);
    delete_at(link);
    return true;
}

// `mv x, y` and a later `mv y, x`, with neither register written between: the second
// changes nothing.
static bool move_back(Rv_Instr *mv)
{
    int x = mv->opnd[0].reg, y = mv->opnd[1].reg;
    for (Rv_Instr **link = &mv->next; *link; link = &(*link)->next) {
        Rv_Instr *n = *link;
        if (n->op == mv->op && n->opnd[0].reg == y && n->opnd[1].reg == x) {
            delete_at(link);
            return true;
        }
        if (writes(n, x) || writes(n, y))
            return false;
    }
    return false;
}

// `in` computes into t, and a later `mv d, t` is the last read of it: compute into d,
// when d is neither read nor written in between.  The reads of t in between read d.
static bool compute_in_place(Rv_Instr *in)
{
    int t = in->opnd[0].reg;
    for (Rv_Instr **link = &in->next; *link; link = &(*link)->next) {
        Rv_Instr *n = *link;
        if (is_move(n->op) && n->opnd[1].reg == t && n->opnd[0].reg != t &&
            rv_is_freg(n->opnd[0].reg) == rv_is_freg(t) && dies_after(n, t)) {
            int d = n->opnd[0].reg;
            for (Rv_Instr *m = in->next; m != n; m = m->next)
                if (reads(m, d) || writes(m, d))
                    return false;
            for (Rv_Instr *m = in->next; m != n; m = m->next)
                replace_reads(m, t, d, true);
            in->opnd[0].reg = d;
            delete_at(link);
            return true;
        }
        if (writes(n, t) || is_call(n->op))
            return false;
    }
    return false;
}

// One rewrite at *link; true when something changed.
static bool rewrite(Rv_Instr **link)
{
    Rv_Instr *in = *link, *next = in->next;
    Rv_Operand *o = in->opnd;

    if (in->op == RV_LI && is_scratch(o[0].reg) && fold_li(link))
        return true;

    // Identities.
    if (is_move(in->op) && o[0].reg == o[1].reg) {
        delete_at(link);
        return true;
    }
    if ((in->op == RV_ADDI || in->op == RV_ORI || in->op == RV_XORI || in->op == RV_SLLI ||
         in->op == RV_SRLI || in->op == RV_SRAI) &&
        o[2].imm == 0) {
        in->op      = RV_MV;
        in->opnd[2] = (Rv_Operand){ 0 };
        return true;
    }
    if ((in->op == RV_ADD || in->op == RV_OR || in->op == RV_XOR || in->op == RV_SUB) &&
        o[2].kind == RV_OPND_REG && o[2].reg == RV_ZERO) {
        in->op      = RV_MV;
        in->opnd[2] = (Rv_Operand){ 0 };
        return true;
    }
    if (!next)
        return false;

    // A move into its uses.
    if (is_move(in->op) && forward_move(link))
        return true;
    if (is_move(in->op) && move_back(in))
        return true;

    // A result computed into a scratch register only to be moved: compute it in place.
    if (!no_dest(in->op) && !is_call(in->op) && o[0].kind == RV_OPND_REG && o[0].reg != RV_SP &&
        compute_in_place(in))
        return true;

    // A byte load is already zero-extended.
    if (in->op == RV_LBU && next->op == RV_ANDI && next->opnd[2].imm == 255 &&
        next->opnd[0].reg == o[0].reg && next->opnd[1].reg == o[0].reg) {
        delete_at(&in->next);
        return true;
    }

    // The reload of what was stored, into the same register.
    // A word reload restores the register only on rv32, where a word is all of it.
    static const Rv_Op reload[][2] = { { RV_SD, RV_LD },
                                       { RV_FSD, RV_FLD },
                                       { RV_FSW, RV_FLW },
                                       { RV_SW, RV_LW } };
    size_t nreload = sizeof(reload) / sizeof(reload[0]) - (riscv_xlen == 8);
    for (size_t k = 0; k < nreload; k++)
        if (in->op == reload[k][0] && o[1].reg != o[0].reg && delete_reload(in, reload[k][1]))
            return true;
    return false;
}

// Whether label `l` is on `b` or on an empty block between `b` and the next code.
static bool falls_to(const Rv_Block *b, const char *l)
{
    for (; b; b = b->next) {
        if (b->label && strcmp(b->label, l) == 0)
            return true;
        if (b->head)
            return false;
    }
    return false;
}

static Rv_Instr **last_link(Rv_Block *b, int back)
{
    Rv_Instr **link = &b->head;
    int n           = 0;
    for (Rv_Instr *in = b->head; in; in = in->next)
        n++;
    if (n <= back)
        return NULL;
    for (int i = 0; i < n - 1 - back; i++)
        link = &(*link)->next;
    return link;
}

static bool rewrite_block_end(Rv_Block *b)
{
    // Nothing runs after a jump or return.
    for (Rv_Instr *in = b->head; in; in = in->next) {
        if ((in->op == RV_J || in->op == RV_RET) && in->next) {
            while (in->next)
                delete_at(&in->next);
            return true;
        }
    }
    Rv_Instr **jl = last_link(b, 0);
    if (!jl || (*jl)->op != RV_J)
        return false;
    // A branch over a jump: branch the other way to the jump's target.
    Rv_Instr **bl = last_link(b, 1);
    if (bl && is_branch((*bl)->op) && falls_to(b->next, (*bl)->opnd[1].sym)) {
        Rv_Instr *br = *bl;
        br->op       = br->op == RV_BEQZ ? RV_BNEZ : RV_BEQZ;
        xfree(br->opnd[1].sym);
        br->opnd[1]     = (*jl)->opnd[0];
        (*jl)->opnd[0]  = (Rv_Operand){ 0 };
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

void rv_peephole(Rv_Func *fn)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (Rv_Block *b = fn->blocks; b; b = b->next) {
            for (Rv_Instr **link = &b->head; *link;) {
                if (rewrite(link))
                    changed = true;
                else
                    link = &(*link)->next;
            }
            while (rewrite_block_end(b))
                changed = true;
            b->tail = b->head;
            while (b->tail && b->tail->next)
                b->tail = b->tail->next;
        }
    }
}
