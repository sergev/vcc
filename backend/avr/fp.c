//
// Floating point, in software: float, double and long double are all IEEE binary32,
// and every operation but negation and the truth test is a call of the libgcc-named
// runtime (libc/common/float32.c), with the ordinary ABI: operands in r25:r22 and
// r21:r18, the result in r25:r22, a comparison's in r24.
//
#include "internal.h"

// The helper of FP binary operator `op`, or NULL for a comparison.
static const char *arith_helper(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_ADD_DOUBLE:
        return "__addsf3";
    case TAC_BINARY_SUBTRACT_DOUBLE:
        return "__subsf3";
    case TAC_BINARY_MULTIPLY_DOUBLE:
        return "__mulsf3";
    case TAC_BINARY_DIVIDE_DOUBLE:
        return "__divsf3";
    default:
        return NULL;
    }
}

// A comparison: the helper, and the branch taken on its result against zero, as
// cp r24, r1 (or swapped, cp r1, r24, for > and <=).
static bool compare_helper(Tac_BinaryOperator op, const char **name, AVR_Op *br, bool *swap)
{
    *swap = false;
    switch (op) {
    case TAC_BINARY_EQUAL:
        *name = "__eqsf2", *br = AVR_BREQ;
        return true;
    case TAC_BINARY_NOT_EQUAL:
        *name = "__nesf2", *br = AVR_BRNE;
        return true;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        *name = "__ltsf2", *br = AVR_BRLT;
        return true;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        *name = "__lesf2", *br = AVR_BRGE, *swap = true; // 0 >= r
        return true;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        *name = "__gtsf2", *br = AVR_BRLT, *swap = true; // 0 < r
        return true;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        *name = "__gesf2", *br = AVR_BRGE;
        return true;
    default:
        return false;
    }
}

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    load_two(g, in->u.binary.src1, 22, 4, in->u.binary.src2, 18, 4);
    const char *name = arith_helper(op);
    if (name) {
        emit1(g, AVR_CALL, avr_label(name));
        store_val(g, in->u.binary.dst, 22, 4);
        return;
    }
    AVR_Op br;
    bool swap;
    if (!compare_helper(op, &name, &br, &swap))
        fatal_error("avr: %s: FP operator %d is not implemented", gen_name(g), op);
    emit1(g, AVR_CALL, avr_label(name));
    if (swap)
        emit2(g, AVR_CP, avr_reg(AVR_ZERO), avr_reg(24));
    else
        emit2(g, AVR_CP, avr_reg(24), avr_reg(AVR_ZERO));
    gen_set_on(g, br);
    store_val(g, in->u.binary.dst, 24, 1);
}

AVR_Op gen_fp_compare(Gen *g, const Tac_Instruction *in)
{
    const char *name;
    AVR_Op br;
    bool swap;
    if (!compare_helper(in->u.binary.op, &name, &br, &swap))
        return AVR_NUM_OPS;
    load_two(g, in->u.binary.src1, 22, 4, in->u.binary.src2, 18, 4);
    emit1(g, AVR_CALL, avr_label(name));
    if (swap)
        emit2(g, AVR_CP, avr_reg(AVR_ZERO), avr_reg(24));
    else
        emit2(g, AVR_CP, avr_reg(24), avr_reg(AVR_ZERO));
    return br;
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_DOUBLE:
        load_val(g, in->u.unary.src, 22, 4, EXT_TYPE);
        emit2(g, AVR_SUBI, avr_reg(25), avr_imm(0x80));
        store_val(g, in->u.unary.dst, 22, 4);
        break;
    case TAC_UNARY_NOT:
        gen_fp_test(g, in->u.unary.src);
        gen_set_on(g, AVR_BREQ);
        store_val(g, in->u.unary.dst, 24, 1);
        break;
    default:
        fatal_error("avr: %s: FP unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
}

void gen_fp_test(Gen *g, const Tac_Val *v)
{
    load_val(g, v, 22, 4, EXT_TYPE);
    emit2(g, AVR_ANDI, avr_reg(25), avr_imm(0x7f));
    for (int i = 0; i < 4; i++)
        emit2(g, i == 0 ? AVR_CP : AVR_CPC, avr_reg(22 + i), avr_reg(AVR_ZERO));
}

void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    int ssize = avr_type_size(val_type(g, src)), dsize = avr_type_size(val_type(g, dst));
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
        if (ssize == 8) {
            load_val(g, src, 18, 8, EXT_TYPE);
            emit1(g, AVR_CALL, avr_label(is_unsigned ? "__floatundisf" : "__floatdisf"));
        } else {
            // Widened to 32 bits by the source's own signedness.
            load_val(g, src, 22, 4, is_unsigned ? EXT_ZERO : EXT_SIGN);
            emit1(g, AVR_CALL, avr_label(is_unsigned ? "__floatunsisf" : "__floatsisf"));
        }
        store_val(g, dst, 22, 4);
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
        load_val(g, src, 22, 4, EXT_TYPE);
        if (dsize == 8) {
            emit1(g, AVR_CALL, avr_label(is_unsigned ? "__fixunssfdi" : "__fixsfdi"));
            store_val(g, dst, 18, 8);
        } else {
            // A 32-bit unsigned through __fixunssfsi; narrower ones fit __fixsfsi.
            emit1(g, AVR_CALL,
                  avr_label(is_unsigned && dsize == 4 ? "__fixunssfsi" : "__fixsfsi"));
            store_val(g, dst, 22, dsize);
        }
        break;
    }
    default: // between float, double and long double: all binary32
        load_val(g, src, 22, 4, EXT_TYPE);
        store_val(g, dst, 22, 4);
        break;
    }
}
