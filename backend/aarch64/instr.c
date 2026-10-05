//
// Instruction selection: one TAC instruction at a time, on the allocated registers of
// its operands, or scratch registers for those in memory.
//
#include <string.h>

#include "codegen.h"
#include "flow.h"
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
    a64_new_block(g->fn, l);
    xfree(l);
}

static void gen_jump(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    emit1(g, A64_B, a64_sym(l, 0));
    xfree(l);
}

// x`reg` = 0 when long double `v` is zero of either sign, else nonzero: its two
// doublewords or'ed, the sign bit shifted out.  x10 is the temporary.
static void ld_nonzero(Gen *g, int reg, const Tac_Val *v)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        Float128 q = v->u.constant->u.long_double_val;
        gen_li(g, reg, A64_X, (q.lo | q.hi << 1) != 0);
        return;
    }
    int base;
    int64_t off;
    name_addr(g, v->u.var_name, T3, &base, &off);
    A64_Operand r = a64_reg(reg, A64_X);
    emit2(g, A64_LDR, r, mem(g, base, off, 8));
    emit2(g, A64_LDR, a64_reg(T1, A64_X), mem(g, base, off + 8, 8));
    emit3(g, A64_ORR, r, r, a64_shift(T1, A64_X, A64_SHIFT_LSL, 1));
}

// Branch to `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    char *l           = label_name(target);
    if (a64_is_ld(t)) {
        ld_nonzero(g, T0, cond);
        emit2(g, if_zero ? A64_CBZ : A64_CBNZ, a64_reg(T0, A64_X), a64_sym(l, 0));
    } else if (a64_is_fp(t)) {
        // A NaN is not zero: unordered leaves Z clear.
        emit2(g, A64_FCMP, a64_reg(use_val(g, F0, cond), a64_width(t)), a64_fzero());
        emit2(g, A64_BCOND, a64_cond(if_zero ? A64_EQ : A64_NE), a64_sym(l, 0));
    } else {
        emit2(g, if_zero ? A64_CBZ : A64_CBNZ, a64_reg(use_val(g, T0, cond), a64_width(t)),
              a64_sym(l, 0));
    }
    xfree(l);
}

// An aggregate, or a long double: copied as bytes, never in a register.
static bool is_wide(const Tac_Type *t)
{
    return a64_is_aggregate(t) || a64_is_ld(t);
}

// Store wide value `src` of type `t` at base + off: a copy of its object, or a long
// double constant's two words.  `base` is not x11, x12 or ip0.
static void store_wide(Gen *g, const Tac_Val *src, const Tac_Type *t, int base, int64_t off)
{
    if (src->kind == TAC_VAL_CONSTANT) {
        Float128 q = src->u.constant->u.long_double_val;
        gen_li(g, T0, A64_X, (int64_t)q.lo);
        emit2(g, A64_STR, a64_reg(T0, A64_X), mem(g, base, off, 8));
        gen_li(g, T0, A64_X, (int64_t)q.hi);
        emit2(g, A64_STR, a64_reg(T0, A64_X), mem(g, base, off + 8, 8));
        return;
    }
    int sbase;
    int64_t soff;
    name_addr(g, src->u.var_name, T3, &sbase, &soff);
    gen_memcopy(g, base, off, sbase, soff, a64_size(t), a64_align(t));
}

// The register holding scalar `v` for an operation on type `t`: its own, or a scratch
// register of its class after loading it (a constant as type `t`).
static int use_scalar(Gen *g, const Tac_Val *v, const Tac_Type *t)
{
    if (a64_is_fp(t))
        return use_val(g, F0, v);
    if (v->kind == TAC_VAL_CONSTANT) {
        load_const_as(g, T0, v->u.constant, t);
        return T0;
    }
    return use_val(g, T0, v);
}

// dst = src, for any type.  A constant goes straight into a register variable.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (is_wide(t)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, T4, &base, &off);
        store_wide(g, src, t, base, off);
        return;
    }
    int d = var_reg(g, dst);
    if (d && src->kind == TAC_VAL_CONSTANT && !a64_is_fp(t))
        load_const_as(g, d, src->u.constant, t);
    else if (d && src->kind == TAC_VAL_CONSTANT)
        load_val(g, d, src);
    else
        store_val(g, use_scalar(g, src, t), dst);
}

// The type stored through pointer value `ptr`, or NULL when not known.
static const Tac_Type *pointee(const Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// dst = *src_ptr.
static void gen_load(Gen *g, const Tac_Val *src_ptr, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    int p             = use_val(g, T3, src_ptr);
    if (is_wide(t)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, T4, &base, &off);
        gen_memcopy(g, base, off, p, 0, a64_size(t), a64_align(t));
        return;
    }
    int d = def_reg(g, a64_is_fp(t) ? F0 : T0, dst);
    load_mem(g, d, t, p, 0);
    store_val(g, d, dst);
}

// *dst_ptr = src, in the width of the pointee (or of src when that is not known).
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *dst_ptr)
{
    const Tac_Type *t = pointee(g, dst_ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (a64_is_aggregate(t) && !a64_is_aggregate(val_type(g, src))))
        t = val_type(g, src);
    int p = use_val(g, T4, dst_ptr);
    if (is_wide(t)) {
        store_wide(g, src, t, p, 0);
        return;
    }
    store_mem(g, use_scalar(g, src, t), t, p, 0);
}

// dst = ptr + index * scale (bytes).  An index narrower than 64 bits is extended by its
// type first: a W load has zeroed the upper half.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    int scale          = in->u.add_ptr.scale;
    const Tac_Type *it = val_type(g, in->u.add_ptr.index);
    int p              = use_val(g, T0, in->u.add_ptr.ptr);
    int i              = use_val(g, T1, in->u.add_ptr.index);
    if (a64_size(it) <= 4 && !a64_is_unsigned(it)) {
        emit2(g, A64_SXTW, a64_reg(T1, A64_X), a64_reg(i, A64_W));
        i = T1;
    }
    int shift = 0;
    while ((1 << shift) < scale)
        shift++;
    if ((1 << shift) != scale) {
        gen_li(g, T2, A64_X, scale);
        emit3(g, A64_MUL, a64_reg(T1, A64_X), a64_reg(i, A64_X), a64_reg(T2, A64_X));
        i     = T1;
        shift = 0;
    }
    int d = def_reg(g, T0, in->u.add_ptr.dst);
    emit3(g, A64_ADD, a64_reg(d, A64_X), a64_reg(p, A64_X),
          shift ? a64_shift(i, A64_X, A64_SHIFT_LSL, shift) : a64_reg(i, A64_X));
    store_val(g, d, in->u.add_ptr.dst);
}

// dst = a - b, a byte count.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    int a = use_val(g, T0, in->u.ptr_diff.ptr_a);
    int b = use_val(g, T1, in->u.ptr_diff.ptr_b);
    int d = def_reg(g, T0, in->u.ptr_diff.dst);
    emit3(g, A64_SUB, a64_reg(d, A64_X), a64_reg(a, A64_X), a64_reg(b, A64_X));
    store_val(g, d, in->u.ptr_diff.dst);
}

// The scalar type at byte `offset` of aggregate type `t`, or NULL.  Of several union
// members there, one of `size` bytes is preferred, else the first.
static const Tac_Type *scalar_at(const Tac_Type *t, int offset, int size)
{
    if (!t)
        return NULL;
    if (t->kind == TAC_TYPE_ARRAY) {
        int esize = a64_size(t->u.array.elem_type);
        return esize > 0 ? scalar_at(t->u.array.elem_type, offset % esize, size) : NULL;
    }
    if (t->kind != TAC_TYPE_STRUCTURE)
        return offset == 0 ? t : NULL;
    const Tac_Type *first = NULL;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (offset < m->offset || offset >= m->offset + a64_size(m->type))
            continue;
        const Tac_Type *s = scalar_at(m->type, offset - m->offset, size);
        if (s && a64_size(s) == size)
            return s;
        if (!first)
            first = s;
    }
    return first;
}

// Member store: aggregate `dst` at byte `offset` = src.  A constant takes the width of
// the member there (its own kind may be wider); a byte copy is one byte.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *dst, int offset, bool byte)
{
    static const Tac_Type uchar = { .kind = TAC_TYPE_UCHAR };
    const Tac_Type *t           = val_type(g, src);
    if (byte) {
        t = &uchar;
    } else if (src->kind == TAC_VAL_CONSTANT) {
        const Tac_Type *m = scalar_at(name_type(g, dst), offset, a64_size(t));
        if (m && a64_is_fp(m) == a64_is_fp(t) && !is_wide(m))
            t = m;
    }
    int base;
    int64_t off;
    name_addr(g, dst, T4, &base, &off);
    off += offset;
    if (is_wide(t)) {
        store_wide(g, src, t, base, off);
        return;
    }
    store_mem(g, use_scalar(g, src, t), t, base, off);
}

// Member load: dst = aggregate `src` at byte `offset`.
static void gen_copy_from_offset(Gen *g, const char *src, int offset, const Tac_Val *dst, bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (byte && a64_size(t) != 1)
        fatal_error("aarch64: %s: byte copy into %s", gen_name(g), dst->u.var_name);
    int base;
    int64_t off;
    name_addr(g, src, T3, &base, &off);
    off += offset;
    if (is_wide(t)) {
        int dbase;
        int64_t doff;
        name_addr(g, dst->u.var_name, T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, base, off, a64_size(t), a64_align(t));
        return;
    }
    int d = def_reg(g, a64_is_fp(t) ? F0 : T0, dst);
    load_mem(g, d, t, base, off);
    store_val(g, d, dst);
}

// The register view of an operation on type `t`: W up to 32 bits, else X.
static A64_Width int_width(const Tac_Type *t)
{
    return a64_size(t) <= 4 ? A64_W : A64_X;
}

// An integer conversion.  A store truncates to the destination's width, and a move
// into a register brings the value to its canonical form; a value in a register or
// loaded is extended by the source's own type, so an extension is explicit only where
// that differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type) or into 64 bits, a zero extension of a signed one.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    int ssize    = a64_size(st);
    A64_Width dw = int_width(dt);
    int s        = use_val(g, T0, src);
    int d        = def_reg(g, T0, dst);
    A64_Op op    = A64_RET;
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND)
        op = ssize == 1 ? A64_SXTB : ssize == 2 ? A64_SXTH : ssize == 4 && dw == A64_X ? A64_SXTW : op;
    else if (kind == TAC_INSTRUCTION_ZERO_EXTEND)
        op = ssize == 1 ? A64_UXTB : ssize == 2 ? A64_UXTH : op; // a W value's upper half is zero
    if (op == A64_RET) {
        store_int(g, s, dst);
        return;
    }
    emit2(g, op, a64_reg(d, op == A64_SXTB || op == A64_SXTH || op == A64_SXTW ? dw : A64_W),
          a64_reg(s, A64_W));
    store_int(g, d, dst);
}

// A floating-point negation, or `!` (equal to zero, a NaN is not).
static void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    A64_Width w        = a64_width(t);
    int s              = use_val(g, F0, in->u.unary.src);
    const Tac_Val *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        int d = def_reg(g, T0, dst);
        emit2(g, A64_FCMP, a64_reg(s, w), a64_fzero());
        emit2(g, A64_CSET, a64_reg(d, A64_W), a64_cond(A64_EQ));
        store_int(g, d, dst);
        return;
    }
    bool is_sqrt = in->u.unary.op == TAC_UNARY_SQRT_DOUBLE;
    if (!is_sqrt && in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("aarch64: %s: bad floating-point unary operator", gen_name(g));
    int d = def_reg(g, F0, dst);
    emit2(g, is_sqrt ? A64_FSQRT : A64_FNEG, a64_reg(d, w), a64_reg(s, w));
    store_val(g, d, dst);
}

// A long double negation (its sign bit flipped in a copy), or `!`.
static void gen_ld_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        ld_nonzero(g, T0, in->u.unary.src);
        emit2(g, A64_CMP, a64_reg(T0, A64_X), a64_imm(0));
        emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(A64_EQ));
        store_int(g, T0, dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("aarch64: %s: bad long double unary operator", gen_name(g));
    gen_copy(g, in->u.unary.src, dst);
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, T4, &base, &off);
    A64_Operand hi = a64_reg(T0, A64_X);
    emit2(g, A64_LDR, hi, mem(g, base, off + 8, 8));
    gen_li(g, T1, A64_X, INT64_MIN);
    emit3(g, A64_EOR, hi, hi, a64_reg(T1, A64_X));
    emit2(g, A64_STR, hi, mem(g, base, off + 8, 8));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (a64_is_ld(t)) {
        gen_ld_unary(g, in);
        return;
    }
    if (a64_is_fp(t)) {
        gen_fp_unary(g, in, t);
        return;
    }
    A64_Width w   = int_width(t);
    int s         = use_scalar(g, in->u.unary.src, t);
    int d         = def_reg(g, T0, in->u.unary.dst);
    A64_Operand r = a64_reg(d, w), a = a64_reg(s, w);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit2(g, A64_NEG, r, a);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A64_MVN, r, a);
        break;
    case TAC_UNARY_NOT:
        emit2(g, A64_CMP, a, a64_imm(0));
        emit2(g, A64_CSET, a64_reg(d, A64_W), a64_cond(A64_EQ));
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
    case TAC_UNARY_SQRT_DOUBLE:
        fatal_error("aarch64: %s: a floating-point unary operator on an integer", gen_name(g));
    }
    store_int(g, d, in->u.unary.dst);
}

// Whether operator `op` is unsigned whatever its operands' types: they may differ in
// signedness, once copy propagation has removed a cast.
static bool unsigned_op(Tac_BinaryOperator op)
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

// The condition of comparison `op`, or -1 when it is not one.
static int compare_cond(Tac_BinaryOperator op, bool is_unsigned)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A64_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A64_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return is_unsigned ? A64_LO : A64_LT;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A64_LS : A64_LE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return is_unsigned ? A64_HI : A64_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A64_HS : A64_GE;
    default:
        return -1;
    }
}

// d = a op b for an integer operator, all at view `w`.  The sources are read before d
// is written; d is not a or b only for the remainder.
static void gen_int_binop(Gen *g, Tac_BinaryOperator op, bool is_unsigned, A64_Width w, int d,
                          int a, int b)
{
    A64_Operand rd = a64_reg(d, w), ra = a64_reg(a, w), rb = a64_reg(b, w);
    int cond = compare_cond(op, is_unsigned);
    if (cond >= 0) {
        emit2(g, A64_CMP, ra, rb);
        emit2(g, A64_CSET, a64_reg(d, A64_W), a64_cond(cond));
        return;
    }
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        emit3(g, A64_ADD, rd, ra, rb);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        emit3(g, A64_SUB, rd, ra, rb);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        emit3(g, A64_MUL, rd, ra, rb);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
        emit3(g, is_unsigned ? A64_UDIV : A64_SDIV, rd, ra, rb);
        break;
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED: {
        // a - (a / b) * b
        A64_Operand q = a64_reg(T2, w);
        emit3(g, is_unsigned ? A64_UDIV : A64_SDIV, q, ra, rb);
        emit4(g, A64_MSUB, rd, q, rb, ra);
        break;
    }
    case TAC_BINARY_BITWISE_AND:
        emit3(g, A64_AND, rd, ra, rb);
        break;
    case TAC_BINARY_BITWISE_OR:
        emit3(g, A64_ORR, rd, ra, rb);
        break;
    case TAC_BINARY_BITWISE_XOR:
        emit3(g, A64_EOR, rd, ra, rb);
        break;
    case TAC_BINARY_LEFT_SHIFT:
        emit3(g, A64_LSL, rd, ra, rb);
        break;
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        emit3(g, is_unsigned ? A64_LSR : A64_ASR, rd, ra, rb);
        break;
    default:
        fatal_error("aarch64: floating-point operator %d on integers", op);
    }
}

// A floating-point operator.  fcmp sets C and V for unordered operands, so mi and ls
// are false for a NaN where lt and le would not be.
static int fp_compare_cond(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A64_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A64_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        return A64_MI;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        return A64_LS;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        return A64_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        return A64_GE;
    default:
        return -1;
    }
}

static void gen_fp_binary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    A64_Width w        = a64_width(t);
    A64_Operand fa     = a64_reg(use_val(g, F0, in->u.binary.src1), w);
    A64_Operand fb     = a64_reg(use_val(g, F1, in->u.binary.src2), w);
    const Tac_Val *dst = in->u.binary.dst;
    int cond           = fp_compare_cond(in->u.binary.op);
    if (cond >= 0) {
        int d = def_reg(g, T0, dst);
        emit2(g, A64_FCMP, fa, fb);
        emit2(g, A64_CSET, a64_reg(d, A64_W), a64_cond(cond));
        store_int(g, d, dst);
        return;
    }
    A64_Op op;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        op = A64_FADD;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        op = A64_FSUB;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        op = A64_FMUL;
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        op = A64_FDIV;
        break;
    default:
        fatal_error("aarch64: %s: bad floating-point operator %d", gen_name(g), in->u.binary.op);
    }
    int d = def_reg(g, F0, dst);
    emit3(g, op, a64_reg(d, w), fa, fb);
    store_val(g, d, dst);
}

// Long double arithmetic and comparison: a call to the runtime (libgcc names), the
// operands in q0 and q1, the result in q0.  A comparison routine returns an int in w0,
// to compare against zero: unordered operands make each false but `!=`.
static void gen_ld_binary(Gen *g, const Tac_Instruction *in)
{
    static const struct {
        Tac_BinaryOperator op;
        const char *name;
        int cond;
    } ops[] = {
        { TAC_BINARY_ADD, "__addtf3", -1 },
        { TAC_BINARY_ADD_DOUBLE, "__addtf3", -1 },
        { TAC_BINARY_SUBTRACT, "__subtf3", -1 },
        { TAC_BINARY_SUBTRACT_DOUBLE, "__subtf3", -1 },
        { TAC_BINARY_MULTIPLY, "__multf3", -1 },
        { TAC_BINARY_MULTIPLY_DOUBLE, "__multf3", -1 },
        { TAC_BINARY_DIVIDE, "__divtf3", -1 },
        { TAC_BINARY_DIVIDE_DOUBLE, "__divtf3", -1 },
        { TAC_BINARY_EQUAL, "__eqtf2", A64_EQ },
        { TAC_BINARY_NOT_EQUAL, "__netf2", A64_NE },
        { TAC_BINARY_LESS_THAN, "__lttf2", A64_LT },
        { TAC_BINARY_LESS_THAN_DOUBLE, "__lttf2", A64_LT },
        { TAC_BINARY_LESS_OR_EQUAL, "__letf2", A64_LE },
        { TAC_BINARY_LESS_OR_EQUAL_DOUBLE, "__letf2", A64_LE },
        { TAC_BINARY_GREATER_THAN, "__gttf2", A64_GT },
        { TAC_BINARY_GREATER_THAN_DOUBLE, "__gttf2", A64_GT },
        { TAC_BINARY_GREATER_OR_EQUAL, "__getf2", A64_GE },
        { TAC_BINARY_GREATER_OR_EQUAL_DOUBLE, "__getf2", A64_GE },
    };
    size_t i = 0;
    while (i < sizeof(ops) / sizeof(ops[0]) && ops[i].op != in->u.binary.op)
        i++;
    if (i == sizeof(ops) / sizeof(ops[0]))
        fatal_error("aarch64: %s: bad long double operator %d", gen_name(g), in->u.binary.op);
    load_val(g, A64_V(0), in->u.binary.src1);
    load_val(g, A64_V(1), in->u.binary.src2);
    emit1(g, A64_BL, a64_sym(ops[i].name, 0));
    if (ops[i].cond < 0) {
        store_val(g, A64_V(0), in->u.binary.dst);
        return;
    }
    emit2(g, A64_CMP, a64_reg(A64_X(0), A64_W), a64_imm(0));
    emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(ops[i].cond));
    store_int(g, T0, in->u.binary.dst);
}

// The register holding integer operand 2 of binary `in`, on type `t` (a shift count
// on its own type).
static int use_operand2(Gen *g, const Tac_Instruction *in, const Tac_Type *t, bool shift)
{
    const Tac_Val *src2 = in->u.binary.src2;
    if (src2->kind == TAC_VAL_CONSTANT) {
        load_const_as(g, T1, src2->u.constant, shift ? val_type(g, src2) : t);
        return T1;
    }
    return use_val(g, T1, src2);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (a64_is_ld(t)) {
        gen_ld_binary(g, in);
        return;
    }
    if (a64_is_fp(t)) {
        gen_fp_binary(g, in, t);
        return;
    }
    // A shift count may be of another width: it is used at the shifted value's.
    Tac_BinaryOperator op = in->u.binary.op;
    bool shift            = op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
                            op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
    int a = use_scalar(g, in->u.binary.src1, t);
    int b = use_operand2(g, in, t, shift);
    int d = def_reg(g, T0, in->u.binary.dst);
    gen_int_binop(g, op, t->kind == TAC_TYPE_POINTER || unsigned_op(op), int_width(t), d, a, b);
    store_int(g, d, in->u.binary.dst);
}

// An int/FP or float/double conversion.  To an integer it truncates toward zero, into
// the destination's width (a narrower one is truncated by the store); the signedness of
// an integer source is the conversion's (its own type may differ, once copy propagation
// has removed a cast).
static void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    bool sfp = a64_is_fp(st), dfp = a64_is_fp(dt);
    int s    = use_val(g, sfp ? F0 : T0, src);
    int d    = def_reg(g, dfp ? F1 : T1, dst);
    A64_Op op;
    if (sfp && dfp) {
        op = A64_FCVT;
    } else if (dfp) {
        bool u = kind == TAC_INSTRUCTION_UINT_TO_DOUBLE || kind == TAC_INSTRUCTION_UINT_TO_FLOAT;
        op     = u ? A64_UCVTF : A64_SCVTF;
    } else {
        op = a64_is_unsigned(dt) ? A64_FCVTZU : A64_FCVTZS;
    }
    emit2(g, op, a64_reg(d, dfp ? a64_width(dt) : int_width(dt)),
          a64_reg(s, sfp ? a64_width(st) : int_width(st)));
    if (dfp)
        store_val(g, d, dst);
    else
        store_int(g, d, dst);
}

// A conversion to or from long double: a call to the runtime, the value in x0/w0, s0/d0
// or q0 both ways.  An integer is signed or unsigned by the conversion's kind or the
// destination's type, as above.
static void gen_ld_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    const Tac_Type *other = a64_is_ld(st) ? dt : st;
    bool w = a64_size(other) <= 4, fp = a64_is_fp(other);
    const char *name;
    if (a64_is_ld(st)) {
        bool u = a64_is_unsigned(dt);
        if (fp)
            name = a64_is_double(dt) ? "__trunctfdf2" : "__trunctfsf2";
        else
            name = w ? (u ? "__fixunstfsi" : "__fixtfsi") : (u ? "__fixunstfdi" : "__fixtfdi");
    } else {
        bool u = kind == TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE;
        if (fp)
            name = a64_is_double(st) ? "__extenddftf2" : "__extendsftf2";
        else
            name = w ? (u ? "__floatunsitf" : "__floatsitf") : (u ? "__floatunditf" : "__floatditf");
    }
    load_val(g, a64_is_ld(st) || fp ? A64_V(0) : A64_X(0), src);
    emit1(g, A64_BL, a64_sym(name, 0));
    if (a64_is_ld(dt) || fp)
        store_val(g, A64_V(0), dst);
    else
        store_int(g, A64_X(0), dst);
}

// dst = &src, of a named object or function.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    int base;
    int64_t off;
    int d = def_reg(g, T0, dst);
    name_addr(g, src->u.var_name, d, &base, &off);
    gen_addr(g, d, base, off);
    store_val(g, d, dst);
}

bool runtime_call(const Tac_Instruction *in, TypeOf *type_of, const void *arg,
                  const Tac_Val **dst)
{
    switch (in->kind) {
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
        *dst              = in->u.binary.dst;
        const Tac_Type *t = type_of(arg, in->u.binary.src1);
        return t && a64_is_ld(t);
    }
    default:
        return false;
    }
}

bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (!g->uses || !next || in->kind != TAC_INSTRUCTION_BINARY ||
        (next->kind != TAC_INSTRUCTION_JUMP_IF_ZERO && next->kind != TAC_INSTRUCTION_JUMP_IF_NOT_ZERO))
        return false;
    const Tac_Val *c = next->u.jump_if_zero.condition, *dst = in->u.binary.dst;
    if (c->kind != TAC_VAL_VAR || strcmp(c->u.var_name, dst->u.var_name) != 0)
        return false;
    int v = flow_var(g->flow, dst->u.var_name);
    if (v < 0 || g->uses[v] != 1 || flow_has(g->flow->in_memory, v))
        return false;
    const Tac_Type *t     = val_type(g, in->u.binary.src1);
    Tac_BinaryOperator op = in->u.binary.op;
    int cond;
    if (a64_is_ld(t)) {
        return false;
    } else if (a64_is_fp(t)) {
        if ((cond = fp_compare_cond(op)) < 0)
            return false;
        A64_Width w = a64_width(t);
        int a       = use_val(g, F0, in->u.binary.src1);
        int b       = use_val(g, F1, in->u.binary.src2);
        emit2(g, A64_FCMP, a64_reg(a, w), a64_reg(b, w));
    } else {
        if ((cond = compare_cond(op, t->kind == TAC_TYPE_POINTER || unsigned_op(op))) < 0)
            return false;
        A64_Width w = int_width(t);
        int a       = use_scalar(g, in->u.binary.src1, t);
        int b       = use_operand2(g, in, t, false);
        emit2(g, A64_CMP, a64_reg(a, w), a64_reg(b, w));
    }
    // The conditions come in pairs, a condition and its inverse: for FP compares too,
    // since unordered operands make each one used false but ne.
    if (next->kind == TAC_INSTRUCTION_JUMP_IF_ZERO)
        cond ^= 1;
    char *l = label_name(next->u.jump_if_zero.target);
    emit2(g, A64_BCOND, a64_cond(cond), a64_sym(l, 0));
    xfree(l);
    return true;
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL:
        gen_label(g, in->u.label.name);
        break;
    case TAC_INSTRUCTION_JUMP:
        gen_jump(g, in->u.jump.target);
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
        fatal_error("aarch64: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
