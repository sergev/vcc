//
// The peephole pass over the MSP430 IR.  The body first, before the prologue and
// epilogue exist (so that the registers it no longer uses are not saved), to a fixed
// point:
//
//   - the constant-generator forms: `mov #0` is `clr`, `add #1` `inc`, `cmp #0` `tst`
//     and the like (the same encodings, which the rules below then match);
//   - code after an unconditional jump, up to the next label, goes; a jump to the next
//     instruction goes, and a branch over a jump becomes the inverse branch;
//   - forward through each block, what is known of the registers: which hold the same
//     word (a copy), which a constant, which a word of a slot or a global just loaded or
//     stored, which a byte zero-extended.  A move of what a register already holds goes;
//     a reload becomes a move, a store of what a slot already holds goes, and a
//     `mov.b r, r` of a byte already extended goes;
//   - backward, the liveness of r4-r15 and SR: an instruction whose results are all dead
//     goes (a volatile access stays); a load into a dead register folds into the one
//     instruction that reads it (`mov x, r14; add r14, r12` is `add x, r12`), and a
//     load, an operation and a store back into one operation on memory; a `tst` after an
//     instruction that set the same flags from the same word goes, when only jumps that
//     read those flags follow; an operation by a neutral constant (`add #0` ahead of an
//     `addc`, `bis #0`, `and #-1`) goes;
//   - backward, the liveness of the slot bytes: a store to slot bytes nothing reads
//     before they are overwritten goes (a call reads only its stack arguments unless the
//     frame's address escaped), and codegen.c drops a frame no instruction refers to.
//
// After the frame: the jumps again, a jump to a lone `ret` becomes `ret`, and a call
// followed by a bare `ret` a tail jump `br`.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

typedef uint32_t Set; // r0-r15, and SR as bit 16

#define SR_BIT  (1u << 16)
#define R(r)    (1u << (r))
#define TRACKED (0xfff0u | SR_BIT) // r4-r15 and SR
#define ARGS    (0xf000u)          // r12-r15: the arguments of a call
#define ARGS_R8 (0x0f00u)          // r8-r11: the first operand of an r8 helper
#define CLOBBER (0xf800u | SR_BIT) // r11-r15 and SR: what a call writes

static bool is_branch(Msp_Op op)
{
    return msp_form[op] == MSP_FORM_JUMP && op != MSP_JMP;
}

static bool is_jump(Msp_Op op)
{
    return op == MSP_JMP || op == MSP_RET || op == MSP_BR;
}

// Whether a call of `in` is to a helper that takes its first operand in r8-r11, which
// our code takes as clobbered too.
static bool r8_helper(const Msp_Instr *in)
{
    static const char *const names[] = {
        "__mspabi_addd",   "__mspabi_subd",
        "__mspabi_mpyd",   "__mspabi_divd",
        "__mspabi_mpyll",  "__mspabi_divlli",
        "__mspabi_divull", "__mspabi_remlli",
        "__mspabi_remull", NULL,
    };
    const char *s = in->opnd[0].sym;
    if (in->opnd[0].kind != MSP_OPND_IMM || !s)
        return false;
    for (int i = 0; names[i]; i++)
        if (strcmp(names[i], s) == 0)
            return true;
    return false;
}

// The argument registers call (or tail call) `in` reads: those it recorded, else all.
static Set call_args(const Msp_Instr *in)
{
    return in->args ? in->args & ARGS : ARGS;
}

// What the return reads: the result and the call-saved registers.
static Set ret_use(unsigned result)
{
    return result | 0x07f0u;
}

// The registers operand `o` reads: itself, or its base.
static Set opnd_regs(const Msp_Operand *o)
{
    switch (o->kind) {
    case MSP_OPND_REG:
    case MSP_OPND_INDEXED:
    case MSP_OPND_IND:
    case MSP_OPND_POSTINC:
        return R(o->reg);
    default:
        return 0;
    }
}

static bool is_reg(const Msp_Operand *o)
{
    return o->kind == MSP_OPND_REG;
}

static bool sets_flags(Msp_Op op)
{
    switch (op) {
    case MSP_MOV:
    case MSP_BIC:
    case MSP_BIS:
    case MSP_SWPB:
    case MSP_PUSH:
    case MSP_CLR:
    case MSP_POP:
    case MSP_BR:
    case MSP_NOP:
        return false;
    default:
        return msp_form[op] != MSP_FORM_JUMP && op != MSP_RET && op != MSP_CALL;
    }
}

static bool reads_carry(Msp_Op op)
{
    return op == MSP_ADDC || op == MSP_SUBC || op == MSP_RRC || op == MSP_ADC || op == MSP_RLC;
}

// Whether `op` only reads its destination operand (compares and tests).
static bool reads_only(Msp_Op op)
{
    return op == MSP_CMP || op == MSP_BIT || op == MSP_TST;
}

// The operand `in` writes, or NULL: the destination of a two-operand instruction, the
// one operand of the others that write.
static const Msp_Operand *written(const Msp_Instr *in)
{
    if (reads_only(in->op))
        return NULL;
    switch (msp_form[in->op]) {
    case MSP_FORM_DOUBLE:
        return &in->opnd[1];
    case MSP_FORM_SINGLE:
        return in->op == MSP_PUSH || in->op == MSP_CALL ? NULL : &in->opnd[0];
    case MSP_FORM_DST:
    case MSP_FORM_TWICE:
        return &in->opnd[0];
    default:
        return NULL;
    }
}

// What `in` writes and reads, in a function whose result is in `result`.
static void def_use(const Msp_Instr *in, unsigned result, Set *def, Set *use)
{
    const Msp_Operand *a = &in->opnd[0], *b = &in->opnd[1];
    *def = *use = 0;
    switch (msp_form[in->op]) {
    case MSP_FORM_DOUBLE:
        *use |= opnd_regs(a);
        if (a->kind == MSP_OPND_POSTINC)
            *def |= R(a->reg);
        if (is_reg(b)) {
            if (in->op != MSP_MOV)
                *use |= R(b->reg);
            if (!reads_only(in->op))
                *def |= R(b->reg);
        } else {
            *use |= opnd_regs(b);
        }
        break;
    case MSP_FORM_SINGLE:
        if (in->op == MSP_CALL) {
            *use = opnd_regs(a) | call_args(in) | (r8_helper(in) ? ARGS_R8 : 0);
            *def = CLOBBER | (r8_helper(in) ? 0x0700u : 0);
            break;
        }
        *use |= opnd_regs(a);
        if (is_reg(a) && in->op != MSP_PUSH)
            *def |= R(a->reg);
        break;
    case MSP_FORM_DST:
    case MSP_FORM_TWICE:
        if (is_reg(a)) {
            if (in->op != MSP_CLR && in->op != MSP_POP)
                *use |= R(a->reg);
            if (in->op != MSP_TST)
                *def |= R(a->reg);
        } else {
            *use |= opnd_regs(a);
        }
        break;
    case MSP_FORM_SRC: // br: a tail call
        *use = opnd_regs(a) | call_args(in) | (r8_helper(in) ? ARGS_R8 : 0) | ret_use(result);
        break;
    case MSP_FORM_JUMP:
        if (in->op != MSP_JMP)
            *use = SR_BIT;
        break;
    case MSP_FORM_NONE:
        if (in->op == MSP_RET)
            *use = ret_use(result);
        break;
    }
    if (sets_flags(in->op))
        *def |= SR_BIT;
    if (reads_carry(in->op))
        *use |= SR_BIT;
    *def &= TRACKED;
    *use &= TRACKED;
}

// Whether `in` does nothing but write registers and flags.
static bool removable(const Msp_Instr *in)
{
    if (in->vol)
        return false;
    switch (in->op) {
    case MSP_PUSH:
    case MSP_POP:
    case MSP_CALL:
    case MSP_BR:
    case MSP_RET:
    case MSP_NOP:
        return false;
    default:
        break;
    }
    if (msp_form[in->op] == MSP_FORM_JUMP)
        return false;
    // A write to SP (alloca) moves the stack: never dead, though not tracked.
    const Msp_Operand *w = written(in);
    return !w || (is_reg(w) && w->reg >= 4);
}

//
// The blocks, as arrays
//

typedef struct {
    Msp_Block *b;
    Msp_Instr **in; // the instructions, in order
    int n;
    Set live_in;
} Blk;

typedef struct {
    Msp_Func *fn;
    unsigned result;
    Blk *blk;
    int n;
    StringMap labels; // label → block index
} Cfg;

static void cfg_build(Cfg *c, Msp_Func *fn, unsigned result)
{
    c->fn     = fn;
    c->result = result;
    c->n      = 0;
    for (const Msp_Block *b = fn->blocks; b; b = b->next)
        c->n++;
    c->blk = xalloc((c->n + 1) * sizeof(Blk), __func__, __FILE__, __LINE__);
    map_init(&c->labels);
    int i = 0;
    for (Msp_Block *b = fn->blocks; b; b = b->next, i++) {
        Blk *k = &c->blk[i];
        k->b   = b;
        k->n   = 0;
        for (const Msp_Instr *in = b->head; in; in = in->next)
            k->n++;
        k->in = xalloc((k->n + 1) * sizeof(Msp_Instr *), __func__, __FILE__, __LINE__);
        int j = 0;
        for (Msp_Instr *in = b->head; in; in = in->next)
            k->in[j++] = in;
        k->live_in = 0;
        if (b->label)
            map_insert(&c->labels, b->label, i, 0);
    }
}

static void cfg_free(Cfg *c)
{
    for (int i = 0; i < c->n; i++)
        xfree(c->blk[i].in);
    xfree(c->blk);
    map_destroy(&c->labels);
}

// Rebuild block `k`'s list from its array, the NULL entries removed (freed already).
static void relink(Blk *k)
{
    Msp_Block *b = k->b;
    b->head = b->tail = NULL;
    int j             = 0;
    for (int i = 0; i < k->n; i++) {
        Msp_Instr *in = k->in[i];
        if (!in)
            continue;
        in->next = NULL;
        if (b->tail)
            b->tail->next = in;
        else
            b->head = in;
        b->tail    = in;
        k->in[j++] = in;
    }
    k->n = j;
}

static void drop(Blk *k, int i)
{
    msp_free_instr(k->in[i]);
    k->in[i] = NULL;
}

// The live-in set of the block labelled `label`; for one not found, the epilogue's
// (before it exists, a jump there is the only jump out of the body).
static Set target_live(const Cfg *c, const char *label)
{
    intptr_t t;
    return label && map_get(&c->labels, label, &t) ? c->blk[t].live_in : ret_use(c->result);
}

// The set live after instruction i of block `bi`, given the set live after i+1.
static Set live_across(const Cfg *c, int bi, int i, Set after)
{
    const Msp_Instr *in = c->blk[bi].in[i];
    if (in->op == MSP_JMP)
        return target_live(c, in->opnd[0].sym);
    if (in->op == MSP_RET || in->op == MSP_BR)
        return 0;
    if (is_branch(in->op))
        return after | target_live(c, in->opnd[0].sym);
    return after;
}

// The set live at the end of block `bi`: what its fall-through successor needs.
static Set block_out(const Cfg *c, int bi)
{
    return bi + 1 < c->n ? c->blk[bi + 1].live_in : ret_use(c->result);
}

static void liveness(Cfg *c)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = c->n - 1; bi >= 0; bi--) {
            Set live = block_out(c, bi);
            for (int i = c->blk[bi].n - 1; i >= 0; i--) {
                Set def, use;
                live = live_across(c, bi, i, live);
                def_use(c->blk[bi].in[i], c->result, &def, &use);
                live = (live & ~def) | use;
            }
            if (live != c->blk[bi].live_in) {
                c->blk[bi].live_in = live;
                changed            = true;
            }
        }
    }
}

//
// The constant-generator forms
//

// Whether `o` is the immediate v; an offset into the incoming arguments is not known
// until the frame is.
static bool imm_is(const Msp_Operand *o, int64_t v, bool byte)
{
    return o->kind == MSP_OPND_IMM && !o->sym && !o->incoming && msp_imm_value(o->imm, byte) == v;
}

// `op #k, dst` as its one-operand alias.
static void to_alias(Msp_Instr *in, Msp_Op op)
{
    in->op      = op;
    in->opnd[0] = in->opnd[1];
    in->opnd[1] = (Msp_Operand){ 0 };
}

static void canonical(Cfg *c)
{
    for (int bi = 0; bi < c->n; bi++)
        for (int i = 0; i < c->blk[bi].n; i++) {
            Msp_Instr *in        = c->blk[bi].in[i];
            const Msp_Operand *a = &in->opnd[0];
            bool byte            = in->byte;
            switch (in->op) {
            case MSP_MOV:
                if (imm_is(a, 0, byte))
                    to_alias(in, MSP_CLR);
                break;
            case MSP_ADD:
                if (imm_is(a, 1, byte))
                    to_alias(in, MSP_INC);
                else if (imm_is(a, 2, byte))
                    to_alias(in, MSP_INCD);
                break;
            case MSP_SUB:
                if (imm_is(a, 1, byte))
                    to_alias(in, MSP_DEC);
                else if (imm_is(a, 2, byte))
                    to_alias(in, MSP_DECD);
                break;
            case MSP_CMP:
                if (imm_is(a, 0, byte))
                    to_alias(in, MSP_TST);
                break;
            case MSP_ADDC:
                if (imm_is(a, 0, byte))
                    to_alias(in, MSP_ADC);
                break;
            case MSP_XOR:
                if (imm_is(a, -1, byte))
                    to_alias(in, MSP_INV);
                break;
            default:
                break;
            }
        }
}

//
// Jumps
//

// The index of the first block after `bi` with an instruction, or c->n.
static int next_nonempty(const Cfg *c, int bi)
{
    int j = bi + 1;
    while (j < c->n && c->blk[j].n == 0)
        j++;
    return j;
}

// Whether `label` names one of blocks bi+1 .. upto.
static bool labels_between(const Cfg *c, int bi, int upto, const char *label)
{
    for (int j = bi + 1; j <= upto && j < c->n; j++)
        if (c->blk[j].b->label && label && strcmp(c->blk[j].b->label, label) == 0)
            return true;
    return false;
}

// Whether some instruction refers to `label`.
static bool referenced(const Cfg *c, const char *label)
{
    for (int bi = 0; bi < c->n; bi++)
        for (int i = 0; i < c->blk[bi].n; i++) {
            const Msp_Instr *in = c->blk[bi].in[i];
            if (in && in->opnd[0].kind == MSP_OPND_LABEL && strcmp(in->opnd[0].sym, label) == 0)
                return true;
        }
    return false;
}

static bool clean_jumps(Cfg *c)
{
    bool changed = false;
    bool dead    = false; // control does not reach the start of this block
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k = &c->blk[bi];
        if (k->b->label && referenced(c, k->b->label))
            dead = false;
        if (dead && bi > 0) {
            for (int i = 0; i < k->n; i++) {
                drop(k, i);
                changed = true;
            }
            relink(k);
            continue;
        }
        // Unreachable after an unconditional jump.
        for (int i = 0; i < k->n; i++) {
            if (!is_jump(k->in[i]->op))
                continue;
            for (int j = i + 1; j < k->n; j++) {
                drop(k, j);
                changed = true;
            }
            break;
        }
        relink(k);
        if (k->n == 0)
            continue;
        dead            = is_jump(k->in[k->n - 1]->op);
        int next        = next_nonempty(c, bi);
        Msp_Instr *last = k->in[k->n - 1];
        // A jump to the next instruction.
        if (last->op == MSP_JMP && labels_between(c, bi, next, last->opnd[0].sym)) {
            drop(k, k->n - 1);
            relink(k);
            changed = true;
            dead    = false;
            continue;
        }
        // A branch over a jump: the inverse branch (jn has none).
        if (k->n >= 2 && last->op == MSP_JMP && is_branch(k->in[k->n - 2]->op) &&
            msp_inverse(k->in[k->n - 2]->op) != MSP_NUM_OPS &&
            labels_between(c, bi, next, k->in[k->n - 2]->opnd[0].sym)) {
            Msp_Instr *br = k->in[k->n - 2];
            br->op        = msp_inverse(br->op);
            xfree(br->opnd[0].sym);
            br->opnd[0]       = last->opnd[0];
            last->opnd[0].sym = NULL;
            drop(k, k->n - 1);
            relink(k);
            changed = true;
            dead    = false;
        }
    }
    return changed;
}

//
// Forward: what the registers are known to hold
//

typedef struct {
    Msp_Operand loc; // a word of a slot (x(r1)) or of a global (&sym+x); not owned
    int reg;
} MemFact;

typedef struct {
    int eq[16];    // the register a register is known to equal (its class's root), or -1
    int32_t k[16]; // its known constant, or -1
    bool zx[16];   // its high byte known clear
    MemFact m[32];
    int nm;
} Facts;

static void facts_reset(Facts *f)
{
    for (int r = 0; r < 16; r++) {
        f->eq[r] = f->k[r] = -1;
        f->zx[r]           = false;
    }
    f->nm = 0;
}

static int root(const Facts *f, int r)
{
    return f->eq[r] >= 0 ? f->eq[r] : r;
}

// Register r gets a new value: the others that equalled it keep the old one.
static void kill(Facts *f, int r)
{
    int heir = -1;
    for (int x = 0; x < 16; x++)
        if (x != r && f->eq[x] == r) {
            if (heir < 0) {
                heir     = x;
                f->eq[x] = -1;
            } else {
                f->eq[x] = heir;
            }
        }
    f->eq[r] = -1;
    f->k[r]  = -1;
    f->zx[r] = false;
    for (int i = 0; i < f->nm;) {
        if (f->m[i].reg == r && heir >= 0) {
            f->m[i++].reg = heir;
        } else if (f->m[i].reg == r) {
            f->m[i] = f->m[--f->nm];
        } else {
            i++;
        }
    }
}

static void kill_set(Facts *f, Set s)
{
    for (int r = 0; r < 16; r++)
        if (s >> r & 1)
            kill(f, r);
}

// The function being rewritten addresses its slots from r4 (Msp_Func.fp): x(r4) is a
// slot there, and a pointer's target elsewhere.
static bool fp_frame;

// Whether `o` is a word of a slot or a global, whose facts can be kept.
static bool is_mem_word(const Msp_Operand *o)
{
    return (o->kind == MSP_OPND_INDEXED && (o->reg == MSP_SP || (fp_frame && o->reg == MSP_FP))) ||
           o->kind == MSP_OPND_ABS;
}

static int mem_find(const Facts *f, const Msp_Operand *o)
{
    for (int i = 0; i < f->nm; i++)
        if (same_opnd(&f->m[i].loc, o))
            return i;
    return -1;
}

// Forget what is known of the memory `o` (a byte or a word) writes: its word, or all of
// memory through a pointer.
static void mem_forget(Facts *f, const Msp_Operand *o)
{
    if (!is_mem_word(o)) {
        f->nm = 0;
        return;
    }
    for (int i = 0; i < f->nm;) {
        const Msp_Operand *m = &f->m[i].loc;
        bool same = m->kind == o->kind && m->reg == o->reg && m->incoming == o->incoming &&
                    ((!m->sym && !o->sym) || (m->sym && o->sym && strcmp(m->sym, o->sym) == 0)) &&
                    (m->imm & ~1) == (o->imm & ~1);
        if (same)
            f->m[i] = f->m[--f->nm];
        else
            i++;
    }
}

// Forget the words at x(r1): SP has moved (those at x(r4) stay).
static void forget_slots(Facts *f)
{
    for (int i = 0; i < f->nm;) {
        if (f->m[i].loc.kind == MSP_OPND_INDEXED && f->m[i].loc.reg == MSP_SP)
            f->m[i] = f->m[--f->nm];
        else
            i++;
    }
}

static void mem_note(Facts *f, const Msp_Operand *o, int reg)
{
    mem_forget(f, o);
    if (reg >= 4 && f->nm < 32) // SP and the like move: no copy of them is kept
        f->m[f->nm++] = (MemFact){ *o, reg };
}

// A move d = s of registers, known or not: whether it is redundant; the facts updated
// when not.
static bool move_fact(Facts *f, int d, int s)
{
    if (d == s)
        return true;
    if (s < 4) { // SP and the like move: no copy of them is kept
        kill(f, d);
        return false;
    }
    if (root(f, d) == root(f, s) || (f->k[d] >= 0 && f->k[d] == f->k[s]))
        return true;
    int32_t k = f->k[s];
    bool zx   = f->zx[s];
    int rs    = root(f, s);
    kill(f, d);
    f->eq[d] = rs == d ? -1 : rs;
    f->k[d]  = k;
    f->zx[d] = zx;
    return false;
}

static void set_src_reg(Msp_Instr *in, int reg)
{
    xfree(in->opnd[0].sym);
    in->opnd[0] = msp_reg(reg);
}

// The facts after `in`, which no rule rewrote.
static void step(Facts *f, const Msp_Instr *in, unsigned result)
{
    const Msp_Operand *wr = written(in);
    if (in->op == MSP_CALL) {
        f->nm = 0;
    } else if (in->op == MSP_PUSH || in->op == MSP_POP || (wr && is_reg(wr) && wr->reg == MSP_SP)) {
        forget_slots(f);
    } else if (wr && !is_reg(wr)) {
        mem_forget(f, wr);
    }
    Set def, use;
    def_use(in, result, &def, &use);
    kill_set(f, def);
    const Msp_Operand *w = written(in);
    if (in->byte && w && is_reg(w) && in->op != MSP_SXT)
        f->zx[w->reg] = true; // a byte operation into a register clears its high byte
    if (in->op == MSP_CLR && is_reg(&in->opnd[0]))
        f->k[in->opnd[0].reg] = 0, f->zx[in->opnd[0].reg] = true;
}

// Register `r` of an operand read as a value (or as a base): the oldest register known
// to hold the same.
static bool rename_read(const Facts *f, Msp_Operand *o, bool value)
{
    if ((o->kind == MSP_OPND_REG && value) || o->kind == MSP_OPND_INDEXED ||
        o->kind == MSP_OPND_IND) {
        int r = root(f, o->reg);
        if (o->reg >= 4 && r != o->reg) {
            o->reg = r;
            return true;
        }
    }
    return false;
}

// Every register `in` reads (and does not write) renamed to the oldest copy.
static bool rename_reads(const Facts *f, Msp_Instr *in)
{
    bool changed = false;
    switch (msp_form[in->op]) {
    case MSP_FORM_DOUBLE:
        changed |= rename_read(f, &in->opnd[0], true);
        changed |= rename_read(f, &in->opnd[1], reads_only(in->op));
        break;
    case MSP_FORM_SINGLE:
        changed |= rename_read(f, &in->opnd[0], in->op == MSP_PUSH || in->op == MSP_CALL);
        break;
    case MSP_FORM_DST:
    case MSP_FORM_TWICE:
        changed |= rename_read(f, &in->opnd[0], in->op == MSP_TST);
        break;
    case MSP_FORM_SRC:
        changed |= rename_read(f, &in->opnd[0], true);
        break;
    default:
        break;
    }
    return changed;
}

static bool forward(Cfg *c)
{
    bool changed = false;
    Facts f;
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k = &c->blk[bi];
        facts_reset(&f);
        for (int i = 0; i < k->n; i++) {
            Msp_Instr *in        = k->in[i];
            const Msp_Operand *a = &in->opnd[0], *b = &in->opnd[1];
            if (in->vol) {
                step(&f, in, c->result);
                continue;
            }
            changed |= rename_reads(&f, in);
            if (in->op == MSP_CLR && is_reg(a) && f.k[a->reg] == 0) {
                drop(k, i);
                changed = true;
                continue;
            }
            if (in->op != MSP_MOV) {
                step(&f, in, c->result);
                continue;
            }
            if (in->byte) {
                // mov.b r, r of a byte already extended.
                if (is_reg(a) && is_reg(b) && a->reg == b->reg && f.zx[a->reg]) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
                step(&f, in, c->result);
                continue;
            }
            if (is_reg(b)) {
                int d = b->reg;
                if (is_reg(a)) {
                    if (move_fact(&f, d, a->reg)) {
                        drop(k, i);
                        changed = true;
                    }
                    continue;
                }
                if (a->kind == MSP_OPND_IMM && !a->sym && !a->incoming) {
                    int32_t v = (int32_t)(a->imm & 0xffff);
                    if (f.k[d] == v) {
                        drop(k, i);
                        changed = true;
                        continue;
                    }
                    kill(&f, d);
                    f.k[d]  = v;
                    f.zx[d] = v < 0x100;
                    continue;
                }
                if (is_mem_word(a)) {
                    int m = mem_find(&f, a);
                    if (m >= 0) { // a reload: a move, or nothing
                        int r = f.m[m].reg;
                        if (root(&f, r) == root(&f, d)) {
                            drop(k, i);
                        } else {
                            set_src_reg(in, r);
                            move_fact(&f, d, r);
                        }
                        changed = true;
                        continue;
                    }
                    kill(&f, d);
                    mem_note(&f, a, d);
                    continue;
                }
                step(&f, in, c->result);
                continue;
            }
            if (is_mem_word(b) && is_reg(a)) {
                int m = mem_find(&f, b);
                if (m >= 0 && root(&f, f.m[m].reg) == root(&f, a->reg)) { // already there
                    drop(k, i);
                    changed = true;
                    continue;
                }
                mem_note(&f, b, a->reg);
                continue;
            }
            step(&f, in, c->result);
        }
        relink(k);
    }
    return changed;
}

//
// Backward: liveness
//

// Whether `op` sets N and Z from its result, as `tst` of it would.
static bool sets_nz(Msp_Op op)
{
    switch (op) {
    case MSP_ADD:
    case MSP_ADDC:
    case MSP_SUB:
    case MSP_SUBC:
    case MSP_AND:
    case MSP_XOR:
    case MSP_INC:
    case MSP_INCD:
    case MSP_DEC:
    case MSP_DECD:
    case MSP_INV:
    case MSP_ADC:
    case MSP_RLA:
    case MSP_RLC:
    case MSP_RRA:
    case MSP_RRC:
    case MSP_SXT:
        return true;
    default:
        return false;
    }
}

// Whether `op` also clears V, as `tst` does (and and sxt).
static bool clears_v(Msp_Op op)
{
    return op == MSP_AND || op == MSP_SXT;
}

// Whether the operation `in` is by a neutral constant, which changes nothing but the
// flags (none for bis and bic).
static bool neutral(const Msp_Instr *in)
{
    const Msp_Operand *a = &in->opnd[0];
    switch (in->op) {
    case MSP_BIS:
    case MSP_XOR:
    case MSP_BIC:
    case MSP_SUB:
    case MSP_ADD:
        return imm_is(a, 0, in->byte);
    case MSP_AND:
        return imm_is(a, -1, in->byte);
    default:
        return false;
    }
}

// Whether `rmw`, an operation on register t, can operate on memory instead: it writes t,
// reads it as its destination only, and its source does not involve t.
static bool rmw_on(const Msp_Instr *rmw, int t)
{
    const Msp_Operand *w = written(rmw);
    if (!w || !is_reg(w) || w->reg != t || rmw->op == MSP_MOV || rmw->op == MSP_POP ||
        rmw->op == MSP_CLR)
        return false;
    switch (msp_form[rmw->op]) {
    case MSP_FORM_DOUBLE:
        return !(opnd_regs(&rmw->opnd[0]) & R(t)) && rmw->opnd[0].kind != MSP_OPND_POSTINC;
    case MSP_FORM_SINGLE:
    case MSP_FORM_DST:
    case MSP_FORM_TWICE:
        return true;
    default:
        return false;
    }
}

// Whether `in` writes memory, or moves SP (a slot's x(r1) then names another word).
static bool writes_memory(const Msp_Instr *in)
{
    if (in->op == MSP_CALL || in->op == MSP_PUSH || in->op == MSP_POP)
        return true;
    const Msp_Operand *w = written(in);
    return w && (!is_reg(w) || w->reg == MSP_SP);
}

// The `mov s, t` in block k before instruction i whose s the read of t at i can take
// instead: t neither read nor written in between, nor s's registers written, nor memory
// when s is in memory, and no jump.  Its index, or -1.
static int sink_source(const Blk *k, int i, int t, unsigned result)
{
    for (int j = i - 1; j >= 0; j--) {
        const Msp_Instr *p = k->in[j];
        if (!p)
            continue;
        if (p->vol || is_branch(p->op))
            return -1;
        if (p->op == MSP_MOV && is_reg(&p->opnd[1]) && p->opnd[1].reg == t) {
            const Msp_Operand *s = &p->opnd[0];
            // s may involve t only right before i: t holds nothing else in between.
            if (s->kind == MSP_OPND_POSTINC || ((opnd_regs(s) & R(t)) && j != i - 1))
                return -1;
            Set srcregs = opnd_regs(s);
            bool mem    = s->kind != MSP_OPND_REG && s->kind != MSP_OPND_IMM;
            for (int q = j + 1; q < i; q++) {
                const Msp_Instr *m = k->in[q];
                if (!m)
                    continue;
                Set def, use;
                def_use(m, result, &def, &use);
                if ((def & srcregs) || (mem && writes_memory(m)))
                    return -1;
            }
            return j;
        }
        Set def, use;
        def_use(p, result, &def, &use);
        if ((def | use) & R(t))
            return -1;
    }
    return -1;
}

// The constant `in` adds to its register, as an immediate and a sign; false for another
// instruction.
static bool adds_constant(const Msp_Instr *in, Msp_Operand *k, int *sign)
{
    *k    = (Msp_Operand){ .kind = MSP_OPND_IMM };
    *sign = 1;
    if (in->byte)
        return false;
    switch (in->op) {
    case MSP_ADD:
    case MSP_SUB:
        if (in->opnd[0].kind != MSP_OPND_IMM || in->opnd[0].incoming || !is_reg(&in->opnd[1]))
            return false;
        *k    = in->opnd[0];
        *sign = in->op == MSP_SUB ? -1 : 1;
        return !k->sym || *sign > 0;
    case MSP_INC:
    case MSP_INCD:
    case MSP_DEC:
    case MSP_DECD:
        if (!is_reg(&in->opnd[0]))
            return false;
        k->imm = in->op == MSP_INC || in->op == MSP_DEC ? 1 : 2;
        *sign  = in->op == MSP_DEC || in->op == MSP_DECD ? -1 : 1;
        return true;
    default:
        return false;
    }
}

// Whether `in` reads register b only as the base of its memory operands (@b or x(b)),
// besides perhaps writing it by a mov; no auto-increment, no call or push.
static bool base_only(const Msp_Instr *in, int b)
{
    if (in->op == MSP_CALL || in->op == MSP_PUSH || in->op == MSP_BR)
        return false;
    bool based = false;
    int n      = msp_form[in->op] == MSP_FORM_DOUBLE                                      ? 2
                 : msp_form[in->op] == MSP_FORM_JUMP || msp_form[in->op] == MSP_FORM_NONE ? 0
                                                                                          : 1;
    for (int o = 0; o < n; o++) {
        const Msp_Operand *x = &in->opnd[o];
        if (!(opnd_regs(x) & R(b)))
            continue;
        if (x->kind == MSP_OPND_IND || (x->kind == MSP_OPND_INDEXED && !x->incoming)) {
            based = true;
            continue;
        }
        if (is_reg(x) && o == 1 && in->op == MSP_MOV)
            continue; // overwritten, after the address is used
        return false;
    }
    return based;
}

// Fold an add of constant `k` (times `sign`) into every operand of `in` based on
// register b: @b becomes k(b), x(b) x+k(b); false when an operand cannot take a symbol.
static bool fold_offset(Msp_Instr *in, int b, const Msp_Operand *k, int sign)
{
    for (int pass = 0; pass < 2; pass++)
        for (int o = 0; o < MSP_MAX_OPERANDS; o++) {
            Msp_Operand *x = &in->opnd[o];
            if ((x->kind != MSP_OPND_IND && x->kind != MSP_OPND_INDEXED) || x->reg != b)
                continue;
            if (pass == 0) {
                if (k->sym && x->sym)
                    return false;
                continue;
            }
            if (x->kind == MSP_OPND_IND) {
                x->kind = MSP_OPND_INDEXED;
                x->imm  = 0;
            }
            x->imm = msp_imm_value(x->imm + sign * k->imm, false); // 16 bits, wrapped
            if (k->sym)
                x->sym = xstrdup(k->sym);
        }
    return true;
}

static bool backward(Cfg *c)
{
    liveness(c);
    bool changed = false;
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k = &c->blk[bi];
        // The set live after each instruction.
        Set *after = xalloc((k->n + 1) * sizeof(Set), __func__, __FILE__, __LINE__);
        Set live   = block_out(c, bi);
        for (int i = k->n - 1; i >= 0; i--) {
            live     = live_across(c, bi, i, live);
            after[i] = live;
            Set def, use;
            def_use(k->in[i], c->result, &def, &use);
            live = (live & ~def) | use;
        }
        for (int i = k->n - 1; i >= 0; i--) {
            Msp_Instr *in = k->in[i];
            if (!in)
                continue;
            Set def, use;
            def_use(in, c->result, &def, &use);
            if (removable(in) && def && !(def & after[i])) {
                drop(k, i);
                changed = true;
                continue;
            }
            if (in->vol)
                continue;
            Msp_Instr *prev = i > 0 ? k->in[i - 1] : NULL;
            Msp_Instr *next = i + 1 < k->n ? k->in[i + 1] : NULL;
            // An operation by a neutral constant: none at all, with the flags dead; `add
            // #0` (`sub #0`) leaves C clear (set), so an `addc` (`subc`) after it is an
            // `add` (`sub`).
            if (neutral(in)) {
                if (!(after[i] & SR_BIT) || in->op == MSP_BIS || in->op == MSP_BIC) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
                if (next && !next->vol &&
                    ((in->op == MSP_ADD && next->op == MSP_ADDC) ||
                     (in->op == MSP_SUB && next->op == MSP_SUBC))) {
                    next->op = in->op;
                    drop(k, i);
                    changed = true;
                    continue;
                }
            }
            // A tst of what the instruction before set the flags from, read only by the
            // jumps right after.
            if (in->op == MSP_TST && prev && !prev->vol && sets_nz(prev->op) &&
                prev->byte == in->byte && written(prev) && same_opnd(written(prev), &in->opnd[0])) {
                int j   = i + 1;
                bool ok = j < k->n;
                for (; ok && j < k->n && k->in[j] && is_branch(k->in[j]->op); j++) {
                    Msp_Op op = k->in[j]->op;
                    ok        = op == MSP_JEQ || op == MSP_JNE || op == MSP_JN ||
                                ((op == MSP_JL || op == MSP_JGE) && clears_v(prev->op));
                }
                if (ok && j > i + 1 && (j == k->n || k->in[j]) && !(after[j - 1] & SR_BIT)) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
            }
            // mov @p, x; mov 2(p), y; ... into registers, p dead after: mov @p+, x; mov
            // @p+, y (into a register only: clang's assembler).
            if (in->op == MSP_MOV && is_reg(&in->opnd[1]) &&
                (in->opnd[0].kind == MSP_OPND_IND || msp_zero_indexed(&in->opnd[0]))) {
                int p = in->opnd[0].reg, stride = in->byte ? 1 : 2, n = 1;
                while (i + n < k->n) {
                    const Msp_Instr *m = k->in[i + n];
                    if (!m || m->vol || m->op != MSP_MOV || m->byte != in->byte ||
                        !is_reg(&m->opnd[1]) || m->opnd[1].reg == p ||
                        m->opnd[0].kind != MSP_OPND_INDEXED || m->opnd[0].reg != p ||
                        m->opnd[0].sym || m->opnd[0].incoming || m->opnd[0].imm != stride * n)
                        break;
                    n++;
                }
                if (p >= 4 && in->opnd[1].reg != p && n >= 2 && !(after[i + n - 1] & R(p))) {
                    for (int j = 0; j < n; j++)
                        k->in[i + j]->opnd[0] = msp_postinc(p);
                    changed = true;
                    continue;
                }
            }
            // mov @p, x; add #size, p with the flags dead: mov @p+, x.
            if (in->op == MSP_MOV && !in->vol && is_reg(&in->opnd[1]) && i + 1 < k->n &&
                k->in[i + 1] &&
                (in->opnd[0].kind == MSP_OPND_IND || msp_zero_indexed(&in->opnd[0]))) {
                int p                = in->opnd[0].reg, sgn;
                const Msp_Instr *inc = k->in[i + 1];
                Msp_Operand by;
                if (p >= 4 && in->opnd[1].reg != p && !inc->vol && adds_constant(inc, &by, &sgn) &&
                    sgn > 0 && !by.sym && by.imm == (in->byte ? 1 : 2) && written(inc)->reg == p &&
                    !(after[i + 1] & SR_BIT)) {
                    in->opnd[0] = msp_postinc(p);
                    drop(k, i + 1);
                    changed = true;
                    continue;
                }
            }
            // add #k, b; then b a base and dead: the offset in the address.
            Msp_Operand kk;
            int sign;
            if (next && !next->vol && adds_constant(in, &kk, &sign) && !(after[i] & SR_BIT)) {
                int b                 = written(in)->reg;
                const Msp_Operand *nw = written(next);
                bool redefined        = nw && is_reg(nw) && nw->reg == b && next->op == MSP_MOV;
                if (b >= 4 && base_only(next, b) && (redefined || !(after[i + 1] & R(b))) &&
                    fold_offset(next, b, &kk, sign)) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
            }
            // mov s, t; ...; op t, x with t dead after: op s, x.
            if (msp_form[in->op] == MSP_FORM_DOUBLE && is_reg(&in->opnd[0])) {
                int t = in->opnd[0].reg;
                int j = t >= 4 && !(opnd_regs(&in->opnd[1]) & R(t)) && !(after[i] & R(t))
                            ? sink_source(k, i, t, c->result)
                            : -1;
                const Msp_Operand *s = j >= 0 ? &k->in[j]->opnd[0] : NULL;
                if (s && !(k->in[j]->byte && !in->byte) &&
                    (s->kind != MSP_OPND_POSTINC || is_reg(&in->opnd[1]))) {
                    in->opnd[0] = msp_copy(s);
                    drop(k, j);
                    changed = true;
                    if (j == i - 1)
                        i--;
                    continue;
                }
            }
            if (!prev || prev->vol || prev->op != MSP_MOV || !is_reg(&prev->opnd[1]))
                continue;
            int t                  = prev->opnd[1].reg;
            const Msp_Operand *src = &prev->opnd[0];
            if (opnd_regs(src) & R(t))
                continue;
            // mov m, t; op y, t; mov t, m with t dead after: op y, m.
            if (next && !next->vol && next->op == MSP_MOV && is_reg(&next->opnd[0]) &&
                next->opnd[0].reg == t && same_opnd(&next->opnd[1], src) &&
                prev->byte == in->byte && next->byte == in->byte && is_mem_word(src) &&
                rmw_on(in, t) && !(after[i + 1] & R(t))) {
                if (msp_form[in->op] == MSP_FORM_DOUBLE)
                    in->opnd[1] = msp_copy(src);
                else
                    in->opnd[0] = msp_copy(src);
                drop(k, i + 1);
                drop(k, i - 1);
                changed = true;
                i--; // the mov before is gone
                continue;
            }
        }
        xfree(after);
        relink(k);
    }
    return changed;
}

//
// Dead stores to the frame: the liveness of the first 64 bytes of slots, one bit a byte.
// A slot is read by an x(r1) operand, and by whatever could hold its address: on a path
// past a read of r1 as a value (`mov r1, r12`), an access through any other register
// may read every slot.  Otherwise a call reads only the outgoing arguments, the first
// `out` bytes of the frame, below the slots; nothing is live after the function returns.  Offsets
// into the incoming arguments are not slots.  A body that moves SP itself (a push, a pop) is left
// alone, since its offsets do not name one slot throughout.
//

typedef uint64_t Bytes;

#define ALL_BYTES (~(Bytes)0)

// The bytes of slot memory operand `o` covers, `size` wide, or 0 for no slot operand.
static Bytes slot_bytes(const Msp_Operand *o, int size)
{
    if (o->incoming || o->reg != MSP_SP || o->sym ||
        (o->kind != MSP_OPND_INDEXED && o->kind != MSP_OPND_IND))
        return 0;
    int64_t off = o->kind == MSP_OPND_IND ? 0 : o->imm;
    Bytes b     = 0;
    for (int64_t i = off; i < off + size; i++)
        if (i >= 0 && i < 64)
            b |= (Bytes)1 << i;
    return b;
}

// Whether `in` overwrites the operand it writes whole, reading nothing of it.
static bool overwrites(const Msp_Instr *in)
{
    return in->op == MSP_MOV || in->op == MSP_CLR;
}

// The slot bytes plain store `in` overwrites, when all of them are tracked; else 0.
static Bytes stored_slot(const Msp_Instr *in)
{
    const Msp_Operand *w = written(in);
    if (!w || !overwrites(in) || in->vol || in->opnd[0].kind == MSP_OPND_POSTINC)
        return 0;
    int size = in->byte ? 1 : 2;
    if (w->kind == MSP_OPND_INDEXED && (w->imm < 0 || w->imm + size > 64))
        return 0;
    return slot_bytes(w, size);
}

// The bytes of the outgoing argument area, the first `out` of the frame.
static Bytes outgoing(int out)
{
    return out >= 64 ? ALL_BYTES : ((Bytes)1 << out) - 1;
}

// The slot bytes live before `in`, given those live after it, in a frame whose
// outgoing argument area is `out` bytes.
static Bytes slots_before(const Msp_Instr *in, Bytes after, bool escaped, int out)
{
    if (in->op == MSP_CALL || in->op == MSP_BR)
        return escaped ? ALL_BYTES : after | outgoing(out);
    if (in->op == MSP_RET)
        return 0;
    int size             = in->byte ? 1 : 2;
    const Msp_Operand *w = written(in);
    Bytes read           = 0;
    for (int i = 0; i < MSP_MAX_OPERANDS; i++) {
        const Msp_Operand *o = &in->opnd[i];
        if (o == w && overwrites(in)) {
            after &= ~slot_bytes(o, size);
            continue;
        }
        bool through = (o->kind == MSP_OPND_INDEXED || o->kind == MSP_OPND_IND ||
                        o->kind == MSP_OPND_POSTINC) &&
                       o->reg != MSP_SP;
        if (escaped && through)
            return ALL_BYTES;
        read |= slot_bytes(o, size);
    }
    return after | read;
}

static Bytes slot_target(const Cfg *c, const Bytes *in, const char *label)
{
    intptr_t t;
    return label && map_get(&c->labels, label, &t) ? in[t] : 0;
}

// The slot bytes live after instruction i of block `bi`, given those after i+1.
static Bytes slots_across(const Cfg *c, const Bytes *in, int bi, int i, Bytes after)
{
    const Msp_Instr *x = c->blk[bi].in[i];
    if (x->op == MSP_JMP)
        return slot_target(c, in, x->opnd[0].sym);
    if (is_branch(x->op))
        return after | slot_target(c, in, x->opnd[0].sym);
    return after;
}

// Whether `in` reads r1 as a value: the frame's address escapes into a register.
static bool reads_sp(const Msp_Instr *in)
{
    const Msp_Operand *w = written(in);
    for (int j = 0; j < MSP_MAX_OPERANDS; j++) {
        const Msp_Operand *o = &in->opnd[j];
        if (o->kind == MSP_OPND_REG && o->reg == MSP_SP && o != w)
            return true;
    }
    return false;
}

// Whether the body moves SP: a push, a pop, or a write to r1.
static bool sp_moves(const Cfg *c)
{
    for (int bi = 0; bi < c->n; bi++)
        for (int i = 0; i < c->blk[bi].n; i++) {
            const Msp_Instr *in  = c->blk[bi].in[i];
            const Msp_Operand *w = written(in);
            if (in->op == MSP_PUSH || in->op == MSP_POP ||
                (w && w->kind == MSP_OPND_REG && w->reg == MSP_SP))
                return true;
        }
    return false;
}

static void escape_to(const Cfg *c, bool *esc, const char *label, bool *changed)
{
    intptr_t t;
    if (label && map_get(&c->labels, label, &t) && !esc[t]) {
        esc[t]   = true;
        *changed = true;
    }
}

// For each block, whether a path from the entry to it reads r1 as a value.
static bool *escapes(const Cfg *c)
{
    bool *esc    = xalloc((c->n + 1) * sizeof(bool), __func__, __FILE__, __LINE__);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = 0; bi < c->n; bi++) {
            bool e = esc[bi], falls = true;
            for (int i = 0; i < c->blk[bi].n && falls; i++) {
                const Msp_Instr *in = c->blk[bi].in[i];
                e |= reads_sp(in);
                if (e && (in->op == MSP_JMP || is_branch(in->op)))
                    escape_to(c, esc, in->opnd[0].sym, &changed);
                falls = !is_jump(in->op);
            }
            if (e && falls && bi + 1 < c->n && !esc[bi + 1]) {
                esc[bi + 1] = true;
                changed     = true;
            }
        }
    }
    return esc;
}

// Whether instruction i of block `bi` may follow an escape of the frame's address.
static bool escaped_at(const Cfg *c, const bool *esc, int bi, int i)
{
    if (esc[bi])
        return true;
    for (int j = 0; j <= i; j++)
        if (reads_sp(c->blk[bi].in[j]))
            return true;
    return false;
}

static bool dead_slot_stores(Cfg *c, int out)
{
    if (sp_moves(c))
        return false;
    bool *esc    = escapes(c);
    Bytes *in    = xalloc((c->n + 1) * sizeof(Bytes), __func__, __FILE__, __LINE__);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = c->n - 1; bi >= 0; bi--) {
            Bytes live = bi + 1 < c->n ? in[bi + 1] : 0;
            for (int i = c->blk[bi].n - 1; i >= 0; i--) {
                live = slots_across(c, in, bi, i, live);
                live = slots_before(c->blk[bi].in[i], live, escaped_at(c, esc, bi, i), out);
            }
            if (live != in[bi]) {
                in[bi]  = live;
                changed = true;
            }
        }
    }
    // A plain store to slot bytes none of which is read before it is overwritten goes.
    bool dropped = false;
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k     = &c->blk[bi];
        Bytes live = bi + 1 < c->n ? in[bi + 1] : 0;
        for (int i = k->n - 1; i >= 0; i--) {
            const Msp_Instr *x = k->in[i];
            live               = slots_across(c, in, bi, i, live);
            Bytes w            = stored_slot(x);
            if (w && !(w & live)) {
                drop(k, i);
                dropped = true;
                continue;
            }
            live = slots_before(x, live, escaped_at(c, esc, bi, i), out);
        }
        relink(k);
    }
    xfree(esc);
    xfree(in);
    return dropped;
}

void msp_peephole_pass(Msp_Func *fn, unsigned result, int out)
{
    fp_frame     = fn->fp;
    bool changed = true;
    while (changed) {
        Cfg c;
        cfg_build(&c, fn, result);
        canonical(&c);
        changed = clean_jumps(&c);
        changed |= forward(&c);
        changed |= backward(&c);
        cfg_free(&c);
        cfg_build(&c, fn, result);
        changed |= dead_slot_stores(&c, out);
        cfg_free(&c);
    }
}

bool msp_frame_referenced(const Msp_Func *fn, bool fp)
{
    for (const Msp_Block *b = fn->blocks; b; b = b->next)
        for (const Msp_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < MSP_MAX_OPERANDS; i++) {
                const Msp_Operand *o = &in->opnd[i];
                if (o->kind != MSP_OPND_NONE && o->kind != MSP_OPND_IMM &&
                    o->kind != MSP_OPND_ABS && o->kind != MSP_OPND_LABEL &&
                    (o->reg == MSP_SP || (fp && o->reg == MSP_FP)) && !o->incoming)
                    return true;
            }
    return false;
}

// Whether block j holds a lone ret.
static bool lone_ret(const Cfg *c, int j)
{
    return j < c->n && c->blk[j].n == 1 && c->blk[j].in[0]->op == MSP_RET;
}

void msp_peephole_frame(Msp_Func *fn, unsigned result)
{
    Cfg c;
    cfg_build(&c, fn, result);
    clean_jumps(&c);
    for (int bi = 0; bi < c.n; bi++) {
        Blk *k = &c.blk[bi];
        for (int i = 0; i < k->n; i++) {
            Msp_Instr *in = k->in[i];
            intptr_t t;
            // A jump to a lone ret: ret.
            if (in->op == MSP_JMP && map_get(&c.labels, in->opnd[0].sym, &t) &&
                lone_ret(&c, next_nonempty(&c, (int)t - 1))) {
                xfree(in->opnd[0].sym);
                in->opnd[0] = (Msp_Operand){ 0 };
                in->op      = MSP_RET;
            }
        }
        // A call, then ret (or the epilogue's lone ret): a tail jump.
        for (int i = 0; i < k->n; i++) {
            Msp_Instr *in = k->in[i];
            if (!in || in->op != MSP_CALL)
                continue;
            bool then_ret =
                i + 1 < k->n ? k->in[i + 1]->op == MSP_RET : lone_ret(&c, next_nonempty(&c, bi));
            if (!then_ret)
                continue;
            in->op = MSP_BR;
            if (i + 1 < k->n)
                drop(k, i + 1);
        }
        relink(k);
    }
    cfg_free(&c);
    // The tail of a block after its new jump or ret.
    cfg_build(&c, fn, result);
    clean_jumps(&c);
    cfg_free(&c);
}
