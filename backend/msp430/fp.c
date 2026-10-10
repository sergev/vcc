//
// Floating point, in software: float is IEEE binary32, double and long double binary64,
// and every operation but negation and the truth test is a call of the runtime, by the
// names and conventions GCC's code uses, which libgcc provides as well as our runtime
// (libc/common/float32.c and float64.c, libc/msp430/mspabi*.[cs]):
//   - binary32 arithmetic, __mspabi_addf and the like: r13:r12 and r15:r14;
//   - binary64 arithmetic, __mspabi_addd and the like: the first operand in r11:r8 (so
//     the prologue saves r8-r10), the second in r15:r12;
//   - comparisons, the libgcc predicates __ltdf2 and the like: a float in r13:r12 and
//     r15:r14, a double in r15:r12 and on the stack at 0(r1), the result 0/1 set from
//     the flags straight into its variable;
//   - conversions, __mspabi_fixdli, __mspabi_fltlid and the like (__fixunssfsi and
//     __fixunssfdi from float to unsigned), the operand in r12 up;
// the result in r12 up, a comparison's an int in r12.  Negation flips the sign bit and
// the truth test looks at the bits, in place.
//
#include "internal.h"

// The helper of FP binary operator `op` on `size`-byte operands, or NULL.
static const char *arith_helper(Tac_BinaryOperator op, int size)
{
    bool d = size == 8;
    switch (op) {
    case TAC_BINARY_ADD_DOUBLE:
        return d ? "__mspabi_addd" : "__mspabi_addf";
    case TAC_BINARY_SUBTRACT_DOUBLE:
        return d ? "__mspabi_subd" : "__mspabi_subf";
    case TAC_BINARY_MULTIPLY_DOUBLE:
        return d ? "__mspabi_mpyd" : "__mspabi_mpyf";
    case TAC_BINARY_DIVIDE_DOUBLE:
        return d ? "__mspabi_divd" : "__mspabi_divf";
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

// Two FP operands into place for a helper, at once: r13:r12 and r15:r14; for binary64,
// r11:r8 and r15:r12 (`r8`), or r15:r12 and 0(r1), the stack first.
static void load_operands(Gen *g, const Tac_Val *a, const Tac_Val *b, int size, bool r8)
{
    if (size == 8 && r8) {
        Load l[2] = { { a, 8, 4, EXT_TYPE }, { b, 12, 4, EXT_TYPE } };
        load_vals(g, l, 2);
    } else if (size == 8) {
        for (int i = 0; i < 4; i++)
            emit2(g, MSP_MOV, val_word(g, b, i), msp_indexed(MSP_SP, NULL, 2 * i));
        load_val(g, a, 12, 4, EXT_TYPE);
    } else {
        Load l[2] = { { a, 12, 2, EXT_TYPE }, { b, 14, 2, EXT_TYPE } };
        load_vals(g, l, 2);
    }
}

bool fp_arith(Tac_BinaryOperator op, int size)
{
    return arith_helper(op, size) != NULL;
}

int fp_out_size(Tac_BinaryOperator op, int size)
{
    return size == 8 && !arith_helper(op, size) ? 8 : 0;
}

Msp_Op gen_fp_compare(Gen *g, const Tac_Instruction *in)
{
    int size = msp_type_size(val_type(g, in->u.binary.src1));
    const char *name;
    int k;
    Msp_Op cond;
    if (!compare_helper(in->u.binary.op, size, &name, &k, &cond))
        return MSP_NUM_OPS;
    load_operands(g, in->u.binary.src1, in->u.binary.src2, size, false);
    call_helper(g, name);
    emit2(g, MSP_CMP, msp_imm(k), msp_reg(12));
    return cond;
}

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    int size              = msp_type_size(val_type(g, in->u.binary.src1));
    const char *name      = arith_helper(op, size);
    if (name) {
        load_operands(g, in->u.binary.src1, in->u.binary.src2, size, true);
        call_helper(g, name);
        store_val(g, in->u.binary.dst, 12, size / 2);
        return;
    }
    Msp_Op cond = gen_fp_compare(g, in);
    if (cond == MSP_NUM_OPS)
        internal_error("msp430: %s: FP operator %d is not implemented", gen_name(g), op);
    gen_set_on(g, cond, in->u.binary.dst);
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    int n              = msp_words(val_type(g, src));
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_DOUBLE: {
        Move m[4];
        for (int i = 0; i < n; i++)
            m[i] = (Move){ val_word(g, dst, i), val_word(g, src, i), false };
        parallel_moves(g, m, n);
        emit2(g, MSP_XOR, msp_imm(0x8000), val_word(g, dst, n - 1));
        break;
    }
    case TAC_UNARY_NOT:
        gen_fp_test(g, src);
        gen_set_on(g, MSP_JEQ, dst);
        break;
    default:
        internal_error("msp430: %s: FP unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
}

void gen_fp_test(Gen *g, const Tac_Val *v)
{
    int n = msp_words(val_type(g, v));
    if (v->kind == TAC_VAL_CONSTANT) {
        // Zero or not, known now: the flags of 0 or 1.
        uint64_t bits = const_bits(v->u.constant) & ~(1ull << (16 * n - 1));
        emit2(g, MSP_MOV, msp_imm(bits != 0), msp_reg(MSP_SCRATCH));
        emit1(g, MSP_TST, msp_reg(MSP_SCRATCH));
        return;
    }
    // The low words, then the top one without its sign.
    char nonzero[32];
    new_label(nonzero);
    for (int i = 0; i < n - 1; i++) {
        emit1(g, MSP_TST, val_word(g, v, i));
        emit1(g, MSP_JNE, msp_label(nonzero));
    }
    emit2(g, MSP_BIT, msp_imm(0x7fff), val_word(g, v, n - 1));
    gen_label_block(g, nonzero);
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
            call_helper(g, d ? (is_unsigned ? "__mspabi_fltulld" : "__mspabi_fltllid")
                             : (is_unsigned ? "__mspabi_fltullf" : "__mspabi_fltllif"));
        } else {
            // Widened to 32 bits by the source's own signedness.
            load_val(g, src, 12, 2, is_unsigned ? EXT_ZERO : EXT_SIGN);
            call_helper(g, d ? (is_unsigned ? "__mspabi_fltuld" : "__mspabi_fltlid")
                             : (is_unsigned ? "__mspabi_fltulf" : "__mspabi_fltlif"));
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
            call_helper(g, d ? (is_unsigned ? "__mspabi_fixdull" : "__mspabi_fixdlli")
                             : (is_unsigned ? "__fixunssfdi" : "__mspabi_fixflli"));
        else // to 32 bits, as GCC's code converts a narrower integer too
            call_helper(g, d ? (is_unsigned ? "__mspabi_fixdul" : "__mspabi_fixdli")
                             : (is_unsigned ? "__fixunssfsi" : "__mspabi_fixfli"));
        store_val(g, dst, 12, dwords);
        break;
    }
    default: // between float and double (long double is double)
        load_val(g, src, 12, ssize / 2, EXT_TYPE);
        if (ssize == 4 && dsize == 8)
            call_helper(g, "__mspabi_cvtfd");
        else if (ssize == 8 && dsize == 4)
            call_helper(g, "__mspabi_cvtdf");
        store_val(g, dst, 12, dwords);
        break;
    }
}
