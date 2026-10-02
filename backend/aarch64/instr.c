//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers and its result stored back.
//
#include "codegen.h"
#include "internal.h"

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a64_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a64_size(t), a64_align(t));
        return;
    }
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    if (a64_is_fp(t)) {
        load_val(g, F0, src);
    } else {
        load_int_as(g, T0, src, t);
    }
    store_val(g, a64_is_fp(t) ? F0 : T0, dst);
}

// The register view of an operation on type `t`: W up to 32 bits, else X.
static A64_Width int_width(const Tac_Type *t)
{
    return a64_size(t) <= 4 ? A64_W : A64_X;
}

// An integer conversion.  A store truncates to the destination's width; a load
// extends by the source's own type, so an extension is explicit only where that
// differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type) or into 64 bits, a zero extension of a signed one.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    int ssize    = a64_size(st);
    A64_Width dw = int_width(dt);
    load_val(g, T0, src);
    A64_Operand d = a64_reg(T0, dw), s = a64_reg(T0, A64_W);
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND) {
        if (ssize == 1)
            emit2(g, A64_SXTB, d, s);
        else if (ssize == 2)
            emit2(g, A64_SXTH, d, s);
        else if (ssize == 4 && dw == A64_X)
            emit2(g, A64_SXTW, d, s);
    } else if (kind == TAC_INSTRUCTION_ZERO_EXTEND) {
        if (ssize == 1)
            emit2(g, A64_UXTB, s, s);
        else if (ssize == 2)
            emit2(g, A64_UXTH, s, s);
        // A W register's upper half is already zero.
    }
    store_val(g, T0, dst);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (a64_is_fp(t) || a64_is_ld(t))
        fatal_error("aarch64: %s: floating-point unary is not implemented yet", gen_name(g));
    A64_Width w = int_width(t);
    load_int_as(g, T0, in->u.unary.src, t);
    A64_Operand r = a64_reg(T0, w);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit2(g, A64_NEG, r, r);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A64_MVN, r, r);
        break;
    case TAC_UNARY_NOT:
        emit2(g, A64_CMP, r, a64_imm(0));
        emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(A64_EQ));
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("aarch64: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, T0, in->u.unary.dst);
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

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (a64_is_fp(t) || a64_is_ld(t))
        fatal_error("aarch64: %s: floating-point arithmetic is not implemented yet", gen_name(g));
    // A shift count may be of another width: it is used at the shifted value's.
    Tac_BinaryOperator op = in->u.binary.op;
    bool shift            = op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
                            op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
    load_int_as(g, T0, in->u.binary.src1, t);
    load_int_as(g, T1, in->u.binary.src2, shift ? val_type(g, in->u.binary.src2) : t);
    gen_int_binop(g, op, t->kind == TAC_TYPE_POINTER || unsigned_op(op), int_width(t), T0, T0, T1);
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
        fatal_error("aarch64: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
