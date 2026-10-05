//
// Floating point, in hardware: binary64 arithmetic, comparisons and conversions are
// instructions.  A float is held in a register as its exact binary64 value: ldsf loads
// it, and stsf stores it rounded to binary32, so a float result is computed in binary64
// and rounded once, on its store.  For + - * / of two floats that is correctly rounded
// (53 >= 2*24 + 2).
//
#include "internal.h"

enum { ROUND_OFF = 1 }; // fix's rounding mode: toward zero, as C converts

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    load_val(g, in->u.binary.src1, REG_A);
    load_val(g, in->u.binary.src2, REG_B);
    Mmix_Operand a = mmix_reg(REG_A), b = mmix_reg(REG_B), c = mmix_reg(REG_C);
    switch (op) {
    case TAC_BINARY_ADD_DOUBLE:
        emit3(g, MMIX_FADD, c, a, b);
        break;
    case TAC_BINARY_SUBTRACT_DOUBLE:
        emit3(g, MMIX_FSUB, c, a, b);
        break;
    case TAC_BINARY_MULTIPLY_DOUBLE:
        emit3(g, MMIX_FMUL, c, a, b);
        break;
    case TAC_BINARY_DIVIDE_DOUBLE:
        emit3(g, MMIX_FDIV, c, a, b);
        break;
    case TAC_BINARY_EQUAL:
        emit3(g, MMIX_FEQL, c, a, b); // 0 for a NaN
        break;
    case TAC_BINARY_NOT_EQUAL:
        emit3(g, MMIX_FEQL, c, a, b);
        emit3(g, MMIX_ZSZ, c, c, mmix_imm(1));
        break;
    // fcmp gives -1/0/1, and 0 for an unordered pair too: < and > take its sign, <= and
    // >= also need fun to rule out a NaN.
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        emit3(g, MMIX_FCMP, c, a, b);
        emit3(g, MMIX_ZSN, c, c, mmix_imm(1));
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        emit3(g, MMIX_FCMP, c, a, b);
        emit3(g, MMIX_ZSP, c, c, mmix_imm(1));
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE: {
        bool le = op == TAC_BINARY_LESS_OR_EQUAL || op == TAC_BINARY_LESS_OR_EQUAL_DOUBLE;
        emit3(g, MMIX_FCMP, c, a, b);
        emit3(g, MMIX_FUN, mmix_reg(MMIX_TMP), a, b);
        emit3(g, le ? MMIX_ZSNP : MMIX_ZSNN, c, c, mmix_imm(1));
        emit3(g, MMIX_CSNZ, c, mmix_reg(MMIX_TMP), mmix_imm(0));
        break;
    }
    default:
        fatal_error("mmix: %s: binary operator %d on floating point", gen_name(g), op);
    }
    store_val(g, REG_C, in->u.binary.dst);
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    load_val(g, in->u.unary.src, REG_A);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_DOUBLE:
        // The sign bit flipped: exact for -0 and a NaN.
        emit2(g, MMIX_SETH, mmix_reg(MMIX_TMP), mmix_wyde(0x8000));
        emit3(g, MMIX_XOR, mmix_reg(REG_A), mmix_reg(REG_A), mmix_reg(MMIX_TMP));
        break;
    case TAC_UNARY_SQRT_DOUBLE:
        emit3(g, MMIX_FSQRT, mmix_reg(REG_A), mmix_imm(0), mmix_reg(REG_A)); // rA's mode
        break;
    default:
        fatal_error("mmix: %s: unary operator %d on floating point", gen_name(g),
                    in->u.unary.op);
    }
    store_val(g, REG_A, in->u.unary.dst);
}

// Any bit but the sign: a NaN is true, -0 false.
void gen_fp_test(Gen *g, const Tac_Val *v, int reg)
{
    load_val(g, v, reg);
    emit3(g, MMIX_SLU, mmix_reg(reg), mmix_reg(reg), mmix_imm(1));
}

void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    load_val(g, src, REG_A);
    Mmix_Operand a = mmix_reg(REG_A);
    switch (kind) {
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
        emit2(g, MMIX_FLOT, a, a);
        break;
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
        emit2(g, MMIX_FLOTU, a, a);
        break;
    // sflot rounds the integer to binary32 once; flot and then stsf would round twice.
    case TAC_INSTRUCTION_INT_TO_FLOAT:
        emit2(g, MMIX_SFLOT, a, a);
        break;
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
        emit2(g, MMIX_SFLOTU, a, a);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
        emit3(g, MMIX_FIX, a, mmix_imm(ROUND_OFF), a);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
        emit3(g, MMIX_FIXU, a, mmix_imm(ROUND_OFF), a);
        break;
    default:
        // Between float, double and long double: ldsf widens exactly, stsf rounds, and
        // long double is double.
        break;
    }
    store_val(g, REG_A, dst);
}
