//
// Types, the frame, and access to values in memory.
//
#include <stdio.h>
#include <string.h>

#include "float128.h"
#include "internal.h"
#include "xalloc.h"

int avr_type_size(const Tac_Type *t)
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
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
        return 4;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
        return 8;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * avr_type_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 2;
}

bool avr_is_unsigned(const Tac_Type *t)
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

bool avr_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool avr_is_scalar(const Tac_Type *t)
{
    return t->kind != TAC_TYPE_ARRAY && t->kind != TAC_TYPE_STRUCTURE;
}

int block_a(int size)
{
    return size == 1 ? 24 : 26 - (size + 1) / 2 * 2;
}

int block_b(int size)
{
    return size == 1 ? 22 : 26 - 2 * ((size + 1) / 2 * 2);
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
    g->fn       = avr_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    avr_new_block(g->fn, NULL); // the body
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
    avr_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int q)
{
    Slot *s = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type = type;
    s->q    = q;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

// The stack arguments start above the slots, the saved Y and the return address; the
// slots' size is not known yet, so the displacement is completed by layout_frame.
enum { STACK_PARAM = 1 << 20 };

void place_stack_param(Gen *g, const char *name, const Tac_Type *type, int off)
{
    insert_slot(g, name, type, STACK_PARAM + off);
}

// The size of the slot of local `p`: its type's, or more for an ALLOCATE_LOCAL.
static int local_size(const Tac_Param *p, const StringMap *allocs)
{
    int size = avr_type_size(p->type);
    intptr_t v;
    if (map_get(allocs, p->name, &v)) {
        const Tac_Instruction *in = (const Tac_Instruction *)v;
        if (in->u.allocate_local.size > size)
            size = in->u.allocate_local.size;
    }
    return size;
}

static bool is_aggregate_local(const Tac_Param *p, const StringMap *allocs)
{
    intptr_t v;
    return !avr_is_scalar(p->type) || map_get(allocs, p->name, &v);
}

static void free_nothing(intptr_t p)
{
    (void)p;
}

void layout_frame(Gen *g)
{
    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    // The register parameters first (the stack ones have their slots), then scalars,
    // so that the common case stays within Y+63, then aggregates.
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
        if (!find_slot(g, p->name)) {
            insert_slot(g, p->name, p->type, 1 + g->frame_size);
            g->frame_size += avr_type_size(p->type);
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
            if (!p->type)
                fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
            if (is_aggregate_local(p, &allocs) != (pass == 1) || find_slot(g, p->name))
                continue;
            insert_slot(g, p->name, p->type, 1 + g->frame_size);
            g->frame_size += local_size(p, &allocs);
        }
    }
    map_destroy_free(&allocs, free_nothing);

    // The stack parameters, now that the slots' size is known.
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        intptr_t v;
        if (map_get(&g->frame, p->name, &v) && ((Slot *)v)->q >= STACK_PARAM)
            ((Slot *)v)->q += g->frame_size + 5 - STACK_PARAM;
    }
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
    fatal_error("avr: %s: no type for %s", gen_name(g), name);
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

bool is_function(const Gen *g, const char *name)
{
    if (find_slot(g, name))
        return false;
    intptr_t v;
    return map_get(&g->globals, name, &v) && ((const Tac_Type *)v)->kind == TAC_TYPE_FUN_TYPE;
}

AVR_Instr *emit0(Gen *g, AVR_Op op)
{
    return avr_append(g->fn, op);
}

AVR_Instr *emit1(Gen *g, AVR_Op op, AVR_Operand a)
{
    AVR_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

AVR_Instr *emit2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b)
{
    AVR_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

void gen_li(Gen *g, int reg, int imm)
{
    imm &= 0xff;
    if (reg >= 16) {
        emit2(g, AVR_LDI, avr_reg(reg), avr_imm(imm));
    } else if (imm == 0) {
        emit2(g, AVR_MOV, avr_reg(reg), avr_reg(AVR_ZERO));
    } else {
        emit2(g, AVR_LDI, avr_reg(AVR_X), avr_imm(imm));
        emit2(g, AVR_MOV, avr_reg(reg), avr_reg(AVR_X));
    }
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
    case TAC_CONST_FLOAT:
    case TAC_CONST_DOUBLE: {
        float f = (float)(c->kind == TAC_CONST_FLOAT ? c->u.float_val : c->u.double_val);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    case TAC_CONST_LONG_DOUBLE: {
        float f = f128_to_float(c->u.long_double_val);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    }
    return 0;
}

// Point pointer register `ptr` (X or Z) at Y+q.
static void address_slot(Gen *g, int ptr, int q)
{
    emit2(g, AVR_MOVW, avr_reg(ptr), avr_reg(AVR_Y));
    if (q <= Y_MAX) {
        emit2(g, AVR_ADIW, avr_reg(ptr), avr_imm(q));
    } else {
        emit2(g, AVR_SUBI, avr_reg(ptr), avr_imm(-q & 0xff));
        emit2(g, AVR_SBCI, avr_reg(ptr + 1), avr_imm((-q >> 8) & 0xff));
    }
}

void access_bytes(Gen *g, bool store, const char *name, int off, int reg, int n)
{
    const Slot *s = find_slot(g, name);
    if (!s) {
        if (name[0] == '%')
            fatal_error("avr: %s: no slot for %s", gen_name(g), name);
        for (int i = 0; i < n; i++) {
            if (store)
                emit2(g, AVR_STS, avr_sym(AVR_MOD_NONE, name, off + i), avr_reg(reg + i));
            else
                emit2(g, AVR_LDS, avr_reg(reg + i), avr_sym(AVR_MOD_NONE, name, off + i));
        }
        return;
    }
    int q = s->q + off;
    if (q + n - 1 <= Y_MAX) {
        for (int i = 0; i < n; i++) {
            if (store)
                emit2(g, AVR_STD, avr_disp(AVR_Y, q + i), avr_reg(reg + i));
            else
                emit2(g, AVR_LDD, avr_reg(reg + i), avr_disp(AVR_Y, q + i));
        }
        return;
    }
    // Past Y+63: through Z, or X when the registers are Z's.
    int ptr = reg + n > AVR_Z ? AVR_X : AVR_Z;
    address_slot(g, ptr, q);
    for (int i = 0; i < n; i++) {
        if (store)
            emit2(g, AVR_ST, avr_ptr(ptr, AVR_PTR_POST_INC), avr_reg(reg + i));
        else
            emit2(g, AVR_LD, avr_reg(reg + i), avr_ptr(ptr, AVR_PTR_POST_INC));
    }
}

void extend_regs(Gen *g, int reg, int from, int n, bool sign)
{
    if (from >= n)
        return;
    if (!sign) {
        for (int i = from; i < n; i++)
            emit2(g, AVR_MOV, avr_reg(reg + i), avr_reg(AVR_ZERO));
        return;
    }
    // The sign of the top byte into C, then 0 - C: 0x00 or 0xff.
    int top = reg + from;
    emit2(g, AVR_MOV, avr_reg(top), avr_reg(reg + from - 1));
    emit1(g, AVR_LSL, avr_reg(top));
    emit2(g, AVR_SBC, avr_reg(top), avr_reg(top));
    for (int i = from + 1; i < n; i++)
        emit2(g, AVR_MOV, avr_reg(reg + i), avr_reg(top));
}

void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext)
{
    const Tac_Type *t = val_type(g, v);
    int size          = avr_type_size(t);
    if (v->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_bits(v->u.constant);
        if (size < 8 && n > size && ext != EXT_TYPE) {
            uint64_t mask = (1ull << (8 * size)) - 1;
            bool neg      = ext == EXT_SIGN && (bits >> (8 * size - 1) & 1);
            bits          = neg ? bits | ~mask : bits & mask;
        }
        for (int i = 0; i < n; i++)
            gen_li(g, reg + i, (int)(i < 8 ? bits >> (8 * i) & 0xff : 0));
        return;
    }
    int m = size < n ? size : n;
    access_bytes(g, false, v->u.var_name, 0, reg, m);
    bool sign = ext == EXT_SIGN || (ext == EXT_TYPE && !avr_is_unsigned(t) && !avr_is_fp(t));
    extend_regs(g, reg, m, n, sign);
}

void store_val(Gen *g, const Tac_Val *v, int reg, int n)
{
    if (v->kind != TAC_VAL_VAR)
        fatal_error("avr: %s: store to a constant", gen_name(g));
    int size = avr_type_size(val_type(g, v));
    int m    = size < n ? size : n;
    access_bytes(g, true, v->u.var_name, 0, reg, m);
    for (int i = m; i < size; i++)
        access_bytes(g, true, v->u.var_name, i, AVR_ZERO, 1);
}

void move_regs(Gen *g, int rd, int rs, int n)
{
    if (rd == rs)
        return;
    for (int i = 0; i < n;) {
        int d = rd < rs ? i : n - 1 - i; // overlapping ranges: the far end first
        if (n - i >= 2 && (rd + d) % 2 == 0 && (rs + d) % 2 == 0 && rd < rs) {
            emit2(g, AVR_MOVW, avr_reg(rd + d), avr_reg(rs + d));
            i += 2;
        } else if (n - i >= 2 && rd > rs && (rd + d - 1) % 2 == 0 && (rs + d - 1) % 2 == 0) {
            emit2(g, AVR_MOVW, avr_reg(rd + d - 1), avr_reg(rs + d - 1));
            i += 2;
        } else {
            emit2(g, AVR_MOV, avr_reg(rd + d), avr_reg(rs + d));
            i++;
        }
    }
}

void gen_label_block(Gen *g, const char *label)
{
    avr_new_block(g->fn, label);
}

static void pro1(Gen *g, AVR_Op op, AVR_Operand a)
{
    avr_append_to(g->prologue, op)->opnd[0] = a;
}

static AVR_Instr *pro2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b)
{
    AVR_Instr *in = avr_append_to(g->prologue, op);
    in->opnd[0]   = a;
    in->opnd[1]   = b;
    return in;
}

// Y += delta, on the block being emitted (`pro` for the prologue).
static void adjust_y(Gen *g, bool pro, int delta)
{
    AVR_Instr *(*e)(Gen *, AVR_Op, AVR_Operand, AVR_Operand) = pro ? pro2 : emit2;
    if (delta >= -Y_MAX && delta < 0) {
        e(g, AVR_SBIW, avr_reg(AVR_Y), avr_imm(-delta));
    } else if (delta > 0 && delta <= Y_MAX) {
        e(g, AVR_ADIW, avr_reg(AVR_Y), avr_imm(delta));
    } else {
        e(g, AVR_SUBI, avr_reg(AVR_Y), avr_imm(-delta & 0xff));
        e(g, AVR_SBCI, avr_reg(AVR_Y + 1), avr_imm((-delta >> 8) & 0xff));
    }
}

// SP = Y, with interrupts held off between the halves (the I flag is restored before
// the low half, which the next instruction still completes).
static void write_sp(Gen *g, bool pro)
{
    AVR_Instr *(*e)(Gen *, AVR_Op, AVR_Operand, AVR_Operand) = pro ? pro2 : emit2;
    e(g, AVR_IN, avr_reg(AVR_TMP), avr_sym(AVR_MOD_NONE, "__SREG__", 0));
    if (pro)
        avr_append_to(g->prologue, AVR_CLI);
    else
        emit0(g, AVR_CLI);
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_H__", 0), avr_reg(AVR_Y + 1));
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SREG__", 0), avr_reg(AVR_TMP));
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_L__", 0), avr_reg(AVR_Y));
}

// The call-saved registers r2-r17 the body uses, as a bit mask.
static uint32_t saved_regs(const Gen *g)
{
    uint32_t mask = 0;
    for (const AVR_Block *b = g->fn->blocks; b; b = b->next)
        for (const AVR_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < AVR_MAX_OPERANDS; i++) {
                const AVR_Operand *o = &in->opnd[i];
                if (o->kind != AVR_OPND_REG || o->reg < 2 || o->reg > 17)
                    continue;
                mask |= 1u << o->reg;
                if (in->op == AVR_MOVW)
                    mask |= 1u << (o->reg + 1);
            }
    return mask;
}

void gen_frame(Gen *g)
{
    uint32_t saved = saved_regs(g);

    // Prologue: save Y, point it at the slots, then save the registers.
    pro1(g, AVR_PUSH, avr_reg(AVR_Y));
    pro1(g, AVR_PUSH, avr_reg(AVR_Y + 1));
    pro2(g, AVR_IN, avr_reg(AVR_Y), avr_sym(AVR_MOD_NONE, "__SP_L__", 0));
    pro2(g, AVR_IN, avr_reg(AVR_Y + 1), avr_sym(AVR_MOD_NONE, "__SP_H__", 0));
    if (g->frame_size) {
        adjust_y(g, true, -g->frame_size);
        write_sp(g, true);
    }
    for (int r = 2; r <= 17; r++)
        if (saved & (1u << r))
            pro1(g, AVR_PUSH, avr_reg(r));

    // Epilogue, the reverse.
    avr_new_block(g->fn, g->exit);
    for (int r = 17; r >= 2; r--)
        if (saved & (1u << r))
            emit1(g, AVR_POP, avr_reg(r));
    if (g->frame_size) {
        adjust_y(g, false, g->frame_size);
        write_sp(g, false);
    }
    emit1(g, AVR_POP, avr_reg(AVR_Y + 1));
    emit1(g, AVR_POP, avr_reg(AVR_Y));
    emit0(g, AVR_RET);
}
