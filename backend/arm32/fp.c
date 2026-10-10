//
// Floating point on VFPv3-D16: float in s28/s30, double (and long double, the same
// type here) in d14/d15.  A comparison is vcmp and vmrs, which copies the FP flags to
// the core ones.
//
#include "codegen.h"
#include "internal.h"

// The .f32 or .f64 form of an operation on type `t`.
static A32_Op fp_op(const Tac_Type *t, A32_Op f32, A32_Op f64)
{
    return a32_is_double(t) ? f64 : f32;
}

static A32_Operand fp_reg(const Tac_Type *t, int reg)
{
    return a32_is_double(t) ? a32_dreg(reg) : a32_sreg(reg);
}

// Copy the FP status flags to the core ones.
static void vmrs(Gen *g)
{
    emit2(g, A32_VMRS, a32_sym("APSR_nzcv", 0), a32_sym("fpscr", 0));
}

void fp_test_zero(Gen *g, const Tac_Val *v)
{
    const Tac_Type *t = val_type(g, v);
    emit2(g, fp_op(t, A32_VCMP_F32, A32_VCMP_F64), fp_reg(t, use_val(g, F0, v)), a32_imm(0));
    vmrs(g);
}

// The condition of FP comparison `op`, or -1.  An unordered compare sets C and V, so
// mi and ls are false for a NaN where lt and le would not be; gt, ge and eq are false
// too, and ne true.
static int fp_compare_cond(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A32_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A32_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        return A32_MI;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        return A32_LS;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        return A32_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        return A32_GE;
    default:
        return -1;
    }
}

int gen_fp_compare(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    int cond          = fp_compare_cond(in->u.binary.op);
    if (cond < 0)
        return -1;
    int a = use_val(g, F0, in->u.binary.src1);
    int b = use_val(g, F1, in->u.binary.src2);
    emit2(g, fp_op(t, A32_VCMP_F32, A32_VCMP_F64), fp_reg(t, a), fp_reg(t, b));
    vmrs(g);
    return cond;
}

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t  = val_type(g, in->u.binary.src1);
    const Tac_Val *dst = in->u.binary.dst;
    int cond           = gen_fp_compare(g, in);
    if (cond >= 0) {
        int d = def_reg(g, T0, dst);
        set_cond(g, d, cond);
        store_val(g, d, dst);
        return;
    }
    int a = use_val(g, F0, in->u.binary.src1);
    int b = use_val(g, F1, in->u.binary.src2);
    A32_Op op;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        op = fp_op(t, A32_VADD_F32, A32_VADD_F64);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        op = fp_op(t, A32_VSUB_F32, A32_VSUB_F64);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        op = fp_op(t, A32_VMUL_F32, A32_VMUL_F64);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        op = fp_op(t, A32_VDIV_F32, A32_VDIV_F64);
        break;
    default:
        internal_error("arm32: %s: bad floating-point operator %d", gen_name(g), in->u.binary.op);
    }
    int d = def_reg(g, F0, dst);
    emit3(g, op, fp_reg(t, d), fp_reg(t, a), fp_reg(t, b));
    store_val(g, d, dst);
}

// A negation, or `!` (equal to zero, a NaN is not).
void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t  = val_type(g, in->u.unary.src);
    const Tac_Val *dst = in->u.unary.dst;
    if (in->u.unary.op == TAC_UNARY_NOT) {
        fp_test_zero(g, in->u.unary.src);
        int d = def_reg(g, T0, dst);
        set_cond(g, d, A32_EQ);
        store_val(g, d, dst);
        return;
    }
    bool is_sqrt = in->u.unary.op == TAC_UNARY_SQRT_DOUBLE;
    if (!is_sqrt && in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        internal_error("arm32: %s: bad floating-point unary operator", gen_name(g));
    int a = use_val(g, F0, in->u.unary.src), d = def_reg(g, F0, dst);
    emit2(g, is_sqrt ? A32_VSQRT_F64 : fp_op(t, A32_VNEG_F32, A32_VNEG_F64), fp_reg(t, d), fp_reg(t, a));
    store_val(g, d, dst);
}

// Between float and double, by vcvt; between them and an integer, through s28: the
// integer moved there and converted, or converted there toward zero and moved out (a
// narrower destination is truncated by the store).  The signedness of an integer
// source is the conversion's, of a destination its type's.
void gen_fp_convert32(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    bool sfp = a32_is_fp(st), dfp = a32_is_fp(dt);
    if (sfp && dfp) {
        int a = use_val(g, F0, src), d = def_reg(g, F0, dst);
        if (a32_is_double(st) != a32_is_double(dt))
            emit2(g, a32_is_double(dt) ? A32_VCVT_F64_F32 : A32_VCVT_F32_F64, fp_reg(dt, d),
                  fp_reg(st, a));
        else
            move_reg(g, d, a, dt);
        store_val(g, d, dst);
        return;
    }
    if (dfp) {
        bool u = from_unsigned(kind);
        emit2(g, A32_VMOV, a32_sreg(F0), a32_reg(use_val(g, T0, src)));
        A32_Op op = a32_is_double(dt) ? (u ? A32_VCVT_F64_U32 : A32_VCVT_F64_S32)
                                      : (u ? A32_VCVT_F32_U32 : A32_VCVT_F32_S32);
        int d = def_reg(g, F0, dst);
        emit2(g, op, fp_reg(dt, d), a32_sreg(F0));
        store_val(g, d, dst);
        return;
    }
    bool u = a32_is_unsigned(dt);
    int a  = use_val(g, F0, src);
    A32_Op op = a32_is_double(st) ? (u ? A32_VCVT_U32_F64 : A32_VCVT_S32_F64)
                                  : (u ? A32_VCVT_U32_F32 : A32_VCVT_S32_F32);
    emit2(g, op, a32_sreg(F0), fp_reg(st, a));
    int d = def_reg(g, T0, dst);
    emit2(g, A32_VMOV, a32_reg(d), a32_sreg(F0));
    store_val(g, d, dst);
}
