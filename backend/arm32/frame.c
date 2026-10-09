//
// Types, frame slots, loading and storing values, the prologue and epilogue.
//
#include <string.h>

#include "bitops.h"
#include "codegen.h"
#include "float128.h"
#include "internal.h"
#include "xalloc.h"

int a32_size(const Tac_Type *t)
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
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
    case TAC_TYPE_FLOAT:
        return 4;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
        return 8;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * a32_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 4;
}

int a32_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return a32_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return a32_size(t);
    }
}

bool a32_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool a32_is_double(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_DOUBLE || t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool a32_is_pair(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_LONG_LONG || t->kind == TAC_TYPE_ULONG_LONG;
}

bool a32_is_unsigned(const Tac_Type *t)
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

bool a32_is_aggregate(const Tac_Type *t)
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
    g->program  = program;
    g->tl       = tl;
    g->fn       = a32_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    a32_new_block(g->fn, NULL); // the body
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->regs);
    map_init(&g->dead);
    map_init(&g->regs_hi);
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
    map_destroy(&g->regs_hi);
    a32_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int offset, int reg,
                        int hi)
{
    Slot *s   = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type   = type;
    s->offset = offset;
    s->reg    = reg;
    s->hi     = hi;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

// r11 is only 8-byte aligned, so a stricter alignment is not kept.
int alloc_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align)
{
    if (align < 1)
        align = 1;
    if (align > 8)
        align = 8;
    g->locals_size = (g->locals_size + size + align - 1) / align * align;
    int offset     = -g->locals_size;
    if (name)
        insert_slot(g, name, type, offset, -1, -1);
    return offset;
}

void place_slot(Gen *g, const char *name, const Tac_Type *type, int offset)
{
    insert_slot(g, name, type, offset, -1, -1);
}

void place_reg(Gen *g, const char *name, const Tac_Type *type, int reg, int hi)
{
    insert_slot(g, name, type, 0, reg, hi);
}

const Slot *find_slot(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->frame, name, &v) ? (const Slot *)v : NULL;
}

int assigned_reg(const Gen *g, const char *name, int *hi)
{
    intptr_t v;
    *hi = map_get(&g->regs_hi, name, &v) ? (int)v - 1 : -1;
    return map_get(&g->regs, name, &v) ? (int)v - 1 : -1;
}

int var_reg(const Gen *g, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_VAR)
        return -1;
    const Slot *s = find_slot(g, v->u.var_name);
    return s ? s->reg : -1;
}

int var_reg_hi(const Gen *g, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_VAR)
        return -1;
    const Slot *s = find_slot(g, v->u.var_name);
    return s ? s->hi : -1;
}

const Tac_Type *name_type(const Gen *g, const char *name)
{
    const Slot *s = find_slot(g, name);
    if (s)
        return s->type;
    intptr_t v;
    if (map_get(&g->globals, name, &v))
        return (const Tac_Type *)v;
    fatal_error("arm32: %s: no type for %s", gen_name(g), name);
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

A32_Instr *emit0(Gen *g, A32_Op op)
{
    return a32_append(g->fn, op);
}

A32_Instr *emit1(Gen *g, A32_Op op, A32_Operand a)
{
    A32_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

A32_Instr *emit2(Gen *g, A32_Op op, A32_Operand a, A32_Operand b)
{
    A32_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

A32_Instr *emit3(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c)
{
    A32_Instr *in = emit2(g, op, a, b);
    in->opnd[2]   = c;
    return in;
}

A32_Instr *emit4(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c, A32_Operand d)
{
    A32_Instr *in = emit3(g, op, a, b, c);
    in->opnd[3]   = d;
    return in;
}

// A constant takes one `mov` when it is a modified immediate, one `mvn` when its
// complement is, one `movw` when it fits 16 bits, and otherwise `movw` + `movt`.
void gen_li(Gen *g, int reg, uint32_t imm)
{
    if (a32_operand2_imm(imm)) {
        emit2(g, A32_MOV, a32_reg(reg), a32_imm(imm));
    } else if (a32_operand2_imm(~imm)) {
        emit2(g, A32_MVN, a32_reg(reg), a32_imm(~imm));
    } else {
        emit2(g, A32_MOVW, a32_reg(reg), a32_imm(imm & 0xffff));
        if (imm >> 16)
            emit2(g, A32_MOVT, a32_reg(reg), a32_imm(imm >> 16));
    }
}

// Each step takes the 8 bits from the lowest set bit, rounded down to an even
// position: a modified immediate.  Four steps cover any 32-bit value.
void gen_addr(Gen *g, int reg, int base, int64_t off)
{
    A32_Op op    = off < 0 ? A32_SUB : A32_ADD;
    uint32_t abs = off < 0 ? -(uint32_t)off : (uint32_t)off;
    int from     = base;
    if (abs == 0) {
        if (reg != base)
            emit2(g, A32_MOV, a32_reg(reg), a32_reg(base));
        return;
    }
    while (abs) {
        int p          = ctz32(abs) & ~1;
        uint32_t chunk = abs & (0xffu << p);
        emit3(g, op, a32_reg(reg), a32_reg(from), a32_imm(chunk));
        abs &= ~chunk;
        from = reg;
    }
}

// The offset reach of a memory instruction: ldr/str/ldrb/strb 12 bits, the halfword
// and signed-byte forms 8 bits, vldr/vstr 8 bits of words.
static bool fits(A32_Op op, int64_t off)
{
    switch (op) {
    case A32_LDRH:
    case A32_LDRSH:
    case A32_LDRSB:
    case A32_STRH:
        return off >= -255 && off <= 255;
    case A32_VLDR:
    case A32_VSTR:
        return off >= -1020 && off <= 1020 && off % 4 == 0;
    default:
        return off >= -4095 && off <= 4095;
    }
}

A32_Operand mem(Gen *g, A32_Op op, int base, int64_t off, int scratch)
{
    if (fits(op, off))
        return a32_mem(base, off);
    gen_addr(g, scratch, base, off);
    return a32_mem(scratch, 0);
}

void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off)
{
    const Slot *s = find_slot(g, name);
    if (s && s->reg >= 0)
        fatal_error("arm32: %s: %s is in a register", gen_name(g), name);
    if (s) {
        *base = FB;
        *off  = s->offset;
        return;
    }
    if (name[0] == '%')
        fatal_error("arm32: %s: no slot for %s", gen_name(g), name);
    emit2(g, A32_MOVW, a32_reg(scratch), a32_lower16(name, 0));
    emit2(g, A32_MOVT, a32_reg(scratch), a32_upper16(name, 0));
    *base = scratch;
    *off  = 0;
}

// The load or store instruction for a scalar of type `t` in register `reg`.
static A32_Op ldst_op(int reg, const Tac_Type *t, bool store)
{
    if (a32_is_vfp(reg))
        return store ? A32_VSTR : A32_VLDR;
    switch (a32_size(t)) {
    case 1:
        return store ? A32_STRB : a32_is_unsigned(t) ? A32_LDRB : A32_LDRSB;
    case 2:
        return store ? A32_STRH : a32_is_unsigned(t) ? A32_LDRH : A32_LDRSH;
    default:
        return store ? A32_STR : A32_LDR;
    }
}

// The operand of register `reg` holding a value of type `t`.
static A32_Operand reg_opnd(int reg, const Tac_Type *t)
{
    if (!a32_is_vfp(reg))
        return a32_reg(reg);
    return a32_is_double(t) ? a32_dreg(reg) : a32_sreg(reg);
}

static void check_scalar(const Gen *g, int reg, const Tac_Type *t)
{
    if (a32_is_aggregate(t) || (!a32_is_vfp(reg) && a32_size(t) > 4))
        fatal_error("arm32: %s: a value of %d bytes in a register", gen_name(g), a32_size(t));
}

void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off)
{
    check_scalar(g, reg, t);
    A32_Op op = ldst_op(reg, t, false);
    emit2(g, op, reg_opnd(reg, t), mem(g, op, base, off, a32_is_vfp(reg) ? T0 : reg));
}

void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off, int scratch)
{
    check_scalar(g, reg, t);
    A32_Op op = ldst_op(reg, t, true);
    emit2(g, op, reg_opnd(reg, t), mem(g, op, base, off, scratch));
}

uint64_t const_bits(const Tac_Const *c, const Tac_Type *t)
{
    if (c->kind == TAC_CONST_FLOAT || c->kind == TAC_CONST_DOUBLE ||
        c->kind == TAC_CONST_LONG_DOUBLE) {
        double d = c->kind == TAC_CONST_FLOAT    ? c->u.float_val
                   : c->kind == TAC_CONST_DOUBLE ? c->u.double_val
                                                 : f128_to_double(c->u.long_double_val);
        if (t->kind == TAC_TYPE_FLOAT) {
            float f = (float)d;
            uint32_t bits;
            memcpy(&bits, &f, 4);
            return bits;
        }
        if (!a32_is_double(t))
            fatal_error("arm32: floating-point constant as an integer");
        uint64_t bits;
        memcpy(&bits, &d, 8);
        return bits;
    }
    if (a32_is_fp(t))
        fatal_error("arm32: integer constant as floating point");
    int64_t v;
    switch (c->kind) {
    case TAC_CONST_INT:
        v = (int32_t)c->u.int_val;
        break;
    case TAC_CONST_UINT:
        v = (uint32_t)c->u.uint_val;
        break;
    case TAC_CONST_SCHAR:
        v = (int8_t)c->u.char_val;
        break;
    case TAC_CONST_UCHAR:
        v = c->u.uchar_val;
        break;
    case TAC_CONST_LONG:
        v = (int32_t)c->u.long_val;
        break;
    case TAC_CONST_ULONG:
        v = (uint32_t)c->u.ulong_val;
        break;
    case TAC_CONST_LONG_LONG:
        v = c->u.long_long_val;
        break;
    default:
        v = (int64_t)c->u.ulong_long_val;
        break;
    }
    switch (a32_size(t)) {
    case 1:
        return a32_is_unsigned(t) ? (uint32_t)(uint8_t)v : (uint32_t)(int8_t)v;
    case 2:
        return a32_is_unsigned(t) ? (uint32_t)(uint16_t)v : (uint32_t)(int16_t)v;
    case 4:
        return (uint32_t)v;
    default:
        return (uint64_t)v;
    }
}

void load_fp_const(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t, int lo, int hi)
{
    uint64_t bits = const_bits(c, t);
    double value;
    if (a32_is_double(t)) {
        memcpy(&value, &bits, 8);
    } else {
        float f;
        uint32_t b = (uint32_t)bits;
        memcpy(&f, &b, 4);
        value = f;
    }
    if (a32_fp_imm(value)) {
        bool d = a32_is_double(t);
        emit2(g, d ? A32_VMOV_F64 : A32_VMOV_F32, d ? a32_dreg(reg) : a32_sreg(reg),
              a32_fpimm(value));
        return;
    }
    gen_li(g, lo, (uint32_t)bits);
    if (!a32_is_double(t)) {
        emit2(g, A32_VMOV, a32_sreg(reg), a32_reg(lo));
        return;
    }
    gen_li(g, hi, (uint32_t)(bits >> 32));
    emit3(g, A32_VMOV, a32_dreg(reg), a32_reg(lo), a32_reg(hi));
}

void move_reg(Gen *g, int dst, int src, const Tac_Type *t)
{
    if (dst == src)
        return;
    bool dv = a32_is_vfp(dst), sv = a32_is_vfp(src);
    if (!dv && !sv)
        emit2(g, A32_MOV, a32_reg(dst), a32_reg(src));
    else if (dv && sv && a32_is_double(t))
        emit2(g, A32_VMOV_F64, a32_dreg(dst), a32_dreg(src));
    else if (dv && sv)
        emit2(g, A32_VMOV_F32, a32_sreg(dst), a32_sreg(src));
    else
        emit2(g, A32_VMOV, dv ? a32_sreg(dst) : a32_reg(dst), sv ? a32_sreg(src) : a32_reg(src));
}

void gen_canon(Gen *g, int dst, int src, const Tac_Type *t)
{
    int size = a32_size(t);
    if (size == 1)
        emit2(g, a32_is_unsigned(t) ? A32_UXTB : A32_SXTB, a32_reg(dst), a32_reg(src));
    else if (size == 2)
        emit2(g, a32_is_unsigned(t) ? A32_UXTH : A32_SXTH, a32_reg(dst), a32_reg(src));
    else
        move_reg(g, dst, src, t);
}

void load_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as)
{
    const Tac_Type *t = val_type(g, v);
    int r             = var_reg(g, v);
    if (r >= 0) {
        move_reg(g, reg, r, t);
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT) {
        if (!as)
            as = t;
        if (a32_is_vfp(reg))
            load_fp_const(g, reg, v->u.constant, as, T0, T1);
        else
            gen_li(g, reg, (uint32_t)const_bits(v->u.constant, as));
        return;
    }
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, a32_is_vfp(reg) ? T0 : reg, &base, &off);
    load_mem(g, reg, t, base, off);
}

void load_val(Gen *g, int reg, const Tac_Val *v)
{
    load_as(g, reg, v, NULL);
}

void store_val(Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    if (r >= 0) {
        const Tac_Type *t = val_type(g, v);
        if (!a32_is_vfp(r) && !a32_is_vfp(reg))
            gen_canon(g, r, reg, t);
        else
            move_reg(g, r, reg, t);
        return;
    }
    int scratch = reg == T0 ? T1 : T0;
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, scratch, &base, &off);
    store_mem(g, reg, name_type(g, v->u.var_name), base, off, scratch);
}

// Whether a register of `reg`'s file holds `v`'s value.
static bool same_file(const Gen *g, int reg, const Tac_Val *v)
{
    int r = var_reg(g, v);
    return r >= 0 && a32_is_vfp(r) == a32_is_vfp(reg);
}

int use_val(Gen *g, int scratch, const Tac_Val *v)
{
    return use_as(g, scratch, v, NULL);
}

int use_as(Gen *g, int scratch, const Tac_Val *v, const Tac_Type *as)
{
    if (same_file(g, scratch, v))
        return var_reg(g, v);
    load_as(g, scratch, v, as);
    return scratch;
}

int def_reg(const Gen *g, int scratch, const Tac_Val *v)
{
    return same_file(g, scratch, v) ? var_reg(g, v) : scratch;
}

int use_word(Gen *g, int scratch, const Tac_Val *v, const Tac_Type *as, int half)
{
    if (var_reg_hi(g, v) >= 0)
        return half ? var_reg_hi(g, v) : var_reg(g, v);
    load_word(g, scratch, v, as, half);
    return scratch;
}

void load_word(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as, int half)
{
    const Tac_Type *t = val_type(g, v);
    int r             = var_reg(g, v);
    if (r >= 0 && var_reg_hi(g, v) >= 0) {
        move_reg(g, reg, half ? var_reg_hi(g, v) : r, t);
        return;
    }
    if (r >= 0 && a32_is_double(t)) {
        emit2(g, A32_VMOV, a32_reg(reg), a32_sreg(r + half));
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT) {
        gen_li(g, reg, (uint32_t)(const_bits(v->u.constant, as) >> (32 * half)));
        return;
    }
    if (a32_size(t) <= 4) {
        // A narrower integer converted to long long: extended by its type.
        load_val(g, reg, v);
        if (half && a32_is_unsigned(t))
            gen_li(g, reg, 0);
        else if (half)
            emit2(g, A32_MOV, a32_reg(reg), a32_shift(reg, A32_SHIFT_ASR, 31));
        return;
    }
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, reg, &base, &off);
    emit2(g, A32_LDR, a32_reg(reg), mem(g, A32_LDR, base, off + 4 * half, reg));
}

// A scratch register other than `a` and `b`.
static int other_scratch(int a, int b)
{
    return a != T0 && b != T0 ? T0 : a != T1 && b != T1 ? T1 : T2;
}

void store_pair(Gen *g, const Tac_Val *dst, int lo, int hi)
{
    int r = var_reg(g, dst);
    if (r >= 0 && a32_is_vfp(r)) {
        emit3(g, A32_VMOV, a32_dreg(r), a32_reg(lo), a32_reg(hi));
        return;
    }
    if (r >= 0) {
        Move m[2] = { { r, lo, MOVE_CORE }, { var_reg_hi(g, dst), hi, MOVE_CORE } };
        parallel_move(g, m, 2);
        return;
    }
    int scratch = other_scratch(lo, hi);
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, scratch, &base, &off);
    emit2(g, A32_STR, a32_reg(lo), mem(g, A32_STR, base, off, scratch));
    emit2(g, A32_STR, a32_reg(hi), mem(g, A32_STR, base, off + 4, scratch));
}

// The registers move `m` reads, or writes: a bit each of r0-r15 and s0-s31.
static uint64_t footprint(int reg, bool d)
{
    return (d ? 3ull : 1ull) << reg;
}

static uint64_t src_set(const Move *m)
{
    return footprint(m->src, m->kind == MOVE_D);
}

static uint64_t dst_set(const Move *m)
{
    return footprint(m->dst, m->kind == MOVE_D);
}

static void emit_move(Gen *g, const Move *m)
{
    static const Tac_Type f = { .kind = TAC_TYPE_FLOAT }, d = { .kind = TAC_TYPE_DOUBLE },
                          i = { .kind = TAC_TYPE_INT };
    switch (m->kind) {
    case MOVE_HI_SIGN:
        emit2(g, A32_MOV, a32_reg(m->dst), a32_shift(m->src, A32_SHIFT_ASR, 31));
        break;
    case MOVE_D:
        move_reg(g, m->dst, m->src, &d);
        break;
    case MOVE_S:
    case MOVE_S_TO_CORE:
        move_reg(g, m->dst, m->src, &f);
        break;
    default:
        move_reg(g, m->dst, m->src, &i);
        break;
    }
}

// A move goes when no other still reads its destination.  When every one waits, one
// whose source another writes has that source copied to a scratch register first.
void parallel_move(Gen *g, Move *m, int n)
{
    while (n > 0) {
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++) {
            bool blocked = false;
            for (int j = 0; j < n && !blocked; j++)
                blocked = j != i && (src_set(&m[j]) & dst_set(&m[i]));
            if (!blocked)
                pick = i;
        }
        if (pick >= 0) {
            emit_move(g, &m[pick]);
            m[pick] = m[--n];
            continue;
        }
        int i = 0;
        for (int k = 0; k < n; k++)
            for (int j = 0; j < n; j++)
                if (j != k && (src_set(&m[k]) & dst_set(&m[j])))
                    i = k;
        bool vfp = a32_is_vfp(m[i].src), d = m[i].kind == MOVE_D;
        int tmp  = -1;
        const int cand[2] = { vfp ? F0 : T0, vfp ? F1 : T1 };
        for (int c = 0; c < 2 && tmp < 0; c++) {
            bool busy = false;
            for (int j = 0; j < n; j++)
                busy |= (src_set(&m[j]) & footprint(cand[c], vfp)) != 0;
            if (!busy)
                tmp = cand[c];
        }
        if (tmp < 0)
            fatal_error("arm32: %s: no scratch register for a parallel move", gen_name(g));
        Move save = { tmp, m[i].src, vfp ? (d ? MOVE_D : MOVE_S) : MOVE_CORE };
        emit_move(g, &save);
        m[i].src = tmp;
    }
}

void load_words(Gen *g, const WordLoad *w, int n)
{
    Move m[8];
    int nm = 0;
    for (int i = 0; i < n; i++) {
        const Tac_Type *t = val_type(g, w[i].v);
        int r             = var_reg(g, w[i].v);
        if (r < 0)
            continue;
        if (var_reg_hi(g, w[i].v) >= 0)
            m[nm++] = (Move){ w[i].reg, w[i].half ? var_reg_hi(g, w[i].v) : r, MOVE_CORE };
        else if (a32_is_vfp(r))
            m[nm++] = (Move){ w[i].reg, r + w[i].half, MOVE_S_TO_CORE };
        else if (!w[i].half)
            m[nm++] = (Move){ w[i].reg, r, MOVE_CORE };
        else if (!a32_is_unsigned(t))
            m[nm++] = (Move){ w[i].reg, r, MOVE_HI_SIGN };
    }
    parallel_move(g, m, nm);
    for (int i = 0; i < n; i++) {
        int r = var_reg(g, w[i].v);
        if (r < 0)
            load_word(g, w[i].reg, w[i].v, w[i].as, w[i].half);
        else if (w[i].half && var_reg_hi(g, w[i].v) < 0 && !a32_is_vfp(r) &&
                 a32_is_unsigned(val_type(g, w[i].v)))
            gen_li(g, w[i].reg, 0); // the high word of an unsigned int
    }
}

void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align)
{
    int data = other_scratch(dst, src);
    for (int i = 0; i < size;) {
        int chunk = align >= 4 && size - i >= 4 ? 4 : 1;
        A32_Op ld = chunk == 4 ? A32_LDR : A32_LDRB, st = chunk == 4 ? A32_STR : A32_STRB;
        // Out of reach, a base moves along: a scratch one in place, else into the
        // scratch register neither the data nor the other base is.
        if (!fits(ld, src_off + i)) {
            int r = src == T0 || src == T1 || src == T2 ? src : other_scratch(data, dst);
            gen_addr(g, r, src, src_off + i);
            src     = r;
            src_off = -i;
        }
        if (!fits(st, dst_off + i)) {
            int r = dst == T0 || dst == T1 || dst == T2 ? dst : other_scratch(data, src);
            gen_addr(g, r, dst, dst_off + i);
            dst     = r;
            dst_off = -i;
        }
        emit2(g, ld, a32_reg(data), a32_mem(src, src_off + i));
        emit2(g, st, a32_reg(data), a32_mem(dst, dst_off + i));
        i += chunk;
    }
}

void gen_epilogue(Gen *g)
{
    emit0(g, A32_EPILOGUE);
}

// Emit into block `b` instead of the current one: the prologue, or an epilogue.
static A32_Block *redirect(Gen *g, A32_Block *b)
{
    A32_Block *tail = g->fn->tail;
    g->fn->tail     = b;
    return tail;
}

// What the body needs saved, found by a look at its instructions, and how the frame is
// laid out: from r11 (a frame record), or from sp.
typedef struct {
    bool sp;        // addressed from sp
    bool frame;     // anything to set up at all
    bool fb;        // the frame base in use: a slot or a stack argument
    bool calls;     // a bl or blx: lr changes
    bool lr;        // lr in use as a scratch register
    bool sp_used;   // the outgoing area
    bool r10;       // the third scratch register
    unsigned dmask; // the VFP registers saved, as d registers: a range of d8-d15
    int locals;     // the slots' bytes (with r4-r9 of a frame record), 8-aligned
    // From r11:
    int core;  // the bytes of r4-r9 pushed with the frame record
    int saves; // the bytes of the VFP registers and r10 saved below the slots
    // From sp:
    unsigned push; // the core registers pushed: r4-r11 in use, and lr
    int below;     // the bytes of the slots and the outgoing area, below the saves
    int size;      // the bytes below the incoming arguments (and r0-r3 of a variadic)
} Frame;

static void note_reg(Frame *fr, int reg)
{
    if (reg == FB)
        fr->fb = true;
    else if (reg == A32_SP)
        fr->sp_used = true;
    else if (reg == A32_LR)
        fr->lr = true;
    else if (reg == T2)
        fr->r10 = true;
    else if (reg >= F0 && reg < F1 + 2)
        fr->dmask |= 1u << ((reg - A32_S0) / 2);
}

static Frame scan_body(const Gen *g)
{
    Frame fr = { .sp = g->sp_frame };
    for (const A32_Block *b = g->fn->blocks; b; b = b->next) {
        for (const A32_Instr *in = b->head; in; in = in->next) {
            if (in->op == A32_BL || in->op == A32_BLX)
                fr.calls = true;
            for (int i = 0; i < A32_MAX_OPERANDS; i++) {
                const A32_Operand *o = &in->opnd[i];
                if (o->kind == A32_OPND_REG || o->kind == A32_OPND_MEM || o->kind == A32_OPND_SHIFT)
                    note_reg(&fr, o->reg);
                if ((o->kind == A32_OPND_MEM || o->kind == A32_OPND_SHIFT) && o->reg2 >= 0)
                    note_reg(&fr, o->reg2);
            }
        }
    }
    // One vpush of a range: from the lowest d register saved to the highest.
    fr.dmask |= g->saved_vfp;
    if (fr.dmask) {
        int lo = ctz32(fr.dmask), hi = 31 - clz32(fr.dmask);
        fr.dmask = (2u << hi) - (1u << lo);
    }
    bool variadic = g->tl->u.function.variadic;
    int vfp       = 8 * popcount32(fr.dmask);
    fr.locals     = (g->locals_size + 7) / 8 * 8;
    if (!fr.sp) {
        fr.core  = 4 * popcount32(g->saved_core);
        fr.saves = vfp + (fr.r10 ? 4 : 0);
        fr.frame = fr.calls || fr.fb || fr.sp_used || fr.lr || fr.locals || fr.core ||
                   g->outgoing || fr.r10 || fr.dmask || variadic;
        return fr;
    }
    // sp stays 8-byte aligned for a call, and for the slots' alignment: by one more
    // register pushed, as clang does, while there is one.
    fr.push  = g->saved_core | (fr.r10 ? 1u << T2 : 0) | (fr.calls || fr.lr ? 1u << A32_LR : 0);
    fr.below = (g->outgoing + 7) / 8 * 8 + fr.locals;
    fr.size  = 4 * popcount32(fr.push) + vfp + fr.below;
    if ((fr.calls || fr.locals) && fr.size % 8) {
        int pad = A32_FP;
        while (pad >= A32_R4 && (fr.push & 1u << pad))
            pad--;
        if (pad >= A32_R4)
            fr.push |= 1u << pad;
        else
            fr.below += 4;
        fr.size += 4;
    }
    fr.frame = fr.size > 0 || variadic;
    return fr;
}

// The sp offset of frame offset `off` (from where r11 would point), or -1: a stack
// argument above everything the function pushed, a slot just above the outgoing area.
static int64_t sp_offset(const Gen *g, const Frame *fr, int64_t off)
{
    if (off >= 8)
        return fr->size + off - 8;
    if (off < 0 && off >= -fr->locals)
        return (g->outgoing + 7) / 8 * 8 + fr->locals + off;
    return -1;
}

// Whether every use of the frame base in the body can be rebased onto sp (a memory
// operand that still fits its instruction, or an add or sub of an immediate that is
// still one); with `apply`, do it.
static bool rebase_to_sp(const Gen *g, const Frame *fr, bool apply)
{
    for (A32_Block *b = g->fn->blocks; b; b = b->next) {
        for (A32_Instr *in = b->head; in; in = in->next) {
            for (int i = 0; i < A32_MAX_OPERANDS; i++) {
                A32_Operand *o = &in->opnd[i];
                if (o->kind != A32_OPND_REG && o->kind != A32_OPND_MEM && o->kind != A32_OPND_SHIFT)
                    continue;
                if ((o->kind != A32_OPND_REG && o->reg2 == FB) ||
                    (o->kind == A32_OPND_SHIFT && o->reg == FB))
                    return false;
                if (o->reg != FB)
                    continue;
                if (o->kind == A32_OPND_MEM) {
                    int64_t off = sp_offset(g, fr, o->imm);
                    if (o->sub != A32_MEM_OFFSET || off < 0 || !fits(in->op, off))
                        return false;
                    if (apply) {
                        o->reg = A32_SP;
                        o->imm = off;
                    }
                    continue;
                }
                if (i != 1 || (in->op != A32_ADD && in->op != A32_SUB) ||
                    in->opnd[2].kind != A32_OPND_IMM || in->opnd[3].kind != A32_OPND_NONE)
                    return false;
                int64_t off = sp_offset(g, fr, in->op == A32_SUB ? -in->opnd[2].imm : in->opnd[2].imm);
                if (off < 0 || !a32_operand2_imm((uint32_t)off))
                    return false;
                if (apply) {
                    in->op      = A32_ADD;
                    o->reg      = A32_SP;
                    in->opnd[2] = a32_imm(off);
                }
            }
        }
    }
    return true;
}

// The frame base is r11.
static void base_on_r11(const Gen *g)
{
    for (A32_Block *b = g->fn->blocks; b; b = b->next) {
        for (A32_Instr *in = b->head; in; in = in->next) {
            for (int i = 0; i < A32_MAX_OPERANDS; i++) {
                A32_Operand *o = &in->opnd[i];
                if (o->kind != A32_OPND_REG && o->kind != A32_OPND_MEM && o->kind != A32_OPND_SHIFT)
                    continue;
                if (o->reg == FB)
                    o->reg = A32_FP;
                if (o->kind != A32_OPND_REG && o->reg2 == FB)
                    o->reg2 = A32_FP;
            }
        }
    }
}

// The return sequence; just `bx lr` without a frame.  From r11: the registers saved
// below the slots back, then sp, r4-r9, r11 and pc as on entry.  From sp: sp back over
// the slots, then the saved registers, lr into pc.  A variadic function drops its r0-r3
// too, and returns through lr.
static void epilogue(Gen *g, const Frame *fr)
{
    bool variadic = g->tl->u.function.variadic;
    if (!fr->frame) {
        emit1(g, A32_BX, a32_reg(A32_LR));
        return;
    }
    unsigned push;
    if (fr->sp) {
        if (fr->below)
            gen_addr(g, A32_SP, A32_SP, fr->below);
        if (fr->dmask)
            emit1(g, A32_VPOP, a32_dreglist(fr->dmask));
        push = fr->push;
    } else {
        if (fr->saves) {
            gen_addr(g, A32_SP, A32_FP, -(fr->locals + fr->saves));
            if (fr->r10)
                emit1(g, A32_POP, a32_reglist(1u << T2));
            if (fr->dmask)
                emit1(g, A32_VPOP, a32_dreglist(fr->dmask));
        }
        gen_addr(g, A32_SP, A32_FP, -fr->core);
        push = g->saved_core | 1u << A32_FP | 1u << A32_LR;
    }
    if (!variadic && (push & 1u << A32_LR)) {
        emit1(g, A32_POP, a32_reglist((push & ~(1u << A32_LR)) | 1u << A32_PC));
        return;
    }
    if (push)
        emit1(g, A32_POP, a32_reglist(push));
    if (variadic)
        emit3(g, A32_ADD, a32_reg(A32_SP), a32_reg(A32_SP), a32_imm(16));
    emit1(g, A32_BX, a32_reg(A32_LR));
}

// Replace each epilogue marker by the return sequence.
static void expand_epilogues(Gen *g, const Frame *fr)
{
    for (A32_Block *b = g->fn->blocks; b; b = b->next) {
        for (A32_Instr **link = &b->head; *link;) {
            A32_Instr *marker = *link;
            if (marker->op != A32_EPILOGUE) {
                link = &marker->next;
                continue;
            }
            A32_Block seq   = { 0 };
            A32_Block *tail = redirect(g, &seq);
            epilogue(g, fr);
            g->fn->tail    = tail;
            seq.tail->next = marker->next;
            *link          = seq.head;
            link           = &seq.tail->next;
            xfree(marker);
        }
        b->tail = b->head;
        while (b->tail && b->tail->next)
            b->tail = b->tail->next;
    }
}

// A variadic function first pushes r0-r3, which then lie just below its stack
// arguments: one area of all its arguments, from r11 + 8.  From r11: push {r4-r9 in
// use, r11, lr}; r11 = the address of the saved r11; the slots; the VFP registers and
// r10 in use; the outgoing area, with sp 8-byte aligned.  From sp: push the core
// registers in use and lr, vpush the VFP ones, and move sp over the slots and the
// outgoing area; nothing at all in a function that needs none of it.
bool gen_prologue(Gen *g)
{
    Frame fr = scan_body(g);
    if (fr.sp && !rebase_to_sp(g, &fr, false))
        return false;
    if (fr.sp)
        rebase_to_sp(g, &fr, true);
    else
        base_on_r11(g);
    A32_Block *tail = redirect(g, g->prologue);
    if (g->tl->u.function.variadic)
        emit1(g, A32_PUSH, a32_reglist(0xf));
    if (fr.sp) {
        if (fr.push)
            emit1(g, A32_PUSH, a32_reglist(fr.push));
        if (fr.dmask)
            emit1(g, A32_VPUSH, a32_dreglist(fr.dmask));
        if (fr.below)
            gen_addr(g, A32_SP, A32_SP, -fr.below);
    } else if (fr.frame) {
        emit1(g, A32_PUSH, a32_reglist(g->saved_core | 1u << A32_FP | 1u << A32_LR));
        gen_addr(g, A32_FP, A32_SP, fr.core);
        if (fr.locals > fr.core)
            gen_addr(g, A32_SP, A32_SP, -(fr.locals - fr.core));
        if (fr.dmask)
            emit1(g, A32_VPUSH, a32_dreglist(fr.dmask));
        if (fr.r10)
            emit1(g, A32_PUSH, a32_reglist(1u << T2));
        int rest = (fr.locals + fr.saves + g->outgoing + 7) / 8 * 8 - fr.locals - fr.saves;
        // alloca's memory starts at the outgoing area rounded to 8: that much below the
        // saves, sp still 8-byte aligned.
        if (g->moves_sp)
            rest = (g->outgoing + 7) / 8 * 8 + (fr.locals + fr.saves) % 8;
        if (rest)
            gen_addr(g, A32_SP, A32_SP, -rest);
    }
    g->fn->tail = tail;
    expand_epilogues(g, &fr);
    return true;
}
