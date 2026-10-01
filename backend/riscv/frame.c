//
// Types, frame slots, and loading/storing values (LP64D).
//
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "xalloc.h"

int rv_size(const Tac_Type *t)
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
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return 8;
    case TAC_TYPE_LONG_DOUBLE:
        return 16;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * rv_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 8;
}

int rv_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return rv_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return rv_size(t);
    }
}

bool rv_is_fp(const Tac_Type *t)
{
    if (t->kind == TAC_TYPE_LONG_DOUBLE)
        fatal_error("riscv: long double is not implemented");
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE;
}

bool rv_is_double(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_DOUBLE;
}

bool rv_is_unsigned(const Tac_Type *t)
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

bool rv_is_aggregate(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_ARRAY || t->kind == TAC_TYPE_STRUCTURE;
}

static void add_global(Gen *g, const char *name, const Tac_Type *type)
{
    if (name && type)
        map_insert(&g->globals, name, (intptr_t)type, 0);
}

void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl)
{
    memset(g, 0, sizeof(*g));
    g->program = program;
    g->tl      = tl;
    g->fn      = rv_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    rv_new_block(g->fn, NULL); // the body
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->regs);
    g->header = gen_variadic(g) ? 80 : 16;
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
    rv_free_func(g->fn);
}

bool gen_variadic(const Gen *g)
{
    const Tac_Type *t = g->tl->u.function.type;
    return t && t->kind == TAC_TYPE_FUN_TYPE && t->u.fun_type.variadic;
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
    g->locals_size = (g->locals_size + size + align - 1) / align * align;
    int offset     = -g->header - g->locals_size;
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

int assigned_reg(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->regs, name, &v) ? (int)v : 0;
}

const Slot *find_slot(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->frame, name, &v) ? (const Slot *)v : NULL;
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
    fatal_error("riscv: %s: no type for %s", gen_name(g), name);
}

const Tac_Type *val_type(const Gen *g, const Tac_Val *v)
{
    static const Tac_Type types[] = {
        [TAC_CONST_INT] = { .kind = TAC_TYPE_INT },
        [TAC_CONST_LONG] = { .kind = TAC_TYPE_LONG },
        [TAC_CONST_LONG_LONG] = { .kind = TAC_TYPE_LONG_LONG },
        [TAC_CONST_UINT] = { .kind = TAC_TYPE_UINT },
        [TAC_CONST_ULONG] = { .kind = TAC_TYPE_ULONG },
        [TAC_CONST_ULONG_LONG] = { .kind = TAC_TYPE_ULONG_LONG },
        [TAC_CONST_FLOAT] = { .kind = TAC_TYPE_FLOAT },
        [TAC_CONST_DOUBLE] = { .kind = TAC_TYPE_DOUBLE },
        [TAC_CONST_LONG_DOUBLE] = { .kind = TAC_TYPE_LONG_DOUBLE },
        [TAC_CONST_SCHAR] = { .kind = TAC_TYPE_SCHAR },
        [TAC_CONST_UCHAR] = { .kind = TAC_TYPE_UCHAR },
    };
    if (v->kind == TAC_VAL_CONSTANT)
        return &types[v->u.constant->kind];
    return name_type(g, v->u.var_name);
}

Rv_Instr *emit2(Gen *g, Rv_Op op, Rv_Operand a, Rv_Operand b)
{
    Rv_Instr *in = rv_append(g->fn, op);
    in->opnd[0]  = a;
    in->opnd[1]  = b;
    return in;
}

Rv_Instr *emit3(Gen *g, Rv_Op op, Rv_Operand a, Rv_Operand b, Rv_Operand c)
{
    Rv_Instr *in = emit2(g, op, a, b);
    in->opnd[2]  = c;
    return in;
}

void gen_li(Gen *g, int reg, int64_t imm)
{
    emit2(g, RV_LI, rv_reg(reg), rv_imm(imm));
}

static bool fits12(int64_t v)
{
    return v >= -2048 && v <= 2047;
}

Rv_Operand mem(Gen *g, int base, int64_t off)
{
    if (fits12(off))
        return rv_mem(base, off);
    gen_li(g, RV_T6, off);
    emit3(g, RV_ADD, rv_reg(RV_T6), rv_reg(base), rv_reg(RV_T6));
    return rv_mem(RV_T6, 0);
}

void gen_addr(Gen *g, int reg, int base, int64_t off)
{
    if (fits12(off)) {
        if (off || reg != base)
            emit3(g, RV_ADDI, rv_reg(reg), rv_reg(base), rv_imm(off));
        return;
    }
    gen_li(g, reg, off);
    emit3(g, RV_ADD, rv_reg(reg), rv_reg(base), rv_reg(reg));
}

void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off)
{
    const Slot *s = find_slot(g, name);
    if (s && s->reg)
        fatal_error("riscv: %s: %s is in a register", gen_name(g), name);
    if (s) {
        *base = RV_S0;
        *off  = s->offset;
        return;
    }
    if (name[0] == '%')
        fatal_error("riscv: %s: no slot for %s", gen_name(g), name);
    emit2(g, RV_LA, rv_reg(scratch), rv_sym(name, 0));
    *base = scratch;
    *off  = 0;
}

#define is_freg rv_is_freg

void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off)
{
    Rv_Op op;
    if (is_freg(reg)) {
        op = rv_is_double(t) ? RV_FLD : RV_FLW;
    } else {
        switch (rv_size(t)) {
        case 1:
            op = rv_is_unsigned(t) ? RV_LBU : RV_LB;
            break;
        case 2:
            op = rv_is_unsigned(t) ? RV_LHU : RV_LH;
            break;
        case 4:
            op = RV_LW; // 32-bit values are kept sign-extended, unsigned too
            break;
        default:
            op = RV_LD;
            break;
        }
    }
    emit2(g, op, rv_reg(reg), mem(g, base, off));
}

void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off)
{
    Rv_Op op;
    if (is_freg(reg)) {
        op = rv_is_double(t) ? RV_FSD : RV_FSW;
    } else {
        switch (rv_size(t)) {
        case 1:
            op = RV_SB;
            break;
        case 2:
            op = RV_SH;
            break;
        case 4:
            op = RV_SW;
            break;
        default:
            op = RV_SD;
            break;
        }
    }
    emit2(g, op, rv_reg(reg), mem(g, base, off));
}

// The value of an integer constant, in the canonical register form of its type: a
// 32-bit value sign-extended, a narrower one extended by its signedness.
static int64_t const_int(const Gen *g, const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_INT:
        return (int32_t)c->u.int_val;
    case TAC_CONST_LONG:
        return c->u.long_val;
    case TAC_CONST_LONG_LONG:
        return c->u.long_long_val;
    case TAC_CONST_UINT:
        return (int32_t)c->u.uint_val;
    case TAC_CONST_ULONG:
        return (int64_t)c->u.ulong_val;
    case TAC_CONST_ULONG_LONG:
        return (int64_t)c->u.ulong_long_val;
    case TAC_CONST_SCHAR:
        return (int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    default:
        fatal_error("riscv: %s: constant kind %d in an integer register", gen_name(g), c->kind);
    }
}

static void load_fp_const(Gen *g, int reg, const Tac_Const *c)
{
    if (c->kind == TAC_CONST_FLOAT) {
        float f = (float)c->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        gen_li(g, RV_T6, (int32_t)bits);
        emit2(g, RV_FMVWX, rv_reg(reg), rv_reg(RV_T6));
    } else if (c->kind == TAC_CONST_DOUBLE) {
        uint64_t bits;
        memcpy(&bits, &c->u.double_val, 8);
        gen_li(g, RV_T6, (int64_t)bits);
        emit2(g, RV_FMVDX, rv_reg(reg), rv_reg(RV_T6));
    } else if (c->kind == TAC_CONST_LONG_DOUBLE) {
        fatal_error("riscv: long double is not implemented");
    } else {
        fatal_error("riscv: %s: integer constant in an FP register", gen_name(g));
    }
}

void move_reg(Gen *g, int dst, int src, const Tac_Type *t)
{
    if (dst == src)
        return;
    if (is_freg(dst))
        emit2(g, rv_is_double(t) ? RV_FMVD : RV_FMVS, rv_reg(dst), rv_reg(src));
    else
        emit2(g, RV_MV, rv_reg(dst), rv_reg(src));
}

void gen_canon(Gen *g, int dst, int src, const Tac_Type *t)
{
    int size = rv_size(t);
    if (size >= 8) {
        move_reg(g, dst, src, t);
    } else if (size == 4) {
        emit2(g, RV_SEXTW, rv_reg(dst), rv_reg(src));
    } else if (size == 1 && rv_is_unsigned(t)) {
        emit3(g, RV_ANDI, rv_reg(dst), rv_reg(src), rv_imm(255));
    } else {
        int shift = 64 - 8 * size;
        emit3(g, RV_SLLI, rv_reg(dst), rv_reg(src), rv_imm(shift));
        emit3(g, rv_is_unsigned(t) ? RV_SRLI : RV_SRAI, rv_reg(dst), rv_reg(dst), rv_imm(shift));
    }
}

void load_const_as(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t)
{
    int64_t v = const_int(g, c);
    switch (rv_size(t)) {
    case 1:
        v = rv_is_unsigned(t) ? (int64_t)(uint8_t)v : (int64_t)(int8_t)v;
        break;
    case 2:
        v = rv_is_unsigned(t) ? (int64_t)(uint16_t)v : (int64_t)(int16_t)v;
        break;
    case 4:
        v = (int32_t)v;
        break;
    }
    gen_li(g, reg, v);
}

void load_val(Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    if (r) {
        move_reg(g, reg, r, val_type(g, v));
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT) {
        if (is_freg(reg))
            load_fp_const(g, reg, v->u.constant);
        else
            gen_li(g, reg, const_int(g, v->u.constant));
        return;
    }
    int base;
    int64_t off;
    const Tac_Type *t = name_type(g, v->u.var_name);
    name_addr(g, v->u.var_name, RV_T5, &base, &off);
    load_mem(g, reg, t, base, off);
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
        move_reg(g, r, reg, val_type(g, v));
        return;
    }
    int base;
    int64_t off;
    const Tac_Type *t = name_type(g, v->u.var_name);
    name_addr(g, v->u.var_name, RV_T5, &base, &off);
    store_mem(g, reg, t, base, off);
}

void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align)
{
    static const Rv_Op loads[]  = { [1] = RV_LBU, [2] = RV_LHU, [4] = RV_LW, [8] = RV_LD };
    static const Rv_Op stores[] = { [1] = RV_SB, [2] = RV_SH, [4] = RV_SW, [8] = RV_SD };
    int chunk = align >= 8 ? 8 : align >= 4 ? 4 : align >= 2 ? 2 : 1;
    for (int i = 0; i < size;) {
        while (chunk > size - i)
            chunk /= 2;
        emit2(g, loads[chunk], rv_reg(RV_T2), mem(g, src, src_off + i));
        emit2(g, stores[chunk], rv_reg(RV_T2), mem(g, dst, dst_off + i));
        i += chunk;
    }
}

void load_bytes(Gen *g, int reg, int base, int64_t off, int size)
{
    if (size == 8) {
        emit2(g, RV_LD, rv_reg(reg), mem(g, base, off));
        return;
    }
    emit2(g, RV_LBU, rv_reg(reg), mem(g, base, off + size - 1));
    for (int i = size - 2; i >= 0; i--) {
        emit3(g, RV_SLLI, rv_reg(reg), rv_reg(reg), rv_imm(8));
        emit2(g, RV_LBU, rv_reg(RV_T2), mem(g, base, off + i));
        emit3(g, RV_OR, rv_reg(reg), rv_reg(reg), rv_reg(RV_T2));
    }
}

void store_bytes(Gen *g, int reg, int base, int64_t off, int size)
{
    if (size == 8) {
        emit2(g, RV_SD, rv_reg(reg), mem(g, base, off));
        return;
    }
    emit2(g, RV_MV, rv_reg(RV_T2), rv_reg(reg));
    for (int i = 0; i < size; i++) {
        if (i)
            emit3(g, RV_SRLI, rv_reg(RV_T2), rv_reg(RV_T2), rv_imm(8));
        emit2(g, RV_SB, rv_reg(RV_T2), mem(g, base, off + i));
    }
}

// Save or restore the callee-saved registers in use.
static void save_regs(Gen *g, Rv_Block *b, bool restore)
{
    for (int i = 0; i < g->nsaved; i++) {
        int reg = g->saved_reg[i];
        Rv_Op op;
        if (is_freg(reg))
            op = restore ? RV_FLD : RV_FSD;
        else
            op = restore ? RV_LD : RV_SD;
        Rv_Instr *in = b ? rv_append_to(b, op) : rv_append(g->fn, op);
        in->opnd[0]  = rv_reg(reg);
        in->opnd[1]  = rv_mem(RV_S0, g->saved_off[i]);
    }
}

void gen_epilogue(Gen *g)
{
    save_regs(g, NULL, true);
    emit3(g, RV_ADDI, rv_reg(RV_SP), rv_reg(RV_S0), rv_imm(-g->header));
    emit2(g, RV_LD, rv_reg(RV_RA), rv_mem(RV_SP, 8));
    emit2(g, RV_LD, rv_reg(RV_S0), rv_mem(RV_SP, 0));
    emit3(g, RV_ADDI, rv_reg(RV_SP), rv_reg(RV_SP), rv_imm(g->header));
    rv_append(g->fn, RV_RET);
}

// Fill the prologue block, now that the frame size is known.
void gen_prologue(Gen *g)
{
    Rv_Block *b  = g->prologue;
    Rv_Instr *in = rv_append_to(b, RV_ADDI);
    in->opnd[0]  = rv_reg(RV_SP);
    in->opnd[1]  = rv_reg(RV_SP);
    in->opnd[2]  = rv_imm(-g->header);
    in           = rv_append_to(b, RV_SD);
    in->opnd[0]  = rv_reg(RV_RA);
    in->opnd[1]  = rv_mem(RV_SP, 8);
    in           = rv_append_to(b, RV_SD);
    in->opnd[0]  = rv_reg(RV_S0);
    in->opnd[1]  = rv_mem(RV_SP, 0);
    in           = rv_append_to(b, RV_ADDI);
    in->opnd[0]  = rv_reg(RV_S0);
    in->opnd[1]  = rv_reg(RV_SP);
    in->opnd[2]  = rv_imm(g->header);
    int rest     = (g->locals_size + g->outgoing + 15) / 16 * 16;
    if (rest == 0) {
        // nothing
    } else if (fits12(-rest)) {
        in          = rv_append_to(b, RV_ADDI);
        in->opnd[0] = rv_reg(RV_SP);
        in->opnd[1] = rv_reg(RV_SP);
        in->opnd[2] = rv_imm(-rest);
    } else {
        in          = rv_append_to(b, RV_LI);
        in->opnd[0] = rv_reg(RV_T0);
        in->opnd[1] = rv_imm(rest);
        in          = rv_append_to(b, RV_SUB);
        in->opnd[0] = rv_reg(RV_SP);
        in->opnd[1] = rv_reg(RV_SP);
        in->opnd[2] = rv_reg(RV_T0);
    }
    save_regs(g, b, false);
}
