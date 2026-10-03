//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers and its result stored back.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// dst = src, for any type: an aggregate copied as bytes, an 8-byte scalar as two
// words, any other through r12.  A float or double needs no VFP register to move.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a32_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T0, &sbase, &soff);
        name_addr(g, dst->u.var_name, T1, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a32_size(t), a32_align(t));
        return;
    }
    if (a32_size(t) == 8) {
        load_word(g, T0, src, t, 0);
        load_word(g, T1, src, t, 1);
        store_pair(g, dst, T0, T1);
        return;
    }
    load_as(g, T0, src, t);
    store_val(g, T0, dst);
}

// An integer conversion.  A store truncates to the destination's width; a loaded value
// is extended by the source's own type, so an extension is explicit only where that
// differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type), a zero extension of a signed one.  A long long's
// high word is the low one's sign, or zero, by the conversion's kind.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (src->kind == TAC_VAL_CONSTANT) {
        gen_copy(g, src, dst); // converted as it is loaded
        return;
    }
    if (a32_is_pair(st)) {
        load_word(g, T0, src, st, 0);
        if (a32_is_pair(dt)) {
            load_word(g, T1, src, st, 1);
            store_pair(g, dst, T0, T1);
        } else {
            store_val(g, T0, dst);
        }
        return;
    }
    load_val(g, T0, src);
    int ssize = a32_size(st);
    A32_Op op = A32_EPILOGUE;
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND && a32_is_unsigned(st))
        op = ssize == 1 ? A32_SXTB : ssize == 2 ? A32_SXTH : op;
    else if (kind == TAC_INSTRUCTION_ZERO_EXTEND && !a32_is_unsigned(st))
        op = ssize == 1 ? A32_UXTB : ssize == 2 ? A32_UXTH : op;
    if (op != A32_EPILOGUE)
        emit2(g, op, a32_reg(T0), a32_reg(T0));
    if (!a32_is_pair(dt)) {
        store_val(g, T0, dst);
        return;
    }
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND)
        emit2(g, A32_MOV, a32_reg(T1), a32_shift(T0, A32_SHIFT_ASR, 31));
    else
        gen_li(g, T1, 0);
    store_pair(g, dst, T0, T1);
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

int compare_cond(Tac_BinaryOperator op, bool is_unsigned)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A32_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A32_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return is_unsigned ? A32_LO : A32_LT;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A32_LS : A32_LE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return is_unsigned ? A32_HI : A32_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A32_HS : A32_GE;
    default:
        return -1;
    }
}

bool unsigned_operation(const Tac_Type *t, Tac_BinaryOperator op)
{
    return t->kind == TAC_TYPE_POINTER || unsigned_op(op);
}

void set_cond(Gen *g, int reg, int cond)
{
    gen_li(g, reg, 0);
    emit2(g, A32_MOV, a32_reg(reg), a32_imm(1))->cond = cond;
}

A32_Operand operand2(Gen *g, A32_Op *op, const Tac_Val *v, const Tac_Type *t, int scratch)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        uint32_t c = (uint32_t)const_bits(v->u.constant, t);
        if (a32_operand2_imm(c))
            return a32_imm(c);
        // cmp and cmn set the same flags for c and -c, but for 0 and 1 << 31, which
        // are modified immediates.
        A32_Op alt  = *op;
        uint32_t ac = -c;
        if (*op == A32_ADD)
            alt = A32_SUB;
        else if (*op == A32_SUB)
            alt = A32_ADD;
        else if (*op == A32_CMP)
            alt = A32_CMN;
        else if (*op == A32_AND)
            alt = A32_BIC, ac = ~c;
        if (alt != *op && a32_operand2_imm(ac)) {
            *op = alt;
            return a32_imm(ac);
        }
    }
    load_as(g, scratch, v, t);
    return a32_reg(scratch);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (a32_is_fp(t) || a32_is_pair(t))
        fatal_error("arm32: %s: unary operator on %d bytes is not implemented yet", gen_name(g),
                    a32_size(t));
    load_as(g, T0, in->u.unary.src, t);
    A32_Operand r = a32_reg(T0);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit3(g, A32_RSB, r, r, a32_imm(0));
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A32_MVN, r, r);
        break;
    case TAC_UNARY_NOT:
        emit2(g, A32_CMP, r, a32_imm(0));
        set_cond(g, T0, A32_EQ);
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("arm32: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, T0, in->u.unary.dst);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (a32_is_fp(t) || a32_is_pair(t))
        fatal_error("arm32: %s: binary operator on %d bytes is not implemented yet", gen_name(g),
                    a32_size(t));
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *b      = in->u.binary.src2;
    bool u                = unsigned_operation(t, op);
    A32_Operand d = a32_reg(T0), a = a32_reg(T0), rb = a32_reg(T1);
    load_as(g, T0, in->u.binary.src1, t);
    int cond = compare_cond(op, u);
    if (cond >= 0) {
        A32_Op o       = A32_CMP;
        A32_Operand ob = operand2(g, &o, b, t, T1);
        emit2(g, o, a, ob);
        set_cond(g, T0, cond);
        store_val(g, T0, in->u.binary.dst);
        return;
    }
    A32_Op o;
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        o = A32_ADD;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        o = A32_SUB;
        break;
    case TAC_BINARY_BITWISE_AND:
        o = A32_AND;
        break;
    case TAC_BINARY_BITWISE_OR:
        o = A32_ORR;
        break;
    case TAC_BINARY_BITWISE_XOR:
        o = A32_EOR;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        load_as(g, T1, b, t);
        emit3(g, A32_MUL, d, a, rb);
        store_val(g, T0, in->u.binary.dst);
        return;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
        load_as(g, T1, b, t);
        emit3(g, u ? A32_UDIV : A32_SDIV, d, a, rb);
        store_val(g, T0, in->u.binary.dst);
        return;
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        // a - (a / b) * b, the quotient in r10
        load_as(g, T1, b, t);
        emit3(g, u ? A32_UDIV : A32_SDIV, a32_reg(T2), a, rb);
        emit4(g, A32_MLS, d, a32_reg(T2), rb, a);
        store_val(g, T0, in->u.binary.dst);
        return;
    case TAC_BINARY_LEFT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL: {
        // The count at its own type.  A constant one beyond the width is undefined:
        // its low five bits are used.
        A32_Op s           = op == TAC_BINARY_LEFT_SHIFT ? A32_LSL : u ? A32_LSR : A32_ASR;
        const Tac_Type *ct = val_type(g, b);
        if (b->kind == TAC_VAL_CONSTANT) {
            int n = (int)(const_bits(b->u.constant, ct) & 31);
            if (n)
                emit3(g, s, d, a, a32_imm(n));
        } else {
            if (a32_is_pair(ct))
                load_word(g, T1, b, ct, 0);
            else
                load_val(g, T1, b);
            emit3(g, s, d, a, rb);
        }
        store_val(g, T0, in->u.binary.dst);
        return;
    }
    default:
        fatal_error("arm32: %s: bad integer operator %d", gen_name(g), op);
    }
    A32_Operand ob = operand2(g, &o, b, t, T1);
    emit3(g, o, d, a, ob);
    store_val(g, T0, in->u.binary.dst);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
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
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("arm32: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
