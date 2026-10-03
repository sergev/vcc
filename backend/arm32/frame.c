//
// Types, frame slots, loading and storing values, the prologue and epilogue.
//
#include <string.h>

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
    a32_free_func(g->fn);
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
        int p          = __builtin_ctz(abs) & ~1;
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
    if (s) {
        *base = A32_FP;
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

void load_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as)
{
    const Tac_Type *t = val_type(g, v);
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
    int scratch = reg == T0 ? T1 : T0;
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, scratch, &base, &off);
    store_mem(g, reg, name_type(g, v->u.var_name), base, off, scratch);
}

void load_word(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as, int half)
{
    const Tac_Type *t = val_type(g, v);
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
    int scratch = other_scratch(lo, hi);
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, scratch, &base, &off);
    emit2(g, A32_STR, a32_reg(lo), mem(g, A32_STR, base, off, scratch));
    emit2(g, A32_STR, a32_reg(hi), mem(g, A32_STR, base, off + 4, scratch));
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

// What the body needs saved, found by a look at its instructions.
typedef struct {
    bool frame;     // the frame record: a slot, a call, sp or lr in use
    bool r10;       // the third scratch register
    unsigned dmask; // d14 and d15 in use, as d registers
    int locals;     // the slots' bytes, 8-aligned
    int saves;      // the bytes of the saved scratch registers
} Frame;

static void note_reg(Frame *fr, int reg)
{
    if (reg == A32_FP || reg == A32_SP || reg == A32_LR)
        fr->frame = true;
    else if (reg == T2)
        fr->r10 = true;
    else if (reg >= F0 && reg < F1 + 2)
        fr->dmask |= 1u << ((reg - A32_S0) / 2);
}

static Frame scan_body(const Gen *g)
{
    Frame fr = { 0 };
    for (const A32_Block *b = g->fn->blocks; b; b = b->next) {
        for (const A32_Instr *in = b->head; in; in = in->next) {
            if (in->op == A32_BL || in->op == A32_BLX)
                fr.frame = true; // lr changes
            for (int i = 0; i < A32_MAX_OPERANDS; i++) {
                const A32_Operand *o = &in->opnd[i];
                if (o->kind == A32_OPND_REG || o->kind == A32_OPND_MEM || o->kind == A32_OPND_SHIFT)
                    note_reg(&fr, o->reg);
                if ((o->kind == A32_OPND_MEM || o->kind == A32_OPND_SHIFT) && o->reg2 >= 0)
                    note_reg(&fr, o->reg2);
            }
        }
    }
    fr.locals = (g->locals_size + 7) / 8 * 8;
    fr.saves  = 8 * __builtin_popcount(fr.dmask) + (fr.r10 ? 4 : 0);
    if (fr.locals || g->outgoing || fr.r10 || fr.dmask || g->tl->u.function.variadic)
        fr.frame = true;
    return fr;
}

// The return sequence: the saved scratch registers back, then sp, r11 and pc as on
// entry; or just `bx lr` without a frame.  A variadic function drops its r0-r3 too.
static void epilogue(Gen *g, const Frame *fr)
{
    if (!fr->frame) {
        emit1(g, A32_BX, a32_reg(A32_LR));
        return;
    }
    if (fr->saves) {
        gen_addr(g, A32_SP, A32_FP, -(fr->locals + fr->saves));
        if (fr->r10)
            emit1(g, A32_POP, a32_reglist(1u << T2));
        if (fr->dmask)
            emit1(g, A32_VPOP, a32_dreglist(fr->dmask));
    }
    emit2(g, A32_MOV, a32_reg(A32_SP), a32_reg(A32_FP));
    if (g->tl->u.function.variadic) {
        emit1(g, A32_POP, a32_reglist(1u << A32_FP | 1u << A32_LR));
        emit3(g, A32_ADD, a32_reg(A32_SP), a32_reg(A32_SP), a32_imm(16));
        emit1(g, A32_BX, a32_reg(A32_LR));
        return;
    }
    emit1(g, A32_POP, a32_reglist(1u << A32_FP | 1u << A32_PC));
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

// push {r11, lr}; mov r11, sp; the slots; the scratch registers in use; the outgoing
// area, with sp 8-byte aligned.  A variadic function first pushes r0-r3, which then
// lie just below its stack arguments: one area of all its arguments, from r11 + 8.
void gen_prologue(Gen *g)
{
    Frame fr        = scan_body(g);
    A32_Block *tail = redirect(g, g->prologue);
    if (g->tl->u.function.variadic)
        emit1(g, A32_PUSH, a32_reglist(0xf));
    if (fr.frame) {
        emit1(g, A32_PUSH, a32_reglist(1u << A32_FP | 1u << A32_LR));
        emit2(g, A32_MOV, a32_reg(A32_FP), a32_reg(A32_SP));
        if (fr.locals)
            gen_addr(g, A32_SP, A32_SP, -fr.locals);
        if (fr.dmask)
            emit1(g, A32_VPUSH, a32_dreglist(fr.dmask));
        if (fr.r10)
            emit1(g, A32_PUSH, a32_reglist(1u << T2));
        int rest = (fr.locals + fr.saves + g->outgoing + 7) / 8 * 8 - fr.locals - fr.saves;
        if (rest)
            gen_addr(g, A32_SP, A32_SP, -rest);
    }
    g->fn->tail = tail;
    expand_epilogues(g, &fr);
}
