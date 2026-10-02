//
// Instruction selection: one TAC instruction at a time, on the allocated registers
// of its operands, or scratch registers for those in memory.
//
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Local label for TAC label `%N`: `.LN`.
static char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, ".L");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
}

static void gen_label(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    rv_new_block(g->fn, l);
    xfree(l);
}

static void gen_jump(Gen *g, Rv_Op op, int reg, const char *tac)
{
    char *l      = label_name(tac);
    Rv_Instr *in = rv_append(g->fn, op);
    if (op == RV_J) {
        in->opnd[0] = rv_sym(l, 0);
    } else {
        in->opnd[0] = rv_reg(reg);
        in->opnd[1] = rv_sym(l, 0);
    }
    xfree(l);
}

// reg = 0 when pair value `v` is zero (a long double of either sign), else nonzero.
static void pair_nonzero(Gen *g, int reg, const Tac_Val *v)
{
    pair_half(g, RV_T0, v, 0);
    pair_half(g, RV_T1, v, 1);
    if (rv_is_ld(val_type(g, v)))
        emit3(g, RV_SLLI, rv_reg(RV_T1), rv_reg(RV_T1), rv_imm(1));
    emit3(g, RV_OR, rv_reg(reg), rv_reg(RV_T0), rv_reg(RV_T1));
}

// reg = 0 when long double `v` on rv32, four words in memory, is zero of either sign.
static void ld32_nonzero(Gen *g, int reg, const Tac_Val *v)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        Float128 q = v->u.constant->u.long_double_val;
        gen_li(g, reg, (q.lo | q.hi << 1) != 0);
        return;
    }
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, RV_T5, &base, &off);
    emit2(g, RV_LW, rv_reg(RV_T0), mem(g, base, off));
    for (int i = 1; i < 4; i++) {
        emit2(g, RV_LW, rv_reg(RV_T1), mem(g, base, off + 4 * i));
        if (i == 3)
            emit3(g, RV_SLLI, rv_reg(RV_T1), rv_reg(RV_T1), rv_imm(1)); // not the sign
        emit3(g, RV_OR, rv_reg(i == 3 ? reg : RV_T0), rv_reg(RV_T0), rv_reg(RV_T1));
    }
}

// Branch to `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    int reg;
    if (rv_is_pair(t)) {
        pair_nonzero(g, RV_T0, cond);
        reg = RV_T0;
    } else if (rv_is_ld(t)) {
        ld32_nonzero(g, RV_T0, cond);
        reg = RV_T0;
    } else if (rv_is_fp(t)) {
        // t0 = (cond == 0.0), so a zero condition is a nonzero t0.
        int f = use_val(g, RV_F0, cond);
        fp_zero(g, RV_F0 + 1, rv_is_double(t));
        emit3(g, rv_is_double(t) ? RV_FEQD : RV_FEQS, rv_reg(RV_T0), rv_reg(f), rv_reg(RV_F0 + 1));
        if_zero = !if_zero;
        reg     = RV_T0;
    } else {
        reg = use_val(g, RV_T0, cond);
    }
    gen_jump(g, if_zero ? RV_BEQZ : RV_BNEZ, reg, target);
}

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (var_reg_hi(g, dst)) {
        pair_half(g, RV_T0, src, 0);
        pair_half(g, RV_T1, src, 1);
        set_pair(g, dst, RV_T0, RV_T1);
        return;
    }
    if (rv_is_pair(t) || rv_is_ld(t)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, RV_T4, &base, &off);
        copy_pair(g, src, base, off);
        return;
    }
    if (rv_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, RV_T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, RV_T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, rv_size(t), rv_align(t));
        return;
    }
    int d = var_reg(g, dst);
    if (d && src->kind == TAC_VAL_CONSTANT && !rv_is_fp(t))
        load_const_as(g, d, src->u.constant, t);
    else if (d)
        load_val(g, d, src);
    else
        store_val(g, use_val(g, rv_is_fp(t) ? RV_F0 : RV_T0, src), dst);
}

// dst = &src, of a named object or function.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Slot *slot = find_slot(g, src->u.var_name);
    int d            = def_reg(g, RV_T0, dst);
    if (slot)
        gen_addr(g, d, RV_S0, slot->offset);
    else
        emit2(g, RV_LA, rv_reg(d), rv_sym(src->u.var_name, 0));
    store_val(g, d, dst);
}

// The type stored through pointer value `ptr`, or NULL when not known.
static const Tac_Type *pointee(Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// dst = *src_ptr.
static void gen_load(Gen *g, const Tac_Val *src_ptr, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    int p             = use_val(g, RV_T3, src_ptr);
    if (var_reg_hi(g, dst)) {
        emit2(g, RV_LW, rv_reg(RV_T0), rv_mem(p, 0));
        emit2(g, RV_LW, rv_reg(RV_T1), rv_mem(p, 4));
        set_pair(g, dst, RV_T0, RV_T1);
        return;
    }
    if (rv_is_aggregate(t) || rv_is_pair(t) || rv_is_ld(t)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, RV_T4, &base, &off);
        gen_memcopy(g, base, off, p, 0, rv_size(t), rv_align(t));
        return;
    }
    int d = def_reg(g, rv_is_fp(t) ? RV_F0 : RV_T0, dst);
    load_mem(g, d, t, p, 0);
    store_val(g, d, dst);
}

// *dst_ptr = src, in the width of the pointee (or of src when that is not known).
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *dst_ptr)
{
    const Tac_Type *t = pointee(g, dst_ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (rv_is_aggregate(t) && !rv_is_aggregate(val_type(g, src))))
        t = val_type(g, src);
    int p = use_val(g, RV_T4, dst_ptr);
    if (rv_is_pair(t) || rv_is_ld(t)) {
        copy_pair(g, src, p, 0);
        return;
    }
    if (rv_is_aggregate(t)) {
        int base;
        int64_t off;
        name_addr(g, src->u.var_name, RV_T3, &base, &off);
        gen_memcopy(g, p, 0, base, off, rv_size(t), rv_align(t));
        return;
    }
    store_mem(g, use_val(g, rv_is_fp(t) ? RV_F0 : RV_T0, src), t, p, 0);
}

// dst = ptr + index * scale (bytes).
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    int scale          = in->u.add_ptr.scale;
    int p              = use_val(g, RV_T0, in->u.add_ptr.ptr);
    const Tac_Type *it = val_type(g, in->u.add_ptr.index);
    int i;
    if (rv_is_ll(it)) {
        pair_half(g, RV_T1, in->u.add_ptr.index, 0); // an address has 32 bits
        i = RV_T1;
    } else {
        i = use_val(g, RV_T1, in->u.add_ptr.index);
    }
    if (rv_size(it) == 4 && rv_is_unsigned(it) && riscv_xlen == 8) {
        emit3(g, RV_SLLI, rv_reg(RV_T1), rv_reg(i), rv_imm(32));
        emit3(g, RV_SRLI, rv_reg(RV_T1), rv_reg(RV_T1), rv_imm(32));
        i = RV_T1;
    }
    if (scale > 1 && (scale & (scale - 1)) == 0) {
        int shift = 0;
        while ((1 << shift) < scale)
            shift++;
        emit3(g, RV_SLLI, rv_reg(RV_T1), rv_reg(i), rv_imm(shift));
        i = RV_T1;
    } else if (scale != 1) {
        gen_li(g, RV_T2, scale);
        emit3(g, RV_MUL, rv_reg(RV_T1), rv_reg(i), rv_reg(RV_T2));
        i = RV_T1;
    }
    int d = def_reg(g, RV_T0, in->u.add_ptr.dst);
    emit3(g, RV_ADD, rv_reg(d), rv_reg(p), rv_reg(i));
    store_val(g, d, in->u.add_ptr.dst);
}

// dst = a - b, a byte count.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    int a = use_val(g, RV_T0, in->u.ptr_diff.ptr_a);
    int b = use_val(g, RV_T1, in->u.ptr_diff.ptr_b);
    int d = def_reg(g, RV_T0, in->u.ptr_diff.dst);
    emit3(g, RV_SUB, rv_reg(d), rv_reg(a), rv_reg(b));
    store_val(g, d, in->u.ptr_diff.dst);
}

// The scalar type at byte `offset` of aggregate type `t`, or NULL.  Of several union
// members there, one of `size` bytes is preferred, else the first.
static const Tac_Type *scalar_at(const Tac_Type *t, int offset, int size)
{
    if (!t)
        return NULL;
    if (t->kind == TAC_TYPE_ARRAY) {
        int esize = rv_size(t->u.array.elem_type);
        return esize > 0 ? scalar_at(t->u.array.elem_type, offset % esize, size) : NULL;
    }
    if (t->kind != TAC_TYPE_STRUCTURE)
        return offset == 0 ? t : NULL;
    const Tac_Type *first = NULL;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (offset < m->offset || offset >= m->offset + rv_size(m->type))
            continue;
        const Tac_Type *s = scalar_at(m->type, offset - m->offset, size);
        if (s && rv_size(s) == size)
            return s;
        if (!first)
            first = s;
    }
    return first;
}

// Member store: aggregate `dst` at byte `offset` = src.  A constant takes the width of
// the member there (its own kind may be wider); a byte copy is one byte.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *dst, int offset,
                               bool byte)
{
    static const Tac_Type uchar = { .kind = TAC_TYPE_UCHAR };
    const Tac_Type *t = val_type(g, src);
    if (byte) {
        t = &uchar;
    } else if (src->kind == TAC_VAL_CONSTANT) {
        const Tac_Type *m = scalar_at(name_type(g, dst), offset, rv_size(t));
        if (m && rv_is_fp(m) == rv_is_fp(t))
            t = m;
    }
    int base;
    int64_t off;
    name_addr(g, dst, RV_T4, &base, &off);
    off += offset;
    if (rv_is_pair(t) || rv_is_ld(t)) {
        copy_pair(g, src, base, off);
        return;
    }
    if (rv_is_aggregate(t)) {
        int sbase;
        int64_t soff;
        name_addr(g, src->u.var_name, RV_T3, &sbase, &soff);
        gen_memcopy(g, base, off, sbase, soff, rv_size(t), rv_align(t));
        return;
    }
    store_mem(g, use_val(g, rv_is_fp(t) ? RV_F0 : RV_T0, src), t, base, off);
}

// Member load: dst = aggregate `src` at byte `offset`.
static void gen_copy_from_offset(Gen *g, const char *src, int offset, const Tac_Val *dst,
                                 bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (byte && rv_size(t) != 1)
        fatal_error("riscv: %s: byte copy into %s", gen_name(g), dst->u.var_name);
    int base;
    int64_t off;
    name_addr(g, src, RV_T3, &base, &off);
    off += offset;
    if (var_reg_hi(g, dst)) {
        emit2(g, RV_LW, rv_reg(RV_T0), mem(g, base, off));
        emit2(g, RV_LW, rv_reg(RV_T1), mem(g, base, off + 4));
        set_pair(g, dst, RV_T0, RV_T1);
        return;
    }
    if (rv_is_aggregate(t) || rv_is_pair(t) || rv_is_ld(t)) {
        int dbase;
        int64_t doff;
        name_addr(g, dst->u.var_name, RV_T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, base, off, rv_size(t), rv_align(t));
        return;
    }
    int d = def_reg(g, rv_is_fp(t) ? RV_F0 : RV_T0, dst);
    load_mem(g, d, t, base, off);
    store_val(g, d, dst);
}

// dst = src zero-extended from `size` bytes.
static void gen_zext(Gen *g, int dst, int src, int size)
{
    static const Tac_Type ulong = { .kind = TAC_TYPE_ULONG };
    if (size >= riscv_xlen) {
        move_reg(g, dst, src, &ulong);
        return;
    }
    if (size == 1) {
        emit3(g, RV_ANDI, rv_reg(dst), rv_reg(src), rv_imm(255));
        return;
    }
    int shift = 8 * riscv_xlen - 8 * size;
    emit3(g, RV_SLLI, rv_reg(dst), rv_reg(src), rv_imm(shift));
    emit3(g, RV_SRLI, rv_reg(dst), rv_reg(dst), rv_imm(shift));
}

// dst = src sign-extended from `size` (1 or 2) bytes.
static void gen_sext(Gen *g, int dst, int src, int size)
{
    int shift = 8 * riscv_xlen - 8 * size;
    emit3(g, RV_SLLI, rv_reg(dst), rv_reg(src), rv_imm(shift));
    emit3(g, RV_SRAI, rv_reg(dst), rv_reg(dst), rv_imm(shift));
}

// An integer conversion.  In memory the store truncates; a register is brought to
// the destination's form.  A sign extension keeps the value, but for a narrow
// unsigned source: copy propagation may have removed its cast to a signed type.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    if (rv_is_ll(val_type(g, src)) || rv_is_ll(val_type(g, dst))) {
        gen_ll_int_convert(g, src, dst, kind);
        return;
    }
    int s = use_val(g, RV_T0, src);
    int d = def_reg(g, RV_T0, dst);
    const Tac_Type *st = val_type(g, src);
    if (kind == TAC_INSTRUCTION_ZERO_EXTEND) {
        gen_zext(g, d, s, rv_size(st));
        s = d;
    } else if (kind == TAC_INSTRUCTION_SIGN_EXTEND && rv_size(st) < 4 && rv_is_unsigned(st)) {
        gen_sext(g, d, s, rv_size(st));
        s = d;
    } else if (kind == TAC_INSTRUCTION_TRUNCATE && var_reg(g, dst)) {
        gen_canon(g, d, s, val_type(g, dst));
        s = d;
    }
    store_val(g, s, dst);
}

static void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    bool d = rv_is_double(t);
    int s  = use_val(g, RV_F0, in->u.unary.src);
    if (in->u.unary.op == TAC_UNARY_NOT) {
        int r = def_reg(g, RV_T0, in->u.unary.dst);
        fp_zero(g, RV_F0 + 1, d);
        emit3(g, d ? RV_FEQD : RV_FEQS, rv_reg(r), rv_reg(s), rv_reg(RV_F0 + 1));
        store_val(g, r, in->u.unary.dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("riscv: %s: bad floating-point unary operator", gen_name(g));
    int r = def_reg(g, RV_F0, in->u.unary.dst);
    emit2(g, d ? RV_FNEGD : RV_FNEGS, rv_reg(r), rv_reg(s));
    store_val(g, r, in->u.unary.dst);
}

// Store integer result `d` into `dst`; a register narrower than a word is brought to
// its type's form, as a store and reload would.
void store_int_result(Gen *g, int d, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (var_reg(g, dst) && rv_size(t) < 4)
        gen_canon(g, d, d, t);
    store_val(g, d, dst);
}

// A long double negation flips the sign bit; `!` tests for zero.
static void gen_ld_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        int d = def_reg(g, RV_T0, dst);
        if (riscv_xlen == 4)
            ld32_nonzero(g, d, src);
        else
            pair_nonzero(g, d, src);
        emit2(g, RV_SEQZ, rv_reg(d), rv_reg(d));
        store_val(g, d, dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("riscv: %s: bad long double unary operator", gen_name(g));
    int base;
    int64_t off;
    if (riscv_xlen == 4) {
        // A copy with the sign bit, bit 31 of the last word, flipped.
        name_addr(g, dst->u.var_name, RV_T4, &base, &off);
        copy_pair(g, src, base, off);
        emit2(g, RV_LW, rv_reg(RV_T0), mem(g, base, off + 12));
        gen_li(g, RV_T1, INT32_MIN);
        emit3(g, RV_XOR, rv_reg(RV_T0), rv_reg(RV_T0), rv_reg(RV_T1));
        emit2(g, RV_SW, rv_reg(RV_T0), mem(g, base, off + 12));
        return;
    }
    pair_half(g, RV_T0, src, 0);
    pair_half(g, RV_T1, src, 1);
    gen_li(g, RV_T2, 1);
    emit3(g, RV_SLLI, rv_reg(RV_T2), rv_reg(RV_T2), rv_imm(63));
    emit3(g, RV_XOR, rv_reg(RV_T1), rv_reg(RV_T1), rv_reg(RV_T2));
    name_addr(g, dst->u.var_name, RV_T4, &base, &off);
    emit2(g, RV_SD, rv_reg(RV_T0), mem(g, base, off));
    emit2(g, RV_SD, rv_reg(RV_T1), mem(g, base, off + 8));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (rv_is_ll(t)) {
        gen_ll_unary(g, in);
        return;
    }
    if (rv_is_ld(t)) {
        gen_ld_unary(g, in);
        return;
    }
    if (rv_is_fp(t)) {
        gen_fp_unary(g, in, t);
        return;
    }
    bool word    = rv_size(t) <= 4 && riscv_xlen == 8; // rv32 has no *w forms
    Rv_Operand s = rv_reg(use_val(g, RV_T0, in->u.unary.src));
    int d        = def_reg(g, RV_T0, in->u.unary.dst);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit2(g, word ? RV_NEGW : RV_NEG, rv_reg(d), s);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, RV_NOT, rv_reg(d), s);
        break;
    case TAC_UNARY_NOT:
        emit2(g, RV_SEQZ, rv_reg(d), s);
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("riscv: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_int_result(g, d, in->u.unary.dst);
}

// d = a op b for an integer operator; `word` selects the 32-bit forms.  The sources
// are read before d is written.
static void gen_int_binop(Gen *g, Tac_BinaryOperator op, bool word, bool is_unsigned,
                          Rv_Operand d, Rv_Operand a, Rv_Operand b)
{
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        emit3(g, word ? RV_ADDW : RV_ADD, d, a, b);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        emit3(g, word ? RV_SUBW : RV_SUB, d, a, b);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        emit3(g, word ? RV_MULW : RV_MUL, d, a, b);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
        if (is_unsigned || op == TAC_BINARY_DIVIDE_UNSIGNED)
            emit3(g, word ? RV_DIVUW : RV_DIVU, d, a, b);
        else
            emit3(g, word ? RV_DIVW : RV_DIV, d, a, b);
        break;
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        if (is_unsigned || op == TAC_BINARY_REMAINDER_UNSIGNED)
            emit3(g, word ? RV_REMUW : RV_REMU, d, a, b);
        else
            emit3(g, word ? RV_REMW : RV_REM, d, a, b);
        break;
    case TAC_BINARY_BITWISE_AND:
        emit3(g, RV_AND, d, a, b);
        break;
    case TAC_BINARY_BITWISE_OR:
        emit3(g, RV_OR, d, a, b);
        break;
    case TAC_BINARY_BITWISE_XOR:
        emit3(g, RV_XOR, d, a, b);
        break;
    case TAC_BINARY_LEFT_SHIFT:
        emit3(g, word ? RV_SLLW : RV_SLL, d, a, b);
        break;
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        if (is_unsigned || op == TAC_BINARY_RIGHT_SHIFT_LOGICAL)
            emit3(g, word ? RV_SRLW : RV_SRL, d, a, b);
        else
            emit3(g, word ? RV_SRAW : RV_SRA, d, a, b);
        break;
    case TAC_BINARY_EQUAL:
        emit3(g, RV_XOR, d, a, b);
        emit2(g, RV_SEQZ, d, d);
        break;
    case TAC_BINARY_NOT_EQUAL:
        emit3(g, RV_XOR, d, a, b);
        emit2(g, RV_SNEZ, d, d);
        break;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        emit3(g, is_unsigned ? RV_SLTU : RV_SLT, d, a, b);
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        emit3(g, is_unsigned ? RV_SLTU : RV_SLT, d, b, a);
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        emit3(g, is_unsigned ? RV_SLTU : RV_SLT, d, b, a);
        emit3(g, RV_XORI, d, d, rv_imm(1));
        break;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        emit3(g, is_unsigned ? RV_SLTU : RV_SLT, d, a, b);
        emit3(g, RV_XORI, d, d, rv_imm(1));
        break;
    default:
        fatal_error("riscv: %s: floating-point operator on integers", gen_name(g));
    }
}

bool rv_unsigned_op(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER_UNSIGNED:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        return true;
    default:
        return false;
    }
}

// A floating-point operator; a comparison leaves 0/1 in t0, arithmetic its result in ft0.
static void gen_fp_binary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    bool d          = rv_is_double(t);
    const Tac_Val *dst = in->u.binary.dst;
    Rv_Operand a    = rv_reg(use_val(g, RV_F0, in->u.binary.src1));
    Rv_Operand b    = rv_reg(use_val(g, RV_F0 + 1, in->u.binary.src2));
    int dreg        = def_reg(g, rv_is_fp(val_type(g, dst)) ? RV_F0 : RV_T0, dst);
    Rv_Operand r = rv_reg(dreg), f = r;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        emit3(g, d ? RV_FADDD : RV_FADDS, f, a, b);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        emit3(g, d ? RV_FSUBD : RV_FSUBS, f, a, b);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        emit3(g, d ? RV_FMULD : RV_FMULS, f, a, b);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        emit3(g, d ? RV_FDIVD : RV_FDIVS, f, a, b);
        break;
    case TAC_BINARY_EQUAL:
        emit3(g, d ? RV_FEQD : RV_FEQS, r, a, b);
        break;
    case TAC_BINARY_NOT_EQUAL:
        emit3(g, d ? RV_FEQD : RV_FEQS, r, a, b);
        emit3(g, RV_XORI, r, r, rv_imm(1));
        break;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        emit3(g, d ? RV_FLTD : RV_FLTS, r, a, b);
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        emit3(g, d ? RV_FLED : RV_FLES, r, a, b);
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        emit3(g, d ? RV_FLTD : RV_FLTS, r, b, a);
        break;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        emit3(g, d ? RV_FLED : RV_FLES, r, b, a);
        break;
    default:
        fatal_error("riscv: %s: bad floating-point operator %d", gen_name(g), in->u.binary.op);
    }
    store_val(g, dreg, dst);
}

void call_runtime(Gen *g, const char *name)
{
    rv_append(g->fn, RV_CALL)->opnd[0] = rv_sym(name, 0);
}

void pair_arg(Gen *g, int reg, const Tac_Val *v)
{
    pair_half(g, reg, v, 0);
    pair_half(g, reg + 1, v, 1);
}

void pair_result(Gen *g, const Tac_Val *dst)
{
    set_pair(g, dst, RV_A0, RV_A0 + 1);
}

// Long double arithmetic and comparison: a call to the runtime (libgcc names).  A
// comparison routine returns an int to test against zero.
static void gen_ld_binary(Gen *g, const Tac_Instruction *in)
{
    static const struct {
        Tac_BinaryOperator op;
        const char *name;
    } ops[] = {
        { TAC_BINARY_ADD, "__addtf3" },
        { TAC_BINARY_ADD_DOUBLE, "__addtf3" },
        { TAC_BINARY_SUBTRACT, "__subtf3" },
        { TAC_BINARY_SUBTRACT_DOUBLE, "__subtf3" },
        { TAC_BINARY_MULTIPLY, "__multf3" },
        { TAC_BINARY_MULTIPLY_DOUBLE, "__multf3" },
        { TAC_BINARY_DIVIDE, "__divtf3" },
        { TAC_BINARY_DIVIDE_DOUBLE, "__divtf3" },
        { TAC_BINARY_EQUAL, "__eqtf2" },
        { TAC_BINARY_NOT_EQUAL, "__netf2" },
        { TAC_BINARY_LESS_THAN, "__lttf2" },
        { TAC_BINARY_LESS_THAN_DOUBLE, "__lttf2" },
        { TAC_BINARY_LESS_OR_EQUAL, "__letf2" },
        { TAC_BINARY_LESS_OR_EQUAL_DOUBLE, "__letf2" },
        { TAC_BINARY_GREATER_THAN, "__gttf2" },
        { TAC_BINARY_GREATER_THAN_DOUBLE, "__gttf2" },
        { TAC_BINARY_GREATER_OR_EQUAL, "__getf2" },
        { TAC_BINARY_GREATER_OR_EQUAL_DOUBLE, "__getf2" },
    };
    Tac_BinaryOperator op = in->u.binary.op;
    const char *name      = NULL;
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]) && !name; i++)
        if (ops[i].op == op)
            name = ops[i].name;
    if (!name)
        fatal_error("riscv: %s: bad long double operator %d", gen_name(g), op);
    const Tac_Val *dst = in->u.binary.dst;
    bool arith         = rv_is_ld(val_type(g, dst));
    if (riscv_xlen == 4) {
        // By reference, the result through a hidden pointer: as any call.
        static const Tac_Type ld = { .kind = TAC_TYPE_LONG_DOUBLE }, i = { .kind = TAC_TYPE_INT };
        const Tac_Val *args[2] = { in->u.binary.src1, in->u.binary.src2 };
        gen_runtime_call(g, name, arith ? &ld : &i, args, NULL, 2, arith ? dst : NULL);
        if (arith)
            return;
    } else {
        pair_arg(g, RV_A0, in->u.binary.src1);
        pair_arg(g, RV_A0 + 2, in->u.binary.src2);
        call_runtime(g, name);
        if (arith) {
            pair_result(g, dst);
            return;
        }
    }
    Rv_Operand r = rv_reg(RV_A0), z = rv_reg(RV_ZERO);
    switch (op) {
    case TAC_BINARY_EQUAL:
        emit2(g, RV_SEQZ, r, r);
        break;
    case TAC_BINARY_NOT_EQUAL:
        emit2(g, RV_SNEZ, r, r);
        break;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        emit3(g, RV_SLT, r, r, z);
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        emit3(g, RV_SLTI, r, r, rv_imm(1));
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        emit3(g, RV_SLT, r, z, r);
        break;
    default:
        emit3(g, RV_SLT, r, r, z);
        emit3(g, RV_XORI, r, r, rv_imm(1));
        break;
    }
    store_val(g, RV_A0, dst);
}

static bool is_shift(Tac_BinaryOperator op)
{
    return op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
           op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    bool ll2          = rv_is_ll(val_type(g, in->u.binary.src2));
    if (rv_is_ll(t) || (ll2 && !is_shift(in->u.binary.op))) {
        gen_ll_binary(g, in);
        return;
    }
    if (rv_is_ld(t)) {
        gen_ld_binary(g, in);
        return;
    }
    if (rv_is_fp(t)) {
        gen_fp_binary(g, in, t);
        return;
    }
    Rv_Operand a = rv_reg(use_val(g, RV_T0, in->u.binary.src1));
    Rv_Operand b;
    if (ll2) {
        pair_half(g, RV_T1, in->u.binary.src2, 0); // a shift count
        b = rv_reg(RV_T1);
    } else {
        b = rv_reg(use_val(g, RV_T1, in->u.binary.src2));
    }
    // The operator says the signedness: an operand's own type may differ, once copy
    // propagation has removed a cast.  Only pointers compare unsigned under a plain one.
    int d = def_reg(g, RV_T0, in->u.binary.dst);
    gen_int_binop(g, in->u.binary.op, rv_size(t) <= 4 && riscv_xlen == 8,
                  t->kind == TAC_TYPE_POINTER || rv_unsigned_op(in->u.binary.op), rv_reg(d), a,
                  b);
    store_int_result(g, d, in->u.binary.dst);
}

// Whether conversion `kind` is from an unsigned integer.  The source's own type may
// differ in signedness, once copy propagation has removed a cast.
bool rv_from_unsigned(Tac_InstructionKind kind)
{
    return kind == TAC_INSTRUCTION_UINT_TO_DOUBLE || kind == TAC_INSTRUCTION_UINT_TO_FLOAT ||
           kind == TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE;
}

// An int/FP or float/double conversion.
static void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    bool sfp = rv_is_fp(st), dfp = rv_is_fp(dt);
    if (rv_is_ll(st) || rv_is_ll(dt)) {
        gen_ll_fp_convert(g, src, dst, kind);
        return;
    }
    Rv_Op op;
    if (sfp && dfp) {
        op = rv_is_double(dt) ? RV_FCVTDS : RV_FCVTSD;
    } else if (dfp) {
        bool w = rv_size(st) <= 4, u = rv_from_unsigned(kind);
        if (rv_is_double(dt))
            op = w ? (u ? RV_FCVTDWU : RV_FCVTDW) : (u ? RV_FCVTDLU : RV_FCVTDL);
        else
            op = w ? (u ? RV_FCVTSWU : RV_FCVTSW) : (u ? RV_FCVTSLU : RV_FCVTSL);
    } else {
        // To an integer: truncate toward zero, into the destination's width.
        bool w = rv_size(dt) <= 4, u = rv_is_unsigned(dt);
        if (rv_is_double(st))
            op = w ? (u ? RV_FCVTWUD : RV_FCVTWD) : (u ? RV_FCVTLUD : RV_FCVTLD);
        else
            op = w ? (u ? RV_FCVTWUS : RV_FCVTWS) : (u ? RV_FCVTLUS : RV_FCVTLS);
    }
    int sreg     = use_val(g, sfp ? RV_F0 : RV_T0, src);
    int dreg     = def_reg(g, dfp ? RV_F0 + 1 : RV_T1, dst);
    Rv_Instr *cv = emit2(g, op, rv_reg(dreg), rv_reg(sreg));
    if (!dfp) {
        cv->opnd[2] = rv_sym("rtz", 0);
        if (rv_size(dt) < 4 && var_reg(g, dst))
            gen_canon(g, dreg, dreg, dt);
    }
    store_val(g, dreg, dst);
}

// A conversion to or from long double: a call to the runtime.
static void gen_ld32_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

static void gen_ld_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    const char *name;
    if (riscv_xlen == 4) {
        gen_ld32_convert(g, src, dst, kind);
        return;
    }
    if (rv_is_ld(st)) {
        pair_arg(g, RV_A0, src);
        if (rv_is_fp(dt)) {
            call_runtime(g, rv_is_double(dt) ? "__trunctfdf2" : "__trunctfsf2");
            store_val(g, RV_FA0, dst);
            return;
        }
        bool w = rv_size(dt) <= 4, u = rv_is_unsigned(dt);
        name   = w ? (u ? "__fixunstfsi" : "__fixtfsi") : (u ? "__fixunstfdi" : "__fixtfdi");
        call_runtime(g, name);
        store_int_result(g, RV_A0, dst);
        return;
    }
    if (rv_is_fp(st)) {
        load_val(g, RV_FA0, src);
        name = rv_is_double(st) ? "__extenddftf2" : "__extendsftf2";
    } else {
        load_val(g, RV_A0, src);
        bool w = rv_size(st) <= 4, u = rv_from_unsigned(kind);
        name   = w ? (u ? "__floatunsitf" : "__floatsitf") : (u ? "__floatunditf" : "__floatditf");
    }
    call_runtime(g, name);
    pair_result(g, dst);
}

// On rv32, by the calling convention: the routine's own types decide the registers.
static void gen_ld32_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    static const Tac_Type ld = { .kind = TAC_TYPE_LONG_DOUBLE }, d = { .kind = TAC_TYPE_DOUBLE },
                          f = { .kind = TAC_TYPE_FLOAT }, i = { .kind = TAC_TYPE_INT },
                          u = { .kind = TAC_TYPE_UINT }, ll = { .kind = TAC_TYPE_LONG_LONG },
                          ull = { .kind = TAC_TYPE_ULONG_LONG };
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst), *ret = &ld;
    const char *name;
    if (rv_is_ld(st)) {
        bool w = rv_size(dt) <= 4, un = rv_is_unsigned(dt);
        if (rv_is_fp(dt)) {
            name = rv_is_double(dt) ? "__trunctfdf2" : "__trunctfsf2";
            ret  = rv_is_double(dt) ? &d : &f;
        } else if (w) {
            name = un ? "__fixunstfsi" : "__fixtfsi";
            ret  = un ? &u : &i;
        } else {
            name = un ? "__fixunstfdi" : "__fixtfdi";
            ret  = un ? &ull : &ll;
        }
    } else if (rv_is_fp(st)) {
        name = rv_is_double(st) ? "__extenddftf2" : "__extendsftf2";
    } else {
        bool w = rv_size(st) <= 4, un = rv_from_unsigned(kind);
        name   = w ? (un ? "__floatunsitf" : "__floatsitf") : (un ? "__floatunditf" : "__floatditf");
    }
    gen_runtime_call(g, name, ret, &src, NULL, 1, dst);
}

bool runtime_call(const Tac_Instruction *in, TypeOf *type_of, const void *arg,
                  const Tac_Val **dst)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
        *dst = in->u.int_to_double.dst;
        return ll_runtime_call(in, type_of(arg, in->u.int_to_double.src),
                               type_of(arg, in->u.int_to_double.dst));
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        *dst = in->u.long_double_to_int.dst;
        return true;
    case TAC_INSTRUCTION_BINARY: {
        *dst               = in->u.binary.dst;
        const Tac_Type *t1 = type_of(arg, in->u.binary.src1);
        const Tac_Type *t2 = type_of(arg, in->u.binary.src2);
        if (t1 && rv_is_ld(t1))
            return true;
        const Tac_Type *t = t1 && rv_is_ll(t1) ? t1 : t2;
        return t && ll_runtime_call(in, t, NULL);
    }
    default:
        return false;
    }
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL:
        gen_label(g, in->u.label.name);
        break;
    case TAC_INSTRUCTION_JUMP:
        gen_jump(g, RV_J, 0, in->u.jump.target);
        break;
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                      in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    case TAC_INSTRUCTION_COPY:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_SIGN_EXTEND:
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_int_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, in->kind);
        break;
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst, in->kind);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        gen_ld_convert(g, in->u.long_double_to_int.src, in->u.long_double_to_int.dst, in->kind);
        break;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        gen_get_address(g, in->u.get_address.src, in->u.get_address.dst);
        break;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        gen_load(g, in->u.load.src_ptr, in->u.load.dst);
        break;
    case TAC_INSTRUCTION_STORE:
    case TAC_INSTRUCTION_STORE_BYTE:
        gen_store(g, in->u.store.src, in->u.store.dst_ptr);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        gen_add_ptr(g, in);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        gen_ptr_diff(g, in);
        break;
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.ptr_to_char_ptr.src, in->u.ptr_to_char_ptr.dst);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset,
                           in->kind == TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET);
        break;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        gen_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                             in->u.copy_from_offset.dst,
                             in->kind == TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET);
        break;
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        gen_call(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("riscv: %s: %s not implemented", gen_name(g), tac_instruction_name(in->kind));
    }
}
