//
// 64-bit integers on rv32: a long long lives in an 8-byte slot, the low word first,
// and is operated on in register pairs by inline sequences of t0-t4.  Division,
// remainder and the conversions with float and double call the runtime, under the
// libgcc names (libc/riscv32/int64.c).
//
#include "codegen.h"
#include "internal.h"

static void load_pair(Gen *g, int lo, int hi, const Tac_Val *v)
{
    pair_half(g, lo, v, 0);
    pair_half(g, hi, v, 1);
}

static void store_pair(Gen *g, int lo, int hi, const Tac_Val *dst)
{
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, RV_T5, &base, &off);
    emit2(g, RV_SW, rv_reg(lo), mem(g, base, off));
    emit2(g, RV_SW, rv_reg(hi), mem(g, base, off + 4));
}

static void op3(Gen *g, Rv_Op op, int d, int a, int b)
{
    emit3(g, op, rv_reg(d), rv_reg(a), rv_reg(b));
}

static void opi(Gen *g, Rv_Op op, int d, int a, int64_t imm)
{
    emit3(g, op, rv_reg(d), rv_reg(a), rv_imm(imm));
}

static void op2(Gen *g, Rv_Op op, int d, int a)
{
    emit2(g, op, rv_reg(d), rv_reg(a));
}

enum { T0 = RV_T0, T1, T2, T3 = RV_T3, T4 };

// t0 = a < b, the pairs in t0/t1 and t2/t3.
static void less_than(Gen *g, bool is_unsigned)
{
    op3(g, is_unsigned ? RV_SLTU : RV_SLT, T4, T1, T3); // high words
    op3(g, RV_XOR, T1, T1, T3);
    op2(g, RV_SEQZ, T1, T1);
    op3(g, RV_SLTU, T0, T0, T2); // low words, when the high ones are equal
    op3(g, RV_AND, T0, T0, T1);
    op3(g, RV_OR, T0, T0, T4);
}

// A shift by constant `n` (0..63) of the pair in t0/t1.
static void shift_const(Gen *g, Tac_BinaryOperator op, bool arith, int n)
{
    if (n == 0)
        return;
    if (op == TAC_BINARY_LEFT_SHIFT) {
        if (n >= 32) {
            opi(g, RV_SLLI, T1, T0, n - 32);
            gen_li(g, T0, 0);
            return;
        }
        opi(g, RV_SLLI, T1, T1, n);
        opi(g, RV_SRLI, T4, T0, 32 - n);
        op3(g, RV_OR, T1, T1, T4);
        opi(g, RV_SLLI, T0, T0, n);
        return;
    }
    Rv_Op right = arith ? RV_SRAI : RV_SRLI;
    if (n >= 32) {
        opi(g, right, T0, T1, n - 32);
        if (arith)
            opi(g, RV_SRAI, T1, T1, 31);
        else
            gen_li(g, T1, 0);
        return;
    }
    opi(g, RV_SRLI, T0, T0, n);
    opi(g, RV_SLLI, T4, T1, 32 - n);
    op3(g, RV_OR, T0, T0, T4);
    opi(g, right, T1, T1, n);
}

// A shift of the pair in t0/t1 by the count in t2, without branches: with m all ones
// when the count is 32 or more, the result is m ? (one word shifted) : (both words
// shifted, the bits crossing over).  A shift by count & 31 is what sll/srl/sra do.
static void shift_var(Gen *g, Tac_BinaryOperator op, bool arith)
{
    opi(g, RV_SLLI, T4, T2, 26);
    opi(g, RV_SRAI, T4, T4, 31); // m
    if (op == TAC_BINARY_LEFT_SHIFT) {
        op3(g, RV_SLL, T3, T0, T2); // a = lo << n
        op3(g, RV_SLL, T1, T1, T2);
        op2(g, RV_NOT, T2, T2);     // 31 - n
        opi(g, RV_SRLI, T0, T0, 1);
        op3(g, RV_SRL, T0, T0, T2);
        op3(g, RV_OR, T1, T1, T0); // b = hi << n | lo >> (32 - n)
        op3(g, RV_XOR, T0, T3, T1);
        op3(g, RV_AND, T0, T0, T4);
        op3(g, RV_XOR, T1, T1, T0); // hi = m ? a : b
        op3(g, RV_AND, T0, T3, T4);
        op3(g, RV_XOR, T0, T3, T0); // lo = m ? 0 : a
        return;
    }
    op3(g, arith ? RV_SRA : RV_SRL, T3, T1, T2); // a = hi >> n
    op3(g, RV_SRL, T0, T0, T2);
    op2(g, RV_NOT, T2, T2);
    opi(g, RV_SLLI, T1, T1, 1);
    op3(g, RV_SLL, T1, T1, T2);
    op3(g, RV_OR, T0, T0, T1); // b = lo >> n | hi << (32 - n)
    op3(g, RV_XOR, T1, T3, T0);
    op3(g, RV_AND, T1, T1, T4);
    op3(g, RV_XOR, T0, T0, T1); // lo = m ? a : b
    if (arith)
        opi(g, RV_SRAI, T2, T3, 31); // the sign
    else
        gen_li(g, T2, 0);
    op3(g, RV_XOR, T1, T3, T2);
    op3(g, RV_AND, T1, T1, T4);
    op3(g, RV_XOR, T1, T3, T1); // hi = m ? sign : a
}

static void gen_ll_shift(Gen *g, const Tac_Instruction *in, bool is_unsigned)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *count  = in->u.binary.src2;
    bool arith = op == TAC_BINARY_RIGHT_SHIFT && !is_unsigned;
    if (count->kind == TAC_VAL_CONSTANT) {
        int n = (int)(const_value(g, count->u.constant) & 63);
        load_pair(g, T0, T1, in->u.binary.src1);
        shift_const(g, op, arith, n);
    } else {
        if (rv_is_ll(val_type(g, count)))
            pair_half(g, T2, count, 0);
        else
            load_val(g, T2, count);
        load_pair(g, T0, T1, in->u.binary.src1);
        shift_var(g, op, arith);
    }
    store_pair(g, T0, T1, in->u.binary.dst);
}

bool ll_runtime_call(const Tac_Instruction *in, const Tac_Type *t, const Tac_Type *dt)
{
    if (in->kind != TAC_INSTRUCTION_BINARY)
        return (t && rv_is_ll(t)) || (dt && rv_is_ll(dt)); // a conversion with FP
    switch (in->u.binary.op) {
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        return rv_is_ll(t);
    default:
        return false;
    }
}

void gen_ll_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *dst = in->u.binary.dst;
    bool u = rv_unsigned_op(op); // not the operands' types: see gen_binary
    switch (op) {
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED: {
        bool div = op == TAC_BINARY_DIVIDE || op == TAC_BINARY_DIVIDE_UNSIGNED;
        pair_arg(g, RV_A0, a);
        pair_arg(g, RV_A0 + 2, b);
        call_runtime(g, div ? (u ? "__udivdi3" : "__divdi3") : (u ? "__umoddi3" : "__moddi3"));
        pair_result(g, dst);
        return;
    }
    case TAC_BINARY_LEFT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        gen_ll_shift(g, in, u);
        return;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        load_pair(g, T0, T1, b); // b < a
        load_pair(g, T2, T3, a);
        break;
    default:
        load_pair(g, T0, T1, a);
        load_pair(g, T2, T3, b);
        break;
    }
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        op3(g, RV_ADD, T2, T0, T2);
        op3(g, RV_SLTU, T0, T2, T0); // the carry
        op3(g, RV_ADD, T1, T1, T3);
        op3(g, RV_ADD, T1, T1, T0);
        store_pair(g, T2, T1, dst);
        return;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        op3(g, RV_SLTU, T4, T0, T2); // the borrow
        op3(g, RV_SUB, T0, T0, T2);
        op3(g, RV_SUB, T1, T1, T3);
        op3(g, RV_SUB, T1, T1, T4);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        op3(g, RV_MULHU, T4, T0, T2);
        op3(g, RV_MUL, T3, T0, T3);
        op3(g, RV_ADD, T4, T4, T3);
        op3(g, RV_MUL, T1, T1, T2);
        op3(g, RV_ADD, T1, T4, T1);
        op3(g, RV_MUL, T0, T0, T2);
        break;
    case TAC_BINARY_BITWISE_AND:
    case TAC_BINARY_BITWISE_OR:
    case TAC_BINARY_BITWISE_XOR: {
        Rv_Op o = op == TAC_BINARY_BITWISE_AND ? RV_AND : op == TAC_BINARY_BITWISE_OR ? RV_OR : RV_XOR;
        op3(g, o, T0, T0, T2);
        op3(g, o, T1, T1, T3);
        break;
    }
    case TAC_BINARY_EQUAL:
    case TAC_BINARY_NOT_EQUAL:
        op3(g, RV_XOR, T0, T0, T2);
        op3(g, RV_XOR, T1, T1, T3);
        op3(g, RV_OR, T0, T0, T1);
        op2(g, op == TAC_BINARY_EQUAL ? RV_SEQZ : RV_SNEZ, T0, T0);
        store_val(g, T0, dst);
        return;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        less_than(g, u);
        store_val(g, T0, dst);
        return;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        less_than(g, u);
        opi(g, RV_XORI, T0, T0, 1);
        store_val(g, T0, dst);
        return;
    default:
        fatal_error("riscv: %s: bad long long operator %d", gen_name(g), op);
    }
    store_pair(g, T0, T1, dst);
}

void gen_ll_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *dst = in->u.unary.dst;
    load_pair(g, T0, T1, in->u.unary.src);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        op2(g, RV_SNEZ, T2, T0); // the borrow
        op2(g, RV_NEG, T0, T0);
        op2(g, RV_NEG, T1, T1);
        op3(g, RV_SUB, T1, T1, T2);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        op2(g, RV_NOT, T0, T0);
        op2(g, RV_NOT, T1, T1);
        break;
    case TAC_UNARY_NOT:
        op3(g, RV_OR, T0, T0, T1);
        op2(g, RV_SEQZ, T0, T0);
        store_val(g, T0, dst);
        return;
    default:
        fatal_error("riscv: %s: bad long long unary operator", gen_name(g));
    }
    store_pair(g, T0, T1, dst);
}

void gen_ll_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (rv_is_ll(st) && rv_is_ll(dt)) {
        int base;
        int64_t off;
        name_addr(g, dst->u.var_name, RV_T4, &base, &off);
        copy_pair(g, src, base, off);
        return;
    }
    if (rv_is_ll(dt)) {
        // Extend: a register holds a narrower value extended by its own type, which
        // may not be the conversion's (see gen_int_convert).
        int s    = use_val(g, T0, src);
        int size = rv_size(st);
        if (kind == TAC_INSTRUCTION_SIGN_EXTEND && size < 4 && rv_is_unsigned(st)) {
            opi(g, RV_SLLI, T0, s, 32 - 8 * size);
            opi(g, RV_SRAI, T0, T0, 32 - 8 * size);
            s = T0;
            opi(g, RV_SRAI, T1, s, 31);
        } else if (kind == TAC_INSTRUCTION_ZERO_EXTEND) {
            if (size == 1) {
                opi(g, RV_ANDI, T0, s, 255);
                s = T0;
            } else if (size == 2) {
                opi(g, RV_SLLI, T0, s, 16);
                opi(g, RV_SRLI, T0, T0, 16);
                s = T0;
            }
            gen_li(g, T1, 0);
        } else {
            opi(g, RV_SRAI, T1, s, 31);
        }
        store_pair(g, s, T1, dst);
        return;
    }
    // Truncate: the low word, brought to the destination's form in a register.
    pair_half(g, T0, src, 0);
    int s = T0;
    if (var_reg(g, dst) && rv_size(dt) < 4) {
        s = def_reg(g, T0, dst);
        gen_canon(g, s, T0, dt);
    }
    store_val(g, s, dst);
}

void gen_ll_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (rv_is_ll(st)) {
        bool u = rv_from_unsigned(kind);
        pair_arg(g, RV_A0, src);
        if (rv_is_double(dt))
            call_runtime(g, u ? "__floatundidf" : "__floatdidf");
        else
            call_runtime(g, u ? "__floatundisf" : "__floatdisf");
        store_val(g, RV_FA0, dst);
        return;
    }
    bool u = rv_is_unsigned(dt);
    load_val(g, RV_FA0, src);
    if (rv_is_double(st))
        call_runtime(g, u ? "__fixunsdfdi" : "__fixdfdi");
    else
        call_runtime(g, u ? "__fixunssfdi" : "__fixsfdi");
    pair_result(g, dst);
}
