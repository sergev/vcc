//
// Instruction selection: one TAC instruction at a time, operands through scratch
// registers.
//
#include <stdlib.h>
#include <string.h>

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

// Branch to `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    if (rv_is_fp(t)) {
        // t0 = (cond == 0.0), so a zero condition is a nonzero t0.
        load_val(g, RV_F0, cond);
        emit2(g, rv_is_double(t) ? RV_FMVDX : RV_FMVWX, rv_reg(RV_F0 + 1), rv_reg(RV_ZERO));
        emit3(g, rv_is_double(t) ? RV_FEQD : RV_FEQS, rv_reg(RV_T0), rv_reg(RV_F0),
              rv_reg(RV_F0 + 1));
        if_zero = !if_zero;
    } else {
        load_val(g, RV_T0, cond);
    }
    gen_jump(g, if_zero ? RV_BEQZ : RV_BNEZ, RV_T0, target);
}

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (rv_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, RV_T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, RV_T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, rv_size(t), rv_align(t));
        return;
    }
    int reg = rv_is_fp(t) ? RV_F0 : RV_T0;
    load_val(g, reg, src);
    store_val(g, reg, dst);
}

// dst = &src, of a named object or function.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Slot *slot = find_slot(g, src->u.var_name);
    if (slot)
        gen_addr(g, RV_T0, RV_S0, slot->offset);
    else
        emit2(g, RV_LA, rv_reg(RV_T0), rv_sym(src->u.var_name, 0));
    store_val(g, RV_T0, dst);
}

// Zero-extend `reg` from `size` bytes.
static void gen_zext(Gen *g, int reg, int size)
{
    if (size >= 8)
        return;
    if (size == 1) {
        emit3(g, RV_ANDI, rv_reg(reg), rv_reg(reg), rv_imm(255));
        return;
    }
    int shift = 64 - 8 * size;
    emit3(g, RV_SLLI, rv_reg(reg), rv_reg(reg), rv_imm(shift));
    emit3(g, RV_SRLI, rv_reg(reg), rv_reg(reg), rv_imm(shift));
}

// An integer conversion: load by the source type, store by the destination's.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, bool zext)
{
    load_val(g, RV_T0, src);
    if (zext)
        gen_zext(g, RV_T0, rv_size(val_type(g, src)));
    store_val(g, RV_T0, dst);
}

static void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    bool d = rv_is_double(t);
    load_val(g, RV_F0, in->u.unary.src);
    if (in->u.unary.op == TAC_UNARY_NOT) {
        emit2(g, d ? RV_FMVDX : RV_FMVWX, rv_reg(RV_F0 + 1), rv_reg(RV_ZERO));
        emit3(g, d ? RV_FEQD : RV_FEQS, rv_reg(RV_T0), rv_reg(RV_F0), rv_reg(RV_F0 + 1));
        store_val(g, RV_T0, in->u.unary.dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("riscv: %s: bad floating-point unary operator", gen_name(g));
    emit2(g, d ? RV_FNEGD : RV_FNEGS, rv_reg(RV_F0), rv_reg(RV_F0));
    store_val(g, RV_F0, in->u.unary.dst);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (rv_is_fp(t)) {
        gen_fp_unary(g, in, t);
        return;
    }
    bool word = rv_size(t) <= 4;
    load_val(g, RV_T0, in->u.unary.src);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit2(g, word ? RV_NEGW : RV_NEG, rv_reg(RV_T0), rv_reg(RV_T0));
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, RV_NOT, rv_reg(RV_T0), rv_reg(RV_T0));
        break;
    case TAC_UNARY_NOT:
        emit2(g, RV_SEQZ, rv_reg(RV_T0), rv_reg(RV_T0));
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("riscv: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, RV_T0, in->u.unary.dst);
}

// t0 = t0 op t1 for an integer operator; `word` selects the 32-bit forms.
static void gen_int_binop(Gen *g, Tac_BinaryOperator op, bool word, bool is_unsigned)
{
    Rv_Operand d = rv_reg(RV_T0), a = rv_reg(RV_T0), b = rv_reg(RV_T1);
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

static bool is_unsigned_op(Tac_BinaryOperator op)
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
    bool d       = rv_is_double(t);
    Rv_Operand a = rv_reg(RV_F0), b = rv_reg(RV_F0 + 1), r = rv_reg(RV_T0);
    load_val(g, RV_F0, in->u.binary.src1);
    load_val(g, RV_F0 + 1, in->u.binary.src2);
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        emit3(g, d ? RV_FADDD : RV_FADDS, a, a, b);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        emit3(g, d ? RV_FSUBD : RV_FSUBS, a, a, b);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        emit3(g, d ? RV_FMULD : RV_FMULS, a, a, b);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        emit3(g, d ? RV_FDIVD : RV_FDIVS, a, a, b);
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
    store_val(g, rv_is_fp(val_type(g, in->u.binary.dst)) ? RV_F0 : RV_T0, in->u.binary.dst);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (rv_is_fp(t)) {
        gen_fp_binary(g, in, t);
        return;
    }
    load_val(g, RV_T0, in->u.binary.src1);
    load_val(g, RV_T1, in->u.binary.src2);
    gen_int_binop(g, in->u.binary.op, rv_size(t) <= 4,
                  rv_is_unsigned(t) || is_unsigned_op(in->u.binary.op));
    store_val(g, RV_T0, in->u.binary.dst);
}

// An int/FP or float/double conversion.
static void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    bool sfp = rv_is_fp(st), dfp = rv_is_fp(dt);
    Rv_Op op;
    if (sfp && dfp) {
        op = rv_is_double(dt) ? RV_FCVTDS : RV_FCVTSD;
    } else if (dfp) {
        bool w = rv_size(st) <= 4, u = rv_is_unsigned(st);
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
    int sreg = sfp ? RV_F0 : RV_T0;
    int dreg = dfp ? RV_F0 + 1 : RV_T1;
    load_val(g, sreg, src);
    Rv_Instr *cv = emit2(g, op, rv_reg(dreg), rv_reg(sreg));
    if (!dfp)
        cv->opnd[2] = rv_sym("rtz", 0);
    store_val(g, dreg, dst);
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
        gen_int_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, false);
        break;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_int_convert(g, in->u.zero_extend.src, in->u.zero_extend.dst, true);
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
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        fatal_error("riscv: long double is not implemented");
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
