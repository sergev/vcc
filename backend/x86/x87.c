//
// long double on the x87: the 80-bit extended format in a 16-byte slot.  A value
// never stays on the x87 stack between TAC instructions: a pattern loads its operands
// with fldt (at most two deep), computes, and stores the result with fstpt, so the
// stack is empty at every call, as the psABI wants, but for a result in st(0).
//
// fucomip sets ZF, PF and CF as ucomisd does (all three for unordered operands), so
// comparisons follow the rules of fp.c.  In AT&T syntax `fsubrp %st, %st(1)` computes
// st(1) - st(0) and `fdivrp` st(1) / st(0): with a loaded before b, that is a - b.
//
#include "codegen.h"
#include "float128.h"
#include "internal.h"
#include "xalloc.h"

// The x87 format of `q`: the 64-bit significand and the 16 bits of sign and exponent.
static void ld_bits(Float128 q, uint64_t *mant, unsigned *sexp)
{
    uint8_t x[10];
    f128_to_x87(q, x);
    *mant = 0;
    for (int i = 7; i >= 0; i--)
        *mant = *mant << 8 | x[i];
    *sexp = x[8] | x[9] << 8;
}

// The function's scratch slot for the x87: an integer at +0, control words at +8
// and +10.
static int x87_tmp(Gen *g)
{
    if (!g->x87_tmp)
        g->x87_tmp = alloc_slot(g, NULL, NULL, 16, 16);
    return g->x87_tmp;
}

static void fld_val(Gen *g, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_CONSTANT) {
        emit1(g, X86_FLDT, X86_Q, name_mem(g, v->u.var_name, 0));
        return;
    }
    uint64_t mant;
    unsigned sexp;
    ld_bits(v->u.constant->u.long_double_val, &mant, &sexp);
    if (mant == 0 && sexp == 0)
        emit0(g, X86_FLDZ, X86_Q);
    else if (mant == (uint64_t)1 << 63 && sexp == 0x3fff)
        emit0(g, X86_FLD1, X86_Q);
    else
        emit1(g, X86_FLDT, X86_Q, const_mem(g, mant, sexp, 16));
}

void gen_ld_copy(Gen *g, const Tac_Val *src, X86_Operand dst)
{
    X86_Operand hi = dst;
    hi.imm += 8;
    if (dst.kind == X86_OPND_RIP || dst.kind == X86_OPND_LABEL)
        hi.sym = xstrdup(dst.sym); // each operand owns its symbol
    if (src->kind == TAC_VAL_CONSTANT) {
        uint64_t mant;
        unsigned sexp;
        ld_bits(src->u.constant->u.long_double_val, &mant, &sexp);
        gen_li(g, T0, X86_Q, (int64_t)mant);
        emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), dst);
        emit2(g, X86_MOV, X86_W, x86_imm(sexp), hi);
        return;
    }
    for (int i = 0; i < 2; i++) {
        emit2(g, X86_MOV, X86_Q, name_mem(g, src->u.var_name, 8 * i), x86_reg(T0, X86_Q));
        emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), i ? hi : dst);
    }
}

// Pop st(0) into variable `dst`.
static void fstp_val(Gen *g, const Tac_Val *dst)
{
    emit1(g, X86_FSTPT, X86_Q, name_mem(g, dst->u.var_name, 0));
}

// Push long double `v` onto the x87 stack.
static void fld_val(Gen *g, const Tac_Val *v);

// eax = 1 when the flags of a fucomip say equal (or, with `ne`, not equal).
static void set_equal(Gen *g, bool ne)
{
    X86_Instr *a = emit1(g, X86_SET, X86_B, x86_reg(T0, X86_B));
    a->cond      = ne ? X86_CC_NE : X86_CC_E;
    X86_Instr *p = emit1(g, X86_SET, X86_B, x86_reg(T2, X86_B));
    p->cond      = ne ? X86_CC_P : X86_CC_NP;
    emit2(g, ne ? X86_OR : X86_AND, X86_B, x86_reg(T2, X86_B), x86_reg(T0, X86_B));
    emit2(g, X86_MOVZB, X86_L, x86_reg(T0, X86_B), x86_reg(T0, X86_L));
}

// Compare st(0) with st(1) and pop both.
static void fcompare_pop(Gen *g)
{
    emit2(g, X86_FUCOMIP, X86_Q, x86_st(1), x86_st(0));
    emit1(g, X86_FSTP, X86_Q, x86_st(0));
}

// Compare long double `v` with zero.
static void compare_zero(Gen *g, const Tac_Val *v)
{
    fld_val(g, v);
    emit0(g, X86_FLDZ, X86_Q);
    fcompare_pop(g);
}

void gen_ld_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        compare_zero(g, in->u.unary.src);
        set_equal(g, false);
        store_val(g, T0, dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        internal_error("x86: %s: bad long double unary operator", gen_name(g));
    fld_val(g, in->u.unary.src);
    emit0(g, X86_FCHS, X86_Q);
    fstp_val(g, dst);
}

void gen_ld_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *dst = in->u.binary.dst;
    X86_Op op;
    int cond  = -1;
    bool swap = false;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        op = X86_FADDP;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        op = X86_FSUBRP;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        op = X86_FMULP;
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        op = X86_FDIVRP;
        break;
    case TAC_BINARY_EQUAL:
        cond = X86_CC_E;
        break;
    case TAC_BINARY_NOT_EQUAL:
        cond = X86_CC_NE;
        break;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        cond = X86_CC_A;
        swap = true;
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        cond = X86_CC_AE;
        swap = true;
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        cond = X86_CC_A;
        break;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        cond = X86_CC_AE;
        break;
    default:
        internal_error("x86: %s: bad long double operator %d", gen_name(g), in->u.binary.op);
    }
    if (cond < 0) {
        fld_val(g, a);
        fld_val(g, b);
        emit2(g, op, X86_Q, x86_st(0), x86_st(1));
        fstp_val(g, dst);
        return;
    }
    // st(0) is the left operand of the comparison: loaded last.
    if (swap) {
        const Tac_Val *x = a;
        a                = b;
        b                = x;
    }
    fld_val(g, b);
    fld_val(g, a);
    fcompare_pop(g);
    if (cond == X86_CC_E || cond == X86_CC_NE) {
        set_equal(g, cond == X86_CC_NE);
    } else {
        X86_Instr *set = emit1(g, X86_SET, X86_B, x86_reg(T0, X86_B));
        set->cond      = cond;
        emit2(g, X86_MOVZB, X86_L, x86_reg(T0, X86_B), x86_reg(T0, X86_L));
    }
    store_val(g, T0, dst);
}

static void jcc(Gen *g, int cond, const char *label)
{
    X86_Instr *j = emit1(g, X86_J, X86_Q, x86_label(label));
    j->cond      = cond;
}

void gen_ld_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *label)
{
    compare_zero(g, cond);
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

// rax = st(0) truncated toward zero, popped: fistp under a control word switched to
// truncation, since baseline x86-64 has no fisttp.
static void fistp_trunc(Gen *g)
{
    int tmp = x87_tmp(g);
    emit1(g, X86_FNSTCW, X86_Q, x86_mem(X86_FRAME, tmp + 8));
    emit2(g, X86_MOVZW, X86_L, x86_mem(X86_FRAME, tmp + 8), x86_reg(T0, X86_L));
    emit2(g, X86_OR, X86_L, x86_imm(0xc00), x86_reg(T0, X86_L));
    emit2(g, X86_MOV, X86_W, x86_reg(T0, X86_W), x86_mem(X86_FRAME, tmp + 10));
    emit1(g, X86_FLDCW, X86_Q, x86_mem(X86_FRAME, tmp + 10));
    emit1(g, X86_FISTPQ, X86_Q, x86_mem(X86_FRAME, tmp));
    emit1(g, X86_FLDCW, X86_Q, x86_mem(X86_FRAME, tmp + 8));
    emit2(g, X86_MOV, X86_Q, x86_mem(X86_FRAME, tmp), x86_reg(T0, X86_Q));
}

// A conversion to or from long double.  Integers go through a 64-bit fildq/fistpq in
// the scratch slot, unsigned 64-bit ones with a 2^64 or 2^63 correction; float and
// double are loaded or stored by the x87, which rounds.  An integer is signed or
// unsigned by the conversion's kind or the destination's type, as in fp.c.
void gen_ld_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (x86_is_ld(st) && x86_is_ld(dt)) {
        fld_val(g, src);
        fstp_val(g, dst);
    } else if (x86_is_ld(dt) && x86_is_fp(st)) {
        // The x87 loads from memory only: a register goes through the scratch slot.
        X86_Operand m = fp_operand(g, src);
        if (m.kind == X86_OPND_REG) {
            store_mem(g, m.reg, st, x86_mem(X86_FRAME, x87_tmp(g)));
            m = x86_mem(X86_FRAME, x87_tmp(g));
        }
        emit1(g, x86_is_double(st) ? X86_FLDL : X86_FLDS, X86_Q, m);
        fstp_val(g, dst);
    } else if (x86_is_ld(st) && x86_is_fp(dt)) {
        fld_val(g, src);
        int r         = var_reg(g, dst);
        X86_Operand m = r ? x86_mem(X86_FRAME, x87_tmp(g)) : name_mem(g, dst->u.var_name, 0);
        emit1(g, x86_is_double(dt) ? X86_FSTPL : X86_FSTPS, X86_Q, m);
        if (r)
            load_mem(g, r, dt, x86_mem(X86_FRAME, x87_tmp(g)));
    } else if (x86_is_ld(dt)) {
        bool u  = kind == TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE;
        int tmp = x87_tmp(g);
        load_val(g, T0, src); // a 32-bit load zero-extends to 64
        if (!u && x86_size(st) <= 4)
            emit2(g, X86_MOVSL, X86_Q, x86_reg(T0, X86_L), x86_reg(T0, X86_Q));
        emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), x86_mem(X86_FRAME, tmp));
        emit1(g, X86_FILDQ, X86_Q, x86_mem(X86_FRAME, tmp));
        if (u && x86_size(st) == 8) {
            char done[32];
            new_label(done);
            emit2(g, X86_TEST, X86_Q, x86_reg(T0, X86_Q), x86_reg(T0, X86_Q));
            jcc(g, X86_CC_NS, done);
            emit1(g, X86_FADDS, X86_Q, const_mem(g, 0x5f800000, 0, 4)); // 2^64
            x86_new_block(g->fn, done);
        }
        fstp_val(g, dst);
    } else if (x86_is_unsigned(dt) && x86_size(dt) == 8) {
        // From 2^63 up, 2^63 is subtracted first and the top bit set after: the mask
        // waits in r11.
        char big[32], done[32];
        new_label(big);
        new_label(done);
        fld_val(g, src);
        emit1(g, X86_FLDS, X86_Q, const_mem(g, 0x5f000000, 0, 4));
        emit2(g, X86_FUCOMIP, X86_Q, x86_st(1), x86_st(0));
        jcc(g, X86_CC_BE, big);
        emit2(g, X86_XOR, X86_L, x86_reg(T2, X86_L), x86_reg(T2, X86_L));
        emit1(g, X86_JMP, X86_Q, x86_label(done));
        x86_new_block(g->fn, big);
        emit1(g, X86_FSUBS, X86_Q, const_mem(g, 0x5f000000, 0, 4));
        gen_li(g, T2, X86_Q, INT64_MIN);
        x86_new_block(g->fn, done);
        fistp_trunc(g);
        emit2(g, X86_XOR, X86_Q, x86_reg(T2, X86_Q), x86_reg(T0, X86_Q));
        store_val(g, T0, dst);
    } else {
        fld_val(g, src);
        fistp_trunc(g);
        store_val(g, T0, dst);
    }
}

void gen_ld_load(Gen *g, const Tac_Val *v)
{
    fld_val(g, v);
}
