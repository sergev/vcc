//
// Types, frame slots, loading and storing values, the prologue and epilogue.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

int a64_size(const Tac_Type *t)
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
        return aarch64_darwin ? 8 : 16; // Apple: long double is double
    case TAC_TYPE_ARRAY:
        return t->u.array.size * a64_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 8;
}

int a64_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return a64_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return a64_size(t);
    }
}

// Under Apple's ABI long double is double: an FP type, never a binary128 one.
bool a64_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || a64_is_double(t);
}

bool a64_is_double(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_DOUBLE || (aarch64_darwin && t->kind == TAC_TYPE_LONG_DOUBLE);
}

bool a64_is_ld(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_LONG_DOUBLE && !aarch64_darwin;
}

bool a64_is_unsigned(const Tac_Type *t)
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

bool a64_is_aggregate(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_ARRAY || t->kind == TAC_TYPE_STRUCTURE;
}

A64_Width a64_width(const Tac_Type *t)
{
    if (a64_is_ld(t))
        return A64_Q;
    if (a64_is_fp(t))
        return a64_is_double(t) ? A64_D : A64_S;
    return a64_size(t) <= 4 ? A64_W : A64_X;
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
    g->fn       = a64_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    a64_new_block(g->fn, NULL); // the body
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->regs);
    map_init(&g->dead);
    map_init(&g->defined);
    for (const Tac_TopLevel *t = program; t; t = t->next) {
        switch (t->kind) {
        case TAC_TOPLEVEL_FUNCTION:
            add_global(g, t->u.function.name, t->u.function.type);
            map_insert(&g->defined, t->u.function.name, 1, 0);
            for (const Tac_StaticLocal *s = t->u.function.static_locals; s; s = s->next) {
                add_global(g, s->name, s->type);
                map_insert(&g->defined, s->name, 1, 0);
            }
            break;
        case TAC_TOPLEVEL_STATIC_VARIABLE:
            add_global(g, t->u.static_variable.name, t->u.static_variable.type);
            map_insert(&g->defined, t->u.static_variable.name, 1, 0);
            break;
        case TAC_TOPLEVEL_STATIC_CONSTANT:
            add_global(g, t->u.static_constant.name, t->u.static_constant.type);
            map_insert(&g->defined, t->u.static_constant.name, 1, 0);
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
    map_destroy(&g->defined);
    a64_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int offset, int reg)
{
    Slot *s   = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type   = type;
    s->offset = offset;
    s->reg    = reg;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

int alloc_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align)
{
    if (align < 1)
        align = 1;
    if (align > g->max_align)
        g->max_align = align;
    g->locals_size = (g->locals_size + size + align - 1) / align * align;
    int offset     = -g->locals_size;
    if (name)
        insert_slot(g, name, type, offset, 0);
    return offset;
}

void place_slot(Gen *g, const char *name, const Tac_Type *type, int offset)
{
    insert_slot(g, name, type, offset, 0);
}

void place_reg(Gen *g, const char *name, const Tac_Type *type, int reg)
{
    insert_slot(g, name, type, 0, reg);
}

const Slot *find_slot(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->frame, name, &v) ? (const Slot *)v : NULL;
}

int assigned_reg(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->regs, name, &v) ? (int)v : 0;
}

int var_reg(const Gen *g, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_VAR)
        return 0;
    const Slot *s = find_slot(g, v->u.var_name);
    return s ? s->reg : 0;
}

const Tac_Type *name_type(const Gen *g, const char *name)
{
    const Slot *s = find_slot(g, name);
    if (s)
        return s->type;
    intptr_t v;
    if (map_get(&g->globals, name, &v))
        return (const Tac_Type *)v;
    fatal_error("aarch64: %s: no type for %s", gen_name(g), name);
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

A64_Instr *emit1(Gen *g, A64_Op op, A64_Operand a)
{
    A64_Instr *in = a64_append(g->fn, op);
    in->opnd[0]   = a;
    return in;
}

A64_Instr *emit2(Gen *g, A64_Op op, A64_Operand a, A64_Operand b)
{
    A64_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

A64_Instr *emit3(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c)
{
    A64_Instr *in = emit2(g, op, a, b);
    in->opnd[2]   = c;
    return in;
}

A64_Instr *emit4(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c, A64_Operand d)
{
    A64_Instr *in = emit3(g, op, a, b, c);
    in->opnd[3]   = d;
    return in;
}

// A constant takes one `mov` when all but one of its 16-bit chunks are zero (movz), or
// all ones (movn); otherwise a movz or movn, whichever leaves fewer chunks, and a movk
// for each of the rest.
void gen_li(Gen *g, int reg, A64_Width width, int64_t imm)
{
    int n      = width == A64_W ? 2 : 4;
    uint64_t v = width == A64_W ? (uint32_t)imm : (uint64_t)imm;
    int zeros = 0, ones = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = (v >> (16 * i)) & 0xffff;
        zeros += c == 0;
        ones += c == 0xffff;
    }
    if (zeros >= n - 1 || ones >= n - 1) {
        emit2(g, A64_MOV, a64_reg(reg, width), a64_imm(width == A64_W ? (int32_t)v : (int64_t)v));
        return;
    }
    unsigned fill = ones > zeros ? 0xffff : 0;
    bool first    = true;
    for (int i = 0; i < n; i++) {
        unsigned c = (v >> (16 * i)) & 0xffff;
        if (c == fill)
            continue;
        A64_Op op = !first ? A64_MOVK : fill ? A64_MOVN : A64_MOVZ;
        A64_Instr *mv =
            emit2(g, op, a64_reg(reg, width), a64_imm(op == A64_MOVN ? ~c & 0xffff : c));
        if (i > 0)
            mv->opnd[2] = a64_lsl(16 * i);
        first = false;
    }
}

// An ldr/str takes an unsigned offset scaled by the access size, up to 4095 of them;
// an ldur/stur (the assembler picks it) any offset in [-256, 255].
static bool fits_ldst(int64_t off, int size)
{
    return (off >= -256 && off <= 255) || (off >= 0 && off % size == 0 && off / size <= 4095);
}

A64_Operand mem(Gen *g, int base, int64_t off, int size)
{
    if (fits_ldst(off, size))
        return a64_mem(base, off);
    gen_addr(g, IP0, base, off);
    return a64_mem(IP0, 0);
}

void gen_addr(Gen *g, int reg, int base, int64_t off)
{
    A64_Op op    = off < 0 ? A64_SUB : A64_ADD;
    uint64_t abs = off < 0 ? -(uint64_t)off : (uint64_t)off;
    if (abs == 0) {
        if (reg != base)
            emit2(g, A64_MOV, a64_reg(reg, A64_X), a64_reg(base, A64_X));
    } else if (abs < (1 << 24)) {
        // An add/sub immediate has 12 bits, optionally shifted left by 12.
        int from = base;
        if (abs >> 12) {
            emit4(g, op, a64_reg(reg, A64_X), a64_reg(from, A64_X), a64_imm(abs >> 12),
                  a64_lsl(12));
            from = reg;
        }
        if (abs & 0xfff)
            emit3(g, op, a64_reg(reg, A64_X), a64_reg(from, A64_X), a64_imm(abs & 0xfff));
    } else {
        gen_li(g, IP1, A64_X, off);
        emit3(g, A64_ADD, a64_reg(reg, A64_X), a64_reg(base, A64_X), a64_reg(IP1, A64_X));
    }
}

void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off)
{
    const Slot *s = find_slot(g, name);
    if (s && s->reg)
        fatal_error("aarch64: %s: %s is in a register", gen_name(g), name);
    if (s) {
        *base = A64_FP;
        *off  = s->offset;
        return;
    }
    if (name[0] == '%')
        fatal_error("aarch64: %s: no slot for %s", gen_name(g), name);
    if (aarch64_darwin && !map_get(&g->defined, name, NULL)) {
        // Defined elsewhere, maybe in a shared library: its address from the GOT.
        emit2(g, A64_ADRP, a64_reg(scratch, A64_X), a64_gotpage(name));
        emit2(g, A64_LDR, a64_reg(scratch, A64_X), a64_mem_got(scratch, name));
    } else {
        emit2(g, A64_ADRP, a64_reg(scratch, A64_X), a64_page(name));
        emit3(g, A64_ADD, a64_reg(scratch, A64_X), a64_reg(scratch, A64_X), a64_lo12(name, 0));
    }
    *base = scratch;
    *off  = 0;
}

void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off)
{
    if ((a64_is_ld(t) && !a64_is_fpreg(reg)) || a64_is_aggregate(t))
        fatal_error("aarch64: %s: a value of %d bytes in a register", gen_name(g), a64_size(t));
    int size = a64_size(t);
    A64_Op op;
    A64_Width w = a64_width(t);
    if (a64_is_fpreg(reg))
        op = A64_LDR;
    else if (size == 1)
        op = a64_is_unsigned(t) ? A64_LDRB : A64_LDRSB;
    else if (size == 2)
        op = a64_is_unsigned(t) ? A64_LDRH : A64_LDRSH;
    else
        op = A64_LDR;
    emit2(g, op, a64_reg(reg, w), mem(g, base, off, size));
}

void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off)
{
    if ((a64_is_ld(t) && !a64_is_fpreg(reg)) || a64_is_aggregate(t))
        fatal_error("aarch64: %s: a value of %d bytes in a register", gen_name(g), a64_size(t));
    int size  = a64_size(t);
    A64_Op op = A64_STR;
    if (!a64_is_fpreg(reg) && size == 1)
        op = A64_STRB;
    else if (!a64_is_fpreg(reg) && size == 2)
        op = A64_STRH;
    emit2(g, op, a64_reg(reg, a64_width(t)), mem(g, base, off, size));
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
        fatal_error("aarch64: floating-point constant %d as an integer", c->kind);
    }
}

void load_const_as(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t)
{
    int64_t v = const_value(c);
    switch (a64_size(t)) {
    case 1:
        v = a64_is_unsigned(t) ? (int64_t)(uint8_t)v : (int64_t)(int8_t)v;
        break;
    case 2:
        v = a64_is_unsigned(t) ? (int64_t)(uint16_t)v : (int64_t)(int16_t)v;
        break;
    }
    gen_li(g, reg, a64_width(t), v);
}

// FP register `reg` = constant `c`, through the bits in ip1.
static void load_fp_const(Gen *g, int reg, const Tac_Const *c)
{
    if (c->kind == TAC_CONST_FLOAT) {
        float f = (float)c->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        gen_li(g, IP1, A64_W, (int32_t)bits);
        emit2(g, A64_FMOV, a64_reg(reg, A64_S), a64_reg(IP1, A64_W));
    } else if (c->kind == TAC_CONST_DOUBLE) {
        uint64_t bits;
        memcpy(&bits, &c->u.double_val, 8);
        gen_li(g, IP1, A64_X, (int64_t)bits);
        emit2(g, A64_FMOV, a64_reg(reg, A64_D), a64_reg(IP1, A64_X));
    } else if (c->kind == TAC_CONST_LONG_DOUBLE && aarch64_darwin) {
        gen_li(g, IP1, A64_X, (int64_t)f128_to_double_bits(c->u.long_double_val));
        emit2(g, A64_FMOV, a64_reg(reg, A64_D), a64_reg(IP1, A64_X));
    } else if (c->kind == TAC_CONST_LONG_DOUBLE) {
        // Its two doublewords through a slot.
        Float128 q = c->u.long_double_val;
        int slot   = alloc_slot(g, NULL, NULL, 16, 16);
        gen_addr(g, IP0, A64_FP, slot);
        gen_li(g, IP1, A64_X, (int64_t)q.lo);
        emit2(g, A64_STR, a64_reg(IP1, A64_X), a64_mem(IP0, 0));
        gen_li(g, IP1, A64_X, (int64_t)q.hi);
        emit2(g, A64_STR, a64_reg(IP1, A64_X), a64_mem(IP0, 8));
        emit2(g, A64_LDR, a64_reg(reg, A64_Q), a64_mem(IP0, 0));
    } else {
        fatal_error("aarch64: integer constant in an FP register");
    }
}

void move_reg(Gen *g, int dst, int src, const Tac_Type *t)
{
    if (dst == src)
        return;
    A64_Width w = a64_width(t);
    emit2(g, a64_is_fpreg(dst) ? A64_FMOV : A64_MOV, a64_reg(dst, w), a64_reg(src, w));
}

void gen_canon(Gen *g, int dst, int src, const Tac_Type *t)
{
    int size      = a64_size(t);
    A64_Operand d = a64_reg(dst, A64_W), s = a64_reg(src, A64_W);
    if (size == 1)
        emit2(g, a64_is_unsigned(t) ? A64_UXTB : A64_SXTB, d, s);
    else if (size == 2)
        emit2(g, a64_is_unsigned(t) ? A64_UXTH : A64_SXTH, d, s);
    else if (size == 4)
        emit2(g, A64_MOV, d, s); // the upper half zero, even in place
    else
        move_reg(g, dst, src, t);
}

void load_val(Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    if (r) {
        move_reg(g, reg, r, val_type(g, v));
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT) {
        const Tac_Const *c = v->u.constant;
        if (a64_is_fpreg(reg))
            load_fp_const(g, reg, c);
        else
            gen_li(g, reg, a64_width(val_type(g, v)), const_value(c));
        return;
    }
    int base;
    int64_t off;
    const Tac_Type *t = name_type(g, v->u.var_name);
    name_addr(g, v->u.var_name, T5, &base, &off);
    load_mem(g, reg, t, base, off);
}

void load_int_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *t)
{
    if (v->kind == TAC_VAL_CONSTANT)
        load_const_as(g, reg, v->u.constant, t);
    else
        load_val(g, reg, v);
}

int use_val(Gen *g, int scratch, const Tac_Val *v)
{
    int r = var_reg(g, v);
    if (r)
        return r;
    load_val(g, scratch, v);
    return scratch;
}

int def_reg(const Gen *g, int scratch, const Tac_Val *v)
{
    int r = var_reg(g, v);
    return r ? r : scratch;
}

void store_val(Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    if (r) {
        const Tac_Type *t = val_type(g, v);
        if (r == reg)
            return;
        if (a64_is_fpreg(r))
            move_reg(g, r, reg, t);
        else
            gen_canon(g, r, reg, t);
        return;
    }
    int base;
    int64_t off;
    const Tac_Type *t = name_type(g, v->u.var_name);
    name_addr(g, v->u.var_name, T5, &base, &off);
    store_mem(g, reg, t, base, off);
}

void store_int(Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    const Tac_Type *t = val_type(g, v);
    if (r && a64_size(t) < 4)
        gen_canon(g, r, reg, t);
    else
        store_val(g, reg, v);
}

void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align)
{
    static const A64_Op loads[]  = { [1] = A64_LDRB, [2] = A64_LDRH, [4] = A64_LDR, [8] = A64_LDR };
    static const A64_Op stores[] = { [1] = A64_STRB, [2] = A64_STRH, [4] = A64_STR, [8] = A64_STR };
    int chunk                    = align >= 8 ? 8 : align >= 4 ? 4 : align >= 2 ? 2 : 1;
    for (int i = 0; i < size;) {
        while (chunk > size - i)
            chunk /= 2;
        A64_Operand r = a64_reg(T2, chunk == 8 ? A64_X : A64_W);
        emit2(g, loads[chunk], r, mem(g, src, src_off + i, chunk));
        emit2(g, stores[chunk], r, mem(g, dst, dst_off + i, chunk));
        i += chunk;
    }
}

void load_bytes(Gen *g, int reg, int base, int64_t off, int size)
{
    A64_Operand r = a64_reg(reg, A64_X);
    if (size == 8) {
        emit2(g, A64_LDR, r, mem(g, base, off, 8));
        return;
    }
    emit2(g, A64_LDRB, a64_reg(reg, A64_W), mem(g, base, off + size - 1, 1));
    for (int i = size - 2; i >= 0; i--) {
        emit2(g, A64_LDRB, a64_reg(T2, A64_W), mem(g, base, off + i, 1));
        emit3(g, A64_ORR, r, a64_reg(T2, A64_X), a64_shift(reg, A64_X, A64_SHIFT_LSL, 8));
    }
}

void store_bytes(Gen *g, int reg, int base, int64_t off, int size)
{
    if (size == 8) {
        emit2(g, A64_STR, a64_reg(reg, A64_X), mem(g, base, off, 8));
        return;
    }
    for (int i = 0; i < size; i++) {
        if (i == 0)
            emit2(g, A64_STRB, a64_reg(reg, A64_W), mem(g, base, off, 1));
        else {
            emit3(g, A64_LSR, a64_reg(T2, A64_X), a64_reg(reg, A64_X), a64_imm(8 * i));
            emit2(g, A64_STRB, a64_reg(T2, A64_W), mem(g, base, off + i, 1));
        }
    }
}

void gen_epilogue(Gen *g)
{
    a64_append(g->fn, A64_EPILOGUE);
    emit1(g, A64_RET, (A64_Operand){ 0 });
}

// Emit into block `b` instead of the current one: the prologue, or an epilogue.
static A64_Block *redirect(Gen *g, A64_Block *b)
{
    A64_Block *tail = g->fn->tail;
    g->fn->tail     = b;
    return tail;
}

// How the frame is addressed: none at all, from x29 (the frame record), or from sp,
// `size` bytes below the caller's sp.
typedef enum { FRAME_NONE, FRAME_FP, FRAME_SP } FrameKind;

typedef struct {
    FrameKind kind;
    int size;   // FRAME_SP: the bytes below the caller's sp
    int rest;   // the slots and outgoing area, below where x29 would point
    bool calls; // x30 must be saved
    int lone;   // FRAME_SP: the saved register beside x30 in the record (an index), or -1
} Frame;

// The sp offset of x29 offset `off`.  x29 would be at sp + rest: a slot is below it,
// and when there are calls the 16 bytes of the frame record above it hold x30.  Without
// calls there is no record, and the incoming stack arguments start at sp + size.
static int64_t sp_offset(const Frame *fr, int64_t off)
{
    return off < 16 ? off + fr->rest : off - 16 + fr->size;
}

// The base and offset of frame offset `off`, relative to x29.
static A64_Operand frame_mem(const Frame *fr, int64_t off)
{
    return fr->kind == FRAME_SP ? a64_mem(A64_SP, sp_offset(fr, off)) : a64_mem(A64_FP, off);
}

// Save or restore the callee-saved registers in use: a pair of one file with stp/ldp,
// a lone one with str/ldr.  layout_frame gives a pair adjacent slots.
static void save_regs(Gen *g, const Frame *fr, bool restore)
{
    for (int i = 0; i < g->nsaved;) {
        int r       = g->saved_reg[i];
        A64_Width w = a64_is_fpreg(r) ? A64_D : A64_X;
        if (i + 1 < g->nsaved && g->saved_off[i + 1] == g->saved_off[i] + 8) {
            emit3(g, restore ? A64_LDP : A64_STP, a64_reg(r, w), a64_reg(g->saved_reg[i + 1], w),
                  frame_mem(fr, g->saved_off[i]));
            i += 2;
        } else {
            emit2(g, restore ? A64_LDR : A64_STR, a64_reg(r, w), frame_mem(fr, g->saved_off[i]));
            i++;
        }
    }
}

// A save of an sp-addressed frame: a register, or a pair (r2 not 0), at sp + off.
typedef struct {
    int r1, r2;
    A64_Width w;
    int64_t off;
} Save;

// The saves of an sp-addressed frame, lowest address first: the callee-saved
// registers in use, in pairs where layout_frame made them adjacent; with calls, x30
// where the record would hold x29, and beside it a lone general register.
static int sp_saves(const Gen *g, const Frame *fr, Save *s)
{
    int n = 0;
    for (int i = 0; i < g->nsaved;) {
        int r       = g->saved_reg[i];
        A64_Width w = a64_is_fpreg(r) ? A64_D : A64_X;
        if (i == fr->lone) {
            i++;
        } else if (i + 1 < g->nsaved && g->saved_off[i + 1] == g->saved_off[i] + 8) {
            s[n++] = (Save){ r, g->saved_reg[i + 1], w, sp_offset(fr, g->saved_off[i]) };
            i += 2;
        } else {
            s[n++] = (Save){ r, 0, w, sp_offset(fr, g->saved_off[i]) };
            i++;
        }
    }
    if (fr->calls)
        s[n++] = (Save){ A64_LR, fr->lone >= 0 ? g->saved_reg[fr->lone] : 0, A64_X, fr->rest };
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && s[j].off < s[j - 1].off; j--) {
            Save t   = s[j];
            s[j]     = s[j - 1];
            s[j - 1] = t;
        }
    return n;
}

// Whether the lowest save, at sp + 0, takes the change of sp as a pre-index (and the
// restore a post-index): of 16-byte multiples, up to 504 for a pair, 255 for one.
static bool sp_folds(const Frame *fr, const Save *s, int n)
{
    return n > 0 && s[0].off == 0 && fr->size <= (s[0].r2 ? 504 : 255);
}

static void sp_save(Gen *g, const Save *sv, bool restore, A64_Operand mem)
{
    if (sv->r2)
        emit3(g, restore ? A64_LDP : A64_STP, a64_reg(sv->r1, sv->w), a64_reg(sv->r2, sv->w), mem);
    else
        emit2(g, restore ? A64_LDR : A64_STR, a64_reg(sv->r1, sv->w), mem);
}

// The setup and teardown of an sp-addressed frame.
static void sp_frame(Gen *g, const Frame *fr, bool restore)
{
    Save s[34];
    int n     = sp_saves(g, fr, s);
    bool fold = sp_folds(fr, s, n);
    if (!restore) {
        if (!fold)
            gen_addr(g, A64_SP, A64_SP, -fr->size);
        for (int i = 0; i < n; i++)
            sp_save(g, &s[i], false,
                    i == 0 && fold ? a64_mem_pre(A64_SP, -fr->size) : a64_mem(A64_SP, s[i].off));
        return;
    }
    for (int i = fold ? 1 : 0; i < n; i++)
        sp_save(g, &s[i], true, a64_mem(A64_SP, s[i].off));
    if (fold)
        sp_save(g, &s[0], true, a64_mem_post(A64_SP, fr->size));
    else
        gen_addr(g, A64_SP, A64_SP, fr->size);
}

// The frame teardown: the saved registers back, then sp (and x29, x30) as on entry.
static void epilogue(Gen *g, const Frame *fr)
{
    if (fr->kind == FRAME_NONE)
        return;
    if (fr->kind == FRAME_SP) {
        sp_frame(g, fr, true);
        return;
    }
    save_regs(g, fr, true);
    emit2(g, A64_MOV, a64_reg(A64_SP, A64_X), a64_reg(A64_FP, A64_X));
    emit3(g, A64_LDP, a64_reg(A64_FP, A64_X), a64_reg(A64_LR, A64_X), a64_mem_post(A64_SP, 16));
}

// Replace each epilogue marker by the frame teardown.
static void expand_epilogues(Gen *g, const Frame *fr)
{
    for (A64_Block *b = g->fn->blocks; b; b = b->next) {
        for (A64_Instr **link = &b->head; *link;) {
            A64_Instr *marker = *link;
            if (marker->op != A64_EPILOGUE) {
                link = &marker->next;
                continue;
            }
            A64_Block seq   = { 0 };
            A64_Block *tail = redirect(g, &seq);
            epilogue(g, fr);
            g->fn->tail = tail;
            if (seq.head) {
                seq.tail->next = marker->next;
                *link          = seq.head;
                link           = &seq.tail->next;
            } else {
                *link = marker->next;
            }
            xfree(marker);
        }
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}

static bool uses_reg(const A64_Instr *in, int reg)
{
    for (int i = 0; i < A64_MAX_OPERANDS; i++) {
        const A64_Operand *o = &in->opnd[i];
        if ((o->kind == A64_OPND_REG || o->kind == A64_OPND_MEM || o->kind == A64_OPND_SHIFT ||
             o->kind == A64_OPND_EXT) &&
            o->reg == reg)
            return true;
    }
    return false;
}

static bool has_calls(const Gen *g)
{
    for (const A64_Block *b = g->fn->blocks; b; b = b->next)
        for (const A64_Instr *in = b->head; in; in = in->next)
            if (in->op == A64_BL || in->op == A64_BLR)
                return true;
    return false;
}

// Whether the body needs no frame: it makes no call, saves no register, and never uses
// x29 (no slot, no stack argument) or sp.
static bool is_leaf(const Gen *g)
{
    if (g->nsaved > 0 || has_calls(g))
        return false;
    for (const A64_Block *b = g->fn->blocks; b; b = b->next)
        for (const A64_Instr *in = b->head; in; in = in->next)
            if (uses_reg(in, A64_FP) || uses_reg(in, A64_SP))
                return false;
    return true;
}

// The bytes an ldr/str-family instruction accesses, from its opcode and register view.
static int access_size(const A64_Instr *in)
{
    static const int width_bytes[] = { [A64_W] = 4, [A64_X] = 8, [A64_S] = 4, [A64_D] = 8,
                                       [A64_Q] = 16 };
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
        return width_bytes[in->opnd[0].width];
    }
}

// Whether x29-relative operand `i` of `in` can be rebased onto sp, at sp offset `off`;
// with `apply`, do it.  A memory operand must still fit its instruction; an address
// computation is `add`/`sub rd, x29, #imm` or `mov rd, x29`, which become `add rd, sp,
// #off`.
static bool rebase(A64_Instr *in, int i, int64_t off, bool apply)
{
    A64_Operand *o = &in->opnd[i];
    if (o->kind == A64_OPND_MEM) {
        if (o->sub != A64_MEM_OFFSET)
            return false;
        bool ok;
        if (in->op == A64_LDP || in->op == A64_STP)
            ok = off % 8 == 0 && off >= -512 && off <= 504;
        else
            ok = fits_ldst(off, access_size(in));
        if (ok && apply) {
            o->reg = A64_SP;
            o->imm = off;
        }
        return ok;
    }
    if (o->kind != A64_OPND_REG || i != 1 || off < 0 || off > 4095)
        return false;
    bool addsub = (in->op == A64_ADD || in->op == A64_SUB) && in->opnd[2].kind == A64_OPND_IMM &&
                  in->opnd[3].kind == A64_OPND_NONE;
    if (!addsub && !(in->op == A64_MOV && o->width == A64_X))
        return false;
    if (apply) {
        in->op     = A64_ADD;
        o->reg     = A64_SP;
        in->opnd[2] = a64_imm(off);
    }
    return true;
}

// The x29 offset operand `i` of `in` stands for.
static int64_t fp_offset(const A64_Instr *in, int i)
{
    const A64_Operand *o = &in->opnd[i];
    if (o->kind == A64_OPND_MEM)
        return o->imm;
    if (in->op == A64_MOV)
        return 0;
    return in->op == A64_SUB ? -in->opnd[2].imm : in->opnd[2].imm;
}

// Address the body's frame from sp, as `fr` says, when every use of x29 can be
// rebased; else change nothing.
static bool rebase_to_sp(const Gen *g, const Frame *fr)
{
    for (int pass = 0; pass < 2; pass++) {
        for (A64_Block *b = g->fn->blocks; b; b = b->next) {
            for (A64_Instr *in = b->head; in; in = in->next) {
                for (int i = 0; i < A64_MAX_OPERANDS; i++) {
                    const A64_Operand *o = &in->opnd[i];
                    if (o->reg != A64_FP || (o->kind != A64_OPND_REG && o->kind != A64_OPND_MEM &&
                                             o->kind != A64_OPND_SHIFT && o->kind != A64_OPND_EXT))
                        continue;
                    if (o->kind == A64_OPND_SHIFT || o->kind == A64_OPND_EXT)
                        return false;
                    if (!rebase(in, i, sp_offset(fr, fp_offset(in, i)), pass == 1))
                        return false;
                }
            }
        }
    }
    return true;
}

// Whether the saves fit their stp/str from sp.
static bool saves_fit(const Gen *g, const Frame *fr)
{
    Save s[34];
    int n = sp_saves(g, fr, s);
    for (int i = 0; i < n; i++)
        if (s[i].off % 8 != 0 || s[i].off > 504)
            return false;
    return true;
}

// Fill the prologue and the epilogues, now that the frame is known.  A leaf function
// that needs no stack has none; otherwise, unless asked for a frame record, the frame
// is addressed from sp when every x29 offset can be, and x30 saved only with calls.
// Addressed from sp, x30 takes the place of x29 in the record, and the lone general
// register of an odd number saved goes beside it, its own slot given up when it is the
// lowest.
void gen_prologue(Gen *g)
{
    bool calls      = has_calls(g);
    Frame fr        = { FRAME_NONE, 0, (g->locals_size + g->outgoing + 15) / 16 * 16, calls, -1 };
    Frame sp        = fr;
    A64_Block *tail = redirect(g, g->prologue);
    fr.size         = fr.rest + (calls ? 16 : 0);
    int ngpr = 0;
    while (ngpr < g->nsaved && !a64_is_fpreg(g->saved_reg[ngpr]))
        ngpr++;
    if (calls && ngpr % 2 == 1) {
        sp.lone = ngpr - 1;
        if (g->saved_off[sp.lone] == -g->locals_size)
            sp.rest = (g->locals_size - 16 + g->outgoing + 15) / 16 * 16;
    }
    sp.size = sp.rest + (calls ? 16 : 0);
    if (!aarch64_frame_pointer && is_leaf(g)) {
        // nothing
    } else if (!aarch64_frame_pointer && sp.size <= 4095 && saves_fit(g, &sp) &&
               rebase_to_sp(g, &sp)) {
        fr      = sp;
        fr.kind = FRAME_SP;
        sp_frame(g, &fr, false);
    } else {
        fr.kind = FRAME_FP;
        emit3(g, A64_STP, a64_reg(A64_FP, A64_X), a64_reg(A64_LR, A64_X), a64_mem_pre(A64_SP, -16));
        emit2(g, A64_MOV, a64_reg(A64_FP, A64_X), a64_reg(A64_SP, A64_X));
        if (fr.rest)
            gen_addr(g, A64_SP, A64_SP, -fr.rest);
        save_regs(g, &fr, false);
    }
    g->fn->tail = tail;
    expand_epilogues(g, &fr);
}
