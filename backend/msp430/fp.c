//
// Floating point, in software: float is IEEE binary32, double and long double binary64,
// and every operation but negation and the truth test is a call of the runtime under
// its libgcc name (libc/common/float32.c and float64.c), with the ordinary ABI: a float
// in r13:r12 (a second one in r15:r14), a double in r15:r12 (a second one on the stack
// at 0(r1)), the result in r12 up, a comparison's an int in r12.
//
#include "internal.h"

// The helper of FP binary operator `op` on `size`-byte operands, or NULL.
static const char *arith_helper(Tac_BinaryOperator op, int size)
{
    bool d = size == 8;
    switch (op) {
    case TAC_BINARY_ADD_DOUBLE:
        return d ? "__adddf3" : "__addsf3";
    case TAC_BINARY_SUBTRACT_DOUBLE:
        return d ? "__subdf3" : "__subsf3";
    case TAC_BINARY_MULTIPLY_DOUBLE:
        return d ? "__muldf3" : "__mulsf3";
    case TAC_BINARY_DIVIDE_DOUBLE:
        return d ? "__divdf3" : "__divsf3";
    default:
        return NULL;
    }
}

// A comparison: the helper, the compare of its result r with zero that sets the flags,
// as `cmp k, r12` (k is 0, or 1 for <= and >), and the jump taken when the comparison
// holds.  Each helper's result for an unordered pair makes its own comparison false.
static bool compare_helper(Tac_BinaryOperator op, int size, const char **name, int *k,
                           Msp_Op *cond)
{
    bool d = size == 8;
    *k     = 0;
    switch (op) {
    case TAC_BINARY_EQUAL:
        *name = d ? "__eqdf2" : "__eqsf2", *cond = MSP_JEQ;
        return true;
    case TAC_BINARY_NOT_EQUAL:
        *name = d ? "__nedf2" : "__nesf2", *cond = MSP_JNE;
        return true;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        *name = d ? "__ltdf2" : "__ltsf2", *cond = MSP_JL;
        return true;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        *name = d ? "__ledf2" : "__lesf2", *k = 1, *cond = MSP_JL; // r < 1
        return true;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        *name = d ? "__gtdf2" : "__gtsf2", *k = 1, *cond = MSP_JGE; // r >= 1
        return true;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        *name = d ? "__gedf2" : "__gesf2", *cond = MSP_JGE;
        return true;
    default:
        return false;
    }
}

// Two FP operands into place for a helper: r13:r12 and r15:r14, or r15:r12 and 0(r1).
static void load_operands(Gen *g, const Tac_Val *a, const Tac_Val *b, int size)
{
    if (size == 8) {
        for (int i = 0; i < 4; i++)
            emit2(g, MSP_MOV, val_word(g, b, i), msp_indexed(MSP_SP, NULL, 2 * i));
        load_val(g, a, 12, 4, EXT_TYPE);
    } else {
        load_val(g, a, 12, 2, EXT_TYPE);
        load_val(g, b, 14, 2, EXT_TYPE);
    }
}

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    int size              = msp_type_size(val_type(g, in->u.binary.src1));
    load_operands(g, in->u.binary.src1, in->u.binary.src2, size);
    const char *name = arith_helper(op, size);
    if (name) {
        call_helper(g, name);
        store_val(g, in->u.binary.dst, 12, size / 2);
        return;
    }
    int k;
    Msp_Op cond;
    if (!compare_helper(op, size, &name, &k, &cond))
        fatal_error("msp430: %s: FP operator %d is not implemented", gen_name(g), op);
    call_helper(g, name);
    emit2(g, MSP_CMP, msp_imm(k), msp_reg(12));
    gen_set_on(g, cond);
    store_val(g, in->u.binary.dst, 11, 1);
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    int n              = msp_words(val_type(g, src));
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_DOUBLE:
        load_val(g, src, 12, n, EXT_TYPE);
        emit2(g, MSP_XOR, msp_imm(0x8000), msp_reg(12 + n - 1));
        store_val(g, dst, 12, n);
        break;
    case TAC_UNARY_NOT:
        gen_fp_test(g, src);
        gen_set_on(g, MSP_JEQ);
        store_val(g, dst, 11, 1);
        break;
    default:
        fatal_error("msp430: %s: FP unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
}

void gen_fp_test(Gen *g, const Tac_Val *v)
{
    int n = msp_words(val_type(g, v));
    load_val(g, v, 12, n, EXT_TYPE);
    emit2(g, MSP_BIC, msp_imm(0x8000), msp_reg(12 + n - 1));
    for (int i = 1; i < n; i++)
        emit2(g, MSP_BIS, msp_reg(12 + i), msp_reg(12));
    emit1(g, MSP_TST, msp_reg(12));
}

void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    int ssize = msp_type_size(val_type(g, src)), dsize = msp_type_size(val_type(g, dst));
    int dwords = (dsize + 1) / 2;
    switch (kind) {
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE: {
        // Signedness from the kind: copy propagation may leave a source of the other.
        bool is_unsigned = kind == TAC_INSTRUCTION_UINT_TO_DOUBLE ||
                           kind == TAC_INSTRUCTION_UINT_TO_FLOAT ||
                           kind == TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE;
        bool d = dsize == 8;
        if (ssize == 8) {
            load_val(g, src, 12, 4, EXT_TYPE);
            call_helper(g, d ? (is_unsigned ? "__floatundidf" : "__floatdidf")
                             : (is_unsigned ? "__floatundisf" : "__floatdisf"));
        } else {
            // Widened to 32 bits by the source's own signedness.
            load_val(g, src, 12, 2, is_unsigned ? EXT_ZERO : EXT_SIGN);
            call_helper(g, d ? (is_unsigned ? "__floatunsidf" : "__floatsidf")
                             : (is_unsigned ? "__floatunsisf" : "__floatsisf"));
        }
        store_val(g, dst, 12, dwords);
        break;
    }
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT: {
        bool is_unsigned = kind == TAC_INSTRUCTION_DOUBLE_TO_UINT ||
                           kind == TAC_INSTRUCTION_FLOAT_TO_UINT ||
                           kind == TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT;
        bool d = ssize == 8;
        load_val(g, src, 12, ssize / 2, EXT_TYPE);
        if (dsize == 8)
            call_helper(g, d ? (is_unsigned ? "__fixunsdfdi" : "__fixdfdi")
                             : (is_unsigned ? "__fixunssfdi" : "__fixsfdi"));
        else // a 32-bit unsigned needs its own; narrower ones fit the signed long
            call_helper(g, d ? (is_unsigned && dsize == 4 ? "__fixunsdfsi" : "__fixdfsi")
                             : (is_unsigned && dsize == 4 ? "__fixunssfsi" : "__fixsfsi"));
        store_val(g, dst, 12, dwords);
        break;
    }
    default: // between float and double (long double is double)
        load_val(g, src, 12, ssize / 2, EXT_TYPE);
        if (ssize == 4 && dsize == 8)
            call_helper(g, "__extendsfdf2");
        else if (ssize == 8 && dsize == 4)
            call_helper(g, "__truncdfsf2");
        store_val(g, dst, 12, dwords);
        break;
    }
}
