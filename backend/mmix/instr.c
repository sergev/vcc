//
// Instruction selection: one TAC instruction at a time.  An operation works on the
// registers its operands are in, or loads them into the scratch registers, and computes
// its result in the destination's register, or in a scratch register to store.  A
// sequence of several instructions computes in a scratch register, since the
// destination's may be an operand's.
//
#include <string.h>

#include "flow.h"
#include "internal.h"
#include "xalloc.h"

// Whether a value of type `st`, loaded as type `dt` (load_val_as), is in canonical form
// for `dt`: not when narrowed, nor a float from another type.
static bool same_form(const Tac_Type *st, const Tac_Type *dt)
{
    if (mmix_is_fp(st) || mmix_is_fp(dt))
        return mmix_is_float(st) == mmix_is_float(dt);
    return mmix_type_size(dt) >= mmix_type_size(st);
}

// dst = src: a scalar in the destination's type, an aggregate copied.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!mmix_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, src->u.var_name, 0, mmix_type_size(t),
                   mmix_type_align(t));
        return;
    }
    bool canonical = src->kind == TAC_VAL_CONSTANT || same_form(val_type(g, src), t);
    if (val_reg(g, dst) < 0 && canonical) { // stored from where it is
        def_done(g, use_val(g, src, REG_A, t), dst, true);
        return;
    }
    int d = def_reg(g, dst, REG_A);
    load_val_as(g, src, d, t);
    def_done(g, d, dst, canonical);
}

// The type an operation on `a` and `b` works in: a variable's, else a constant's.
static const Tac_Type *operand_type(const Gen *g, const Tac_Val *a, const Tac_Val *b)
{
    return val_type(g, a->kind == TAC_VAL_VAR || !b ? a : b);
}

// A width conversion: the source extended from its own width as `sign` says, or (`sign`
// < 0) truncated to the destination's, which extends it by the destination's type.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, int sign)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    int d = def_reg(g, dst, REG_A);
    if (src->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_bits(src->u.constant);
        int size      = sign < 0 ? mmix_type_size(dt) : mmix_type_size(st);
        bool sx       = sign < 0 ? !mmix_is_unsigned(dt) : sign;
        if (size < 8) {
            int shift = 64 - 8 * size;
            bits      = sx ? (uint64_t)((int64_t)(bits << shift) >> shift) : bits << shift >> shift;
        }
        gen_const(g, d, bits);
        def_done(g, d, dst, sign < 0 || mmix_type_size(dt) == 8 || mmix_is_unsigned(dt) == !sx);
        return;
    }
    if (val_reg(g, src) < 0) {
        // Loaded in the width and signedness wanted; a truncated value's low-order bytes
        // are its last.
        const Tac_Type *lt = sign < 0 ? dt : st;
        bool sx            = sign < 0 ? !mmix_is_unsigned(dt) : sign;
        int64_t off        = sign < 0 ? mmix_type_size(st) - mmix_type_size(dt) : 0;
        mem_op(g, load_op_ext(mmix_type_size(lt), sx), d, src->u.var_name, off);
        def_done(g, d, dst, sign < 0 || mmix_type_size(dt) == 8 || mmix_is_unsigned(dt) == !sx);
        return;
    }
    int s = use_val(g, src, REG_A, NULL);
    if (sign < 0) {
        extend_reg(g, d, s, mmix_type_size(dt), !mmix_is_unsigned(dt));
        def_done(g, d, dst, true);
        return;
    }
    // A register holds the source extended by its own signedness already.
    if (mmix_is_unsigned(st) == !sign)
        move_reg(g, d, s);
    else
        extend_reg(g, d, s, mmix_type_size(st), sign);
    def_done(g, d, dst, mmix_type_size(dt) == 8 || mmix_is_unsigned(dt) == !sign);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_SQRT_DOUBLE ||
        (mmix_is_fp(val_type(g, src)) && in->u.unary.op != TAC_UNARY_NOT)) {
        gen_fp_unary(g, in);
        return;
    }
    const Tac_Type *t = val_type(g, dst);
    int d             = def_reg(g, dst, REG_A);
    bool canonical    = true;
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit3(g, MMIX_NEGU, mmix_reg(d), mmix_imm(0), mmix_reg(use_val(g, src, REG_A, t)));
        canonical = mmix_type_size(t) == 8;
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit3(g, MMIX_NOR, mmix_reg(d), mmix_reg(use_val(g, src, REG_A, t)), mmix_imm(0));
        canonical = mmix_type_size(t) == 8 || !mmix_is_unsigned(t);
        break;
    case TAC_UNARY_NOT: {
        int s = mmix_is_fp(val_type(g, src)) ? gen_fp_test(g, src, REG_A)
                                             : use_val(g, src, REG_A, NULL);
        emit3(g, MMIX_ZSZ, mmix_reg(d), mmix_reg(s), mmix_imm(1));
        break;
    }
    default:
        fatal_error("mmix: %s: unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
    def_done(g, d, dst, canonical);
}

// The conditional set that turns a comparison's -1/0/1 into the operator's 0/1.
static bool compare_set(Tac_BinaryOperator op, Mmix_Op *zs, bool *is_unsigned)
{
    *is_unsigned = false;
    switch (op) {
    case TAC_BINARY_EQUAL:
        *zs = MMIX_ZSZ;
        return true;
    case TAC_BINARY_NOT_EQUAL:
        *zs = MMIX_ZSNZ;
        return true;
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        *is_unsigned = true;
        // fall through
    case TAC_BINARY_LESS_THAN:
        *zs = MMIX_ZSN;
        return true;
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        *is_unsigned = true;
        // fall through
    case TAC_BINARY_LESS_OR_EQUAL:
        *zs = MMIX_ZSNP;
        return true;
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        *is_unsigned = true;
        // fall through
    case TAC_BINARY_GREATER_THAN:
        *zs = MMIX_ZSP;
        return true;
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        *is_unsigned = true;
        // fall through
    case TAC_BINARY_GREATER_OR_EQUAL:
        *zs = MMIX_ZSNN;
        return true;
    default:
        return false;
    }
}

static bool is_zero(const Tac_Val *v)
{
    return v->kind == TAC_VAL_CONSTANT && const_bits(v->u.constant) == 0;
}

// dst = a OP b as 0/1: cmp or cmpu gives -1/0/1, a conditional set the result.  Against
// zero a signed comparison or an equality needs no cmp: the value's own sign decides.
static void gen_compare(Gen *g, const Tac_Instruction *in, Mmix_Op zs, bool is_unsigned)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    const Tac_Type *t = operand_type(g, a, b);
    is_unsigned |= t->kind == TAC_TYPE_POINTER;
    int x = use_val(g, a, REG_A, t), d = def_reg(g, in->u.binary.dst, REG_A);
    if (!is_zero(b) || (is_unsigned && zs != MMIX_ZSZ && zs != MMIX_ZSNZ)) {
        Mmix_Operand z = val_operand(g, b, REG_B, t);
        emit3(g, is_unsigned ? MMIX_CMPU : MMIX_CMP, mmix_reg(REG_A), mmix_reg(x), z);
        x = REG_A;
    }
    emit3(g, zs, mmix_reg(d), mmix_reg(x), mmix_imm(1));
    def_done(g, d, in->u.binary.dst, true);
}

// Signed division: div rounds the quotient down and leaves a remainder with the
// divisor's sign; C truncates.  When the remainder is nonzero and the operands' signs
// differ, the quotient is one more and the remainder one divisor less.
static void gen_signed_divide(Gen *g, const Tac_Instruction *in, const Tac_Type *t,
                              bool remainder)
{
    int a = use_val(g, in->u.binary.src1, REG_A, t);
    int b = use_val(g, in->u.binary.src2, REG_B, t);
    Mmix_Operand A = mmix_reg(REG_A), B = mmix_reg(b), C = mmix_reg(REG_C), T = mmix_reg(MMIX_TMP);
    emit3(g, MMIX_DIV, C, mmix_reg(a), B);
    emit2(g, MMIX_GET, T, mmix_special(MMIX_rR));
    emit3(g, MMIX_XOR, A, mmix_reg(a), B); // signs differ: < 0
    if (remainder) {
        // $248 = the divisor when a fix-up is due, else 0.
        emit3(g, MMIX_ZSN, A, A, B);
        emit3(g, MMIX_CSZ, A, T, mmix_imm(0));
        emit3(g, MMIX_SUBU, C, T, A);
    } else {
        emit3(g, MMIX_ZSN, A, A, mmix_imm(1));
        emit3(g, MMIX_CSZ, A, T, mmix_imm(0));
        emit3(g, MMIX_ADDU, C, C, A);
    }
    def_done(g, REG_C, in->u.binary.dst, true);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Type *t     = operand_type(g, in->u.binary.src1, in->u.binary.src2);
    if (mmix_is_fp(t)) {
        gen_fp_binary(g, in);
        return;
    }
    Mmix_Op zs;
    bool is_unsigned;
    if (compare_set(op, &zs, &is_unsigned)) {
        gen_compare(g, in, zs, is_unsigned);
        return;
    }
    if (op == TAC_BINARY_DIVIDE || op == TAC_BINARY_REMAINDER) {
        gen_signed_divide(g, in, t, op == TAC_BINARY_REMAINDER);
        return;
    }
    Mmix_Op mop;
    bool canonical = false; // may overflow the type
    switch (op) {
    // A signed int or long result that overflows is undefined: like GCC's, ours is left
    // as the 64-bit sum, unextended (with the peephole optimizations on).  A narrower
    // one is a conversion back from int, which wraps.
    case TAC_BINARY_ADD:
        mop       = MMIX_ADDU;
        canonical = mmix_peephole_on;
        break;
    case TAC_BINARY_ADD_UNSIGNED:
        mop = MMIX_ADDU;
        break;
    case TAC_BINARY_SUBTRACT:
        mop       = MMIX_SUBU;
        canonical = mmix_peephole_on;
        break;
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        mop = MMIX_SUBU;
        break;
    case TAC_BINARY_MULTIPLY:
        mop       = MMIX_MULU;
        canonical = mmix_peephole_on;
        break;
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        mop = MMIX_MULU;
        break;
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        mop       = MMIX_DIVU; // divides rD:$Y, rD being 0; the remainder in rR
        canonical = true;
        break;
    case TAC_BINARY_BITWISE_AND:
        mop       = MMIX_AND;
        canonical = true;
        break;
    case TAC_BINARY_BITWISE_OR:
        mop       = MMIX_OR;
        canonical = true;
        break;
    case TAC_BINARY_BITWISE_XOR:
        mop       = MMIX_XOR;
        canonical = true;
        break;
    case TAC_BINARY_LEFT_SHIFT:
        mop = MMIX_SLU;
        break;
    case TAC_BINARY_RIGHT_SHIFT:
        mop       = MMIX_SR;
        canonical = true;
        break;
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        mop       = MMIX_SRU;
        canonical = true;
        break;
    default:
        fatal_error("mmix: %s: binary operator %d is not implemented", gen_name(g), op);
    }
    // A shift count has a type of its own.  A constant first operand of a commutative
    // operation goes second, where a byte is the immediate.
    bool shift       = mop == MMIX_SLU || mop == MMIX_SR || mop == MMIX_SRU;
    const Tac_Val *a_val = in->u.binary.src1, *b = in->u.binary.src2;
    bool commutes = mop == MMIX_ADDU || mop == MMIX_MULU || mop == MMIX_AND || mop == MMIX_OR ||
                    mop == MMIX_XOR;
    if (commutes && a_val->kind == TAC_VAL_CONSTANT && b->kind == TAC_VAL_VAR) {
        b     = a_val;
        a_val = in->u.binary.src2;
    }
    // A constant of -1..-255 added is 1..255 subtracted, and the other way round.
    if ((mop == MMIX_ADDU || mop == MMIX_SUBU) && b->kind == TAC_VAL_CONSTANT) {
        int64_t k = (int64_t)const_as(b->u.constant, t);
        if (k < 0 && k >= -255) {
            int x = use_val(g, a_val, REG_A, t), d = def_reg(g, in->u.binary.dst, REG_A);
            emit3(g, mop == MMIX_ADDU ? MMIX_SUBU : MMIX_ADDU, mmix_reg(d), mmix_reg(x),
                  mmix_imm(-k));
            int size = mmix_type_size(val_type(g, in->u.binary.dst));
            def_done(g, d, in->u.binary.dst, size == 8 || (canonical && size >= 4));
            return;
        }
    }
    int a = use_val(g, a_val, REG_A, t);
    Mmix_Operand z   = val_operand(g, b, REG_B, shift ? val_type(g, b) : t);
    int d            = def_reg(g, in->u.binary.dst, REG_A);
    emit3(g, mop, mmix_reg(d), mmix_reg(a), z);
    if (op == TAC_BINARY_REMAINDER_UNSIGNED)
        emit2(g, MMIX_GET, mmix_reg(d), mmix_special(MMIX_rR));
    int size = mmix_type_size(val_type(g, in->u.binary.dst));
    if (size < 4 && (op == TAC_BINARY_ADD || op == TAC_BINARY_SUBTRACT || op == TAC_BINARY_MULTIPLY))
        canonical = false;
    def_done(g, d, in->u.binary.dst, canonical || size == 8);
}

// Branch to TAC label `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    int r = mmix_is_fp(val_type(g, cond)) ? gen_fp_test(g, cond, REG_A)
                                          : use_val(g, cond, REG_A, NULL);
    char *l = label_name(target);
    emit2(g, if_zero ? MMIX_BZ : MMIX_BNZ, mmix_reg(r), mmix_label(l));
    xfree(l);
}

// The type `ptr` points to, or NULL when unknown.
static const Tac_Type *pointee(const Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// The type a load or store through `ptr` of value `v` accesses: the pointee's when it is
// a known scalar, else the value's.
static const Tac_Type *access_type(const Gen *g, const Tac_Val *ptr, const Tac_Val *v)
{
    const Tac_Type *t = pointee(g, ptr);
    if (!t || !mmix_is_scalar(t) || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return val_type(g, v);
    return t;
}

// The bytes a load or store through `ptr` of aggregate `v` moves.
static int access_size(const Gen *g, const Tac_Val *ptr, const Tac_Val *v)
{
    const Tac_Type *t = pointee(g, ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (t->kind == TAC_TYPE_STRUCTURE && t->u.structure.size == 0))
        t = val_type(g, v);
    return mmix_type_size(t);
}

// dst = *ptr.  A scalar takes the destination's width from the address: the pointee may
// be wider (a row of a 2-D array), and big-endian puts the leading bytes first.  An
// aggregate is copied from $249 to $250.
static void gen_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (!mmix_is_scalar(t)) {
        load_val(g, ptr, REG_B);
        address_of(g, REG_C, dst->u.var_name, 0);
        copy_bytes(g, access_size(g, ptr, dst), mmix_type_align(t));
        return;
    }
    int p      = use_val(g, ptr, REG_B, NULL), d = def_reg(g, dst, REG_A);
    Mmix_Op op = byte ? load_op_ext(1, !mmix_is_unsigned(t)) : load_op(t);
    emit3(g, op, mmix_reg(d), mmix_reg(p), mmix_imm(0));
    def_done(g, d, dst, true);
}

// *ptr = src, in the pointee's width; an aggregate copied from $249 to $250.
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, bool byte)
{
    const Tac_Type *st = val_type(g, src);
    if (src->kind == TAC_VAL_VAR && !mmix_is_scalar(st)) {
        address_of(g, REG_B, src->u.var_name, 0);
        load_val(g, ptr, REG_C);
        copy_bytes(g, access_size(g, ptr, src), mmix_type_align(st));
        return;
    }
    const Tac_Type *t = access_type(g, ptr, src);
    if (mmix_is_fp(t) != mmix_is_fp(st))
        t = st;
    int v = use_val(g, src, REG_A, t), p = use_val(g, ptr, REG_B, NULL);
    emit3(g, byte ? MMIX_STBU : store_op(t), mmix_reg(v), mmix_reg(p), mmix_imm(0));
}

// dst = ptr + index * scale: 2addu..16addu for a scale of 2 to 16, mulu otherwise.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index;
    int scale            = in->u.add_ptr.scale;
    int p = use_val(g, in->u.add_ptr.ptr, REG_A, NULL), d = def_reg(g, in->u.add_ptr.dst, REG_A);
    if (index->kind == TAC_VAL_CONSTANT) {
        int64_t off = (int64_t)const_bits(index->u.constant) * scale;
        if (off)
            add_offset(g, d, p, off);
        else
            move_reg(g, d, p);
    } else {
        int x          = use_val(g, index, REG_B, NULL);
        Mmix_Operand a = mmix_reg(p), b = mmix_reg(x), r = mmix_reg(d);
        switch (scale) {
        case 1:
            emit3(g, MMIX_ADDU, r, a, b);
            break;
        case 2:
            emit3(g, MMIX_ADDU2, r, b, a);
            break;
        case 4:
            emit3(g, MMIX_ADDU4, r, b, a);
            break;
        case 8:
            emit3(g, MMIX_ADDU8, r, b, a);
            break;
        case 16:
            emit3(g, MMIX_ADDU16, r, b, a);
            break;
        default:
            if (scale <= 255) {
                emit3(g, MMIX_MULU, mmix_reg(REG_B), b, mmix_imm(scale));
            } else {
                gen_const(g, REG_C, (uint64_t)scale);
                emit3(g, MMIX_MULU, mmix_reg(REG_B), b, mmix_reg(REG_C));
            }
            emit3(g, MMIX_ADDU, r, a, mmix_reg(REG_B));
            break;
        }
    }
    def_done(g, d, in->u.add_ptr.dst, true);
}

// dst = a - b, two byte pointers.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    int a = use_val(g, in->u.ptr_diff.ptr_a, REG_A, NULL);
    int b = use_val(g, in->u.ptr_diff.ptr_b, REG_B, NULL);
    int d = def_reg(g, in->u.ptr_diff.dst, REG_A);
    emit3(g, MMIX_SUBU, mmix_reg(d), mmix_reg(a), mmix_reg(b));
    def_done(g, d, in->u.ptr_diff.dst, true);
}

// The scalar type at byte `offset` of aggregate type `t`, or NULL.  Of several union
// members there, one of `size` bytes is preferred, else the first.
static const Tac_Type *scalar_at(const Tac_Type *t, int offset, int size)
{
    if (!t)
        return NULL;
    if (t->kind == TAC_TYPE_ARRAY) {
        int esize = mmix_type_size(t->u.array.elem_type);
        return esize > 0 ? scalar_at(t->u.array.elem_type, offset % esize, size) : NULL;
    }
    if (t->kind != TAC_TYPE_STRUCTURE)
        return offset == 0 ? t : NULL;
    const Tac_Type *first = NULL;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (offset < m->offset || offset >= m->offset + mmix_type_size(m->type))
            continue;
        const Tac_Type *s = scalar_at(m->type, offset - m->offset, size);
        if (s && mmix_type_size(s) == size)
            return s;
        if (!first)
            first = s;
    }
    return first;
}

// Member `offset` of aggregate `name` = src, in src's width (a byte for the BYTE form).
// A constant takes the type of the member there: a zero filling a pointer member may
// come as an int.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *name, int offset,
                               bool byte)
{
    const Tac_Type *t = val_type(g, src);
    if (src->kind == TAC_VAL_VAR && !mmix_is_scalar(t)) {
        copy_named(g, name, offset, src->u.var_name, 0, mmix_type_size(t), mmix_type_align(t));
        return;
    }
    if (src->kind == TAC_VAL_CONSTANT && !byte) {
        const Tac_Type *m = scalar_at(name_type(g, name), offset, mmix_type_size(t));
        if (m && mmix_is_scalar(m) && m->kind != TAC_TYPE_VOID && mmix_is_fp(m) == mmix_is_fp(t))
            t = m;
    }
    int v = use_val(g, src, REG_A, t);
    mem_op(g, byte ? MMIX_STBU : store_op(t), v, name, offset);
}

// dst = member `offset` of aggregate `name`, in dst's width (a byte for the BYTE form).
static void gen_copy_from_offset(Gen *g, const char *name, int offset, const Tac_Val *dst,
                                 bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (!mmix_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, name, offset, mmix_type_size(t), mmix_type_align(t));
        return;
    }
    int d = def_reg(g, dst, REG_A);
    mem_op(g, byte ? load_op_ext(1, !mmix_is_unsigned(t)) : load_op(t), d, name, offset);
    def_done(g, d, dst, true);
}

// Whether `dst` is read once only (by the instruction after its definition), and is not
// in memory: the two may be fused, with no `dst` at all.
static bool read_once(const Gen *g, const Tac_Val *dst)
{
    if (!g->uses || !dst || dst->kind != TAC_VAL_VAR)
        return false;
    int v = flow_var(g->flow, dst->u.var_name);
    return v >= 0 && g->uses[v] == 1 && !flow_has(g->flow->in_memory, v);
}

// Whether `in` is a conditional jump on variable `v`.
static bool jumps_on(const Tac_Instruction *in, const Tac_Val *v)
{
    if (!in || (in->kind != TAC_INSTRUCTION_JUMP_IF_ZERO && in->kind != TAC_INSTRUCTION_JUMP_IF_NOT_ZERO))
        return false;
    const Tac_Val *c = in->u.jump_if_zero.condition;
    return c->kind == TAC_VAL_VAR && strcmp(c->u.var_name, v->u.var_name) == 0;
}

static Mmix_Op invert_branch(Mmix_Op op)
{
    switch (op) {
    case MMIX_BZ:
        return MMIX_BNZ;
    case MMIX_BNZ:
        return MMIX_BZ;
    case MMIX_BN:
        return MMIX_BNN;
    case MMIX_BNN:
        return MMIX_BN;
    case MMIX_BP:
        return MMIX_BNP;
    default:
        return MMIX_BP; // bnp
    }
}

static void branch_to(Gen *g, Mmix_Op op, int reg, const char *target)
{
    char *l = label_name(target);
    emit2(g, op, mmix_reg(reg), mmix_label(l));
    xfree(l);
}

// The branch on a comparison's -1/0/1 that is taken when `op` holds.
static Mmix_Op compare_branch_op(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return MMIX_BZ;
    case TAC_BINARY_NOT_EQUAL:
        return MMIX_BNZ;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        return MMIX_BN;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return MMIX_BNP;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        return MMIX_BP;
    default:
        return MMIX_BNN; // >=
    }
}

// A comparison only the next conditional jump reads, as a branch: on cmp's or cmpu's
// sign, or on the value itself against zero.  A floating < or > branches on fcmp's
// sign, where an unordered pair gives 0 (false, and its inverse true), and == or != on
// feql; <= and >= also need fun, and are left to gen_compare.
static bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (in->kind != TAC_INSTRUCTION_BINARY || !jumps_on(next, in->u.binary.dst) ||
        !read_once(g, in->u.binary.dst))
        return false;
    Tac_BinaryOperator op = in->u.binary.op;
    Mmix_Op zs;
    bool is_unsigned;
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    const Tac_Type *t = operand_type(g, a, b);
    bool fp           = mmix_is_fp(t);
    if (fp ? !(op == TAC_BINARY_EQUAL || op == TAC_BINARY_NOT_EQUAL || op == TAC_BINARY_LESS_THAN ||
               op == TAC_BINARY_LESS_THAN_DOUBLE || op == TAC_BINARY_GREATER_THAN ||
               op == TAC_BINARY_GREATER_THAN_DOUBLE)
           : !compare_set(op, &zs, &is_unsigned))
        return false;
    Mmix_Op br = compare_branch_op(op);
    int reg;
    if (fp) {
        int x = use_val(g, a, REG_A, NULL), y = use_val(g, b, REG_B, NULL);
        bool eq = op == TAC_BINARY_EQUAL || op == TAC_BINARY_NOT_EQUAL;
        emit3(g, eq ? MMIX_FEQL : MMIX_FCMP, mmix_reg(REG_C), mmix_reg(x), mmix_reg(y));
        if (eq) // feql gives 1 when equal
            br = op == TAC_BINARY_EQUAL ? MMIX_BNZ : MMIX_BZ;
        reg = REG_C;
    } else {
        is_unsigned |= t->kind == TAC_TYPE_POINTER;
        reg = use_val(g, a, REG_A, t);
        if (!is_zero(b) || (is_unsigned && br != MMIX_BZ && br != MMIX_BNZ)) {
            Mmix_Operand z = val_operand(g, b, REG_B, t);
            emit3(g, is_unsigned ? MMIX_CMPU : MMIX_CMP, mmix_reg(REG_A), mmix_reg(reg), z);
            reg = REG_A;
        }
    }
    if (next->kind == TAC_INSTRUCTION_JUMP_IF_ZERO)
        br = invert_branch(br);
    branch_to(g, br, reg, next->u.jump_if_zero.target);
    return true;
}

// !x only the next conditional jump reads: a branch on x itself, the other way round.
static bool gen_not_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (in->kind != TAC_INSTRUCTION_UNARY || in->u.unary.op != TAC_UNARY_NOT ||
        !jumps_on(next, in->u.unary.dst) || !read_once(g, in->u.unary.dst))
        return false;
    const Tac_Val *src = in->u.unary.src;
    int r = mmix_is_fp(val_type(g, src)) ? gen_fp_test(g, src, REG_A) : use_val(g, src, REG_A, NULL);
    branch_to(g, next->kind == TAC_INSTRUCTION_JUMP_IF_ZERO ? MMIX_BNZ : MMIX_BZ, r,
              next->u.jump_if_zero.target);
    return true;
}

// A pointer sum only the next load or store reads, folded into its address: ptr plus
// a constant offset of 0..255, or plus an index at scale 1.
static bool gen_indexed_access(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (in->kind != TAC_INSTRUCTION_ADD_PTR || !next || !read_once(g, in->u.add_ptr.dst))
        return false;
    const Tac_Val *dst = in->u.add_ptr.dst, *index = in->u.add_ptr.index;
    const Tac_Val *ptr = NULL, *val = NULL;
    bool load = next->kind == TAC_INSTRUCTION_LOAD || next->kind == TAC_INSTRUCTION_LOAD_BYTE;
    bool byte = next->kind == TAC_INSTRUCTION_LOAD_BYTE || next->kind == TAC_INSTRUCTION_STORE_BYTE;
    if (load) {
        ptr = next->u.load.src_ptr;
        val = next->u.load.dst;
    } else if (next->kind == TAC_INSTRUCTION_STORE || next->kind == TAC_INSTRUCTION_STORE_BYTE) {
        ptr = next->u.store.dst_ptr;
        val = next->u.store.src;
    } else {
        return false;
    }
    if (ptr->kind != TAC_VAL_VAR || strcmp(ptr->u.var_name, dst->u.var_name) != 0 ||
        (val->kind == TAC_VAL_VAR && strcmp(val->u.var_name, dst->u.var_name) == 0) ||
        !mmix_is_scalar(val_type(g, val)))
        return false;
    int64_t off = 0;
    if (index->kind == TAC_VAL_CONSTANT) {
        off = (int64_t)const_bits(index->u.constant) * in->u.add_ptr.scale;
        if (off < 0 || off > 255)
            return false;
    } else if (in->u.add_ptr.scale != 1) {
        return false;
    }
    int p          = use_val(g, in->u.add_ptr.ptr, REG_B, NULL);
    Mmix_Operand z = index->kind == TAC_VAL_CONSTANT ? mmix_imm(off)
                                                     : mmix_reg(use_val(g, index, REG_C, NULL));
    if (load) {
        const Tac_Type *t = val_type(g, val);
        int d             = def_reg(g, val, REG_A);
        Mmix_Op op        = byte ? load_op_ext(1, !mmix_is_unsigned(t)) : load_op(t);
        emit3(g, op, mmix_reg(d), mmix_reg(p), z);
        def_done(g, d, val, true);
    } else {
        const Tac_Type *st = val_type(g, val), *t = access_type(g, ptr, val);
        if (mmix_is_fp(t) != mmix_is_fp(st))
            t = st;
        int v = use_val(g, val, REG_A, t);
        emit3(g, byte ? MMIX_STBU : store_op(t), mmix_reg(v), mmix_reg(p), z);
    }
    return true;
}

// A call whose result (or nothing) the next instruction returns: a tail call.
static bool gen_tail(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (in->kind != TAC_INSTRUCTION_FUN_CALL || (next && next->kind != TAC_INSTRUCTION_RETURN))
        return false;
    // The last instruction of a function that falls off its end returns nothing.
    const Tac_Val *dst = in->u.fun_call.dst, *ret = next ? next->u.return_.src : NULL;
    if (dst ? !ret || ret->kind != TAC_VAL_VAR || strcmp(ret->u.var_name, dst->u.var_name) != 0 ||
                  !read_once(g, dst)
            : ret != NULL)
        return false;
    return gen_tail_call(g, in);
}

bool gen_fused(Gen *g, const Tac_Instruction *in)
{
    if (!g->uses)
        return false;
    return gen_compare_branch(g, in, in->next) || gen_not_branch(g, in, in->next) ||
           gen_indexed_access(g, in, in->next) || gen_tail(g, in, in->next);
}

int instr_out_size(const Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        return call_stack_size(g, in);
    default:
        return 0;
    }
}

void gen_instr(Gen *g, const Tac_Instruction *in, bool last)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src, last);
        break;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_SIGN_EXTEND:
        gen_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, 1);
        break;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_convert(g, in->u.zero_extend.src, in->u.zero_extend.dst, 0);
        break;
    case TAC_INSTRUCTION_TRUNCATE:
        gen_convert(g, in->u.truncate.src, in->u.truncate.dst, -1);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst, in->kind);
        break;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_LABEL: {
        char *l = label_name(in->u.label.name);
        gen_label_block(g, l);
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP: {
        char *l = label_name(in->u.jump.target);
        emit1(g, MMIX_JMP, mmix_label(l));
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                      in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        if (in->u.get_address.src->kind != TAC_VAL_VAR)
            fatal_error("mmix: %s: the address of a constant", gen_name(g));
    {
        int d = def_reg(g, in->u.get_address.dst, REG_A);
        address_of(g, d, in->u.get_address.src->u.var_name, 0);
        def_done(g, d, in->u.get_address.dst, true);
    }
        break;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        gen_load(g, in->u.load.src_ptr, in->u.load.dst, in->kind == TAC_INSTRUCTION_LOAD_BYTE);
        break;
    case TAC_INSTRUCTION_STORE:
    case TAC_INSTRUCTION_STORE_BYTE:
        gen_store(g, in->u.store.src, in->u.store.dst_ptr,
                  in->kind == TAC_INSTRUCTION_STORE_BYTE);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        gen_add_ptr(g, in);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        gen_ptr_diff(g, in);
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
        fatal_error("mmix: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
