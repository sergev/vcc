//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers, or used from memory or as an immediate where x86 allows.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Local label for TAC label `%N`: `.LN`.  TAC labels are unique in a translation unit.
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
    x86_new_block(g->fn, l);
    xfree(l);
}

static void gen_jump(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    emit1(g, X86_JMP, X86_Q, x86_label(l));
    xfree(l);
}

// Branch to TAC label `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    char *l           = label_name(target);
    if (x86_is_ld(t)) {
        gen_ld_cond_jump(g, if_zero, cond, l);
        xfree(l);
        return;
    }
    if (x86_is_fp(t)) {
        gen_fp_cond_jump(g, if_zero, cond, l);
        xfree(l);
        return;
    }
    X86_Width w = x86_op_width(t);
    load_val(g, T0, cond);
    emit2(g, X86_TEST, w, x86_reg(T0, w), x86_reg(T0, w));
    X86_Instr *j = emit1(g, X86_J, X86_Q, x86_label(l));
    j->cond      = if_zero ? X86_CC_E : X86_CC_NE;
    xfree(l);
}

// Store scalar `src` as type `t` at memory operand `mem`: a constant straight in as an
// immediate when it fits, a variable through rax.
static void store_to(Gen *g, const Tac_Val *src, const Tac_Type *t, X86_Operand mem)
{
    X86_Width w = x86_width_of(x86_size(t));
    if (x86_is_ld(t)) {
        gen_ld_copy(g, src, mem);
        return;
    }
    if (x86_is_fp(t)) {
        gen_fp_copy(g, src, mem, t);
        return;
    }
    if (src->kind == TAC_VAL_CONSTANT) {
        int64_t imm = const_as(src->u.constant, t);
        if (w != X86_Q || x86_imm32(imm)) {
            emit2(g, X86_MOV, w, x86_imm(w == X86_L ? (int32_t)imm : imm), mem);
            return;
        }
    }
    load_int_as(g, T0, src, t);
    store_mem(g, T0, t, mem);
}

// dst = src, for a scalar.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (x86_is_aggregate(t))
        fatal_error("x86: %s: copying this type is not implemented yet", gen_name(g));
    store_to(g, src, t, name_mem(g, dst->u.var_name, 0));
}

// An integer conversion: the source loaded extended as the conversion says (its own
// type may differ in signedness, once copy propagation has removed a cast), stored at
// the destination's width, which truncates.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (src->kind == TAC_VAL_CONSTANT || kind == TAC_INSTRUCTION_TRUNCATE) {
        gen_copy(g, src, dst);
        return;
    }
    X86_Width w   = x86_op_width(dt);
    X86_Operand m = name_mem(g, src->u.var_name, 0);
    bool sign     = kind == TAC_INSTRUCTION_SIGN_EXTEND;
    switch (x86_size(st)) {
    case 1:
        emit2(g, sign ? X86_MOVSB : X86_MOVZB, w, m, x86_reg(T0, w));
        break;
    case 2:
        emit2(g, sign ? X86_MOVSW : X86_MOVZW, w, m, x86_reg(T0, w));
        break;
    case 4:
        if (sign && w == X86_Q)
            emit2(g, X86_MOVSL, X86_Q, m, x86_reg(T0, X86_Q));
        else
            emit2(g, X86_MOV, X86_L, m, x86_reg(T0, X86_L)); // the upper half zero
        break;
    default:
        load_val(g, T0, src);
        break;
    }
    store_val(g, T0, dst);
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
        return X86_CC_E;
    case TAC_BINARY_NOT_EQUAL:
        return X86_CC_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return is_unsigned ? X86_CC_B : X86_CC_L;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return is_unsigned ? X86_CC_BE : X86_CC_LE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return is_unsigned ? X86_CC_A : X86_CC_G;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return is_unsigned ? X86_CC_AE : X86_CC_GE;
    default:
        return -1;
    }
}

// eax = 0 or 1 by condition `cond` of the flags.
static void gen_setcc(Gen *g, int cond)
{
    X86_Instr *set = emit1(g, X86_SET, X86_B, x86_reg(T0, X86_B));
    set->cond      = cond;
    emit2(g, X86_MOVZB, X86_L, x86_reg(T0, X86_B), x86_reg(T0, X86_L));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (x86_is_ld(t)) {
        gen_ld_unary(g, in);
        return;
    }
    if (x86_is_fp(t)) {
        gen_fp_unary(g, in, t);
        return;
    }
    X86_Width w   = x86_op_width(t);
    X86_Operand r = x86_reg(T0, w);
    load_int_as(g, T0, in->u.unary.src, t);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit1(g, X86_NEG, w, r);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit1(g, X86_NOT, w, r);
        break;
    case TAC_UNARY_NOT:
        emit2(g, X86_TEST, w, r, r);
        gen_setcc(g, X86_CC_E);
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("x86: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, T0, in->u.unary.dst);
}

// dst = src1 / src2 or src1 % src2: the dividend in rdx:rax, sign- or zero-extended,
// the divisor in a register or memory (never an immediate); the quotient comes back in
// rax, the remainder in rdx.
static void gen_divide(Gen *g, const Tac_Instruction *in, const Tac_Type *t, bool is_unsigned)
{
    X86_Width w = x86_op_width(t);
    load_int_as(g, T0, in->u.binary.src1, t);
    X86_Operand d = src_operand(g, in->u.binary.src2, t, T1);
    if (d.kind == X86_OPND_IMM) {
        gen_li(g, T1, w, d.imm);
        d = x86_reg(T1, w);
    }
    if (is_unsigned)
        emit2(g, X86_XOR, X86_L, x86_reg(X86_RDX, X86_L), x86_reg(X86_RDX, X86_L));
    else
        emit0(g, w == X86_Q ? X86_CQTO : X86_CLTD, w);
    emit1(g, is_unsigned ? X86_DIV : X86_IDIV, w, d);
    Tac_BinaryOperator op = in->u.binary.op;
    bool rem = op == TAC_BINARY_REMAINDER || op == TAC_BINARY_REMAINDER_UNSIGNED;
    store_val(g, rem ? X86_RDX : T0, in->u.binary.dst);
}

// dst = src1 shifted by src2: by an immediate, or by %cl.  The count is used modulo
// the width, as the hardware does (C leaves larger counts undefined).
static void gen_shift(Gen *g, const Tac_Instruction *in, const Tac_Type *t, bool is_unsigned)
{
    X86_Width w           = x86_op_width(t);
    Tac_BinaryOperator op = in->u.binary.op;
    X86_Op sh = op == TAC_BINARY_LEFT_SHIFT ? X86_SHL : is_unsigned ? X86_SHR : X86_SAR;
    const Tac_Val *count  = in->u.binary.src2;
    load_int_as(g, T0, in->u.binary.src1, t);
    if (count->kind == TAC_VAL_CONSTANT) {
        emit2(g, sh, w, x86_imm(const_value(count->u.constant) & (w == X86_Q ? 63 : 31)),
              x86_reg(T0, w));
    } else {
        load_val(g, X86_RCX, count);
        emit2(g, sh, w, x86_reg(X86_RCX, X86_B), x86_reg(T0, w));
    }
    store_val(g, T0, in->u.binary.dst);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (x86_is_ld(t)) {
        gen_ld_binary(g, in);
        return;
    }
    if (x86_is_fp(t)) {
        gen_fp_binary(g, in, t);
        return;
    }
    Tac_BinaryOperator op = in->u.binary.op;
    bool is_unsigned      = t->kind == TAC_TYPE_POINTER || unsigned_op(op);
    X86_Width w           = x86_op_width(t);
    X86_Operand r         = x86_reg(T0, w);
    switch (op) {
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        gen_divide(g, in, t, is_unsigned);
        return;
    case TAC_BINARY_LEFT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        gen_shift(g, in, t, is_unsigned);
        return;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED: {
        // An immediate factor takes the three-operand form, the other one from memory.
        X86_Operand b = src_operand(g, in->u.binary.src2, t, T1);
        X86_Operand a = b.kind == X86_OPND_IMM ? src_operand(g, in->u.binary.src1, t, T0) : r;
        if (a.kind != X86_OPND_IMM && b.kind == X86_OPND_IMM) {
            emit2(g, X86_IMUL, w, b, a)->opnd[2] = r;
        } else {
            load_int_as(g, T0, in->u.binary.src1, t);
            emit2(g, X86_IMUL, w, b, r);
        }
        store_val(g, T0, in->u.binary.dst);
        return;
    }
    default:
        break;
    }
    load_int_as(g, T0, in->u.binary.src1, t);
    X86_Operand b = src_operand(g, in->u.binary.src2, t, T1);
    int cond      = compare_cond(op, is_unsigned);
    if (cond >= 0) {
        emit2(g, X86_CMP, w, b, r);
        gen_setcc(g, cond);
        store_val(g, T0, in->u.binary.dst);
        return;
    }
    X86_Op xop;
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        xop = X86_ADD;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        xop = X86_SUB;
        break;
    case TAC_BINARY_BITWISE_AND:
        xop = X86_AND;
        break;
    case TAC_BINARY_BITWISE_OR:
        xop = X86_OR;
        break;
    case TAC_BINARY_BITWISE_XOR:
        xop = X86_XOR;
        break;
    default:
        fatal_error("x86: %s: floating-point operator %d on integers", gen_name(g), op);
    }
    emit2(g, xop, w, b, r);
    store_val(g, T0, in->u.binary.dst);
}

// The type stored through pointer value `ptr`, or NULL when not known.
static const Tac_Type *pointee(const Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// dst = *src_ptr, the pointer in r10.
static void gen_load(Gen *g, const Tac_Val *src_ptr, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    load_val(g, T1, src_ptr);
    if (x86_is_aggregate(t))
        fatal_error("x86: %s: loading this type is not implemented yet", gen_name(g));
    if (x86_is_ld(t)) {
        for (int i = 0; i < 2; i++) {
            emit2(g, X86_MOV, X86_Q, x86_mem(T1, 8 * i), x86_reg(T0, X86_Q));
            emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), name_mem(g, dst->u.var_name, 8 * i));
        }
        return;
    }
    int r = x86_is_fp(t) ? F0 : T0;
    load_mem(g, r, t, x86_mem(T1, 0));
    store_val(g, r, dst);
}

// *dst_ptr = src, the pointer in r10, in the width of the pointee (or of src when that
// is not known).
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *dst_ptr)
{
    const Tac_Type *t = pointee(g, dst_ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (x86_is_aggregate(t) && !x86_is_aggregate(val_type(g, src))))
        t = val_type(g, src);
    if (x86_is_aggregate(t))
        fatal_error("x86: %s: storing this type is not implemented yet", gen_name(g));
    load_val(g, T1, dst_ptr);
    store_to(g, src, t, x86_mem(T1, 0));
}

// dst = ptr + index * scale (bytes): lea with the index scaled by 1, 2, 4 or 8, or
// multiplied first.  An index narrower than 64 bits is extended by its type: a 32-bit
// load has zeroed the upper half.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    int scale            = in->u.add_ptr.scale;
    const Tac_Val *index = in->u.add_ptr.index;
    load_val(g, T0, in->u.add_ptr.ptr);
    if (index->kind == TAC_VAL_CONSTANT && x86_imm32(const_value(index->u.constant) * scale)) {
        emit2(g, X86_LEA, X86_Q, x86_mem(T0, const_value(index->u.constant) * scale),
              x86_reg(T0, X86_Q));
    } else {
        const Tac_Type *it = val_type(g, index);
        load_val(g, T1, index);
        if (x86_size(it) <= 4 && !x86_is_unsigned(it))
            emit2(g, X86_MOVSL, X86_Q, x86_reg(T1, X86_L), x86_reg(T1, X86_Q));
        if (scale != 1 && scale != 2 && scale != 4 && scale != 8) {
            emit2(g, X86_IMUL, X86_Q, x86_imm(scale), x86_reg(T1, X86_Q))->opnd[2] =
                x86_reg(T1, X86_Q);
            scale = 1;
        }
        emit2(g, X86_LEA, X86_Q, x86_mem_index(T0, T1, scale, 0), x86_reg(T0, X86_Q));
    }
    store_val(g, T0, in->u.add_ptr.dst);
}

// dst = a - b, a byte count.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    static const Tac_Type ptr = { .kind = TAC_TYPE_POINTER };
    load_val(g, T0, in->u.ptr_diff.ptr_a);
    emit2(g, X86_SUB, X86_Q, src_operand(g, in->u.ptr_diff.ptr_b, &ptr, T1), x86_reg(T0, X86_Q));
    store_val(g, T0, in->u.ptr_diff.dst);
}

// The scalar type at byte `offset` of aggregate type `t`, or NULL.  Of several union
// members there, one of `size` bytes is preferred, else the first.
static const Tac_Type *scalar_at(const Tac_Type *t, int offset, int size)
{
    if (!t)
        return NULL;
    if (t->kind == TAC_TYPE_ARRAY) {
        int esize = x86_size(t->u.array.elem_type);
        return esize > 0 ? scalar_at(t->u.array.elem_type, offset % esize, size) : NULL;
    }
    if (t->kind != TAC_TYPE_STRUCTURE)
        return offset == 0 ? t : NULL;
    const Tac_Type *first = NULL;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (offset < m->offset || offset >= m->offset + x86_size(m->type))
            continue;
        const Tac_Type *s = scalar_at(m->type, offset - m->offset, size);
        if (s && x86_size(s) == size)
            return s;
        if (!first)
            first = s;
    }
    return first;
}

// Member store: aggregate `dst` at byte `offset` = src.  A constant takes the type of
// the member there (its own kind may be wider); a byte copy is one byte.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *dst, int offset, bool byte)
{
    static const Tac_Type uchar = { .kind = TAC_TYPE_UCHAR };
    const Tac_Type *t           = val_type(g, src);
    if (byte) {
        t = &uchar;
    } else if (src->kind == TAC_VAL_CONSTANT) {
        const Tac_Type *m = scalar_at(name_type(g, dst), offset, x86_size(t));
        if (m && x86_is_fp(m) == x86_is_fp(t) && x86_is_ld(m) == x86_is_ld(t) &&
            !x86_is_aggregate(m))
            t = m;
    }
    if (x86_is_aggregate(t))
        fatal_error("x86: %s: copying an aggregate member is not implemented yet", gen_name(g));
    store_to(g, src, t, name_mem(g, dst, offset));
}

// Member load: dst = aggregate `src` at byte `offset`.
static void gen_copy_from_offset(Gen *g, const char *src, int offset, const Tac_Val *dst, bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (byte && x86_size(t) != 1)
        fatal_error("x86: %s: byte copy into %s", gen_name(g), dst->u.var_name);
    if (x86_is_aggregate(t))
        fatal_error("x86: %s: copying an aggregate member is not implemented yet", gen_name(g));
    if (x86_is_ld(t)) {
        for (int i = 0; i < 2; i++) {
            emit2(g, X86_MOV, X86_Q, name_mem(g, src, offset + 8 * i), x86_reg(T0, X86_Q));
            emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), name_mem(g, dst->u.var_name, 8 * i));
        }
        return;
    }
    int r = x86_is_fp(t) ? F0 : T0;
    load_mem(g, r, t, name_mem(g, src, offset));
    store_val(g, r, dst);
}

// dst = &src, of a named object or function: lea of its slot or symbol.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    emit2(g, X86_LEA, X86_Q, name_mem(g, src->u.var_name, 0), x86_reg(T0, X86_Q));
    store_val(g, T0, dst);
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
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.ptr_to_char_ptr.src, in->u.ptr_to_char_ptr.dst);
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
        fatal_error("x86: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
