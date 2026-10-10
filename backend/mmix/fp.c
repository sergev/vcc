//
// Floating point, in hardware: binary64 arithmetic, comparisons and conversions are
// instructions.  A float is held in a register as its exact binary64 value: ldsf loads
// it, and stsf stores it rounded to binary32, so a float result is computed in binary64
// and rounded once, on its store.  For + - * / of two floats that is correctly rounded
// (53 >= 2*24 + 2).
//
#include "internal.h"

enum { ROUND_OFF = 1 }; // fix's rounding mode: toward zero, as C converts

// An arithmetic result goes straight to the destination's register (a float rounded
// there); a comparison of several instructions computes in $250 first.
void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    int x                 = use_val(g, in->u.binary.src1, REG_A, NULL);
    int y                 = use_val(g, in->u.binary.src2, REG_B, NULL);
    Mmix_Operand a = mmix_reg(x), b = mmix_reg(y), c = mmix_reg(REG_C);
    int d        = def_reg(g, in->u.binary.dst, REG_C);
    bool rounded = !mmix_is_float(val_type(g, in->u.binary.dst));
    switch (op) {
    case TAC_BINARY_ADD_DOUBLE:
        emit3(g, MMIX_FADD, mmix_reg(d), a, b);
        def_done(g, d, in->u.binary.dst, rounded);
        return;
    case TAC_BINARY_SUBTRACT_DOUBLE:
        emit3(g, MMIX_FSUB, mmix_reg(d), a, b);
        def_done(g, d, in->u.binary.dst, rounded);
        return;
    case TAC_BINARY_MULTIPLY_DOUBLE:
        emit3(g, MMIX_FMUL, mmix_reg(d), a, b);
        def_done(g, d, in->u.binary.dst, rounded);
        return;
    case TAC_BINARY_DIVIDE_DOUBLE:
        emit3(g, MMIX_FDIV, mmix_reg(d), a, b);
        def_done(g, d, in->u.binary.dst, rounded);
        return;
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
        internal_error("mmix: %s: binary operator %d on floating point", gen_name(g), op);
    }
    def_done(g, REG_C, in->u.binary.dst, true);
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    int s = use_val(g, in->u.unary.src, REG_A, NULL), d = def_reg(g, in->u.unary.dst, REG_A);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_DOUBLE:
        // The sign bit flipped: exact for -0 and a NaN.
        emit2(g, MMIX_SETH, mmix_reg(MMIX_TMP), mmix_wyde(0x8000));
        emit3(g, MMIX_XOR, mmix_reg(d), mmix_reg(s), mmix_reg(MMIX_TMP));
        break;
    case TAC_UNARY_SQRT_DOUBLE:
        emit3(g, MMIX_FSQRT, mmix_reg(d), mmix_imm(0), mmix_reg(s)); // rA's mode
        break;
    default:
        internal_error("mmix: %s: unary operator %d on floating point", gen_name(g),
                       in->u.unary.op);
    }
    def_done(g, d, in->u.unary.dst, true);
}

// Any bit but the sign: a NaN is true, -0 false.
int gen_fp_test(Gen *g, const Tac_Val *v, int scratch)
{
    int r = use_val(g, v, scratch, NULL);
    emit3(g, MMIX_SLU, mmix_reg(scratch), mmix_reg(r), mmix_imm(1));
    return scratch;
}

void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    int s = use_val(g, src, REG_A, NULL), d = def_reg(g, dst, REG_A);
    Mmix_Operand a = mmix_reg(s), r = mmix_reg(d);
    bool canonical = true;
    switch (kind) {
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
        emit2(g, MMIX_FLOT, r, a);
        break;
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
        emit2(g, MMIX_FLOTU, r, a);
        break;
    // sflot rounds the integer to binary32 once; flot and then stsf would round twice.
    case TAC_INSTRUCTION_INT_TO_FLOAT:
        emit2(g, MMIX_SFLOT, r, a);
        break;
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
        emit2(g, MMIX_SFLOTU, r, a);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
        emit3(g, MMIX_FIX, r, mmix_imm(ROUND_OFF), a);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
        emit3(g, MMIX_FIXU, r, mmix_imm(ROUND_OFF), a);
        break;
    default:
        // Between float, double and long double: a float widens exactly, a double is
        // rounded to float on its way, and long double is double.
        move_reg(g, d, s);
        canonical = !mmix_is_float(val_type(g, dst)) || mmix_is_float(val_type(g, src));
        break;
    }
    def_done(g, d, dst, canonical);
}
