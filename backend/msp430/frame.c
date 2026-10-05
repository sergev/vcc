//
// Types, the frame, and access to values in memory.
//
#include <stdio.h>
#include <string.h>

#include "float128.h"
#include "internal.h"
#include "xalloc.h"

int msp_type_size(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return 2;
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_FLOAT:
        return 4;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
        return 8;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * msp_type_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 2;
}

int msp_type_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_ARRAY:
        return msp_type_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 1 ? 2 : 1;
    default:
        return 2;
    }
}

bool msp_is_unsigned(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_POINTER:
        return true;
    default:
        return false;
    }
}

bool msp_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool msp_is_scalar(const Tac_Type *t)
{
    return t->kind != TAC_TYPE_ARRAY && t->kind != TAC_TYPE_STRUCTURE;
}

int msp_words(const Tac_Type *t)
{
    return (msp_type_size(t) + 1) / 2;
}

static int unit_labels;

void gen_unit_begin(void)
{
    unit_labels = 0;
}

void new_label(char buf[32])
{
    snprintf(buf, 32, ".Lv%d", unit_labels++);
}

static void add_global(Gen *g, const char *name, const Tac_Type *type)
{
    if (type)
        map_insert(&g->globals, name, (intptr_t)type, 0);
}

void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl)
{
    memset(g, 0, sizeof(*g));
    g->program  = program;
    g->tl       = tl;
    g->fn       = msp_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    msp_new_block(g->fn, NULL); // the body
    new_label(g->exit);
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->regs);
    map_init(&g->dead);
    for (const Tac_TopLevel *t = program; t; t = t->next) {
        switch (t->kind) {
        case TAC_TOPLEVEL_FUNCTION:
            add_global(g, t->u.function.name, t->u.function.type);
            for (const Tac_StaticLocal *s = t->u.function.static_locals; s; s = s->next)
                add_global(g, s->name, s->type);
            break;
        case TAC_TOPLEVEL_STATIC_VARIABLE:
            add_global(g, t->u.static_variable.name, t->u.static_variable.type);
            break;
        case TAC_TOPLEVEL_STATIC_CONSTANT:
            add_global(g, t->u.static_constant.name, t->u.static_constant.type);
            break;
        case TAC_TOPLEVEL_EXTERN:
            add_global(g, t->u.extern_.name, t->u.extern_.type);
            break;
        }
    }
}

static void free_slot(intptr_t p)
{
    xfree((void *)p);
}

void gen_done(Gen *g)
{
    map_destroy_free(&g->frame, free_slot);
    map_destroy(&g->globals);
    map_destroy(&g->regs);
    map_destroy(&g->dead);
    msp_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int off, bool incoming)
{
    Slot *s     = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type     = type;
    s->off      = off;
    s->incoming = incoming;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

void place_stack_param(Gen *g, const char *name, const Tac_Type *type, int off)
{
    insert_slot(g, name, type, off, true);
}

static void free_nothing(intptr_t p)
{
    (void)p;
}

// A slot for `name` of `size` bytes aligned to `align`, above the ones placed so far.
static void add_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align)
{
    if (align > 1)
        g->frame_size = (g->frame_size + 1) & ~1;
    insert_slot(g, name, type, g->frame_size, false);
    g->frame_size += size;
}

void layout_frame(Gen *g)
{
    StringMap allocs;
    map_init(&allocs);
    g->out_size = 0;
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next) {
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);
        int out = instr_out_size(g, in);
        if (out > g->out_size)
            g->out_size = out;
    }
    g->frame_size = g->out_size;

    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("msp430: %s: no type for %s", gen_name(g), p->name);
        if (find_slot(g, p->name) || var_reg(g, p->name, 0))
            continue;
        // A structure parameter's slot first holds its address (store_params).
        int size = msp_type_size(p->type), align = msp_type_align(p->type);
        if (!msp_is_scalar(p->type)) {
            size  = size < 2 ? 2 : size;
            align = 2;
        }
        add_slot(g, p->name, p->type, size, align);
    }
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("msp430: %s: no type for %s", gen_name(g), p->name);
        if (find_slot(g, p->name) || var_reg(g, p->name, 0))
            continue;
        int size = msp_type_size(p->type), align = msp_type_align(p->type);
        intptr_t v;
        if (map_get(&allocs, p->name, &v)) {
            const Tac_Instruction *in = (const Tac_Instruction *)v;
            if (in->u.allocate_local.size > size)
                size = in->u.allocate_local.size;
            if (in->u.allocate_local.alignment > align)
                align = in->u.allocate_local.alignment;
        }
        add_slot(g, p->name, p->type, size, align);
    }
    map_destroy_free(&allocs, free_nothing);
    g->frame_size = (g->frame_size + 1) & ~1;
}

const Slot *find_slot(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->frame, name, &v) ? (const Slot *)v : NULL;
}

const Tac_Type *name_type(const Gen *g, const char *name)
{
    const Slot *s = find_slot(g, name);
    if (s)
        return s->type;
    if (name[0] == '%') { // before layout_frame, which asks for the outgoing area first
        for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next)
            if (strcmp(p->name, name) == 0 && p->type)
                return p->type;
        for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next)
            if (strcmp(p->name, name) == 0 && p->type)
                return p->type;
    }
    intptr_t v;
    if (map_get(&g->globals, name, &v))
        return (const Tac_Type *)v;
    fatal_error("msp430: %s: no type for %s", gen_name(g), name);
}

const Tac_Type *val_type(const Gen *g, const Tac_Val *v)
{
    static const Tac_Type types[] = {
        [TAC_CONST_INT]         = { .kind = TAC_TYPE_INT },
        [TAC_CONST_LONG]        = { .kind = TAC_TYPE_LONG },
        [TAC_CONST_LONG_LONG]   = { .kind = TAC_TYPE_LONG_LONG },
        [TAC_CONST_UINT]        = { .kind = TAC_TYPE_UINT },
        [TAC_CONST_ULONG]       = { .kind = TAC_TYPE_ULONG },
        [TAC_CONST_ULONG_LONG]  = { .kind = TAC_TYPE_ULONG_LONG },
        [TAC_CONST_FLOAT]       = { .kind = TAC_TYPE_FLOAT },
        [TAC_CONST_DOUBLE]      = { .kind = TAC_TYPE_DOUBLE },
        [TAC_CONST_LONG_DOUBLE] = { .kind = TAC_TYPE_LONG_DOUBLE },
        [TAC_CONST_SCHAR]       = { .kind = TAC_TYPE_SCHAR },
        [TAC_CONST_UCHAR]       = { .kind = TAC_TYPE_UCHAR },
    };
    if (v->kind == TAC_VAL_CONSTANT)
        return &types[v->u.constant->kind];
    return name_type(g, v->u.var_name);
}

Msp_Instr *emit0(Gen *g, Msp_Op op)
{
    return msp_append(g->fn, op);
}

Msp_Instr *emit1(Gen *g, Msp_Op op, Msp_Operand a)
{
    Msp_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

Msp_Instr *emit2(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b)
{
    Msp_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

Msp_Instr *emit1b(Gen *g, Msp_Op op, Msp_Operand a)
{
    Msp_Instr *in = emit1(g, op, a);
    in->byte      = true;
    return in;
}

Msp_Instr *emit2b(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b)
{
    Msp_Instr *in = emit2(g, op, a, b);
    in->byte      = true;
    return in;
}

uint64_t const_bits(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_SCHAR:
        return (uint64_t)(int64_t)(signed char)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_INT:
        return (uint64_t)(int64_t)(int16_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint16_t)c->u.uint_val;
    case TAC_CONST_LONG:
        return (uint64_t)(int64_t)(int32_t)c->u.long_val;
    case TAC_CONST_ULONG:
        return (uint32_t)c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        return c->u.ulong_long_val;
    case TAC_CONST_FLOAT: {
        float f = (float)c->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    case TAC_CONST_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, &c->u.double_val, 8);
        return bits;
    }
    case TAC_CONST_LONG_DOUBLE: {
        double d = f128_to_double(c->u.long_double_val);
        uint64_t bits;
        memcpy(&bits, &d, 8);
        return bits;
    }
    }
    return 0;
}

int var_reg(const Gen *g, const char *name, int word)
{
    intptr_t v;
    if (!map_get(&g->regs, name, &v))
        return 0;
    return word == 0 ? (int)(v & 0xff) : word == 1 ? (int)(v >> 8) : 0;
}

Msp_Operand mem_at(const Gen *g, const char *name, int off)
{
    int reg = var_reg(g, name, 0);
    if (reg) {
        if (off & 1)
            fatal_error("msp430: %s: byte %d of register variable %s", gen_name(g), off, name);
        reg = var_reg(g, name, off / 2);
        if (!reg)
            fatal_error("msp430: %s: word %d of register variable %s", gen_name(g), off / 2,
                        name);
        return msp_reg(reg);
    }
    const Slot *s = find_slot(g, name);
    if (s) {
        Msp_Operand o = msp_indexed(MSP_SP, NULL, s->off + off + g->sp_bias);
        o.incoming    = s->incoming;
        return o;
    }
    if (name[0] == '%')
        fatal_error("msp430: %s: no slot for %s", gen_name(g), name);
    return msp_abs(name, off);
}

Msp_Operand incoming_at(int off)
{
    Msp_Operand o = msp_indexed(MSP_SP, NULL, off);
    o.incoming    = true;
    return o;
}

Msp_Operand val_word(const Gen *g, const Tac_Val *v, int i)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_bits(v->u.constant);
        int size      = msp_type_size(val_type(g, v));
        return msp_imm(size == 1 ? (int64_t)(bits & 0xff)
                                 : (int64_t)(i < 4 ? (bits >> (16 * i)) & 0xffff : 0));
    }
    return mem_at(g, v->u.var_name, 2 * i);
}

static bool same_sym(const char *a, const char *b)
{
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

bool same_opnd(const Msp_Operand *a, const Msp_Operand *b)
{
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case MSP_OPND_REG:
        return a->reg == b->reg;
    case MSP_OPND_INDEXED:
        return a->reg == b->reg && a->imm == b->imm && a->incoming == b->incoming &&
               same_sym(a->sym, b->sym);
    case MSP_OPND_ABS:
        return a->imm == b->imm && same_sym(a->sym, b->sym);
    default:
        return false;
    }
}

bool opnd_uses_reg(const Msp_Operand *o, int reg)
{
    switch (o->kind) {
    case MSP_OPND_REG:
    case MSP_OPND_INDEXED:
    case MSP_OPND_IND:
    case MSP_OPND_POSTINC:
        return o->reg == reg;
    default:
        return false;
    }
}

Msp_Operand high_byte(const Msp_Operand *o)
{
    Msp_Operand h = *o;
    if (h.kind != MSP_OPND_INDEXED && h.kind != MSP_OPND_ABS)
        fatal_error("msp430: the high byte of a register");
    h.sym = h.sym ? xstrdup(h.sym) : NULL;
    h.imm++;
    return h;
}

void address_of(Gen *g, Msp_Operand dst, const char *name, int off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        emit2(g, MSP_MOV, msp_reg(MSP_SP), dst);
        Msp_Operand k = msp_imm(s->off + off + g->sp_bias);
        k.incoming    = s->incoming;
        if (k.imm != 0 || k.incoming)
            emit2(g, MSP_ADD, k, msp_copy(&dst));
        return;
    }
    if (name[0] == '%' || var_reg(g, name, 0))
        fatal_error("msp430: %s: no slot for %s", gen_name(g), name);
    emit2(g, MSP_MOV, msp_imm_sym(name, off), dst);
}

enum { UNROLL = 16 }; // the most moves of a copy unrolled

// A counted copy loop: `n` units from @r15 to @r14 (`load`), or from @r14 to @r15,
// counted in r13; r13 and r14 pushed around it, r14 pointed at `name` + off.
static void copy_loop(Gen *g, bool load, const char *name, int off, int n, bool byte)
{
    emit1(g, MSP_PUSH, msp_reg(14));
    emit1(g, MSP_PUSH, msp_reg(13));
    g->sp_bias += 4;
    address_of(g, msp_reg(14), name, off);
    g->sp_bias -= 4;
    emit2(g, MSP_MOV, msp_imm(n), msp_reg(13));
    char loop[32];
    new_label(loop);
    gen_label_block(g, loop);
    int from = load ? MSP_SCRATCH : 14, to = load ? 14 : MSP_SCRATCH;
    Msp_Instr *in = emit2(g, MSP_MOV, msp_ind(from), msp_indexed(to, NULL, 0));
    in->byte      = byte;
    emit1(g, byte ? MSP_INC : MSP_INCD, msp_reg(MSP_SCRATCH));
    emit1(g, byte ? MSP_INC : MSP_INCD, msp_reg(14));
    emit1(g, MSP_DEC, msp_reg(13));
    emit1(g, MSP_JNE, msp_label(loop));
    emit1(g, MSP_POP, msp_reg(13));
    emit1(g, MSP_POP, msp_reg(14));
}

void copy_ptr(Gen *g, bool load, int ptr, const char *name, int off, int size, int align)
{
    if (off & 1)
        align = 1;
    bool words = align >= 2 && size % 2 == 0;
    int unit = words ? 2 : 1, n = size / unit;
    if (n > UNROLL) {
        if (ptr != MSP_SCRATCH)
            emit2(g, MSP_MOV, msp_reg(ptr), msp_reg(MSP_SCRATCH));
        copy_loop(g, load, name, off, n, !words);
        return;
    }
    for (int k = 0; k < n; k++) {
        Msp_Operand at  = msp_indexed(ptr, NULL, k * unit);
        Msp_Operand obj = mem_at(g, name, off + k * unit);
        Msp_Instr *in   = load ? emit2(g, MSP_MOV, at, obj) : emit2(g, MSP_MOV, obj, at);
        in->byte        = !words;
    }
}

void copy_named(Gen *g, const char *dst, int doff, const char *src, int soff, int size,
                int align)
{
    if ((doff | soff) & 1)
        align = 1;
    bool words = align >= 2 && size % 2 == 0;
    int unit = words ? 2 : 1, n = size / unit;
    if (n > UNROLL) {
        address_of(g, msp_reg(MSP_SCRATCH), src, soff);
        copy_loop(g, true, dst, doff, n, !words);
        return;
    }
    for (int k = 0; k < n; k++) {
        Msp_Instr *in = emit2(g, MSP_MOV, mem_at(g, src, soff + k * unit),
                              mem_at(g, dst, doff + k * unit));
        in->byte      = !words;
    }
}

// Whether a pending move other than `skip` reads `o`.
static bool is_read(const Move *m, int n, int skip, const Msp_Operand *o)
{
    for (int i = 0; i < n; i++)
        if (i != skip && same_opnd(&m[i].src, o))
            return true;
    return false;
}

// Remove move i, keeping the others in order.
static void drop_move(Move *m, int *n, int i)
{
    xfree(m[i].dst.sym);
    xfree(m[i].src.sym);
    memmove(&m[i], &m[i + 1], (size_t)(--*n - i) * sizeof(Move));
}

void parallel_moves(Gen *g, Move *m, int n)
{
    for (int i = 0; i < n;) {
        if (same_opnd(&m[i].dst, &m[i].src))
            drop_move(m, &n, i);
        else
            i++;
    }
    while (n > 0) {
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++)
            if (!is_read(m, n, i, &m[i].dst))
                pick = i;
        if (pick >= 0) {
            Msp_Instr *in = emit2(g, MSP_MOV, msp_copy(&m[pick].src), msp_copy(&m[pick].dst));
            in->byte      = m[pick].byte;
            drop_move(m, &n, pick);
            continue;
        }
        // A cycle: swap the first destination with its source; the moves that read the
        // destination now read the source.
        Msp_Operand *a = &m[0].dst, *b = &m[0].src;
        bool byte      = m[0].byte && (a->kind != MSP_OPND_REG || b->kind != MSP_OPND_REG);
        emit2(g, MSP_XOR, msp_copy(b), msp_copy(a))->byte = byte;
        emit2(g, MSP_XOR, msp_copy(a), msp_copy(b))->byte = byte;
        emit2(g, MSP_XOR, msp_copy(b), msp_copy(a))->byte = byte;
        for (int i = 1; i < n; i++)
            if (same_opnd(&m[i].src, a)) {
                xfree(m[i].src.sym);
                m[i].src = msp_copy(b);
            }
        drop_move(m, &n, 0);
        for (int i = 0; i < n;) {
            if (same_opnd(&m[i].dst, &m[i].src))
                drop_move(m, &n, i); // the swap did it
            else
                i++;
        }
    }
}

void extend_words(Gen *g, const Msp_Operand *w, int from, int n, bool sign)
{
    if (from >= n)
        return;
    if (!sign) {
        for (int i = from; i < n; i++)
            emit1(g, MSP_CLR, msp_copy(&w[i]));
        return;
    }
    // The sign into C, then 0xffff + C complemented: 0x0000 or 0xffff.
    const Msp_Operand *top = &w[from];
    emit2(g, MSP_MOV, msp_copy(&w[from - 1]), msp_copy(top));
    emit1(g, MSP_RLA, msp_copy(top));
    emit2(g, MSP_SUBC, msp_copy(top), msp_copy(top));
    emit1(g, MSP_INV, msp_copy(top));
    for (int i = from + 1; i < n; i++)
        emit2(g, MSP_MOV, msp_copy(top), msp_copy(&w[i]));
}

bool ext_sign(const Tac_Type *t, Ext ext)
{
    return ext == EXT_SIGN || (ext == EXT_TYPE && !msp_is_unsigned(t) && !msp_is_fp(t));
}

void load_vals(Gen *g, const Load *l, int n)
{
    Move m[16];
    int k = 0;
    for (int j = 0; j < n; j++) {
        const Tac_Type *t = val_type(g, l[j].v);
        int size          = msp_type_size(t);
        if (l[j].v->kind == TAC_VAL_CONSTANT) {
            // The bits extended as asked, word by word.
            uint64_t bits = const_bits(l[j].v->u.constant);
            if (size < 8) {
                uint64_t mask = (1ull << (8 * size)) - 1;
                bool neg      = ext_sign(t, l[j].ext) && (bits >> (8 * size - 1) & 1);
                bits          = neg ? bits | ~mask : bits & mask;
            }
            for (int i = 0; i < l[j].n; i++)
                m[k++] = (Move){ msp_reg(l[j].reg + i),
                                 msp_imm(i < 4 ? (int64_t)(bits >> (16 * i) & 0xffff) : 0),
                                 false };
            continue;
        }
        int w = size == 1 ? 1 : size / 2;
        for (int i = 0; i < w && i < l[j].n; i++)
            m[k++] = (Move){ msp_reg(l[j].reg + i), val_word(g, l[j].v, i), size == 1 };
    }
    parallel_moves(g, m, k);
    // The words past a narrower variable; a char from a register has its high byte to
    // clear.
    for (int j = 0; j < n; j++) {
        const Tac_Val *v = l[j].v;
        if (v->kind != TAC_VAL_VAR)
            continue;
        const Tac_Type *t = val_type(g, v);
        int size = msp_type_size(t), reg = l[j].reg;
        bool sign = ext_sign(t, l[j].ext);
        if (size == 1) {
            if (sign)
                emit1(g, MSP_SXT, msp_reg(reg));
            else if (var_reg(g, v->u.var_name, 0))
                emit2b(g, MSP_MOV, msp_reg(reg), msp_reg(reg));
        }
        Msp_Operand w[4];
        for (int i = 0; i < l[j].n; i++)
            w[i] = msp_reg(reg + i);
        extend_words(g, w, size == 1 ? 1 : size / 2, l[j].n, sign);
    }
}

void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext)
{
    Load l = { v, reg, n, ext };
    load_vals(g, &l, 1);
}

void store_val(Gen *g, const Tac_Val *v, int reg, int n)
{
    if (v->kind != TAC_VAL_VAR)
        fatal_error("msp430: %s: store to a constant", gen_name(g));
    int size = msp_type_size(val_type(g, v));
    if (size == 1) {
        emit2b(g, MSP_MOV, msp_reg(reg), mem_at(g, v->u.var_name, 0));
        return;
    }
    Move m[4];
    int k = 0;
    for (int i = 0; i < size / 2 && i < n; i++)
        m[k++] = (Move){ mem_at(g, v->u.var_name, 2 * i), msp_reg(reg + i), false };
    parallel_moves(g, m, k);
    for (int i = n; i < size / 2; i++)
        emit1(g, MSP_CLR, mem_at(g, v->u.var_name, 2 * i));
}

void gen_label_block(Gen *g, const char *label)
{
    msp_new_block(g->fn, label);
}

// The call-saved registers r4-r10 the body uses, as a bit mask.
static unsigned saved_regs(const Gen *g)
{
    unsigned mask = 0;
    for (const Msp_Block *b = g->fn->blocks; b; b = b->next)
        for (const Msp_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < MSP_MAX_OPERANDS; i++) {
                const Msp_Operand *o = &in->opnd[i];
                if (o->kind != MSP_OPND_NONE && o->kind != MSP_OPND_IMM &&
                    o->kind != MSP_OPND_ABS && o->kind != MSP_OPND_LABEL && o->reg >= 4 &&
                    o->reg <= 10)
                    mask |= 1u << o->reg;
            }
    return mask;
}

// Complete every offset into the incoming arguments: they lie above the frame, the
// saved registers and the return address.
static void complete_incoming(Gen *g, int base)
{
    for (Msp_Block *b = g->fn->blocks; b; b = b->next)
        for (Msp_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < MSP_MAX_OPERANDS; i++)
                if (in->opnd[i].incoming) {
                    in->opnd[i].imm += base;
                    in->opnd[i].incoming = false;
                }
}

void gen_frame(Gen *g)
{
    unsigned saved = saved_regs(g);
    int nsaved     = 0;
    for (int r = 4; r <= 10; r++)
        if (saved & (1u << r)) {
            msp_append_to(g->prologue, MSP_PUSH)->opnd[0] = msp_reg(r);
            nsaved++;
        }
    if (g->frame_size) {
        Msp_Instr *in = msp_append_to(g->prologue, MSP_SUB);
        in->opnd[0]   = msp_imm(g->frame_size);
        in->opnd[1]   = msp_reg(MSP_SP);
    }
    complete_incoming(g, g->frame_size + 2 * nsaved + 2);

    // Frameless: the epilogue is a bare ret, which every early return does in place.
    if (nsaved == 0 && g->frame_size == 0)
        for (Msp_Block *b = g->fn->blocks; b; b = b->next)
            for (Msp_Instr *in = b->head; in; in = in->next)
                if (in->op == MSP_JMP && strcmp(in->opnd[0].sym, g->exit) == 0) {
                    in->op = MSP_RET;
                    xfree(in->opnd[0].sym);
                    in->opnd[0] = (Msp_Operand){ 0 };
                }

    // Epilogue, the reverse.
    msp_new_block(g->fn, g->exit);
    if (g->frame_size)
        emit2(g, MSP_ADD, msp_imm(g->frame_size), msp_reg(MSP_SP));
    for (int r = 10; r >= 4; r--)
        if (saved & (1u << r))
            emit1(g, MSP_POP, msp_reg(r));
    emit0(g, MSP_RET);
}
