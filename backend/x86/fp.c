//
// Floating point in SSE: float and double in the low lane of xmm14/xmm15, operands
// from memory or .rodata, conversions with cvt*, comparisons with ucomis*.
//
// ucomis* sets ZF, PF and CF all to 1 for unordered operands.  So `a` (CF = 0 and
// ZF = 0) and `ae` (CF = 0) are false for a NaN without a parity test, and `<` and
// `<=` swap the operands to use them; `==` needs ZF = 1 and PF = 0, `!=` either ZF = 0
// or PF = 1.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"

// The bits of FP constant `c` as a value of type `t`.
static uint64_t fp_bits(const Tac_Const *c, const Tac_Type *t)
{
    double d;
    switch (c->kind) {
    case TAC_CONST_FLOAT:
        d = c->u.float_val;
        break;
    case TAC_CONST_DOUBLE:
        d = c->u.double_val;
        break;
    default:
        internal_error("x86: integer constant %d as floating point", c->kind);
    }
    if (x86_is_double(t)) {
        uint64_t bits;
        memcpy(&bits, &d, 8);
        return bits;
    }
    float f = (float)d;
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

X86_Operand fp_operand(Gen *g, const Tac_Val *v)
{
    if (var_reg(g, v))
        return x86_xmm(var_reg(g, v));
    if (v->kind != TAC_VAL_CONSTANT)
        return name_mem(g, v->u.var_name, 0);
    const Tac_Type *t = val_type(g, v);
    return const_mem(g, fp_bits(v->u.constant, t), 0, x86_size(t));
}

void gen_fp_copy(Gen *g, const Tac_Val *src, X86_Operand dst, const Tac_Type *t)
{
    X86_Width w = x86_is_double(t) ? X86_Q : X86_L;
    if (src->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = fp_bits(src->u.constant, t);
        if (w == X86_L || x86_imm32((int64_t)bits)) {
            emit2(g, X86_MOV, w, x86_imm(w == X86_L ? (int32_t)bits : (int64_t)bits), dst);
            return;
        }
        gen_li(g, T0, X86_Q, (int64_t)bits);
    } else if (var_reg(g, src)) {
        store_mem(g, var_reg(g, src), t, dst);
        return;
    } else {
        emit2(g, X86_MOV, w, name_mem(g, src->u.var_name, 0), x86_reg(T0, w));
    }
    emit2(g, X86_MOV, w, x86_reg(T0, w), dst);
}

// eax = 1 when the flags of a ucomis say equal (or, with `ne`, not equal).
static void gen_fp_equal(Gen *g, bool ne)
{
    X86_Instr *a = emit1(g, X86_SET, X86_B, x86_reg(T0, X86_B));
    a->cond      = ne ? X86_CC_NE : X86_CC_E;
    X86_Instr *p = emit1(g, X86_SET, X86_B, x86_reg(T2, X86_B));
    p->cond      = ne ? X86_CC_P : X86_CC_NP;
    emit2(g, ne ? X86_OR : X86_AND, X86_B, x86_reg(T2, X86_B), x86_reg(T0, X86_B));
    emit2(g, X86_MOVZB, X86_L, x86_reg(T0, X86_B), x86_reg(T0, X86_L));
}

// Compare FP `v` of type `t` with zero.
static void compare_zero(Gen *g, const Tac_Val *v, const Tac_Type *t)
{
    X86_Op ucomis = x86_is_double(t) ? X86_UCOMISD : X86_UCOMISS;
    int r         = use_val(g, F0, v);
    emit2(g, X86_XORPS, X86_Q, x86_xmm(F1), x86_xmm(F1));
    emit2(g, ucomis, X86_Q, x86_xmm(F1), x86_xmm(r));
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    const Tac_Val *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        compare_zero(g, in->u.unary.src, t);
        gen_fp_equal(g, false);
        store_val(g, T0, dst);
        return;
    }
    if (in->u.unary.op == TAC_UNARY_SQRT_DOUBLE) {
        // The source from a register, memory or .rodata; the result in the destination's
        // register (sqrtsd writes the low lane only, so it needs no copy first).
        int r = def_reg(g, F0, dst);
        emit2(g, X86_SQRTSD, X86_Q, fp_operand(g, in->u.unary.src), x86_xmm(r));
        store_val(g, r, dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        internal_error("x86: %s: bad floating-point unary operator", gen_name(g));
    // The sign bit flipped by xorps with a mask, 16 bytes in memory as xorps reads them.
    uint64_t sign = x86_is_double(t) ? (uint64_t)1 << 63 : (uint64_t)1 << 31;
    int r         = def_reg(g, F0, dst);
    load_val(g, r, in->u.unary.src);
    emit2(g, X86_XORPS, X86_Q, const_mem(g, sign, 0, 16), x86_xmm(r));
    store_val(g, r, dst);
}

// The condition of an FP comparison and whether its operands are swapped, or -1.
static int fp_compare_cond(Tac_BinaryOperator op, bool *swap)
{
    *swap = false;
    switch (op) {
    case TAC_BINARY_EQUAL:
        return X86_CC_E;
    case TAC_BINARY_NOT_EQUAL:
        return X86_CC_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        *swap = true;
        return X86_CC_A;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        *swap = true;
        return X86_CC_AE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        return X86_CC_A;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        return X86_CC_AE;
    default:
        return -1;
    }
}

void gen_fp_binary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    bool d             = x86_is_double(t);
    const Tac_Val *a   = in->u.binary.src1, *b = in->u.binary.src2;
    const Tac_Val *dst = in->u.binary.dst;
    bool swap;
    int cond = fp_compare_cond(in->u.binary.op, &swap);
    if (cond >= 0) {
        if (swap) {
            const Tac_Val *x = a;
            a                = b;
            b                = x;
        }
        int ra = use_val(g, F0, a);
        emit2(g, d ? X86_UCOMISD : X86_UCOMISS, X86_Q, fp_operand(g, b), x86_xmm(ra));
        if (cond == X86_CC_E || cond == X86_CC_NE) {
            gen_fp_equal(g, cond == X86_CC_NE);
        } else {
            X86_Instr *set = emit1(g, X86_SET, X86_B, x86_reg(T0, X86_B));
            set->cond      = cond;
            emit2(g, X86_MOVZB, X86_L, x86_reg(T0, X86_B), x86_reg(T0, X86_L));
        }
        store_val(g, T0, dst);
        return;
    }
    X86_Op op;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        op = d ? X86_ADDSD : X86_ADDSS;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        op = d ? X86_SUBSD : X86_SUBSS;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        op = d ? X86_MULSD : X86_MULSS;
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        op = d ? X86_DIVSD : X86_DIVSS;
        break;
    default:
        internal_error("x86: %s: bad floating-point operator %d", gen_name(g), in->u.binary.op);
    }
    // Two-operand form, as for integers: in the destination's register unless that is
    // b's, when an add or multiply swaps the operands and the others go through xmm14.
    int bs = var_reg(g, b), rd = var_reg(g, dst);
    if (bs && bs == rd && bs != var_reg(g, a) && (op == X86_ADDSD || op == X86_ADDSS ||
                                                  op == X86_MULSD || op == X86_MULSS)) {
        const Tac_Val *x = a;
        a                = b;
        b                = x;
        bs               = var_reg(g, b);
    }
    if (!rd || rd == bs)
        rd = F0;
    load_val(g, rd, a);
    emit2(g, op, X86_Q, fp_operand(g, b), x86_xmm(rd));
    store_val(g, rd, dst);
}

static X86_Instr *jcc(Gen *g, int cond, const char *label)
{
    X86_Instr *j = emit1(g, X86_J, X86_Q, x86_label(label));
    j->cond      = cond;
    return j;
}

static void jmp(Gen *g, const char *label)
{
    emit1(g, X86_JMP, X86_Q, x86_label(label));
}

bool gen_fp_compare_branch(Gen *g, const Tac_Instruction *in, bool if_zero, const char *label)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    const Tac_Type *t = val_type(g, a);
    bool swap;
    int cond = fp_compare_cond(in->u.binary.op, &swap);
    if (cond < 0)
        return false;
    if (swap) {
        const Tac_Val *x = a;
        a                = b;
        b                = x;
    }
    int ra = use_val(g, F0, a);
    emit2(g, x86_is_double(t) ? X86_UCOMISD : X86_UCOMISS, X86_Q, fp_operand(g, b), x86_xmm(ra));
    // Equal is ZF = 1 and PF = 0, so its branch and its inverse take two jumps; `a`
    // and `ae` are false for unordered operands, and `be` and `b` true.
    bool taken_if_equal = (cond == X86_CC_E) != if_zero;
    if (cond == X86_CC_E || cond == X86_CC_NE) {
        if (taken_if_equal) {
            char skip[32];
            new_label(skip);
            jcc(g, X86_CC_P, skip);
            jcc(g, X86_CC_E, label);
            x86_new_block(g->fn, skip);
        } else {
            jcc(g, X86_CC_NE, label);
            jcc(g, X86_CC_P, label);
        }
    } else {
        jcc(g, if_zero ? cond ^ 1 : cond, label);
    }
    return true;
}

void gen_fp_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *label)
{
    compare_zero(g, cond, val_type(g, cond));
    if (if_zero) {
        char skip[32];
        new_label(skip);
        jcc(g, X86_CC_P, skip);
        jcc(g, X86_CC_E, label);
        x86_new_block(g->fn, skip);
    } else {
        jcc(g, X86_CC_NE, label);
        jcc(g, X86_CC_P, label);
    }
}

// xmm14 = 64-bit integer in rax, unsigned: when the top bit is set, half of it (the
// low bit or'ed back in, so the rounding is right) converted, then doubled.
static void gen_u64_to_fp(Gen *g, bool d)
{
    X86_Op cvt = d ? X86_CVTSI2SD : X86_CVTSI2SS;
    char big[32], done[32];
    new_label(big);
    new_label(done);
    emit2(g, X86_TEST, X86_Q, x86_reg(T0, X86_Q), x86_reg(T0, X86_Q));
    jcc(g, X86_CC_S, big);
    emit2(g, cvt, X86_Q, x86_reg(T0, X86_Q), x86_xmm(F0));
    jmp(g, done);
    x86_new_block(g->fn, big);
    emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), x86_reg(T2, X86_Q));
    emit2(g, X86_SHR, X86_Q, x86_imm(1), x86_reg(T2, X86_Q));
    emit2(g, X86_AND, X86_L, x86_imm(1), x86_reg(T0, X86_L));
    emit2(g, X86_OR, X86_Q, x86_reg(T0, X86_Q), x86_reg(T2, X86_Q));
    emit2(g, cvt, X86_Q, x86_reg(T2, X86_Q), x86_xmm(F0));
    emit2(g, d ? X86_ADDSD : X86_ADDSS, X86_Q, x86_xmm(F0), x86_xmm(F0));
    x86_new_block(g->fn, done);
}

// rax = FP in xmm14 truncated to an unsigned 64-bit integer: a value of 2^63 or more
// has 2^63 subtracted first and the top bit set after.
static void gen_fp_to_u64(Gen *g, bool d)
{
    X86_Op cvt   = d ? X86_CVTTSD2SI : X86_CVTTSS2SI;
    uint64_t two63 = d ? 0x43e0000000000000ULL : 0x5f000000;
    char big[32], done[32];
    new_label(big);
    new_label(done);
    emit2(g, d ? X86_MOVSD : X86_MOVSS, X86_Q, const_mem(g, two63, 0, d ? 8 : 4), x86_xmm(F1));
    emit2(g, d ? X86_UCOMISD : X86_UCOMISS, X86_Q, x86_xmm(F1), x86_xmm(F0));
    jcc(g, X86_CC_AE, big);
    emit2(g, cvt, X86_Q, x86_xmm(F0), x86_reg(T0, X86_Q));
    jmp(g, done);
    x86_new_block(g->fn, big);
    emit2(g, d ? X86_SUBSD : X86_SUBSS, X86_Q, x86_xmm(F1), x86_xmm(F0));
    emit2(g, cvt, X86_Q, x86_xmm(F0), x86_reg(T0, X86_Q));
    gen_li(g, T2, X86_Q, INT64_MIN);
    emit2(g, X86_XOR, X86_Q, x86_reg(T2, X86_Q), x86_reg(T0, X86_Q));
    x86_new_block(g->fn, done);
}

// An int/FP or float/double conversion.  To an integer it truncates toward zero, into
// the destination's width (a narrower one is truncated by the store; an unsigned int
// is converted as a 64-bit value); the signedness of an integer source is the
// conversion's (its own type may differ, once copy propagation has removed a cast).
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    bool sfp = x86_is_fp(st), dfp = x86_is_fp(dt);
    if (sfp && dfp) {
        X86_Op cvt = x86_is_double(dt) ? X86_CVTSS2SD : X86_CVTSD2SS;
        int r      = def_reg(g, F0, dst);
        if (x86_is_double(dt) == x86_is_double(st)) {
            load_val(g, r, src);
        } else if (src->kind == TAC_VAL_CONSTANT) {
            load_val(g, F1, src);
            emit2(g, cvt, X86_Q, x86_xmm(F1), x86_xmm(r));
        } else {
            emit2(g, cvt, X86_Q, fp_operand(g, src), x86_xmm(r));
        }
        store_val(g, r, dst);
        return;
    }
    if (dfp) {
        // A register holds an unsigned int zero-extended to 64 bits, a narrower type
        // extended to 32, as a 32-bit load leaves them.
        bool d = x86_is_double(dt);
        bool u = kind == TAC_INSTRUCTION_UINT_TO_DOUBLE || kind == TAC_INSTRUCTION_UINT_TO_FLOAT;
        int size = x86_size(st);
        if (u && size == 8) {
            load_val(g, T0, src);
            gen_u64_to_fp(g, d);
            store_val(g, F0, dst);
            return;
        }
        int s       = use_val(g, T0, src);
        int r       = def_reg(g, F0, dst);
        X86_Width w = u || size == 8 ? X86_Q : X86_L;
        emit2(g, d ? X86_CVTSI2SD : X86_CVTSI2SS, w, x86_reg(s, w), x86_xmm(r));
        store_val(g, r, dst);
        return;
    }
    bool d   = x86_is_double(st);
    int size = x86_size(dt);
    if (x86_is_unsigned(dt) && size == 8) {
        load_val(g, F0, src); // changed in place
        gen_fp_to_u64(g, d);
        store_val(g, T0, dst);
        return;
    }
    int s = use_val(g, F0, src);
    int r = def_reg(g, T0, dst);
    emit2(g, d ? X86_CVTTSD2SI : X86_CVTTSS2SI, X86_Q, x86_xmm(s),
          x86_reg(r, x86_is_unsigned(dt) && size == 4 ? X86_Q : x86_op_width(dt)));
    store_val(g, r, dst);
}
