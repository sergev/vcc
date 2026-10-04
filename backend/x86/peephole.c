//
// Peephole pass over the x86-64 IR, after register allocation and the frame:
//   - a move to itself goes (a 32-bit one when the upper half it clears is not read),
//     so does an extension repeated in place, a lea of its own base, an add or sub of
//     zero, and an instruction whose results are all dead;
//   - a move folds into its uses, and a result is computed in the register it is then
//     moved to;
//   - a load or an immediate folds into the instruction that uses it, an address
//     computation (lea) into the memory operand it feeds, and the reload of what was
//     just stored goes (or becomes a move);
//   - cmp $0 is test, and so is a mask computed only to be tested; a zero is xor where
//     the flags are dead;
//   - a jump to the next block goes, a branch over a jump branches the other way, code
//     after a jump or return goes, and a block no jump goes to joins the one before it
//     (or goes, when unreachable);
//   - a short triangle or diamond that only moves a value into one register becomes
//     cmov, its source a register or a slot (cmov loads unconditionally).
// Whether a value is read again is decided by the liveness of the registers and the
// flags over the function's blocks, computed afresh after each round of rewrites.  An
// instruction selected for a volatile access keeps its memory access as it is.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

// A set of registers: bit r for the general registers (0-15) and xmm0-xmm15 (16-31),
// and the flags.
typedef uint64_t Regs;

#define FLAGS     (1ull << 32)
#define CALLER    (0x0fc7ull | 0xffff0000ull) // rax rcx rdx rsi rdi r8-r11, every xmm
#define ARGS      (0x03c7ull | 0x00ff0000ull) // rax (%al) rcx rdx rsi rdi r8 r9, xmm0-7
#define SAVED     0xf028ull                   // rbx rbp r12-r15

static Regs ret_regs, ret_wide; // the function's result registers, read by ret

static Regs bit(int r)
{
    return r >= 0 && r < 32 ? 1ull << r : 0;
}

enum { READ = 1, WRITE = 2 };

static bool is_alu2(X86_Op op)
{
    switch (op) {
    case X86_ADD:
    case X86_SUB:
    case X86_AND:
    case X86_OR:
    case X86_XOR:
    case X86_SHL:
    case X86_SAR:
    case X86_SHR:
    case X86_ADDSS:
    case X86_ADDSD:
    case X86_SUBSS:
    case X86_SUBSD:
    case X86_MULSS:
    case X86_MULSD:
    case X86_DIVSS:
    case X86_DIVSD:
    case X86_XORPS:
    case X86_CVTSS2SD:
    case X86_CVTSD2SS:
    case X86_CVTSI2SS:
    case X86_CVTSI2SD:
    case X86_CMOV:
        return true;
    default:
        return false;
    }
}

// Whether `in` is a zeroing idiom, xor or xorps of a register with itself: it writes
// the register without reading it.
static bool is_zero_idiom(const X86_Instr *in)
{
    return (in->op == X86_XOR || in->op == X86_XORPS) && in->opnd[0].kind == X86_OPND_REG &&
           in->opnd[1].kind == X86_OPND_REG && in->opnd[0].reg == in->opnd[1].reg;
}

// How `in` uses explicit operand `i`: READ, WRITE or both.
static int role(const X86_Instr *in, int i)
{
    const X86_Operand *o = &in->opnd[i];
    if (o->kind == X86_OPND_NONE)
        return 0;
    int r = READ;
    switch (in->op) {
    case X86_IMUL:
        r = in->opnd[2].kind != X86_OPND_NONE ? (i == 2 ? WRITE : READ) : (i == 1 ? READ | WRITE : READ);
        break;
    case X86_MOV:
    case X86_MOVABS:
    case X86_MOVSB:
    case X86_MOVSW:
    case X86_MOVSL:
    case X86_MOVZB:
    case X86_MOVZW:
    case X86_LEA:
    case X86_MOVAPS:
    case X86_MOVUPS:
    case X86_CVTTSS2SI:
    case X86_CVTTSD2SI:
    case X86_MOVD:
        r = i == 1 ? WRITE : READ;
        break;
    case X86_MOVSS:
    case X86_MOVSD:
        // Between registers, the upper lanes are kept.
        r = i == 1 ? (in->opnd[0].kind == X86_OPND_REG ? READ | WRITE : WRITE) : READ;
        break;
    case X86_NEG:
    case X86_NOT:
        r = READ | WRITE;
        break;
    case X86_POP:
    case X86_SET:
        r = WRITE;
        break;
    default:
        if (is_alu2(in->op))
            r = i == 1 ? READ | WRITE : READ;
        break;
    }
    if (is_zero_idiom(in))
        r = i == 1 ? WRITE : 0;
    // A write of 8 or 16 bits keeps the rest of a general register.
    if ((r & WRITE) && o->kind == X86_OPND_REG && !x86_is_xmm(o->reg) &&
        (o->width == X86_B || o->width == X86_W))
        r |= READ;
    return r;
}

// The registers memory operand `o` addresses with.
static Regs address_regs(const X86_Operand *o)
{
    return o->kind == X86_OPND_MEM ? bit(o->reg) | bit(o->index) : 0;
}

// The registers and flags `in` reads and writes, implicitly included.
static void effects(const X86_Instr *in, Regs *use, Regs *def)
{
    Regs u = 0, d = 0;
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &in->opnd[i];
        int r                = role(in, i);
        u |= address_regs(o);
        if (o->kind == X86_OPND_REG || o->kind == X86_OPND_INDIRECT) {
            if (r & READ)
                u |= bit(o->reg);
            if (r & WRITE)
                d |= bit(o->reg);
        }
    }
    switch (in->op) {
    case X86_ADD:
    case X86_SUB:
    case X86_AND:
    case X86_OR:
    case X86_XOR:
    case X86_IMUL:
    case X86_NEG:
    case X86_CMP:
    case X86_TEST:
    case X86_UCOMISS:
    case X86_UCOMISD:
    case X86_FUCOMIP:
        d |= FLAGS;
        break;
    case X86_SHL:
    case X86_SAR:
    case X86_SHR:
        d |= FLAGS;
        if (in->opnd[0].kind != X86_OPND_IMM)
            u |= FLAGS; // a zero count leaves them
        break;
    case X86_IDIV:
    case X86_DIV:
        u |= bit(X86_RAX) | bit(X86_RDX);
        d |= bit(X86_RAX) | bit(X86_RDX) | FLAGS;
        break;
    case X86_CLTD:
    case X86_CQTO:
        u |= bit(X86_RAX);
        d |= bit(X86_RDX);
        break;
    case X86_J:
    case X86_SET:
    case X86_CMOV:
        u |= FLAGS;
        break;
    case X86_CALL:
        u |= ARGS;
        d |= CALLER | FLAGS;
        break;
    case X86_RET:
        u |= SAVED | bit(X86_RSP) | ret_regs;
        break;
    case X86_EPILOGUE:
        u |= bit(X86_RSP) | bit(X86_RBP);
        d |= SAVED | bit(X86_RSP); // the callee-saved registers restored
        break;
    case X86_LEAVE:
        u |= bit(X86_RBP);
        d |= bit(X86_RBP) | bit(X86_RSP);
        break;
    case X86_PUSH:
    case X86_POP:
        u |= bit(X86_RSP);
        d |= bit(X86_RSP);
        break;
    default:
        break;
    }
    *use = u;
    *def = d;
}

static bool is_reg(const X86_Operand *o);

// The registers `in` reads or writes other than through its explicit register
// operands: implicitly, or to address memory.
static Regs hidden_regs(const X86_Instr *in)
{
    Regs u, d, explicit_regs = 0;
    effects(in, &u, &d);
    Regs addr = 0;
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &in->opnd[i];
        addr |= address_regs(o);
        if (o->kind == X86_OPND_REG)
            explicit_regs |= bit(o->reg);
    }
    // A shift count in a register is always %cl.
    bool shift = in->op == X86_SHL || in->op == X86_SAR || in->op == X86_SHR;
    if (shift && is_reg(&in->opnd[0]))
        addr |= bit(X86_RCX);
    return ((u | d) & ~explicit_regs & ~FLAGS) | addr;
}

static bool writes_memory(const X86_Instr *in)
{
    switch (in->op) {
    case X86_CALL:
    case X86_PUSH:
    case X86_FSTPT:
    case X86_FSTPS:
    case X86_FSTPL:
    case X86_FISTPQ:
    case X86_FNSTCW:
        return true;
    default:
        break;
    }
    for (int i = 0; i < X86_MAX_OPERANDS; i++)
        if ((in->opnd[i].kind == X86_OPND_MEM || in->opnd[i].kind == X86_OPND_RIP) &&
            (role(in, i) & WRITE))
            return true;
    return false;
}

//
// Liveness over the blocks
//
// Of the general registers, those whose upper 32 bits `in` reads (as a 64-bit
// operand, an address, or implicitly) and those it writes whole, which a 32-bit write
// does, zeroing them.  An 8- or 16-bit write leaves them.
static void upper_effects(const X86_Instr *in, Regs *use, Regs *def)
{
    Regs u = 0, d = 0;
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &in->opnd[i];
        int r                = role(in, i);
        u |= address_regs(o);
        if (o->kind == X86_OPND_INDIRECT)
            u |= bit(o->reg);
        if (o->kind != X86_OPND_REG || x86_is_xmm(o->reg))
            continue;
        if ((r & READ) && o->width == X86_Q)
            u |= bit(o->reg);
        if ((r & WRITE) && (o->width == X86_L || o->width == X86_Q))
            d |= bit(o->reg);
    }
    switch (in->op) {
    case X86_IDIV:
    case X86_DIV:
        if (in->width == X86_Q)
            u |= bit(X86_RAX) | bit(X86_RDX);
        d |= bit(X86_RAX) | bit(X86_RDX);
        break;
    case X86_CQTO:
        u |= bit(X86_RAX);
        d |= bit(X86_RDX);
        break;
    case X86_CLTD:
        d |= bit(X86_RDX);
        break;
    case X86_CALL:
        u |= in->wide;
        d |= CALLER & 0xffff;
        break;
    case X86_RET:
        u |= SAVED | bit(X86_RSP) | ret_wide;
        break;
    case X86_EPILOGUE:
        u |= bit(X86_RSP) | bit(X86_RBP);
        d |= SAVED | bit(X86_RSP);
        break;
    case X86_LEAVE:
    case X86_PUSH:
    case X86_POP:
        u |= bit(X86_RSP) | bit(X86_RBP);
        d |= bit(X86_RSP);
        break;
    default:
        break;
    }
    *use = u & 0xffff;
    *def = d & 0xffff;
}

typedef struct Live {
    X86_Func *fn;
    int nblocks;
    X86_Block **blocks;
    Regs *live_in, *live_out;
    bool upper;               // of the upper halves of the general registers
    const struct Live *halves; // with the upper-half liveness of the same blocks
} Live;

static int block_index(const Live *lv, const char *label)
{
    for (int i = 0; i < lv->nblocks; i++)
        if (lv->blocks[i]->label && strcmp(lv->blocks[i]->label, label) == 0)
            return i;
    return -1;
}

// The live registers before `in`, given those after it.
static Regs step(const Live *lv, const X86_Instr *in, Regs live)
{
    Regs u, d;
    if (lv->upper)
        upper_effects(in, &u, &d);
    else
        effects(in, &u, &d);
    live = (live & ~d) | u;
    if (in->op == X86_J) {
        int t = block_index(lv, in->opnd[0].sym);
        live |= t >= 0 ? lv->live_in[t] : ~0ull;
    }
    return live;
}

static void live_compute(Live *lv, X86_Func *fn, bool upper)
{
    lv->fn      = fn;
    lv->upper   = upper;
    lv->halves  = NULL;
    lv->nblocks = 0;
    for (X86_Block *b = fn->blocks; b; b = b->next)
        lv->nblocks++;
    int n        = lv->nblocks ? lv->nblocks : 1;
    lv->blocks   = xalloc(n * sizeof(X86_Block *), __func__, __FILE__, __LINE__);
    lv->live_in  = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    lv->live_out = xalloc(n * sizeof(Regs), __func__, __FILE__, __LINE__);
    int i = 0;
    for (X86_Block *b = fn->blocks; b; b = b->next, i++) {
        lv->blocks[i]   = b;
        lv->live_in[i]  = 0;
        lv->live_out[i] = 0;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (i = lv->nblocks - 1; i >= 0; i--) {
            X86_Block *b        = lv->blocks[i];
            const X86_Instr *t  = b->tail;
            Regs out            = 0;
            if (t && t->op == X86_JMP) {
                int k = block_index(lv, t->opnd[0].sym);
                out   = k >= 0 ? lv->live_in[k] : ~0ull;
            } else if (!t || t->op != X86_RET) {
                if (i + 1 < lv->nblocks)
                    out = lv->live_in[i + 1];
            }
            // Backwards through the block, the instructions in an array.
            int len = 0;
            for (const X86_Instr *in = b->head; in; in = in->next)
                len++;
            const X86_Instr **seq =
                xalloc((len ? len : 1) * sizeof(X86_Instr *), __func__, __FILE__, __LINE__);
            int k = 0;
            for (const X86_Instr *in = b->head; in; in = in->next)
                seq[k++] = in;
            Regs live = out;
            while (k > 0)
                live = step(lv, seq[--k], live);
            xfree(seq);
            if (out != lv->live_out[i] || live != lv->live_in[i]) {
                lv->live_out[i] = out;
                lv->live_in[i]  = live;
                changed         = true;
            }
        }
    }
}

static void live_free(Live *lv)
{
    xfree(lv->blocks);
    xfree(lv->live_in);
    xfree(lv->live_out);
}

// The live registers just after `in`, of block `b` (index `bi`).
static Regs live_after(const Live *lv, int bi, const X86_Instr *in)
{
    const X86_Block *b = lv->blocks[bi];
    int len = 0, at = -1;
    for (const X86_Instr *p = b->head; p; p = p->next, len++)
        if (p == in)
            at = len;
    const X86_Instr **seq = xalloc((len ? len : 1) * sizeof(X86_Instr *), __func__, __FILE__, __LINE__);
    int k = 0;
    for (const X86_Instr *p = b->head; p; p = p->next)
        seq[k++] = p;
    Regs live = lv->live_out[bi];
    for (k = len - 1; k > at; k--)
        live = step(lv, seq[k], live);
    xfree(seq);
    return live;
}

// Whether register `r` is live where jump `in` (a jcc mid-block) may go.
static bool live_at_target(const Live *lv, const X86_Instr *in, int r)
{
    if (in->op != X86_J)
        return false;
    int t = block_index(lv, in->opnd[0].sym);
    return t < 0 || (lv->live_in[t] & bit(r));
}

// Whether register `r` (or the flags) holds nothing read after `in`.
static bool dead_after(const Live *lv, int bi, const X86_Instr *in, Regs r)
{
    return (live_after(lv, bi, in) & r) == 0;
}

//
// Editing
//
static void free_instr(X86_Instr *in)
{
    for (int i = 0; i < X86_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

static void delete_at(X86_Block *b, X86_Instr **link)
{
    X86_Instr *in = *link;
    *link         = in->next;
    free_instr(in);
    b->tail = NULL;
    for (X86_Instr *p = b->head; p; p = p->next)
        b->tail = p;
}

static void delete_instr(X86_Block *b, X86_Instr *in)
{
    for (X86_Instr **link = &b->head; *link; link = &(*link)->next)
        if (*link == in) {
            delete_at(b, link);
            return;
        }
}

static X86_Operand copy_operand(const X86_Operand *o)
{
    X86_Operand c = *o;
    if (c.sym)
        c.sym = xstrdup(c.sym);
    return c;
}

static void set_operand(X86_Instr *in, int i, X86_Operand o)
{
    xfree(in->opnd[i].sym);
    in->opnd[i] = o;
}

static bool is_reg(const X86_Operand *o)
{
    return o->kind == X86_OPND_REG;
}

static bool same_operand(const X86_Operand *a, const X86_Operand *b)
{
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case X86_OPND_REG:
        return a->reg == b->reg && (x86_is_xmm(a->reg) || a->width == b->width);
    case X86_OPND_IMM:
        return a->imm == b->imm;
    case X86_OPND_MEM:
        return a->reg == b->reg && a->index == b->index && a->imm == b->imm &&
               (a->index < 0 || a->scale == b->scale);
    case X86_OPND_RIP:
    case X86_OPND_LABEL:
        return a->imm == b->imm && strcmp(a->sym, b->sym) == 0;
    default:
        return false;
    }
}

static bool same_instr(const X86_Instr *a, const X86_Instr *b)
{
    if (a->op != b->op || a->width != b->width || a->cond != b->cond)
        return false;
    for (int i = 0; i < X86_MAX_OPERANDS; i++)
        if (!same_operand(&a->opnd[i], &b->opnd[i]))
            return false;
    return true;
}

// The width the register view of explicit operand `o` reads or writes; a memory
// operand's registers are read in 64 bits.
static int view_bits(const X86_Operand *o)
{
    if (x86_is_xmm(o->reg))
        return 128;
    return (const int[]){ 8, 16, 32, 64 }[o->width];
}

// Whether every mention of register `t` in `in` is an explicit register operand of at
// most `bits` bits, or (when `bits` is 64) an address register, so that another
// register of the file may stand in for it.
static bool replaceable(const X86_Instr *in, int t, int bits)
{
    Regs h = hidden_regs(in);
    bool addr = false;
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &in->opnd[i];
        if (is_reg(o) && o->reg == t && view_bits(o) > bits)
            return false;
        if (o->kind == X86_OPND_INDIRECT && o->reg == t && bits < 64)
            return false;
        if (address_regs(o) & bit(t))
            addr = true;
    }
    if (addr)
        return bits >= 64;
    return (h & bit(t)) == 0;
}

static void replace_reg(X86_Instr *in, int t, int s)
{
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        X86_Operand *o = &in->opnd[i];
        if ((o->kind == X86_OPND_REG || o->kind == X86_OPND_INDIRECT) && o->reg == t)
            o->reg = s;
        if (o->kind == X86_OPND_MEM) {
            if (o->reg == t)
                o->reg = s;
            if (o->index == t)
                o->index = s;
        }
    }
}

static bool mentions(const X86_Instr *in, int r)
{
    Regs u, d;
    effects(in, &u, &d);
    return ((u | d) & bit(r)) != 0;
}

static bool reads_reg(const X86_Instr *in, int r)
{
    Regs u, d;
    effects(in, &u, &d);
    return (u & bit(r)) != 0;
}

static Regs defs(const X86_Instr *in)
{
    Regs u, d;
    effects(in, &u, &d);
    return d;
}

static bool writes_reg(const X86_Instr *in, int r)
{
    Regs u, d;
    effects(in, &u, &d);
    return (d & bit(r)) != 0;
}

// Whether `in` writes all of general register `r` (32 or 64 bits) without reading it.
static bool overwrites(const X86_Instr *in, int r)
{
    return writes_reg(in, r) && !reads_reg(in, r);
}

// A move between registers of one file: movq or movl, movaps.
static bool is_reg_move(const X86_Instr *in)
{
    if (!is_reg(&in->opnd[0]) || !is_reg(&in->opnd[1]))
        return false;
    if (in->op == X86_MOVAPS)
        return true;
    return in->op == X86_MOV && (in->width == X86_L || in->width == X86_Q) &&
           !x86_is_xmm(in->opnd[0].reg) && !x86_is_xmm(in->opnd[1].reg);
}

static int move_bits(const X86_Instr *in)
{
    return in->op == X86_MOVAPS ? 128 : in->width == X86_L ? 32 : 64;
}

// Whether the upper half of general register `r`, after `in`, is never read before
// it is written.
static bool upper_unread(const Live *lv, int bi, X86_Instr *in, int r)
{
    return (live_after(lv->halves, bi, in) & bit(r)) == 0;
}

// Delete what does nothing: a move to itself, an extension repeated in place, a lea
// of its own base, an add or sub of zero, a result nobody reads.
static bool delete_noop(const Live *lv, int bi, X86_Block *b, X86_Instr **link, X86_Instr *prev)
{
    X86_Instr *in = *link;
    if (in->is_volatile)
        return false;
    const X86_Operand *s = &in->opnd[0], *d = &in->opnd[1];
    if (is_reg_move(in) && s->reg == d->reg &&
        (move_bits(in) != 32 || upper_unread(lv, bi, in, d->reg))) {
        delete_at(b, link);
        return true;
    }
    if (prev && same_instr(prev, in) && is_reg(s) && is_reg(d) && s->reg == d->reg &&
        (in->op == X86_MOVSB || in->op == X86_MOVZB || in->op == X86_MOVSW ||
         in->op == X86_MOVZW || (in->op == X86_MOV && in->width == X86_L))) {
        delete_at(b, link);
        return true;
    }
    if (in->op == X86_LEA && in->width == X86_Q && s->kind == X86_OPND_MEM && s->index < 0 &&
        s->imm == 0 && s->reg == d->reg) {
        delete_at(b, link);
        return true;
    }
    if ((in->op == X86_ADD || in->op == X86_SUB) && s->kind == X86_OPND_IMM && s->imm == 0 &&
        is_reg(d) && dead_after(lv, bi, in, FLAGS)) {
        delete_at(b, link);
        return true;
    }
    // A result nobody reads, of an instruction with no other effect.
    Regs u, df;
    effects(in, &u, &df);
    bool pure = false;
    switch (in->op) {
    case X86_MOV:
    case X86_MOVABS:
    case X86_MOVSB:
    case X86_MOVSW:
    case X86_MOVSL:
    case X86_MOVZB:
    case X86_MOVZW:
    case X86_LEA:
    case X86_MOVAPS:
    case X86_MOVUPS:
    case X86_MOVSS:
    case X86_MOVSD:
    case X86_ADD:
    case X86_SUB:
    case X86_AND:
    case X86_OR:
    case X86_XOR:
    case X86_SHL:
    case X86_SAR:
    case X86_SHR:
    case X86_NEG:
    case X86_NOT:
    case X86_SET:
    case X86_CMP:
    case X86_TEST:
    case X86_XORPS:
    case X86_CVTSS2SD:
    case X86_CVTSD2SS:
    case X86_CVTSI2SS:
    case X86_CVTSI2SD:
    case X86_CVTTSS2SI:
    case X86_CVTTSD2SI:
    case X86_ADDSS:
    case X86_ADDSD:
    case X86_SUBSS:
    case X86_SUBSD:
    case X86_MULSS:
    case X86_MULSD:
    case X86_DIVSS:
    case X86_DIVSD:
    case X86_UCOMISS:
    case X86_UCOMISD:
        pure = true;
        break;
    case X86_IMUL:
        pure = true;
        break;
    default:
        break;
    }
    if (pure && df && !writes_memory(in) && dead_after(lv, bi, in, df)) {
        delete_at(b, link);
        return true;
    }
    return false;
}

// mov S, T: the uses of T that follow read S instead, up to where T dies, when
// neither is written before then (S may be, by the last use); the move goes.
static bool forward_move(const Live *lv, int bi, X86_Block *b, X86_Instr **link)
{
    X86_Instr *mv = *link;
    if (!is_reg_move(mv) || mv->opnd[0].reg == mv->opnd[1].reg)
        return false;
    int s = mv->opnd[0].reg, t = mv->opnd[1].reg, bits = move_bits(mv);
    X86_Instr *last = NULL;
    bool ok         = false;
    for (X86_Instr *p = mv->next; p; p = p->next) {
        if (live_at_target(lv, p, t))
            return false; // the value leaves the block in T
        if (!mentions(p, t)) {
            if (writes_reg(p, s))
                return false;
            continue;
        }
        if (!reads_reg(p, t)) {
            ok = true; // overwritten: the uses end before it
            break;
        }
        if (writes_reg(p, t) || !replaceable(p, t, bits))
            return false;
        last = p;
        if (dead_after(lv, bi, p, bit(t))) {
            ok = true;
            break;
        }
        if (writes_reg(p, s))
            return false;
    }
    if (!ok || !last)
        return false;
    for (X86_Instr *p = mv->next;; p = p->next) {
        replace_reg(p, t, s);
        if (p == last)
            break;
    }
    delete_at(b, link);
    return true;
}

// mov T, D with T dead after it: the instructions since T was last overwritten compute
// in D instead, when they neither read nor write D (the first may read it, as its old
// value is no longer needed); the move goes.
static bool compute_in_place(const Live *lv, int bi, X86_Block *b, X86_Instr **link)
{
    X86_Instr *mv = *link;
    if (!is_reg_move(mv) || mv->opnd[0].reg == mv->opnd[1].reg || mv->is_volatile)
        return false;
    int t = mv->opnd[0].reg, d = mv->opnd[1].reg, bits = move_bits(mv);
    if (!dead_after(lv, bi, mv, bit(t)))
        return false;
    X86_Instr *start = NULL;
    for (X86_Instr *p = b->head; p != mv; p = p->next) {
        if (overwrites(p, t) && !writes_reg(p, d))
            start = p;
        else if (mentions(p, d))
            start = NULL; // D is still in use at this point
    }
    if (!start || !replaceable(start, t, bits) || (hidden_regs(start) & bit(d)))
        return false;
    for (X86_Instr *p = start->next; p != mv; p = p->next)
        if (mentions(p, d) || (mentions(p, t) && !replaceable(p, t, bits)) || p->op == X86_J)
            return false;
    for (X86_Instr *p = start; p != mv; p = p->next)
        replace_reg(p, t, d);
    delete_at(b, link);
    return true;
}

// The operation of `use` that takes operand 0 as a source of its own width, which may
// be memory or an immediate in its place.
static bool takes_source(const X86_Instr *use, bool imm)
{
    switch (use->op) {
    case X86_ADD:
    case X86_SUB:
    case X86_AND:
    case X86_OR:
    case X86_XOR:
    case X86_CMP:
    case X86_MOV:
        return true;
    case X86_IMUL:
        return !imm && use->opnd[2].kind == X86_OPND_NONE;
    case X86_ADDSS:
    case X86_ADDSD:
    case X86_SUBSS:
    case X86_SUBSD:
    case X86_MULSS:
    case X86_MULSD:
    case X86_DIVSS:
    case X86_DIVSD:
    case X86_UCOMISS:
    case X86_UCOMISD:
        return !imm;
    default:
        return false;
    }
}

// mov M, T (a load, or an immediate) then an operation reading T as its source, T
// dead after: the operation takes M; the load goes.  A comparison may take it as its
// other operand too: cmp X, T is cmp X, M.
static bool fold_load(const Live *lv, int bi, X86_Block *b, X86_Instr **link)
{
    X86_Instr *ld = *link, *use = ld->next;
    if (!use || ld->is_volatile || !is_reg(&ld->opnd[1]))
        return false;
    const X86_Operand *m = &ld->opnd[0];
    int t                = ld->opnd[1].reg;
    bool imm             = m->kind == X86_OPND_IMM;
    bool mem             = m->kind == X86_OPND_MEM || m->kind == X86_OPND_RIP;
    bool gp_load = ld->op == X86_MOV && (ld->width == X86_L || ld->width == X86_Q) && (imm || mem);
    bool fp_load = (ld->op == X86_MOVSS || ld->op == X86_MOVSD) && mem;
    if (!gp_load && !fp_load)
        return false;
    if (imm && ld->width == X86_Q && !x86_imm32(m->imm))
        return false;
    if (!dead_after(lv, bi, use, bit(t)) || (hidden_regs(use) & bit(t)))
        return false;
    int k = -1; // the operand of `use` that becomes M
    if (is_reg(&use->opnd[0]) && use->opnd[0].reg == t && takes_source(use, imm)) {
        k = 0;
    } else if (use->op == X86_CMP && is_reg(&use->opnd[1]) && use->opnd[1].reg == t &&
               !imm && use->opnd[0].kind != X86_OPND_MEM && use->opnd[0].kind != X86_OPND_RIP) {
        k = 1;
    }
    if (k < 0 || (gp_load && use->width != ld->width) ||
        (fp_load && (use->op == X86_MOV || (use->op == X86_ADDSD || use->op == X86_SUBSD ||
                                            use->op == X86_MULSD || use->op == X86_DIVSD ||
                                            use->op == X86_UCOMISD) != (ld->op == X86_MOVSD))))
        return false;
    const X86_Operand *other = &use->opnd[1 - k];
    if (other->kind == X86_OPND_REG ? other->reg == t : (address_regs(other) & bit(t)) != 0)
        return false;
    if (mem && (other->kind == X86_OPND_MEM || other->kind == X86_OPND_RIP))
        return false; // one memory operand at most
    if (mem && use->op == X86_MOV && !is_reg(other))
        return false;
    set_operand(use, k, copy_operand(m));
    use->is_volatile = use->is_volatile || ld->is_volatile;
    delete_at(b, link);
    return true;
}

// lea M, T then an instruction addressing memory from T, T dead after (or overwritten
// by it): the address is M plus the displacement, when it can be written as one; the
// lea goes.
static bool fold_address(const Live *lv, int bi, X86_Block *b, X86_Instr **link)
{
    X86_Instr *lea = *link;
    if (lea->op != X86_LEA || lea->width != X86_Q || !is_reg(&lea->opnd[1]))
        return false;
    int t                = lea->opnd[1].reg;
    const X86_Operand *m = &lea->opnd[0];
    Regs mregs           = address_regs(m);
    X86_Instr *use       = lea->next;
    for (; use; use = use->next) {
        if (mentions(use, t))
            break;
        if ((defs(use) & mregs) || use->op == X86_J || use->op == X86_JMP || use->op == X86_CALL)
            return false;
    }
    if (!use)
        return false;
    int k = -1;
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &use->opnd[i];
        if (o->kind == X86_OPND_MEM && o->reg == t && o->index != t)
            k = i;
        else if ((o->kind == X86_OPND_REG || o->kind == X86_OPND_INDIRECT) && o->reg == t &&
                 !(role(use, i) == WRITE))
            return false;
        else if (o->kind == X86_OPND_MEM && o->index == t)
            return false;
    }
    if (k < 0 || (hidden_regs(use) & ~address_regs(&use->opnd[k]) & bit(t)))
        return false;
    if (!dead_after(lv, bi, use, bit(t)) && !writes_reg(use, t))
        return false;
    const X86_Operand *o = &use->opnd[k];
    X86_Operand n        = copy_operand(m);
    n.imm += o->imm;
    if (!x86_imm32(n.imm)) {
        xfree(n.sym);
        return false;
    }
    if (o->index >= 0) {
        if (m->kind != X86_OPND_MEM || m->index >= 0) {
            xfree(n.sym);
            return false;
        }
        n.index = o->index;
        n.scale = o->scale;
    }
    set_operand(use, k, n);
    use->is_volatile = use->is_volatile || lea->is_volatile;
    delete_at(b, link);
    return true;
}

// mov R, M then mov M, R2 of the same width: the reload is a move from R.
static bool delete_reload(X86_Block *b, X86_Instr **link)
{
    X86_Instr *st = *link, *ld = st->next;
    if (!ld || st->is_volatile || ld->is_volatile || st->op != ld->op || st->width != ld->width ||
        !is_reg(&st->opnd[0]) || !is_reg(&ld->opnd[1]) || !same_operand(&st->opnd[1], &ld->opnd[0]))
        return false;
    if (st->opnd[1].kind != X86_OPND_MEM && st->opnd[1].kind != X86_OPND_RIP)
        return false;
    if (st->op == X86_MOV && (st->width == X86_L || st->width == X86_Q)) {
        ld->op = X86_MOV;
    } else if (st->op == X86_MOVSS || st->op == X86_MOVSD) {
        ld->op = X86_MOVAPS;
        ld->width = X86_Q;
    } else {
        return false;
    }
    set_operand(ld, 0, copy_operand(&st->opnd[0]));
    return true;
}

// cmp $0, R is test R, R; and X, T then test T, T (T dead after) is test X, T; mov $0,
// R is xor R, R where the flags are dead.
static bool short_form(const Live *lv, int bi, X86_Block *b, X86_Instr **link)
{
    X86_Instr *in = *link, *next = in->next;
    const X86_Operand *s = &in->opnd[0], *d = &in->opnd[1];
    if (in->op == X86_CMP && s->kind == X86_OPND_IMM && s->imm == 0 && is_reg(d)) {
        in->op = X86_TEST;
        set_operand(in, 0, *d);
        return true;
    }
    if (in->op == X86_AND && is_reg(d) && (s->kind == X86_OPND_IMM || is_reg(s)) && next &&
        next->op == X86_TEST && next->width == in->width && is_reg(&next->opnd[0]) &&
        is_reg(&next->opnd[1]) && next->opnd[0].reg == d->reg && next->opnd[1].reg == d->reg &&
        dead_after(lv, bi, next, bit(d->reg))) {
        set_operand(next, 0, copy_operand(s));
        delete_at(b, link);
        return true;
    }
    if (in->op == X86_MOV && s->kind == X86_OPND_IMM && s->imm == 0 && is_reg(d) &&
        !x86_is_xmm(d->reg) && (in->width == X86_L || in->width == X86_Q) &&
        dead_after(lv, bi, in, FLAGS)) {
        in->op    = X86_XOR;
        in->width = X86_L;
        set_operand(in, 0, x86_reg(d->reg, X86_L));
        set_operand(in, 1, x86_reg(d->reg, X86_L));
        return true;
    }
    return false;
}

//
// Branches
//
static bool jumps_to(const X86_Instr *in, const char *label)
{
    return in && (in->op == X86_J || in->op == X86_JMP) && label &&
           strcmp(in->opnd[0].sym, label) == 0;
}

static int label_refs(const X86_Func *fn, const char *label);

// Whether falling into block `b` reaches label `label`, through empty blocks.
static bool falls_to(const X86_Block *b, const char *label)
{
    for (; b; b = b->next) {
        if (b->label && strcmp(b->label, label) == 0)
            return true;
        if (b->head)
            return false;
    }
    return false;
}

// A block no jump goes to joins the one before it, or goes when that one never falls
// into it.
static bool merge_blocks(X86_Func *fn)
{
    bool changed = false;
    for (X86_Block *b = fn->blocks; b && b->next;) {
        X86_Block *c = b->next;
        if (c->label && label_refs(fn, c->label) > 0) {
            b = c;
            continue;
        }
        bool falls = !b->tail || (b->tail->op != X86_JMP && b->tail->op != X86_RET);
        if (falls && c->head) {
            if (b->tail)
                b->tail->next = c->head;
            else
                b->head = c->head;
            b->tail = c->tail;
        } else {
            for (X86_Instr *in = c->head; in;) {
                X86_Instr *next = in->next;
                free_instr(in);
                in = next;
            }
        }
        b->next = c->next;
        if (fn->tail == c)
            fn->tail = b;
        xfree(c->label);
        xfree(c);
        changed = true;
    }
    return changed;
}

// A jump to the next block goes, a branch over a jump branches the other way, and code
// after a jump or return in its block goes.
static bool rewrite_branches(X86_Func *fn)
{
    bool changed = false;
    for (X86_Block *b = fn->blocks; b; b = b->next) {
        for (X86_Instr *in = b->head; in; in = in->next) {
            if ((in->op == X86_JMP || in->op == X86_RET) && in->next) {
                while (in->next)
                    delete_instr(b, in->next);
                changed = true;
            }
        }
        const char *next = b->next ? b->next->label : NULL;
        if (b->tail && b->tail->op == X86_JMP && falls_to(b->next, b->tail->opnd[0].sym)) {
            delete_instr(b, b->tail);
            changed = true;
        }
        X86_Instr *j = NULL;
        for (X86_Instr *in = b->head; in && in->next; in = in->next)
            if (in->next == b->tail)
                j = in;
        if (j && j->op == X86_J && b->tail->op == X86_JMP && jumps_to(j, next)) {
            j->cond ^= 1;
            set_operand(j, 0, copy_operand(&b->tail->opnd[0]));
            delete_instr(b, b->tail);
            changed = true;
        }
    }
    return changed;
}

//
// cmov
//
static int label_refs(const X86_Func *fn, const char *label)
{
    int n = 0;
    for (const X86_Block *b = fn->blocks; b; b = b->next)
        for (const X86_Instr *in = b->head; in; in = in->next)
            n += jumps_to(in, label);
    return n;
}

// Whether `in` is a move into a general register of 16 bits or more that cmov can
// make conditional: from a register, or a slot (a load cmov may make whatever the
// condition, so only from memory that is always there).
static bool cmov_source(const X86_Instr *in)
{
    if (in->op != X86_MOV || in->width == X86_B || in->is_volatile || !is_reg(&in->opnd[1]) ||
        x86_is_xmm(in->opnd[1].reg))
        return false;
    const X86_Operand *s = &in->opnd[0];
    if (is_reg(s))
        return !x86_is_xmm(s->reg);
    return s->kind == X86_OPND_MEM && s->index < 0 &&
           (s->reg == X86_FRAME || s->reg == X86_RSP || s->reg == X86_RBP);
}

// Whether `in` puts a value into general register `d` and does nothing else: a move,
// or a zero by xor (which make_cmov turns into a move of $0, as it must keep the
// flags).
static bool plain_move(const X86_Instr *in, int d)
{
    if (in->op == X86_XOR && is_zero_idiom(in) && in->opnd[1].reg == d)
        return true;
    return in->op == X86_MOV && in->width != X86_B && is_reg(&in->opnd[1]) &&
           in->opnd[1].reg == d && !in->is_volatile &&
           (in->opnd[0].kind == X86_OPND_IMM || in->opnd[0].kind == X86_OPND_REG ||
            in->opnd[0].kind == X86_OPND_MEM || in->opnd[0].kind == X86_OPND_RIP);
}

// Whether operand `o` reads register `d`.
static bool reads_operand(const X86_Operand *o, int d)
{
    return (is_reg(o) && o->reg == d) || (address_regs(o) & bit(d));
}

// Whether the source of move `in` may be read on either path: a register, a constant,
// a static, or a slot (not memory through a pointer, which may not be valid there).
static bool safe_source(const X86_Instr *in)
{
    const X86_Operand *s = &in->opnd[0];
    if (in->op == X86_XOR || s->kind == X86_OPND_IMM || s->kind == X86_OPND_REG ||
        s->kind == X86_OPND_RIP)
        return true;
    return s->kind == X86_OPND_MEM && s->index < 0 &&
           (s->reg == X86_FRAME || s->reg == X86_RSP || s->reg == X86_RBP);
}

// Whether `in` puts a constant into register `d`: mov $k, or xor.
static bool const_move(const X86_Instr *in, int d)
{
    return plain_move(in, d) && (in->op == X86_XOR || in->opnd[0].kind == X86_OPND_IMM);
}

// Constant move `in` becomes cmov of r11 (`cc`), loaded with the constant first: the
// load, returned, goes ahead of it.  r11 must be dead there.
static X86_Instr *cmov_from_r11(X86_Instr *in, int cc)
{
    X86_Width w   = in->op == X86_XOR ? X86_L : in->width;
    int64_t k     = in->op == X86_XOR ? 0 : in->opnd[0].imm;
    X86_Instr *ld = xalloc(sizeof(X86_Instr), __func__, __FILE__, __LINE__);
    ld->op        = X86_MOV;
    ld->width     = w;
    ld->opnd[0]   = x86_imm(k);
    ld->opnd[1]   = x86_reg(X86_R11, w);
    in->op        = X86_CMOV;
    in->cond      = cc;
    in->width     = w;
    set_operand(in, 0, x86_reg(X86_R11, w));
    set_operand(in, 1, x86_reg(in->opnd[1].reg, w));
    ld->next = in;
    return ld;
}

// Append instructions `first`..`last` (linked) to block `a`.
static void append_seq(X86_Block *a, X86_Instr *first, X86_Instr *last)
{
    last->next = NULL;
    if (a->tail)
        a->tail->next = first;
    else
        a->head = first;
    a->tail = last;
}

// The only instruction of block `b`, or NULL.
static X86_Instr *only(X86_Block *b)
{
    return b && b->head && b->head == b->tail ? b->head : NULL;
}

// Unlink `from` and the instructions after it from block `b`.
static void detach_from(X86_Block *b, X86_Instr *from)
{
    X86_Instr **link = &b->head;
    while (*link != from)
        link = &(*link)->next;
    *link   = NULL;
    b->tail = NULL;
    for (X86_Instr *p = b->head; p; p = p->next)
        b->tail = p;
}

// A jcc L, then a move into D (nothing else) falling into L: cmov of the inverse
// condition.  Or, a diamond: jcc L, a move into D and a jump to M, then L moves into
// D and falls into M: one move made, the other made conditional.  The moves follow the
// jcc in its block, or start the next one when no other jump goes there; the jcc
// must be alone (an FP comparison may take two).  A constant is made conditional
// through r11, where that is dead.
static bool make_cmov(X86_Func *fn)
{
    Live lv;
    live_compute(&lv, fn, false);
    bool done = false;
    int ai    = -1;
    for (X86_Block *a = fn->blocks; a && !done; a = a->next) {
        ai++;
        // r11 is free in the blocks after `a`, up to where the arms join.
        Regs after_regs = 0;
        for (int k = 1; k <= 3 && ai + k < lv.nblocks; k++)
            after_regs |= lv.live_in[ai + k];
        bool r11_free = !(after_regs & bit(X86_R11));
        for (X86_Instr *j = a->head; j && !done; j = j->next) {
            if (j->op != X86_J || (j->next && j->next->op == X86_J))
                continue;
            bool pair = false;
            for (X86_Instr *p = a->head; p; p = p->next)
                if (p->next == j && p->op == X86_J)
                    pair = true;
            if (pair)
                continue;
            // The arm: what follows j in `a`, or else the next block.
            X86_Block *box = a, *after = a->next;
            X86_Instr *mv  = j->next;
            if (!mv) {
                box = a->next;
                if (!box || (box->label && label_refs(fn, box->label) > 0))
                    continue;
                mv    = box->head;
                after = box->next;
            }
            if (!mv || !is_reg(&mv->opnd[1]) || x86_is_xmm(mv->opnd[1].reg) || !after)
                continue;
            int d  = mv->opnd[1].reg;
            int cc = j->cond;
            // A triangle.
            if (mv == box->tail && jumps_to(j, after->label) &&
                (cmov_source(mv) || (r11_free && const_move(mv, d)))) {
                detach_from(box, mv);
                delete_instr(a, j);
                if (cmov_source(mv)) {
                    mv->op   = X86_CMOV;
                    mv->cond = cc ^ 1;
                    append_seq(a, mv, mv);
                } else {
                    append_seq(a, cmov_from_r11(mv, cc ^ 1), mv);
                }
                done = true;
                break;
            }
            // A diamond.
            X86_Instr *jmp = mv->next;
            X86_Block *e   = after;
            if (!jmp || jmp->op != X86_JMP || jmp != box->tail || !jumps_to(j, e->label) ||
                label_refs(fn, e->label) != 1 || !only(e) || !e->next ||
                !jumps_to(jmp, e->next->label))
                continue;
            X86_Instr *other = e->head;
            if (!plain_move(mv, d) || !plain_move(other, d) ||
                (mv->op == X86_MOV && other->op == X86_MOV && mv->width != other->width))
                continue;
            // One move made unconditionally, the other one conditionally after it: from a
            // register or slot that is not D, or else a constant through r11.
            X86_Instr *first = mv, *cond = other;
            int ccond        = cc; // the branch is taken to `other`
            bool via_r11     = false;
            if (!cmov_source(cond) || reads_operand(&cond->opnd[0], d)) {
                first = other;
                cond  = mv;
                ccond = cc ^ 1;
                if (!cmov_source(cond) || reads_operand(&cond->opnd[0], d)) {
                    if (!r11_free)
                        continue;
                    via_r11 = true;
                    if (!const_move(cond, d)) {
                        first = mv;
                        cond  = other;
                        ccond = cc;
                        if (!const_move(cond, d))
                            continue;
                    }
                }
            }
            if (!safe_source(first))
                continue;
            if (first->op == X86_XOR) {
                first->op = X86_MOV;
                set_operand(first, 0, x86_imm(0));
            }
            detach_from(box, mv);
            e->head = e->tail = NULL;
            delete_instr(a, j);
            free_instr(jmp);
            if (via_r11) {
                first->next = cmov_from_r11(cond, ccond);
            } else {
                cond->op    = X86_CMOV;
                cond->cond  = ccond;
                first->next = cond;
            }
            append_seq(a, first, cond);
            done = true;
        }
    }
    live_free(&lv);
    return done;
}

//
// The pass
//
static bool rewrite(const Live *lv, int bi, X86_Block *b, X86_Instr **link, X86_Instr *prev)
{
    return delete_noop(lv, bi, b, link, prev) || forward_move(lv, bi, b, link) ||
           compute_in_place(lv, bi, b, link) || fold_load(lv, bi, b, link) ||
           fold_address(lv, bi, b, link) || delete_reload(b, link) ||
           short_form(lv, bi, b, link);
}

void x86_peephole_func(X86_Func *fn)
{
    ret_regs     = fn->result_regs;
    ret_wide     = fn->result_wide;
    bool changed = true;
    while (changed) {
        changed = false;
        Live lv, halves;
        live_compute(&lv, fn, false);
        live_compute(&halves, fn, true);
        lv.halves = &halves;
        for (int bi = 0; bi < lv.nblocks; bi++) {
            X86_Block *b    = lv.blocks[bi];
            X86_Instr *prev = NULL;
            for (X86_Instr **link = &b->head; *link;) {
                if (rewrite(&lv, bi, b, link, prev)) {
                    changed = true;
                    continue;
                }
                prev = *link;
                link = &(*link)->next;
            }
        }
        live_free(&lv);
        live_free(&halves);
        if (!changed)
            changed = rewrite_branches(fn) || make_cmov(fn) || merge_blocks(fn);
    }
}
