//
// Instruction selection: one TAC instruction at a time, naive.  An operation loads its
// operands into the scratch registers, operates, and stores the result.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

// dst = src: a scalar loaded by its own type and stored in the destination's width, an
// aggregate copied.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!mmix_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, src->u.var_name, 0, mmix_type_size(t),
                   mmix_type_align(t));
        return;
    }
    load_val(g, src, REG_A);
    store_val(g, REG_A, dst);
}

// The type an operation on `a` and `b` works in: a variable's, else a constant's.
static const Tac_Type *operand_type(const Gen *g, const Tac_Val *a, const Tac_Val *b)
{
    return val_type(g, a->kind == TAC_VAL_VAR || !b ? a : b);
}

// Load `v` into `reg` from its own width, extended by `sign` rather than by its type: a
// width conversion's source.
static void load_ext(Gen *g, const Tac_Val *v, int reg, bool sign)
{
    int size = mmix_type_size(val_type(g, v));
    if (v->kind == TAC_VAL_VAR) {
        mem_op(g, load_op_ext(size, sign), reg, v->u.var_name, 0);
        return;
    }
    uint64_t bits = const_bits(v->u.constant);
    if (size < 8) {
        int shift = 64 - 8 * size;
        bits      = sign ? (uint64_t)((int64_t)(bits << shift) >> shift) : bits << shift >> shift;
    }
    gen_const(g, reg, bits);
}

// A width conversion: the source extended as `sign` says (or by its type when
// truncating), stored in the destination's width.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, int sign)
{
    if (sign < 0)
        load_val(g, src, REG_A);
    else
        load_ext(g, src, REG_A, sign);
    store_val(g, REG_A, dst);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_SQRT_DOUBLE ||
        (mmix_is_fp(val_type(g, src)) && in->u.unary.op != TAC_UNARY_NOT)) {
        gen_fp_unary(g, in);
        return;
    }
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        load_val(g, src, REG_A);
        emit3(g, MMIX_NEGU, mmix_reg(REG_A), mmix_imm(0), mmix_reg(REG_A));
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        load_val(g, src, REG_A);
        emit3(g, MMIX_NOR, mmix_reg(REG_A), mmix_reg(REG_A), mmix_imm(0));
        break;
    case TAC_UNARY_NOT:
        if (mmix_is_fp(val_type(g, src)))
            gen_fp_test(g, src, REG_A);
        else
            load_val(g, src, REG_A);
        emit3(g, MMIX_ZSZ, mmix_reg(REG_A), mmix_reg(REG_A), mmix_imm(1));
        break;
    default:
        fatal_error("mmix: %s: unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
    store_val(g, REG_A, dst);
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
    load_val_as(g, a, REG_A, t);
    if (!is_zero(b) || (is_unsigned && zs != MMIX_ZSZ && zs != MMIX_ZSNZ)) {
        Mmix_Operand z = val_operand(g, b, REG_B, t);
        emit3(g, is_unsigned ? MMIX_CMPU : MMIX_CMP, mmix_reg(REG_A), mmix_reg(REG_A), z);
    }
    emit3(g, zs, mmix_reg(REG_A), mmix_reg(REG_A), mmix_imm(1));
    store_val(g, REG_A, in->u.binary.dst);
}

// Signed division: div rounds the quotient down and leaves a remainder with the
// divisor's sign; C truncates.  When the remainder is nonzero and the operands' signs
// differ, the quotient is one more and the remainder one divisor less.
static void gen_signed_divide(Gen *g, const Tac_Instruction *in, const Tac_Type *t,
                              bool remainder)
{
    load_val_as(g, in->u.binary.src1, REG_A, t);
    load_val_as(g, in->u.binary.src2, REG_B, t);
    emit3(g, MMIX_DIV, mmix_reg(REG_C), mmix_reg(REG_A), mmix_reg(REG_B));
    emit2(g, MMIX_GET, mmix_reg(MMIX_TMP), mmix_special(MMIX_rR));
    emit3(g, MMIX_XOR, mmix_reg(REG_A), mmix_reg(REG_A), mmix_reg(REG_B)); // signs differ: < 0
    if (remainder) {
        // $1 = the divisor when a fix-up is due, else 0.
        emit3(g, MMIX_ZSN, mmix_reg(REG_A), mmix_reg(REG_A), mmix_reg(REG_B));
        emit3(g, MMIX_CSZ, mmix_reg(REG_A), mmix_reg(MMIX_TMP), mmix_imm(0));
        emit3(g, MMIX_SUBU, mmix_reg(REG_C), mmix_reg(MMIX_TMP), mmix_reg(REG_A));
    } else {
        emit3(g, MMIX_ZSN, mmix_reg(REG_A), mmix_reg(REG_A), mmix_imm(1));
        emit3(g, MMIX_CSZ, mmix_reg(REG_A), mmix_reg(MMIX_TMP), mmix_imm(0));
        emit3(g, MMIX_ADDU, mmix_reg(REG_C), mmix_reg(REG_C), mmix_reg(REG_A));
    }
    store_val(g, REG_C, in->u.binary.dst);
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
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        mop = MMIX_ADDU;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        mop = MMIX_SUBU;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        mop = MMIX_MULU;
        break;
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        mop = MMIX_DIVU; // divides rD:$Y, rD being 0; the remainder in rR
        break;
    case TAC_BINARY_BITWISE_AND:
        mop = MMIX_AND;
        break;
    case TAC_BINARY_BITWISE_OR:
        mop = MMIX_OR;
        break;
    case TAC_BINARY_BITWISE_XOR:
        mop = MMIX_XOR;
        break;
    case TAC_BINARY_LEFT_SHIFT:
        mop = MMIX_SLU;
        break;
    case TAC_BINARY_RIGHT_SHIFT:
        mop = MMIX_SR;
        break;
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        mop = MMIX_SRU;
        break;
    default:
        fatal_error("mmix: %s: binary operator %d is not implemented", gen_name(g), op);
    }
    // A shift count has a type of its own.
    bool shift              = mop == MMIX_SLU || mop == MMIX_SR || mop == MMIX_SRU;
    const Tac_Val *b        = in->u.binary.src2;
    load_val_as(g, in->u.binary.src1, REG_A, t);
    Mmix_Operand z = val_operand(g, b, REG_B, shift ? val_type(g, b) : t);
    emit3(g, mop, mmix_reg(REG_A), mmix_reg(REG_A), z);
    if (op == TAC_BINARY_REMAINDER_UNSIGNED)
        emit2(g, MMIX_GET, mmix_reg(REG_A), mmix_special(MMIX_rR));
    store_val(g, REG_A, in->u.binary.dst);
}

// Branch to TAC label `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    if (mmix_is_fp(val_type(g, cond)))
        gen_fp_test(g, cond, REG_A);
    else
        load_val(g, cond, REG_A);
    char *l = label_name(target);
    emit2(g, if_zero ? MMIX_BZ : MMIX_BNZ, mmix_reg(REG_A), mmix_label(l));
    xfree(l);
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
        address_of(g, REG_A, in->u.get_address.src->u.var_name, 0);
        store_val(g, REG_A, in->u.get_address.dst);
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
