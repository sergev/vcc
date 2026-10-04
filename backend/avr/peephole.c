//
// The peephole pass over the AVR IR.  The body first, before the prologue and epilogue
// exist (so that the registers it no longer uses are not saved), to a fixed point:
//
//   - code after an unconditional jump, up to the next label, goes;
//   - a jump to the next instruction goes, and a branch over a jump becomes the
//     inverse branch;
//   - forward through each block, what is known of the registers: which hold the same
//     byte (a copy), which a constant, and which a byte of a slot or a global just
//     loaded or stored.  A move or constant load of what a register already holds goes;
//     a reload becomes a move, and a store of what a slot already holds goes;
//   - backward, the liveness of the registers and SREG: an instruction whose results
//     are all dead goes (a volatile access stays); `ldi t, k; cp r, t` becomes
//     `cpi r, k`, and `subi`/`sbci` of a small constant on r24-r30 `adiw`/`sbiw`, where
//     the temporary or the flags are dead.
//
// After the frame: the jumps again, a jump to a lone `ret` becomes `ret`, and a call
// followed by a bare `ret` a tail jump.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

typedef uint64_t Set; // r0-r31, and SREG as bit 32

#define SREG  (1ull << 32)
#define R(r)  (1ull << (r))
#define ALL   ((1ull << 33) - 1)
#define PAIR(r) (R(r) | R((r) + 1))

static bool is_branch(AVR_Op op)
{
    return avr_inverse(op) != AVR_NUM_OPS;
}

static bool is_jump(AVR_Op op)
{
    return op == AVR_RJMP || op == AVR_JMP || op == AVR_RET || op == AVR_IJMP;
}

static bool sym_is(const AVR_Operand *o, const char *name)
{
    return o->kind == AVR_OPND_SYM && o->sym && strcmp(o->sym, name) == 0;
}

static bool is_self_call(const AVR_Instr *in)
{
    return in->op == AVR_RCALL && in->opnd[0].sym && strcmp(in->opnd[0].sym, ".") == 0;
}

// What the return reads: the result, r1 and the call-saved registers.
static Set ret_use(uint32_t result)
{
    return result | R(AVR_ZERO) | ((R(18) - 1) & ~(R(2) - 1)) | PAIR(AVR_Y);
}

// What `in` writes and reads, in a function whose result is in `result`.
static void def_use(const AVR_Instr *in, uint32_t result, Set *def, Set *use)
{
    const AVR_Operand *a = &in->opnd[0], *b = &in->opnd[1];
    int d = a->reg, s = b->reg;
    *def = *use = 0;
    switch (in->op) {
    case AVR_ADD:
    case AVR_SUB:
    case AVR_AND:
    case AVR_OR:
    case AVR_EOR:
        *def = R(d) | SREG;
        if (d != s || (in->op != AVR_EOR && in->op != AVR_SUB))
            *use = R(d) | R(s);
        break;
    case AVR_ADC:
    case AVR_SBC:
        *def = R(d) | SREG;
        *use = SREG | (d == s && in->op == AVR_SBC ? 0 : R(d) | R(s));
        break;
    case AVR_ADIW:
    case AVR_SBIW:
        *def = PAIR(d) | SREG;
        *use = PAIR(d);
        break;
    case AVR_SUBI:
    case AVR_ANDI:
    case AVR_ORI:
    case AVR_COM:
    case AVR_NEG:
    case AVR_INC:
    case AVR_DEC:
    case AVR_LSL:
    case AVR_LSR:
    case AVR_ASR:
        *def = R(d) | SREG;
        *use = R(d);
        break;
    case AVR_SBCI:
    case AVR_ROL:
    case AVR_ROR:
        *def = R(d) | SREG;
        *use = R(d) | SREG;
        break;
    case AVR_SWAP:
        *def = *use = R(d);
        break;
    case AVR_MUL:
    case AVR_MULS:
    case AVR_MULSU:
        *def = R(0) | R(1) | SREG;
        *use = R(d) | R(s);
        break;
    case AVR_CP:
        *def = SREG;
        *use = R(d) | R(s);
        break;
    case AVR_CPC:
        *def = SREG;
        *use = R(d) | R(s) | SREG;
        break;
    case AVR_CPI:
    case AVR_TST:
        *def = SREG;
        *use = R(d);
        break;
    case AVR_CPSE:
        *use = R(d) | R(s);
        break;
    case AVR_CLR:
        *def = R(d) | SREG;
        break;
    case AVR_SER:
    case AVR_LDI:
        *def = R(d);
        break;
    case AVR_MOV:
        *def = R(d);
        *use = R(s);
        break;
    case AVR_MOVW:
        *def = PAIR(d);
        *use = PAIR(s);
        break;
    case AVR_LD:
        *def = R(d) | (b->mode != AVR_PTR_PLAIN ? PAIR(b->reg) : 0);
        *use = PAIR(b->reg);
        break;
    case AVR_LDD:
        *def = R(d);
        *use = PAIR(b->reg);
        break;
    case AVR_LDS:
        *def = R(d);
        break;
    case AVR_ST:
        *def = a->mode != AVR_PTR_PLAIN ? PAIR(a->reg) : 0;
        *use = PAIR(a->reg) | R(s);
        break;
    case AVR_STD:
        *use = PAIR(a->reg) | R(s);
        break;
    case AVR_STS:
    case AVR_PUSH:
        *use = in->op == AVR_PUSH ? R(d) : R(s);
        break;
    case AVR_POP:
        *def = R(d);
        break;
    case AVR_IN:
        *def = R(d);
        *use = sym_is(b, "__SREG__") ? SREG : 0;
        break;
    case AVR_OUT:
        *def = sym_is(a, "__SREG__") ? SREG : 0;
        *use = R(s);
        break;
    case AVR_BST:
        *def = SREG;
        *use = R(d);
        break;
    case AVR_BLD:
        *def = R(d);
        *use = R(d) | SREG;
        break;
    case AVR_SBRC:
    case AVR_SBRS:
        *use = R(d);
        break;
    case AVR_CALL:
    case AVR_RCALL:
    case AVR_ICALL:
        if (is_self_call(in))
            break;
        // The arguments (r8-r25) and r1 in; the call-clobbered registers out.
        *use = ((R(26) - 1) & ~(R(8) - 1)) | R(1) | (in->op == AVR_ICALL ? PAIR(AVR_Z) : 0);
        *def = R(0) | ((R(28) - 1) & ~(R(18) - 1)) | PAIR(AVR_Z) | SREG;
        break;
    case AVR_RET: // the result, r1 and the call-saved registers
        *use = ret_use(result);
        break;
    case AVR_JMP: // a tail call: the arguments too
        *use = ret_use(result) | ((R(26) - 1) & ~(R(8) - 1));
        break;
    default:
        if (is_branch(in->op))
            *use = SREG;
        break;
    }
}

// Whether `in` does nothing but write its results.
static bool removable(const AVR_Instr *in)
{
    if (in->vol)
        return false;
    switch (in->op) {
    case AVR_ADD:
    case AVR_ADC:
    case AVR_ADIW:
    case AVR_SUB:
    case AVR_SUBI:
    case AVR_SBC:
    case AVR_SBCI:
    case AVR_SBIW:
    case AVR_AND:
    case AVR_ANDI:
    case AVR_OR:
    case AVR_ORI:
    case AVR_EOR:
    case AVR_COM:
    case AVR_NEG:
    case AVR_INC:
    case AVR_DEC:
    case AVR_MUL:
    case AVR_MULS:
    case AVR_MULSU:
    case AVR_CP:
    case AVR_CPC:
    case AVR_CPI:
    case AVR_TST:
    case AVR_CLR:
    case AVR_SER:
    case AVR_MOV:
    case AVR_MOVW:
    case AVR_LDI:
    case AVR_LD:
    case AVR_LDD:
    case AVR_LDS:
    case AVR_LSL:
    case AVR_LSR:
    case AVR_ROL:
    case AVR_ROR:
    case AVR_ASR:
    case AVR_SWAP:
    case AVR_BLD:
        return true;
    default:
        return false;
    }
}

//
// The blocks, as arrays
//

typedef struct {
    AVR_Block *b;
    AVR_Instr **in; // the instructions, in order
    int n;
    Set live_in;
} Blk;

typedef struct {
    AVR_Func *fn;
    Blk *blk;
    int n;
    StringMap labels; // label → block index
} Cfg;

static void cfg_build(Cfg *c, AVR_Func *fn)
{
    c->fn = fn;
    c->n  = 0;
    for (AVR_Block *b = fn->blocks; b; b = b->next)
        c->n++;
    c->blk = xalloc((c->n + 1) * sizeof(Blk), __func__, __FILE__, __LINE__);
    map_init(&c->labels);
    int i = 0;
    for (AVR_Block *b = fn->blocks; b; b = b->next, i++) {
        Blk *k = &c->blk[i];
        k->b   = b;
        k->n   = 0;
        for (AVR_Instr *in = b->head; in; in = in->next)
            k->n++;
        k->in = xalloc((k->n + 1) * sizeof(AVR_Instr *), __func__, __FILE__, __LINE__);
        int j = 0;
        for (AVR_Instr *in = b->head; in; in = in->next)
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
    AVR_Block *b = k->b;
    b->head = b->tail = NULL;
    int j            = 0;
    for (int i = 0; i < k->n; i++) {
        AVR_Instr *in = k->in[i];
        if (!in)
            continue;
        in->next = NULL;
        if (b->tail)
            b->tail->next = in;
        else
            b->head = in;
        b->tail   = in;
        k->in[j++] = in;
    }
    k->n = j;
}

static void drop(Blk *k, int i)
{
    avr_free_instr(k->in[i]);
    k->in[i] = NULL;
}

// The live-in set of the block labelled `label`; for one not found, the epilogue's
// (before it exists, a jump there is the only jump out of the body).
static Set target_live(const Cfg *c, const char *label)
{
    intptr_t t;
    return label && map_get(&c->labels, label, &t) ? c->blk[t].live_in : ret_use(c->fn->result);
}

// The set live after instruction i of block `bi`, given the set live after i+1.
static Set live_across(const Cfg *c, int bi, int i, Set after)
{
    const AVR_Instr *in = c->blk[bi].in[i];
    if (in->op == AVR_RJMP || in->op == AVR_JMP)
        return in->op == AVR_RJMP ? target_live(c, in->opnd[0].sym) : 0;
    if (in->op == AVR_IJMP)
        return ALL;
    if (in->op == AVR_RET)
        return 0;
    if (is_branch(in->op))
        return after | target_live(c, in->opnd[0].sym);
    return after;
}

// The set live at the end of block `bi`: what its fall-through successor needs.
static Set block_out(const Cfg *c, int bi)
{
    // Off the end: into the epilogue, which restores the call-saved registers.
    return bi + 1 < c->n ? c->blk[bi + 1].live_in : ret_use(c->fn->result);
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
                def_use(c->blk[bi].in[i], c->fn->result, &def, &use);
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
            const AVR_Instr *in = c->blk[bi].in[i];
            if (in && in->opnd[0].kind == AVR_OPND_LABEL && strcmp(in->opnd[0].sym, label) == 0)
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
        dead     = is_jump(k->in[k->n - 1]->op);
        int next = next_nonempty(c, bi);
        AVR_Instr *last = k->in[k->n - 1];
        // A jump to the next instruction.
        if (last->op == AVR_RJMP && labels_between(c, bi, next, last->opnd[0].sym)) {
            drop(k, k->n - 1);
            relink(k);
            changed = true;
            dead    = false;
            continue;
        }
        // A branch over a jump: the inverse branch.
        if (k->n >= 2 && last->op == AVR_RJMP && is_branch(k->in[k->n - 2]->op) &&
            labels_between(c, bi, next, k->in[k->n - 2]->opnd[0].sym)) {
            AVR_Instr *br = k->in[k->n - 2];
            br->op        = avr_inverse(br->op);
            xfree(br->opnd[0].sym);
            br->opnd[0]  = last->opnd[0];
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
    bool y;        // a slot byte Y+q, else a global's sym+off
    int q;
    const char *sym;
    int reg;
} MemFact;

typedef struct {
    int eq[32];   // the register a register is known to equal (its class's root), or -1
    int k[32];    // its known constant, or -1
    MemFact m[64];
    int nm;
} Facts;

static void facts_reset(Facts *f)
{
    for (int r = 0; r < 32; r++)
        f->eq[r] = f->k[r] = -1;
    f->k[AVR_ZERO] = 0; // r1 is zero between instructions of different TAC ones
    f->nm          = 0;
}

static int root(const Facts *f, int r)
{
    return f->eq[r] >= 0 ? f->eq[r] : r;
}

// Register r gets a new value: the others that equalled it keep the old one.
static void kill(Facts *f, int r)
{
    int heir = -1;
    for (int x = 0; x < 32; x++)
        if (x != r && f->eq[x] == r) {
            if (heir < 0) {
                heir      = x;
                f->eq[x] = -1;
            } else {
                f->eq[x] = heir;
            }
        }
    f->eq[r] = -1;
    f->k[r]  = -1;
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
    for (int r = 0; r < 32; r++)
        if (s >> r & 1)
            kill(f, r);
}

static bool same_mem(const MemFact *m, bool y, int q, const char *sym)
{
    return m->y == y && m->q == q && (y || strcmp(m->sym, sym) == 0);
}

static int mem_find(const Facts *f, bool y, int q, const char *sym)
{
    for (int i = 0; i < f->nm; i++)
        if (same_mem(&f->m[i], y, q, sym))
            return i;
    return -1;
}

static void mem_forget(Facts *f, bool y, int q, const char *sym)
{
    int i = mem_find(f, y, q, sym);
    if (i >= 0)
        f->m[i] = f->m[--f->nm];
}

static void mem_note(Facts *f, bool y, int q, const char *sym, int reg)
{
    mem_forget(f, y, q, sym);
    if (f->nm < 64)
        f->m[f->nm++] = (MemFact){ y, q, sym, reg };
}

// The memory byte `in` loads or stores, when it is a slot (Y+q) or a global.
static bool mem_of(const AVR_Instr *in, bool *y, int *q, const char **sym)
{
    const AVR_Operand *o = in->op == AVR_LDD || in->op == AVR_LDS ? &in->opnd[1] : &in->opnd[0];
    if ((in->op == AVR_LDD || in->op == AVR_STD) && o->reg == AVR_Y) {
        *y = true, *q = (int)o->imm, *sym = NULL;
        return true;
    }
    if ((in->op == AVR_LDS || in->op == AVR_STS) && o->kind == AVR_OPND_SYM &&
        o->mod == AVR_MOD_NONE) {
        *y = false, *q = (int)o->imm, *sym = o->sym;
        return true;
    }
    return false;
}

static void to_mov(AVR_Instr *in, int src)
{
    xfree(in->opnd[1].sym);
    in->op      = AVR_MOV;
    in->opnd[1] = avr_reg(src);
}

// A move d = s, known or not: whether it is redundant; the facts updated when not.
static bool move_fact(Facts *f, int d, int s)
{
    if (d == s || root(f, d) == root(f, s) || (f->k[d] >= 0 && f->k[d] == f->k[s]))
        return true;
    int k = f->k[s], rs = root(f, s);
    kill(f, d);
    f->eq[d] = rs == d ? -1 : rs;
    f->k[d]  = k;
    return false;
}

static bool forward(Cfg *c)
{
    bool changed = false;
    Facts f;
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k = &c->blk[bi];
        facts_reset(&f);
        for (int i = 0; i < k->n; i++) {
            AVR_Instr *in = k->in[i];
            const AVR_Operand *a = &in->opnd[0], *b = &in->opnd[1];
            bool y;
            int q;
            const char *sym;
            if (!in->vol && (in->op == AVR_LDD || in->op == AVR_LDS) && mem_of(in, &y, &q, &sym)) {
                int m = mem_find(&f, y, q, sym);
                if (m >= 0) { // a reload: a move
                    to_mov(in, f.m[m].reg);
                    changed = true;
                } else {
                    kill(&f, a->reg);
                    mem_note(&f, y, q, sym, a->reg);
                    continue;
                }
            }
            if (!in->vol && (in->op == AVR_STD || in->op == AVR_STS) && mem_of(in, &y, &q, &sym)) {
                int m = mem_find(&f, y, q, sym);
                if (m >= 0 && root(&f, f.m[m].reg) == root(&f, b->reg)) { // already there
                    drop(k, i);
                    changed = true;
                    continue;
                }
                mem_note(&f, y, q, sym, b->reg);
                continue;
            }
            switch (in->op) {
            case AVR_MOV:
                if (!in->vol && move_fact(&f, a->reg, b->reg)) {
                    drop(k, i);
                    changed = true;
                }
                continue;
            case AVR_MOVW: {
                bool lo = root(&f, a->reg) == root(&f, b->reg) ||
                          (f.k[a->reg] >= 0 && f.k[a->reg] == f.k[b->reg]);
                bool hi = root(&f, a->reg + 1) == root(&f, b->reg + 1) ||
                          (f.k[a->reg + 1] >= 0 && f.k[a->reg + 1] == f.k[b->reg + 1]);
                if (!in->vol && lo && hi) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
                move_fact(&f, a->reg, b->reg);
                move_fact(&f, a->reg + 1, b->reg + 1);
                continue;
            }
            case AVR_LDI:
                if (b->kind != AVR_OPND_IMM) { // lo8(sym) and the like
                    kill(&f, a->reg);
                    continue;
                }
                if (!in->vol && f.k[a->reg] == (int)(b->imm & 0xff)) {
                    drop(k, i);
                    changed = true;
                    continue;
                }
                kill(&f, a->reg);
                f.k[a->reg] = (int)(b->imm & 0xff);
                continue;
            case AVR_ST:
            case AVR_STD:
            case AVR_CALL:
            case AVR_RCALL:
            case AVR_ICALL:
            case AVR_STS:
                if (!is_self_call(in)) // memory written through a pointer, or anywhere
                    f.nm = 0;
                break;
            default:
                if ((in->op == AVR_LDD || in->op == AVR_LDS) && mem_of(in, &y, &q, &sym))
                    mem_forget(&f, y, q, sym); // a volatile one
                break;
            }
            Set def, use;
            def_use(in, c->fn->result, &def, &use);
            kill_set(&f, def);
            if (in->op == AVR_CLR) // eor r, r
                f.k[a->reg] = 0;
        }
        relink(k);
    }
    return changed;
}

//
// Backward: liveness
//

static bool backward(Cfg *c)
{
    liveness(c);
    bool changed = false;
    for (int bi = 0; bi < c->n; bi++) {
        Blk *k   = &c->blk[bi];
        Set live = block_out(c, bi);
        for (int i = k->n - 1; i >= 0; i--) {
            AVR_Instr *in = k->in[i];
            live          = live_across(c, bi, i, live);
            Set def, use;
            def_use(in, c->fn->result, &def, &use);
            if (removable(in) && def && !(def & live)) {
                drop(k, i);
                changed = true;
                continue; // live unchanged
            }
            // ldi t, k; cp r, t: cpi r, k.
            if (in->op == AVR_CP && i > 0 && k->in[i - 1] && k->in[i - 1]->op == AVR_LDI &&
                !k->in[i - 1]->vol && k->in[i - 1]->opnd[1].kind == AVR_OPND_IMM &&
                k->in[i - 1]->opnd[0].reg == in->opnd[1].reg &&
                in->opnd[0].reg >= 16 && in->opnd[0].reg != in->opnd[1].reg &&
                !(live & R(in->opnd[1].reg))) {
                in->op      = AVR_CPI;
                in->opnd[1] = avr_imm(k->in[i - 1]->opnd[1].imm & 0xff);
                drop(k, i - 1);
                def_use(in, c->fn->result, &def, &use);
                live = (live & ~def) | use;
                i--; // the ldi is gone
                changed = true;
                continue;
            }
            // subi r, lo; sbci r+1, hi on r24-r30 with the flags dead: adiw or sbiw.
            if (in->op == AVR_SBCI && i > 0 && k->in[i - 1] && k->in[i - 1]->op == AVR_SUBI &&
                !(live & SREG) && in->opnd[1].kind == AVR_OPND_IMM &&
                k->in[i - 1]->opnd[1].kind == AVR_OPND_IMM &&
                k->in[i - 1]->opnd[0].reg == in->opnd[0].reg - 1 &&
                k->in[i - 1]->opnd[0].reg >= 24 && k->in[i - 1]->opnd[0].reg % 2 == 0) {
                int v   = (int)((in->opnd[1].imm & 0xff) << 8 | (k->in[i - 1]->opnd[1].imm & 0xff));
                int neg = -v & 0xffff;
                if ((neg >= 1 && neg <= 63) || (v >= 1 && v <= 63)) {
                    AVR_Instr *lo = k->in[i - 1];
                    lo->op        = neg >= 1 && neg <= 63 ? AVR_ADIW : AVR_SBIW;
                    lo->opnd[1]   = avr_imm(lo->op == AVR_ADIW ? neg : v);
                    drop(k, i);
                    changed = true;
                    continue; // the adiw is visited next
                }
            }
            live = (live & ~def) | use;
        }
        relink(k);
    }
    return changed;
}

void avr_peephole_pass(AVR_Func *fn)
{
    bool changed = true;
    while (changed) {
        Cfg c;
        cfg_build(&c, fn);
        changed = clean_jumps(&c);
        changed |= forward(&c);
        changed |= backward(&c);
        cfg_free(&c);
    }
}

// Whether block j holds a lone ret.
static bool lone_ret(const Cfg *c, int j)
{
    return j < c->n && c->blk[j].n == 1 && c->blk[j].in[0]->op == AVR_RET;
}

void avr_peephole_frame(AVR_Func *fn)
{
    Cfg c;
    cfg_build(&c, fn);
    clean_jumps(&c);
    for (int bi = 0; bi < c.n; bi++) {
        Blk *k = &c.blk[bi];
        for (int i = 0; i < k->n; i++) {
            AVR_Instr *in = k->in[i];
            intptr_t t;
            // A jump to a lone ret: ret.
            if (in->op == AVR_RJMP && map_get(&c.labels, in->opnd[0].sym, &t) &&
                lone_ret(&c, next_nonempty(&c, (int)t - 1))) {
                xfree(in->opnd[0].sym);
                in->opnd[0] = (AVR_Operand){ 0 };
                in->op      = AVR_RET;
            }
        }
        // A call, then ret (or the epilogue's lone ret): a tail jump.
        for (int i = 0; i < k->n; i++) {
            AVR_Instr *in = k->in[i];
            if (!in || in->op != AVR_CALL)
                continue;
            bool then_ret = i + 1 < k->n ? k->in[i + 1]->op == AVR_RET
                                         : lone_ret(&c, next_nonempty(&c, bi));
            if (!then_ret)
                continue;
            in->op = AVR_JMP;
            if (i + 1 < k->n)
                drop(k, i + 1);
        }
        relink(k);
    }
    cfg_free(&c);
    // The tail of a block after its new jump or ret.
    cfg_build(&c, fn);
    clean_jumps(&c);
    cfg_free(&c);
}
