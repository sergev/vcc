//
// Types, frame slots, loading and storing values, the prologue and epilogue.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

int x86_size(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
        return 2;
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_FLOAT:
        return 4;
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
    case TAC_TYPE_DOUBLE:
        return 8;
    case TAC_TYPE_LONG_DOUBLE:
        return 16;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * x86_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 8;
}

int x86_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return x86_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return x86_size(t);
    }
}

bool x86_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE;
}

bool x86_is_double(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_DOUBLE;
}

bool x86_is_ld(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool x86_is_unsigned(const Tac_Type *t)
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

bool x86_is_aggregate(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_ARRAY || t->kind == TAC_TYPE_STRUCTURE;
}

X86_Width x86_width_of(int size)
{
    return size == 1 ? X86_B : size == 2 ? X86_W : size == 4 ? X86_L : X86_Q;
}

X86_Width x86_op_width(const Tac_Type *t)
{
    return x86_size(t) <= 4 ? X86_L : X86_Q;
}

static void add_global(Gen *g, const char *name, const Tac_Type *type)
{
    if (name && type)
        map_insert(&g->globals, name, (intptr_t)type, 0);
}

void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl)
{
    memset(g, 0, sizeof(*g));
    g->program  = program;
    g->tl       = tl;
    g->fn       = x86_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    x86_new_block(g->fn, NULL); // the body
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
    x86_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int offset)
{
    Slot *s   = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type   = type;
    s->offset = offset;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

int alloc_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align)
{
    if (align < 1)
        align = 1;
    g->locals_size = (g->locals_size + size + align - 1) / align * align;
    int offset     = -g->locals_size;
    if (name)
        insert_slot(g, name, type, offset);
    return offset;
}

void place_slot(Gen *g, const char *name, const Tac_Type *type, int offset)
{
    insert_slot(g, name, type, offset);
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
    intptr_t v;
    if (map_get(&g->globals, name, &v))
        return (const Tac_Type *)v;
    fatal_error("x86: %s: no type for %s", gen_name(g), name);
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

X86_Operand name_mem(const Gen *g, const char *name, int64_t off)
{
    const Slot *s = find_slot(g, name);
    if (s)
        return x86_mem(X86_RBP, s->offset + off);
    if (name[0] == '%')
        fatal_error("x86: %s: no slot for %s", gen_name(g), name);
    return x86_rip(name, off);
}

X86_Instr *emit0(Gen *g, X86_Op op, X86_Width width)
{
    return x86_append(g->fn, op, width);
}

X86_Instr *emit1(Gen *g, X86_Op op, X86_Width width, X86_Operand a)
{
    X86_Instr *in = emit0(g, op, width);
    in->opnd[0]   = a;
    return in;
}

X86_Instr *emit2(Gen *g, X86_Op op, X86_Width width, X86_Operand src, X86_Operand dst)
{
    X86_Instr *in = emit1(g, op, width, src);
    in->opnd[1]   = dst;
    return in;
}

// Zero is `xor` (shorter, and the flags are never live across a constant load); a
// 64-bit constant is `movl` when it fits 32 bits zero-extended, `movq` when it fits
// them sign-extended, and otherwise `movabsq`.
void gen_li(Gen *g, int reg, X86_Width width, int64_t imm)
{
    if (width == X86_Q && (uint64_t)imm <= UINT32_MAX)
        width = X86_L;
    if (imm == 0)
        emit2(g, X86_XOR, X86_L, x86_reg(reg, X86_L), x86_reg(reg, X86_L));
    else if (width != X86_Q || x86_imm32(imm))
        emit2(g, X86_MOV, width, x86_imm(width == X86_L ? (int32_t)imm : imm), x86_reg(reg, width));
    else
        emit2(g, X86_MOVABS, X86_Q, x86_imm(imm), x86_reg(reg, X86_Q));
}

int64_t const_value(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_INT:
        return (int32_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_SCHAR:
        return (int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_LONG:
        return c->u.long_val;
    case TAC_CONST_LONG_LONG:
        return c->u.long_long_val;
    case TAC_CONST_ULONG:
        return (int64_t)c->u.ulong_val;
    case TAC_CONST_ULONG_LONG:
        return (int64_t)c->u.ulong_long_val;
    default:
        fatal_error("x86: floating-point constant %d as an integer", c->kind);
    }
}

int64_t const_as(const Tac_Const *c, const Tac_Type *t)
{
    int64_t v = const_value(c);
    switch (x86_size(t)) {
    case 1:
        return x86_is_unsigned(t) ? (int64_t)(uint8_t)v : (int64_t)(int8_t)v;
    case 2:
        return x86_is_unsigned(t) ? (int64_t)(uint16_t)v : (int64_t)(int16_t)v;
    case 4:
        return x86_is_unsigned(t) ? (int64_t)(uint32_t)v : (int64_t)(int32_t)v;
    default:
        return v;
    }
}

void load_mem(Gen *g, int reg, const Tac_Type *t, X86_Operand m)
{
    if (x86_is_ld(t) || x86_is_aggregate(t) || x86_is_fp(t))
        fatal_error("x86: %s: a value of type %d in a general register", gen_name(g), t->kind);
    bool u = x86_is_unsigned(t);
    switch (x86_size(t)) {
    case 1:
        emit2(g, u ? X86_MOVZB : X86_MOVSB, X86_L, m, x86_reg(reg, X86_L));
        break;
    case 2:
        emit2(g, u ? X86_MOVZW : X86_MOVSW, X86_L, m, x86_reg(reg, X86_L));
        break;
    case 4:
        emit2(g, X86_MOV, X86_L, m, x86_reg(reg, X86_L));
        break;
    default:
        emit2(g, X86_MOV, X86_Q, m, x86_reg(reg, X86_Q));
        break;
    }
}

void store_mem(Gen *g, int reg, const Tac_Type *t, X86_Operand m)
{
    if (x86_is_ld(t) || x86_is_aggregate(t) || x86_is_fp(t))
        fatal_error("x86: %s: a value of type %d in a general register", gen_name(g), t->kind);
    X86_Width w = x86_width_of(x86_size(t));
    emit2(g, X86_MOV, w, x86_reg(reg, w), m);
}

void load_val(Gen *g, int reg, const Tac_Val *v)
{
    const Tac_Type *t = val_type(g, v);
    if (v->kind == TAC_VAL_CONSTANT)
        gen_li(g, reg, x86_op_width(t), const_value(v->u.constant));
    else
        load_mem(g, reg, t, name_mem(g, v->u.var_name, 0));
}

void store_val(Gen *g, int reg, const Tac_Val *v)
{
    store_mem(g, reg, val_type(g, v), name_mem(g, v->u.var_name, 0));
}

void load_int_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *t)
{
    if (v->kind == TAC_VAL_CONSTANT)
        gen_li(g, reg, x86_op_width(t), const_as(v->u.constant, t));
    else
        load_val(g, reg, v);
}

X86_Operand src_operand(Gen *g, const Tac_Val *v, const Tac_Type *t, int scratch)
{
    X86_Width w = x86_op_width(t);
    if (v->kind == TAC_VAL_CONSTANT) {
        int64_t imm = const_as(v->u.constant, t);
        if (w == X86_L)
            return x86_imm((int32_t)imm);
        if (x86_imm32(imm))
            return x86_imm(imm);
        gen_li(g, scratch, w, imm);
        return x86_reg(scratch, w);
    }
    const Tac_Type *vt = val_type(g, v);
    if (x86_size(vt) == (w == X86_L ? 4 : 8))
        return name_mem(g, v->u.var_name, 0);
    load_val(g, scratch, v);
    return x86_reg(scratch, w);
}

void gen_epilogue(Gen *g)
{
    emit0(g, X86_EPILOGUE, X86_Q);
    emit0(g, X86_RET, X86_Q);
}

static bool uses_reg(const X86_Instr *in, int reg)
{
    for (int i = 0; i < X86_MAX_OPERANDS; i++) {
        const X86_Operand *o = &in->opnd[i];
        if ((o->kind == X86_OPND_REG || o->kind == X86_OPND_MEM || o->kind == X86_OPND_INDIRECT) &&
            (o->reg == reg || (o->kind == X86_OPND_MEM && o->index == reg)))
            return true;
    }
    return false;
}

// Whether the body needs no frame: it makes no call (which needs rsp 16-byte aligned)
// and never uses rbp (no slot, no stack argument) or rsp.
static bool is_leaf(const Gen *g)
{
    for (const X86_Block *b = g->fn->blocks; b; b = b->next)
        for (const X86_Instr *in = b->head; in; in = in->next)
            if (in->op == X86_CALL || uses_reg(in, X86_RBP) || uses_reg(in, X86_RSP))
                return false;
    return true;
}

// Replace each epilogue marker by `leave`, or drop it.
static void expand_epilogues(Gen *g, bool frame)
{
    for (X86_Block *b = g->fn->blocks; b; b = b->next) {
        b->tail = NULL;
        for (X86_Instr **link = &b->head; *link;) {
            X86_Instr *in = *link;
            if (in->op == X86_EPILOGUE && !frame) {
                *link = in->next;
                xfree(in);
                continue;
            }
            if (in->op == X86_EPILOGUE)
                in->op = X86_LEAVE;
            b->tail = in;
            link    = &in->next;
        }
    }
}

// Fill the prologue and the epilogues, now that the frame is known.  A leaf function
// that needs no stack has none; otherwise rbp is pushed, which leaves rsp 16-byte
// aligned, and the slots and outgoing area are reserved in a multiple of 16 below it.
void gen_prologue(Gen *g)
{
    bool frame = !is_leaf(g);
    if (frame) {
        X86_Block *tail = g->fn->tail;
        g->fn->tail     = g->prologue;
        int size        = (g->locals_size + g->outgoing + 15) / 16 * 16;
        emit1(g, X86_PUSH, X86_Q, x86_reg(X86_RBP, X86_Q));
        emit2(g, X86_MOV, X86_Q, x86_reg(X86_RSP, X86_Q), x86_reg(X86_RBP, X86_Q));
        if (size)
            emit2(g, X86_SUB, X86_Q, x86_imm(size), x86_reg(X86_RSP, X86_Q));
        g->fn->tail = tail;
    }
    expand_epilogues(g, frame);
}
