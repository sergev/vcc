//
// Peephole optimization of a function's MMIX code, after the frame is filled in:
//   - a result computed in a scratch register and then moved (set $r,$s) is computed in
//     $r; the scratch registers $248-$250 hold nothing from one TAC instruction to the
//     next, but within a block copy loop, which reads them across its label;
//   - an instruction whose result the next one overwrites unread goes, when it does
//     nothing else (a load from the frame, or arithmetic);
//   - a store of what was just loaded from the same place goes (the rounding of a
//     float through %.fround, done twice);
//   - code after a jmp or pop in a block goes, a jmp to the next block goes, and a branch
//     over a jmp to the next block becomes the inverse branch to the jmp's target;
//   - a branch backward, to a loop's head, becomes probable (pb*);
// and over register liveness ($0-$31 and the scratch registers; any other global is
// taken to be live):
//   - an instruction whose result is dead goes, when it does nothing else;
//   - a copy only the next instruction reads (set $t,$x, then $t read) goes, the next
//     reading $x;
//   - a pointer copied, stepped and read through the copy (set $t,$p; addu $p,$p,k; ld
//     $x,$t,0, as *p++ goes) is read through $p, then stepped.
//
#include <stdint.h>
#include <string.h>

#include "internal.h"
#include "xalloc.h"

static bool is_reg(const Mmix_Operand *o, int reg)
{
    return o->kind == MMIX_OPND_REG && o->reg == reg;
}

static bool is_scratch(int reg)
{
    return reg == REG_A || reg == REG_B || reg == REG_C;
}

static bool is_branch(Mmix_Op op)
{
    return mmix_form[op] == MMIX_FORM_BRANCH;
}

// Whether `in` writes $X (its first operand) and nothing else, and reads $X only as
// any other operand says.  Not a conditional set (csz keeps $X when the condition
// fails), nor a wyde that adds to $X (incl, orl, andnl), nor a store, branch or call.
static bool writes_x(const Mmix_Instr *in)
{
    if (in->opnd[0].kind != MMIX_OPND_REG)
        return false;
    switch (in->op) {
    case MMIX_CSN:
    case MMIX_CSZ:
    case MMIX_CSP:
    case MMIX_CSOD:
    case MMIX_CSNN:
    case MMIX_CSNZ:
    case MMIX_CSNP:
    case MMIX_CSEV:
    case MMIX_GO:
    case MMIX_PUSHGO:
    case MMIX_STB:
    case MMIX_STBU:
    case MMIX_STW:
    case MMIX_STWU:
    case MMIX_STT:
    case MMIX_STTU:
    case MMIX_STO:
    case MMIX_STSF:
        return false;
    default:
        break;
    }
    switch (mmix_form[in->op]) {
    case MMIX_FORM_XYZ:
    case MMIX_FORM_FP:
    case MMIX_FORM_NEG:
    case MMIX_FORM_ROUND:
    case MMIX_FORM_XZ:
    case MMIX_FORM_XY:
    case MMIX_FORM_MEM:
    case MMIX_FORM_ADDR:
    case MMIX_FORM_GET:
        return true;
    case MMIX_FORM_WYDE:
        return in->op == MMIX_SETH || in->op == MMIX_SETMH || in->op == MMIX_SETML ||
               in->op == MMIX_SETL;
    default:
        return false;
    }
}

// Whether `in` reads register `reg`.
static bool reads(const Mmix_Instr *in, int reg)
{
    int first = writes_x(in) ? 1 : 0;
    for (int i = first; i < MMIX_MAX_OPERANDS; i++)
        if (is_reg(&in->opnd[i], reg))
            return true;
    // A wyde that adds to $X, a conditional set: $X read too.
    return first == 0 && in->opnd[0].kind == MMIX_OPND_REG && in->opnd[0].reg == reg;
}

static bool is_load(Mmix_Op op)
{
    return op >= MMIX_LDB && op <= MMIX_LDSF;
}

// Whether `in` writes no memory nor anything but $X, and may be deleted when $X is dead.
static bool pure(const Mmix_Instr *in)
{
    if (!writes_x(in) || in->op == MMIX_GET || in->op == MMIX_DIV || in->op == MMIX_DIVU)
        return false; // div and divu write rR too
    if (is_load(in->op)) // from the frame only: memory elsewhere may be volatile
        return in->opnd[1].kind == MMIX_OPND_REG && in->opnd[1].reg == MMIX_SP;
    // Signed arithmetic and fix set overflow bits in rA, but not the ones we emit.
    return true;
}

// Whether `in` ends its block's flow: nothing after it runs.
static bool is_terminal(const Mmix_Instr *in)
{
    return in->op == MMIX_JMP || in->op == MMIX_POP;
}

static void free_operands(Mmix_Instr *in)
{
    for (int i = 0; i < MMIX_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
}

// Remove the instruction after `prev` (or the head, when `prev` is NULL) from `b`.
static void remove_after(Mmix_Block *b, Mmix_Instr *prev)
{
    Mmix_Instr *dead = prev ? prev->next : b->head;
    if (prev)
        prev->next = dead->next;
    else
        b->head = dead->next;
    if (b->tail == dead)
        b->tail = prev;
    free_operands(dead);
    xfree(dead);
}

// Whether register `reg` is read in `b` after `in`, or after it in the next block when
// `b` falls through, before anything writes it.
static bool read_later(const Mmix_Block *b, const Mmix_Instr *in, int reg)
{
    for (int pass = 0; pass < 2 && b; pass++) {
        for (const Mmix_Instr *i = in ? in->next : b->head; i; i = i->next) {
            if (reads(i, reg))
                return true;
            if (writes_x(i) && i->opnd[0].reg == reg)
                return false;
            if (is_terminal(i))
                return false;
        }
        b  = b->next;
        in = NULL;
    }
    return false;
}

static bool rewrite_block(Mmix_Block *b, int fround_off)
{
    bool changed = false;
    for (Mmix_Instr *prev = NULL, *in = b->head; in && in->next;) {
        Mmix_Instr *next = in->next;
        // A result moved out of a scratch register: computed in place.
        if (next->op == MMIX_SET && writes_x(in) && is_scratch(in->opnd[0].reg) &&
            is_reg(&next->opnd[1], in->opnd[0].reg) && !is_scratch(next->opnd[0].reg) &&
            !read_later(b, next, in->opnd[0].reg)) {
            in->opnd[0].reg = next->opnd[0].reg;
            remove_after(b, in);
            changed = true;
            continue;
        }
        // A result the next instruction overwrites unread.
        if (pure(in) && writes_x(next) && next->opnd[0].reg == in->opnd[0].reg &&
            !reads(next, in->opnd[0].reg)) {
            remove_after(b, prev);
            in      = prev ? prev->next : b->head;
            changed = true;
            continue;
        }
        // A float stored where it was just loaded from (rounded twice).
        if (in->op == MMIX_LDSF && next->op == MMIX_STSF &&
            is_reg(&next->opnd[0], in->opnd[0].reg) && is_reg(&next->opnd[1], MMIX_SP) &&
            is_reg(&in->opnd[1], MMIX_SP) && in->opnd[2].kind == MMIX_OPND_IMM &&
            next->opnd[2].kind == MMIX_OPND_IMM && in->opnd[2].imm == fround_off &&
            next->opnd[2].imm == fround_off) {
            remove_after(b, in);
            changed = true;
            continue;
        }
        // Nothing after a jmp or pop runs.
        if (is_terminal(in)) {
            while (in->next)
                remove_after(b, in);
            changed = true;
            break;
        }
        prev = in;
        in   = next;
    }
    return changed;
}

static Mmix_Op inverse(Mmix_Op op)
{
    switch (op) {
    case MMIX_BN:
        return MMIX_BNN;
    case MMIX_BNN:
        return MMIX_BN;
    case MMIX_BZ:
        return MMIX_BNZ;
    case MMIX_BNZ:
        return MMIX_BZ;
    case MMIX_BP:
        return MMIX_BNP;
    case MMIX_BNP:
        return MMIX_BP;
    case MMIX_BOD:
        return MMIX_BEV;
    default:
        return MMIX_BOD; // bev
    }
}

static const Mmix_Instr *before_tail(const Mmix_Block *b)
{
    const Mmix_Instr *p = NULL;
    for (const Mmix_Instr *i = b->head; i && i != b->tail; i = i->next)
        p = i;
    return p;
}

static bool same_label(const Mmix_Operand *o, const Mmix_Block *b)
{
    return o->kind == MMIX_OPND_LABEL && b && b->label && strcmp(o->sym, b->label) == 0;
}

// A jmp to the next block goes; a branch over such a jmp to the block after it turns.
static bool rewrite_jumps(Mmix_Func *fn)
{
    bool changed = false;
    for (Mmix_Block *b = fn->blocks; b; b = b->next) {
        // The next block that has code or a label: an empty unlabelled one is nothing.
        const Mmix_Block *n = b->next;
        while (n && !n->label && !n->head)
            n = n->next;
        Mmix_Instr *t = b->tail;
        if (!t || !n)
            continue;
        if (t->op == MMIX_JMP && same_label(&t->opnd[0], n)) {
            remove_after(b, (Mmix_Instr *)before_tail(b));
            changed = true;
            continue;
        }
        Mmix_Instr *br = (Mmix_Instr *)before_tail(b);
        if (t->op == MMIX_JMP && br && is_branch(br->op) && br->op < MMIX_PBN &&
            t->opnd[0].kind == MMIX_OPND_LABEL && same_label(&br->opnd[1], n)) {
            br->op = inverse(br->op);
            xfree(br->opnd[1].sym);
            br->opnd[1] = t->opnd[0];
            t->opnd[0]  = (Mmix_Operand){ 0 };
            remove_after(b, br);
            changed = true;
        }
    }
    return changed;
}

// A branch to a block at or before its own: probable, as a loop's back edge is.
static void predict_loops(Mmix_Func *fn)
{
    for (Mmix_Block *b = fn->blocks; b; b = b->next) {
        for (Mmix_Instr *in = b->head; in; in = in->next) {
            if (!is_branch(in->op) || in->op >= MMIX_PBN || in->opnd[1].kind != MMIX_OPND_LABEL)
                continue;
            for (const Mmix_Block *t = fn->blocks; t != b->next; t = t->next) {
                if (same_label(&in->opnd[1], t)) {
                    in->op = (Mmix_Op)(in->op - MMIX_BN + MMIX_PBN);
                    break;
                }
            }
        }
    }
}

//
// Liveness of $0-$31 (bits 0-31) and of $248-$250 and $255 (bits 32-35).
//
typedef uint64_t Regs;

static int bit_of(int reg)
{
    if (reg >= 0 && reg < 32)
        return reg;
    switch (reg) {
    case REG_A:
        return 32;
    case REG_B:
        return 33;
    case REG_C:
        return 34;
    case MMIX_TMP:
        return 35;
    default:
        return -1; // a global: always live
    }
}

static Regs reg_bit(int reg)
{
    int b = bit_of(reg);
    return b < 0 ? 0 : (Regs)1 << b;
}

static bool is_call(Mmix_Op op)
{
    return op == MMIX_PUSHJ || op == MMIX_PUSHGO;
}

// The registers `in` reads and writes.  A call pushj $X reads the registers above $X
// (its arguments) and leaves them and $X (its result) and the scratch registers
// changed; pop X reads $0..$(X-1).
static void effect(const Mmix_Instr *in, Regs *use, Regs *def)
{
    *use = *def = 0;
    if (is_call(in->op)) {
        int x = in->opnd[0].reg;
        for (int r = x; r < 32; r++) {
            if (r > x)
                *use |= reg_bit(r);
            *def |= reg_bit(r);
        }
        *def |= reg_bit(REG_A) | reg_bit(REG_B) | reg_bit(REG_C) | reg_bit(MMIX_TMP);
        if (in->op == MMIX_PUSHGO)
            *use |= reg_bit(in->opnd[1].reg) | reg_bit(in->opnd[2].reg);
        return;
    }
    if (in->op == MMIX_POP) {
        for (int r = 0; r < in->opnd[0].imm; r++)
            *use |= reg_bit(r);
        return;
    }
    if (in->op == MMIX_TRAP) {
        *use = *def = ~(Regs)0;
        return;
    }
    for (int i = 0; i < MMIX_MAX_OPERANDS; i++)
        if (in->opnd[i].kind == MMIX_OPND_REG && reads(in, in->opnd[i].reg))
            *use |= reg_bit(in->opnd[i].reg);
    if (in->opnd[0].kind == MMIX_OPND_REG && (writes_x(in) || reads(in, in->opnd[0].reg)) &&
        !is_branch(in->op) && !(in->op >= MMIX_STB && in->op <= MMIX_STSF))
        *def |= reg_bit(in->opnd[0].reg);
}

typedef struct {
    int n;
    Mmix_Block **blocks;
    Regs *in, *out;
} Liveness;

static int block_of(const Liveness *lv, const char *label)
{
    for (int i = 0; i < lv->n; i++)
        if (lv->blocks[i]->label && strcmp(lv->blocks[i]->label, label) == 0)
            return i;
    return -1;
}

// What is live where `in` may jump: its target's live-in, or everything when the target
// is not a block of the function; 0 when `in` does not jump.
static Regs live_at_target(const Liveness *lv, const Mmix_Instr *in)
{
    const Mmix_Operand *target = in->op == MMIX_JMP ? &in->opnd[0]
                                 : is_branch(in->op) ? &in->opnd[1]
                                                     : NULL;
    if (!target)
        return 0;
    int s = target->kind == MMIX_OPND_LABEL ? block_of(lv, target->sym) : -1;
    return s >= 0 ? lv->in[s] : ~(Regs)0;
}

// One instruction backward: what is live before `in`, given what is after it.
static Regs step_back(const Liveness *lv, const Mmix_Instr *in, Regs live)
{
    Regs use, def;
    effect(in, &use, &def);
    return (live & ~def) | use | live_at_target(lv, in);
}

static Regs live_through(const Liveness *lv, const Mmix_Block *b, Regs live)
{
    // Backward over the block.
    const Mmix_Instr *stack[4096];
    int n = 0;
    for (const Mmix_Instr *i = b->head; i; i = i->next) {
        if (n == 4096)
            return ~(Regs)0;
        stack[n++] = i;
    }
    while (n > 0)
        live = step_back(lv, stack[--n], live);
    return live;
}

static void liveness(Mmix_Func *fn, Liveness *lv)
{
    lv->n = 0;
    for (const Mmix_Block *b = fn->blocks; b; b = b->next)
        lv->n++;
    lv->blocks = xalloc((lv->n + 1) * sizeof(Mmix_Block *), __func__, __FILE__, __LINE__);
    lv->in     = xalloc((lv->n + 1) * sizeof(Regs), __func__, __FILE__, __LINE__);
    lv->out    = xalloc((lv->n + 1) * sizeof(Regs), __func__, __FILE__, __LINE__);
    int k      = 0;
    for (Mmix_Block *b = fn->blocks; b; b = b->next, k++) {
        lv->blocks[k] = b;
        lv->in[k] = lv->out[k] = 0;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = lv->n - 1; i >= 0; i--) {
            const Mmix_Block *b = lv->blocks[i];
            Regs out            = 0;
            const Mmix_Instr *t = b->tail;
            if (!t || !is_terminal(t)) // falls through
                out |= i + 1 < lv->n ? lv->in[i + 1] : 0;
            Regs in = live_through(lv, b, out); // a branch adds its target's, where it is
            if (in != lv->in[i] || out != lv->out[i]) {
                lv->in[i]  = in;
                lv->out[i] = out;
                changed    = true;
            }
        }
    }
}

static void free_liveness(Liveness *lv)
{
    xfree(lv->blocks);
    xfree(lv->in);
    xfree(lv->out);
}

// Rename every read of register `from` in `in` to `to`.
static void rename_reads(Mmix_Instr *in, int from, int to)
{
    int first = writes_x(in) ? 1 : 0;
    for (int i = first; i < MMIX_MAX_OPERANDS; i++)
        if (is_reg(&in->opnd[i], from))
            in->opnd[i].reg = to;
}

// Whether the copy's register may be renamed in `in`: its reads are all explicit.
static bool renamable(const Mmix_Instr *in)
{
    return !is_call(in->op) && in->op != MMIX_POP && in->op != MMIX_TRAP && in->op != MMIX_PUT;
}

static bool rewrite_live(const Liveness *lv, Mmix_Block *b, Regs out)
{
    // live[k]: what is live after the k-th instruction.
    Mmix_Instr *ins[4096];
    Regs live[4096];
    int n = 0;
    for (Mmix_Instr *i = b->head; i; i = i->next) {
        if (n == 4096)
            return false;
        ins[n++] = i;
    }
    Regs l = out;
    for (int k = n - 1; k >= 0; k--) {
        live[k] = l | live_at_target(lv, ins[k]);
        l       = step_back(lv, ins[k], l);
    }
    for (int k = 0; k < n; k++) {
        Mmix_Instr *in   = ins[k];
        Mmix_Instr *prev = k > 0 ? ins[k - 1] : NULL;
        // A dead result.
        if (pure(in) && bit_of(in->opnd[0].reg) >= 0 && !(live[k] & reg_bit(in->opnd[0].reg))) {
            remove_after(b, prev);
            return true;
        }
        if (in->op != MMIX_SET || bit_of(in->opnd[0].reg) < 0 || k + 1 >= n)
            continue;
        int t = in->opnd[0].reg, x = in->opnd[1].reg;
        // set $t,$x; I reading $t: I reads $x.
        Mmix_Instr *next = ins[k + 1];
        bool next_writes_t = writes_x(next) && next->opnd[0].reg == t;
        if (renamable(next) && reads(next, t) && (!(live[k + 1] & reg_bit(t)) || next_writes_t)) {
            rename_reads(next, t, x);
            remove_after(b, prev);
            return true;
        }
        // set $t,$p; addu $p,$p,k; I reading $t, not $p: I reads $p, then $p steps.
        if (k + 2 >= n || (next->op != MMIX_ADDU && next->op != MMIX_SUBU) ||
            !is_reg(&next->opnd[0], x) || !is_reg(&next->opnd[1], x) ||
            next->opnd[2].kind != MMIX_OPND_IMM)
            continue;
        Mmix_Instr *use_in = ins[k + 2];
        bool use_writes_t  = writes_x(use_in) && use_in->opnd[0].reg == t;
        bool use_writes_p  = writes_x(use_in) && use_in->opnd[0].reg == x;
        if (!renamable(use_in) || is_branch(use_in->op) || is_terminal(use_in) ||
            !reads(use_in, t) || reads(use_in, x) || use_writes_p ||
            ((live[k + 2] & reg_bit(t)) && !use_writes_t))
            continue;
        rename_reads(use_in, t, x);
        // Now: set, addu, use -> use, addu.
        remove_after(b, prev); // the set
        Mmix_Instr *step = prev ? prev->next : b->head;
        // Swap the step and the use.
        Mmix_Instr *after = use_in->next;
        if (prev)
            prev->next = use_in;
        else
            b->head = use_in;
        use_in->next = step;
        step->next   = after;
        if (b->tail == use_in)
            b->tail = step;
        return true;
    }
    return false;
}

void mmix_peephole(Mmix_Func *fn, int fround_off)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (Mmix_Block *b = fn->blocks; b; b = b->next)
            changed |= rewrite_block(b, fround_off);
        changed |= rewrite_jumps(fn);
        if (changed)
            continue;
        Liveness lv;
        liveness(fn, &lv);
        for (int i = 0; i < lv.n && !changed; i++)
            changed = rewrite_live(&lv, lv.blocks[i], lv.out[i]);
        free_liveness(&lv);
    }
    predict_loops(fn);
}
