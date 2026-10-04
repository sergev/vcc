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
        if (!find_slot(g, p->name))
            add_slot(g, p->name, p->type, msp_type_size(p->type), msp_type_align(p->type));
    }
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("msp430: %s: no type for %s", gen_name(g), p->name);
        if (find_slot(g, p->name))
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

Msp_Operand mem_at(const Gen *g, const char *name, int off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        Msp_Operand o = msp_indexed(MSP_SP, NULL, s->off + off);
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

void address_of(Gen *g, int reg, const char *name, int off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        emit2(g, MSP_MOV, msp_reg(MSP_SP), msp_reg(reg));
        Msp_Operand k = msp_imm(s->off + off);
        k.incoming    = s->incoming;
        if (k.imm != 0 || k.incoming)
            emit2(g, MSP_ADD, k, msp_reg(reg));
        return;
    }
    if (name[0] == '%')
        fatal_error("msp430: %s: no slot for %s", gen_name(g), name);
    emit2(g, MSP_MOV, msp_imm_sym(name, off), msp_reg(reg));
}

enum { UNROLL = 16 }; // the most moves of a copy unrolled

// A counted copy loop: `n` units from @r14+ to 0(r15), through r12, counted in r13.
static void copy_loop(Gen *g, int n, bool byte)
{
    char loop[32];
    new_label(loop);
    emit2(g, MSP_MOV, msp_imm(n), msp_reg(13));
    gen_label_block(g, loop);
    Msp_Instr *ld = emit2(g, MSP_MOV, msp_postinc(14), msp_reg(12));
    Msp_Instr *st = emit2(g, MSP_MOV, msp_reg(12), msp_indexed(15, NULL, 0));
    ld->byte = st->byte = byte;
    emit1(g, byte ? MSP_INC : MSP_INCD, msp_reg(15));
    emit1(g, MSP_DEC, msp_reg(13));
    emit1(g, MSP_JNE, msp_label(loop));
}

void copy_bytes(Gen *g, int size, int align)
{
    bool words = align >= 2 && size % 2 == 0;
    int unit = words ? 2 : 1, n = size / unit;
    if (n > UNROLL) {
        copy_loop(g, n, !words);
        return;
    }
    for (int k = 0; k < n; k++) {
        Msp_Operand src = k == 0 ? msp_ind(14) : msp_indexed(14, NULL, k * unit);
        Msp_Instr *in   = emit2(g, MSP_MOV, src, msp_indexed(15, NULL, k * unit));
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
        address_of(g, 14, src, soff);
        address_of(g, 15, dst, doff);
        copy_loop(g, n, !words);
        return;
    }
    for (int k = 0; k < n; k++) {
        Msp_Instr *in = emit2(g, MSP_MOV, mem_at(g, src, soff + k * unit),
                              mem_at(g, dst, doff + k * unit));
        in->byte      = !words;
    }
}

void extend_regs(Gen *g, int reg, int from, int n, bool sign)
{
    if (from >= n)
        return;
    if (!sign) {
        for (int i = from; i < n; i++)
            emit1(g, MSP_CLR, msp_reg(reg + i));
        return;
    }
    // The sign into C, then 0xffff + C complemented: 0x0000 or 0xffff.
    int top = reg + from;
    emit2(g, MSP_MOV, msp_reg(reg + from - 1), msp_reg(top));
    emit1(g, MSP_RLA, msp_reg(top));
    emit2(g, MSP_SUBC, msp_reg(top), msp_reg(top));
    emit1(g, MSP_INV, msp_reg(top));
    for (int i = from + 1; i < n; i++)
        emit2(g, MSP_MOV, msp_reg(top), msp_reg(reg + i));
}

static bool ext_sign(const Tac_Type *t, Ext ext)
{
    return ext == EXT_SIGN || (ext == EXT_TYPE && !msp_is_unsigned(t) && !msp_is_fp(t));
}

void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext)
{
    const Tac_Type *t = val_type(g, v);
    int size          = msp_type_size(t);
    bool sign         = ext_sign(t, ext);
    if (v->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_bits(v->u.constant);
        if (size < 8) {
            uint64_t mask = (1ull << (8 * size)) - 1;
            bool neg      = sign && (bits >> (8 * size - 1) & 1);
            bits          = neg ? bits | ~mask : bits & mask;
        }
        for (int i = 0; i < n; i++)
            emit2(g, MSP_MOV, msp_imm(i < 4 ? (int64_t)(bits >> (16 * i) & 0xffff) : 0),
                  msp_reg(reg + i));
        return;
    }
    if (size == 1) {
        emit2b(g, MSP_MOV, mem_at(g, v->u.var_name, 0), msp_reg(reg)); // zero-extends
        if (sign)
            emit1(g, MSP_SXT, msp_reg(reg));
        extend_regs(g, reg, 1, n, sign);
        return;
    }
    int w = size / 2, m = w < n ? w : n;
    for (int i = 0; i < m; i++)
        emit2(g, MSP_MOV, mem_at(g, v->u.var_name, 2 * i), msp_reg(reg + i));
    extend_regs(g, reg, m, n, sign);
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
    for (int i = 0; i < size / 2; i++) {
        if (i < n)
            emit2(g, MSP_MOV, msp_reg(reg + i), mem_at(g, v->u.var_name, 2 * i));
        else
            emit1(g, MSP_CLR, mem_at(g, v->u.var_name, 2 * i));
    }
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

    // Epilogue, the reverse.
    msp_new_block(g->fn, g->exit);
    if (g->frame_size)
        emit2(g, MSP_ADD, msp_imm(g->frame_size), msp_reg(MSP_SP));
    for (int r = 10; r >= 4; r--)
        if (saved & (1u << r))
            emit1(g, MSP_POP, msp_reg(r));
    emit0(g, MSP_RET);
}
