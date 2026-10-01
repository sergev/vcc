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

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    bool word         = rv_size(t) <= 4;
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
        fatal_error("riscv: %s: floating point not implemented", gen_name(g));
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
        fatal_error("riscv: %s: floating point not implemented", gen_name(g));
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

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    load_val(g, RV_T0, in->u.binary.src1);
    load_val(g, RV_T1, in->u.binary.src2);
    gen_int_binop(g, in->u.binary.op, rv_size(t) <= 4,
                  rv_is_unsigned(t) || is_unsigned_op(in->u.binary.op));
    store_val(g, RV_T0, in->u.binary.dst);
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
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("riscv: %s: %s not implemented", gen_name(g), tac_instruction_name(in->kind));
    }
}
