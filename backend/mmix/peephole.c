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
//   - a branch backward, to a loop's head, becomes probable (pb*).
//
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

void mmix_peephole(Mmix_Func *fn, int fround_off)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (Mmix_Block *b = fn->blocks; b; b = b->next)
            changed |= rewrite_block(b, fround_off);
        changed |= rewrite_jumps(fn);
    }
    predict_loops(fn);
}
