//
// Types, constants, the frame, and access to values in memory.
//
#include <stdio.h>
#include <string.h>

#include "float128.h"
#include "internal.h"
#include "xalloc.h"

int mmix_type_size(const Tac_Type *t)
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
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return 8;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * mmix_type_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 8;
}

int mmix_type_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return mmix_type_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return mmix_type_size(t);
    }
}

bool mmix_is_unsigned(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return true;
    default:
        return false;
    }
}

bool mmix_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool mmix_is_float(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT;
}

bool mmix_is_scalar(const Tac_Type *t)
{
    return t->kind != TAC_TYPE_ARRAY && t->kind != TAC_TYPE_STRUCTURE;
}

//
// Constants: setl and the like set one wyde and clear the others, inch and the like
// add one (into a zero wyde, so with no carry), andnl and the like clear bits of one.
// A value with few nonzero wydes is set and completed; one with few wydes other than
// #ffff starts from -1 (negu $x,0,1) and clears the rest; -1..-255 is one negu.
//
int mmix_const_steps(uint64_t value, ConstStep steps[4])
{
    static const Mmix_Op set[4]  = { MMIX_SETL, MMIX_SETML, MMIX_SETMH, MMIX_SETH };
    static const Mmix_Op inc[4]  = { MMIX_INCL, MMIX_INCML, MMIX_INCMH, MMIX_INCH };
    static const Mmix_Op andn[4] = { MMIX_ANDNL, MMIX_ANDNML, MMIX_ANDNMH, MMIX_ANDNH };
    int64_t sv                   = (int64_t)value;
    if (sv < 0 && sv >= -255) {
        steps[0] = (ConstStep){ MMIX_NEGU, (unsigned)-sv };
        return 1;
    }
    int nonzero = 0, notones = 0;
    for (int i = 0; i < 4; i++) {
        unsigned w = (value >> (16 * i)) & 0xffff;
        nonzero += w != 0;
        notones += w != 0xffff;
    }
    int n = 0;
    if (nonzero <= notones + 1) {
        for (int i = 0; i < 4; i++) {
            unsigned w = (value >> (16 * i)) & 0xffff;
            if (w || (value == 0 && i == 0)) {
                steps[n] = (ConstStep){ n == 0 ? set[i] : inc[i], w };
                n++;
            }
        }
        return n;
    }
    steps[n++] = (ConstStep){ MMIX_NEGU, 1 };
    for (int i = 0; i < 4; i++) {
        unsigned w = (value >> (16 * i)) & 0xffff;
        if (w != 0xffff)
            steps[n++] = (ConstStep){ andn[i], ~w & 0xffff };
    }
    return n;
}

void gen_const(Gen *g, int reg, uint64_t value)
{
    ConstStep steps[4];
    int n = mmix_const_steps(value, steps);
    for (int i = 0; i < n; i++) {
        if (steps[i].op == MMIX_NEGU)
            emit3(g, MMIX_NEGU, mmix_reg(reg), mmix_imm(0), mmix_imm(steps[i].arg));
        else
            emit2(g, steps[i].op, mmix_reg(reg), mmix_wyde(steps[i].arg));
    }
}

uint64_t const_bits(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_SCHAR:
        return (uint64_t)(int64_t)(int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_INT:
        return (uint64_t)(int64_t)(int32_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_LONG:
        return (uint64_t)c->u.long_val;
    case TAC_CONST_ULONG:
        return c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        return c->u.ulong_long_val;
    case TAC_CONST_FLOAT: {
        double d = (float)c->u.float_val; // its exact binary64 value
        uint64_t bits;
        memcpy(&bits, &d, 8);
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

uint64_t const_mem_bits(const Tac_Const *c)
{
    if (c->kind == TAC_CONST_FLOAT) {
        float f = (float)c->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    return const_bits(c);
}

static int unit_labels;

void gen_unit_begin(void)
{
    unit_labels = 0;
}

void new_label(char buf[32])
{
    snprintf(buf, 32, "L:x%d", unit_labels++);
}

char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, "L:");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
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
    g->fn       = mmix_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    g->leaf     = !makes_call(tl);
    mmix_new_block(g->fn, NULL); // the body
    new_label(g->exit);
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->consts);
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
            map_insert(&g->consts, t->u.static_constant.name, 1, 0);
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
    map_destroy(&g->consts);
    mmix_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

int ret_reg(const Gen *g)
{
    return g->leaf ? 0 : REG_A;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int off)
{
    Slot *s = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type = type;
    s->off  = off;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

static void free_nothing(intptr_t p)
{
    (void)p;
}

// A slot for `name` of `size` bytes aligned to `align`, above the ones placed so far.
static void add_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align)
{
    if (align > 1)
        g->frame_size = (g->frame_size + align - 1) & ~(align - 1);
    insert_slot(g, name, type, g->frame_size);
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

    int nparam = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, nparam++) {
        if (!p->type)
            fatal_error("mmix: %s: no type for %s", gen_name(g), p->name);
        if (nparam < MAX_REG_ARGS)
            add_slot(g, p->name, p->type, mmix_type_size(p->type), mmix_type_align(p->type));
    }
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("mmix: %s: no type for %s", gen_name(g), p->name);
        if (find_slot(g, p->name))
            continue;
        int size = mmix_type_size(p->type), align = mmix_type_align(p->type);
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
    g->frame_size = (g->frame_size + 7) & ~7;

    // The 17th parameter and later stay where they came in, an 8-byte slot each above
    // the frame, a narrow value in its low-order (last) bytes.
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        if (i < MAX_REG_ARGS)
            continue;
        int size = mmix_type_size(p->type);
        int off  = g->frame_size + 8 * (i - MAX_REG_ARGS) + (size < 8 ? 8 - size : 0);
        insert_slot(g, p->name, p->type, off);
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
    fatal_error("mmix: %s: no type for %s", gen_name(g), name);
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

Mmix_Instr *emit0(Gen *g, Mmix_Op op)
{
    return mmix_append(g->fn, op);
}

Mmix_Instr *emit1(Gen *g, Mmix_Op op, Mmix_Operand a)
{
    Mmix_Instr *in = emit0(g, op);
    in->opnd[0]    = a;
    return in;
}

Mmix_Instr *emit2(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b)
{
    Mmix_Instr *in = emit1(g, op, a);
    in->opnd[1]    = b;
    return in;
}

Mmix_Instr *emit3(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b, Mmix_Operand c)
{
    Mmix_Instr *in = emit2(g, op, a, b);
    in->opnd[2]    = c;
    return in;
}

void add_offset(Gen *g, int reg, int base, int64_t off)
{
    if (off >= 0 && off <= 255) {
        emit3(g, MMIX_ADDU, mmix_reg(reg), mmix_reg(base), mmix_imm(off));
    } else if (off < 0 && off >= -255) {
        emit3(g, MMIX_SUBU, mmix_reg(reg), mmix_reg(base), mmix_imm(-off));
    } else {
        gen_const(g, MMIX_TMP, (uint64_t)off);
        emit3(g, MMIX_ADDU, mmix_reg(reg), mmix_reg(base), mmix_reg(MMIX_TMP));
    }
}

void mem_op_at(Gen *g, Mmix_Op op, int reg, int base, int64_t off)
{
    if (off >= 0 && off <= 255) {
        emit3(g, op, mmix_reg(reg), mmix_reg(base), mmix_imm(off));
        return;
    }
    gen_const(g, MMIX_TMP, (uint64_t)off);
    emit3(g, op, mmix_reg(reg), mmix_reg(base), mmix_reg(MMIX_TMP));
}

static bool is_const_object(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->consts, name, &v);
}

void mem_op(Gen *g, Mmix_Op op, int reg, const char *name, int64_t off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        mem_op_at(g, op, reg, MMIX_SP, s->off + off);
        return;
    }
    if (name[0] == '%')
        fatal_error("mmix: %s: no slot for %s", gen_name(g), name);
    if (is_const_object(g, name)) {
        // .rodata is in the text segment, which geta reaches without a base register.
        emit2(g, MMIX_GETA, mmix_reg(MMIX_TMP), mmix_sym(name, 0));
        if (off >= 0 && off <= 255) {
            emit3(g, op, mmix_reg(reg), mmix_reg(MMIX_TMP), mmix_imm(off));
        } else {
            add_offset(g, MMIX_TMP, MMIX_TMP, off);
            emit3(g, op, mmix_reg(reg), mmix_reg(MMIX_TMP), mmix_imm(0));
        }
        return;
    }
    emit2(g, op, mmix_reg(reg), mmix_sym(name, off));
}

Mmix_Op load_op_ext(int size, bool sign)
{
    switch (size) {
    case 1:
        return sign ? MMIX_LDB : MMIX_LDBU;
    case 2:
        return sign ? MMIX_LDW : MMIX_LDWU;
    case 4:
        return sign ? MMIX_LDT : MMIX_LDTU;
    default:
        return MMIX_LDO;
    }
}

Mmix_Op load_op(const Tac_Type *t)
{
    if (mmix_is_float(t))
        return MMIX_LDSF;
    return load_op_ext(mmix_type_size(t), !mmix_is_unsigned(t));
}

Mmix_Op store_op(const Tac_Type *t)
{
    if (mmix_is_float(t))
        return MMIX_STSF;
    switch (mmix_type_size(t)) {
    case 1:
        return MMIX_STBU;
    case 2:
        return MMIX_STWU;
    case 4:
        return MMIX_STTU;
    default:
        return MMIX_STO;
    }
}

// A function's address is in the text segment, which geta reaches.
static bool is_function(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->globals, name, &v) && ((const Tac_Type *)v)->kind == TAC_TYPE_FUN_TYPE;
}

void address_of(Gen *g, int reg, const char *name, int64_t off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        add_offset(g, reg, MMIX_SP, s->off + off);
        return;
    }
    if (name[0] == '%')
        fatal_error("mmix: %s: no slot for %s", gen_name(g), name);
    if (is_const_object(g, name) || is_function(g, name)) {
        emit2(g, MMIX_GETA, mmix_reg(reg), mmix_sym(name, 0));
        if (off)
            add_offset(g, reg, reg, off);
        return;
    }
    emit2(g, MMIX_LDA, mmix_reg(reg), mmix_sym(name, off));
}

void load_val(Gen *g, const Tac_Val *v, int reg)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        gen_const(g, reg, const_bits(v->u.constant));
        return;
    }
    const Tac_Type *t = val_type(g, v);
    if (!mmix_is_scalar(t))
        fatal_error("mmix: %s: %s is not a scalar", gen_name(g), v->u.var_name);
    mem_op(g, load_op(t), reg, v->u.var_name, 0);
}

uint64_t const_as(const Tac_Const *c, const Tac_Type *t)
{
    uint64_t bits = const_bits(c);
    if (!t || mmix_is_fp(t) || !mmix_is_scalar(t))
        return bits;
    int size = mmix_type_size(t);
    if (size < 8) {
        int shift = 64 - 8 * size;
        bits      = mmix_is_unsigned(t) ? bits << shift >> shift
                                        : (uint64_t)((int64_t)(bits << shift) >> shift);
    }
    return bits;
}

void load_val_as(Gen *g, const Tac_Val *v, int reg, const Tac_Type *t)
{
    const Tac_Type *vt = val_type(g, v);
    if (mmix_is_fp(vt) || !t || mmix_is_fp(t) || !mmix_is_scalar(t)) {
        load_val(g, v, reg);
        return;
    }
    if (v->kind == TAC_VAL_CONSTANT) {
        gen_const(g, reg, const_as(v->u.constant, t));
        return;
    }
    // A variable of the operation's width but the other signedness (copy propagation
    // through a cast that emitted nothing) is extended as the operation's type says.
    int size = mmix_type_size(vt);
    if (mmix_is_scalar(vt) && size == mmix_type_size(t) && size < 8)
        mem_op(g, load_op_ext(size, !mmix_is_unsigned(t)), reg, v->u.var_name, 0);
    else
        load_val(g, v, reg);
}

Mmix_Operand val_operand(Gen *g, const Tac_Val *v, int reg, const Tac_Type *t)
{
    if (v->kind == TAC_VAL_CONSTANT && !mmix_is_fp(val_type(g, v))) {
        uint64_t bits = const_as(v->u.constant, t);
        if (bits <= 255)
            return mmix_imm((int64_t)bits);
    }
    load_val_as(g, v, reg, t);
    return mmix_reg(reg);
}

void store_val(Gen *g, int reg, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_VAR)
        fatal_error("mmix: %s: store to a constant", gen_name(g));
    mem_op(g, store_op(val_type(g, v)), reg, v->u.var_name, 0);
}

enum { UNROLL = 16 }; // the most moves of a copy unrolled

static Mmix_Op unit_load(int unit)
{
    return load_op_ext(unit, false);
}

static Mmix_Op unit_store(int unit)
{
    return unit == 1 ? MMIX_STBU : unit == 2 ? MMIX_STWU : unit == 4 ? MMIX_STTU : MMIX_STO;
}

// The widest unit, at most 8 bytes, that `align` and `size` allow.
static int copy_unit(int size, int align)
{
    int unit = align >= 8 ? 8 : align >= 4 ? 4 : align >= 2 ? 2 : 1;
    while (size % unit)
        unit /= 2;
    return unit;
}

void copy_bytes(Gen *g, int size, int align)
{
    int unit = copy_unit(size, align), n = size / unit;
    if (n > UNROLL) {
        // A counted loop: $255 counts down, $1 carries the unit.
        char loop[32];
        new_label(loop);
        gen_const(g, MMIX_TMP, (uint64_t)n);
        gen_label_block(g, loop);
        emit3(g, unit_load(unit), mmix_reg(REG_A), mmix_reg(REG_B), mmix_imm(0));
        emit3(g, unit_store(unit), mmix_reg(REG_A), mmix_reg(REG_C), mmix_imm(0));
        emit3(g, MMIX_ADDU, mmix_reg(REG_B), mmix_reg(REG_B), mmix_imm(unit));
        emit3(g, MMIX_ADDU, mmix_reg(REG_C), mmix_reg(REG_C), mmix_imm(unit));
        emit3(g, MMIX_SUBU, mmix_reg(MMIX_TMP), mmix_reg(MMIX_TMP), mmix_imm(1));
        emit2(g, MMIX_PBNZ, mmix_reg(MMIX_TMP), mmix_label(loop));
        return;
    }
    for (int k = 0; k < n; k++) {
        mem_op_at(g, unit_load(unit), REG_A, REG_B, k * unit);
        mem_op_at(g, unit_store(unit), REG_A, REG_C, k * unit);
    }
}

void copy_named(Gen *g, const char *dst, int64_t doff, const char *src, int64_t soff, int size,
                int align)
{
    while (align > 1 && ((doff | soff) & (align - 1)))
        align /= 2;
    int unit = copy_unit(size, align), n = size / unit;
    if (n > UNROLL) {
        address_of(g, REG_B, src, soff);
        address_of(g, REG_C, dst, doff);
        copy_bytes(g, size, unit);
        return;
    }
    for (int k = 0; k < n; k++) {
        mem_op(g, unit_load(unit), REG_A, src, soff + k * unit);
        mem_op(g, unit_store(unit), REG_A, dst, doff + k * unit);
    }
}

void gen_label_block(Gen *g, const char *label)
{
    mmix_new_block(g->fn, label);
}

// Make `b` the block instructions go to; returns the one before.
static Mmix_Block *switch_block(Gen *g, Mmix_Block *b)
{
    Mmix_Block *was = g->fn->tail;
    g->fn->tail     = b;
    return was;
}

// `op` $254,$254,N: the frame, allocated or freed.
static void adjust_sp(Gen *g, Mmix_Op op)
{
    if (g->frame_size <= 255) {
        emit3(g, op, mmix_reg(MMIX_SP), mmix_reg(MMIX_SP), mmix_imm(g->frame_size));
    } else {
        gen_const(g, MMIX_TMP, (uint64_t)g->frame_size);
        emit3(g, op, mmix_reg(MMIX_SP), mmix_reg(MMIX_SP), mmix_reg(MMIX_TMP));
    }
}

// Whether the function returns a scalar: then pop hands back $0.
static bool returns_value(const Gen *g)
{
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_RETURN && in->u.return_.src)
            return mmix_is_scalar(val_type(g, in->u.return_.src));
    return false;
}

// Whether an instruction of `fn` names label `label`.
static bool label_used(const Mmix_Func *fn, const char *label)
{
    for (const Mmix_Block *b = fn->blocks; b; b = b->next)
        for (const Mmix_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < MMIX_MAX_OPERANDS; i++)
                if (in->opnd[i].kind == MMIX_OPND_LABEL && strcmp(in->opnd[i].sym, label) == 0)
                    return true;
    return false;
}

void gen_frame(Gen *g)
{
    // The prologue: the frame first, then the register parameters into their slots,
    // then rJ into $0, which the first of them has left.  The prologue block is not the
    // last, so nothing here may start a block.
    Mmix_Block *body = switch_block(g, g->prologue);
    if (g->frame_size)
        adjust_sp(g, MMIX_SUBU);
    store_params(g);
    if (!g->leaf)
        emit2(g, MMIX_GET, mmix_reg(REG_RJ), mmix_special(MMIX_rJ));
    switch_block(g, body);

    // The epilogue: rJ back, the result into $0, the frame freed.
    mmix_new_block(g->fn, label_used(g->fn, g->exit) ? g->exit : NULL);
    bool value = returns_value(g);
    if (!g->leaf) {
        emit2(g, MMIX_PUT, mmix_special(MMIX_rJ), mmix_reg(REG_RJ));
        if (value)
            emit2(g, MMIX_SET, mmix_reg(0), mmix_reg(REG_A));
    }
    if (g->frame_size)
        adjust_sp(g, MMIX_ADDU);
    emit2(g, MMIX_POP, mmix_imm(value ? 1 : 0), mmix_imm(0));
}
