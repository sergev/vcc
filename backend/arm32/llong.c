//
// 64-bit integers: a long long lives in an 8-byte slot, the low word first, and is
// operated on a word at a time in r12 and lr.  The carry flag joins the halves of an
// add, subtract or compare: loads, stores, address and constant moves between them
// leave the flags alone.  Multiplication, division, remainder, variable shifts and the
// conversions with FP call the RTABI helpers (libc/arm32/aeabi_*.s), which take and
// return long longs and doubles in core register pairs (the base standard).
//
#include "codegen.h"
#include "internal.h"

static const Tac_Type ll = { .kind = TAC_TYPE_LONG_LONG }, ull = { .kind = TAC_TYPE_ULONG_LONG };
static const Tac_Type dbl = { .kind = TAC_TYPE_DOUBLE };

static void op3(Gen *g, A32_Op op, int d, int a, A32_Operand b, bool flags)
{
    emit3(g, op, a32_reg(d), a32_reg(a), b)->set_flags = flags;
}

static void bl(Gen *g, const char *name)
{
    emit1(g, A32_BL, a32_sym(name, 0));
}

// r0:r1 = value `v` as type `t`, and r2:r3 = `w` as `t` (or r2 = a word of it, for a
// shift count).
static void load_args(Gen *g, const Tac_Val *v, const Tac_Val *w, const Tac_Type *t, bool count)
{
    load_word(g, A32_R0, v, t, 0);
    load_word(g, A32_R0 + 1, v, t, 1);
    if (!w)
        return;
    const Tac_Type *wt = val_type(g, w);
    if (count)
        load_word(g, A32_R0 + 2, w, a32_size(wt) == 8 ? wt : &ll, 0);
    else {
        load_word(g, A32_R0 + 2, w, t, 0);
        load_word(g, A32_R0 + 3, w, t, 1);
    }
}

// A shift of the pair in r12/lr by constant `n` (1..63).  Across the halves, the bits
// that cross over are or'ed in through r10.
static void shift_const(Gen *g, Tac_BinaryOperator op, bool arith, int n)
{
    A32_Op right = arith ? A32_ASR : A32_LSR;
    if (op == TAC_BINARY_LEFT_SHIFT) {
        if (n >= 32) {
            if (n > 32)
                op3(g, A32_LSL, T1, T0, a32_imm(n - 32), false);
            else
                emit2(g, A32_MOV, a32_reg(T1), a32_reg(T0));
            gen_li(g, T0, 0);
            return;
        }
        op3(g, A32_LSL, T1, T1, a32_imm(n), false);
        op3(g, A32_ORR, T1, T1, a32_shift(T0, A32_SHIFT_LSR, 32 - n), false);
        op3(g, A32_LSL, T0, T0, a32_imm(n), false);
        return;
    }
    if (n >= 32) {
        if (n > 32)
            op3(g, right, T0, T1, a32_imm(n - 32), false);
        else
            emit2(g, A32_MOV, a32_reg(T0), a32_reg(T1));
        if (arith)
            op3(g, A32_ASR, T1, T1, a32_imm(31), false);
        else
            gen_li(g, T1, 0);
        return;
    }
    op3(g, A32_LSR, T0, T0, a32_imm(n), false);
    op3(g, A32_ORR, T0, T0, a32_shift(T1, A32_SHIFT_LSL, 32 - n), false);
    op3(g, right, T1, T1, a32_imm(n), false);
}

static void gen_ll_shift(Gen *g, const Tac_Instruction *in, bool is_unsigned)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *count  = in->u.binary.src2;
    const Tac_Type *t     = val_type(g, in->u.binary.src1);
    bool arith            = op == TAC_BINARY_RIGHT_SHIFT && !is_unsigned;
    if (count->kind == TAC_VAL_CONSTANT) {
        int n = (int)(const_bits(count->u.constant, val_type(g, count)) & 63);
        load_word(g, T0, in->u.binary.src1, t, 0);
        load_word(g, T1, in->u.binary.src1, t, 1);
        if (n)
            shift_const(g, op, arith, n);
        store_pair(g, in->u.binary.dst, T0, T1);
        return;
    }
    load_args(g, in->u.binary.src1, count, t, true);
    bl(g, op == TAC_BINARY_LEFT_SHIFT ? "__aeabi_llsl" : arith ? "__aeabi_lasr" : "__aeabi_llsr");
    store_pair(g, in->u.binary.dst, A32_R0, A32_R0 + 1);
}

// Word `half` of 8-byte variable `dst` = r12, the address through lr.
static void store_word(Gen *g, int reg, const Tac_Val *dst, int half)
{
    int base;
    int64_t off;
    name_addr(g, dst->u.var_name, T1, &base, &off);
    emit2(g, A32_STR, a32_reg(reg), mem(g, A32_STR, base, off + 4 * half, T1));
}

// Words `half` of a and b into r12 and lr.
static void load_halves(Gen *g, const Tac_Val *a, const Tac_Val *b, const Tac_Type *t, int half)
{
    load_word(g, T0, a, t, half);
    load_word(g, T1, b, t, half);
}

void gen_ll_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *dst = in->u.binary.dst;
    bool u            = unsigned_operation(val_type(g, a), op);
    const Tac_Type *t = u ? &ull : &ll;
    A32_Op lo_op = A32_EPILOGUE, hi_op = A32_EPILOGUE;
    bool carry = false;
    switch (op) {
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        // The quotient comes back in r0:r1, the remainder in r2:r3.
        load_args(g, a, b, t, false);
        bl(g, u ? "__aeabi_uldivmod" : "__aeabi_ldivmod");
        if (op == TAC_BINARY_DIVIDE || op == TAC_BINARY_DIVIDE_UNSIGNED)
            store_pair(g, dst, A32_R0, A32_R0 + 1);
        else
            store_pair(g, dst, A32_R0 + 2, A32_R0 + 3);
        return;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        load_args(g, a, b, t, false);
        bl(g, "__aeabi_lmul");
        store_pair(g, dst, A32_R0, A32_R0 + 1);
        return;
    case TAC_BINARY_LEFT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        gen_ll_shift(g, in, u);
        return;
    case TAC_BINARY_EQUAL:
    case TAC_BINARY_NOT_EQUAL:
        // The high words, then the low ones when those are equal.
        load_halves(g, a, b, t, 1);
        emit2(g, A32_CMP, a32_reg(T0), a32_reg(T1));
        load_halves(g, a, b, t, 0);
        emit2(g, A32_CMP, a32_reg(T0), a32_reg(T1))->cond = A32_EQ;
        set_cond(g, T0, op == TAC_BINARY_EQUAL ? A32_EQ : A32_NE);
        store_val(g, T0, dst);
        return;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED: {
        // a - b by cmp and sbcs sets the flags of a 64-bit compare for lt/ge (lo/hs);
        // gt and le compare b with a.
        bool swap = op == TAC_BINARY_GREATER_THAN || op == TAC_BINARY_GREATER_THAN_UNSIGNED ||
                    op == TAC_BINARY_LESS_OR_EQUAL || op == TAC_BINARY_LESS_OR_EQUAL_UNSIGNED;
        const Tac_Val *x = swap ? b : a, *y = swap ? a : b;
        bool less = op == TAC_BINARY_LESS_THAN || op == TAC_BINARY_LESS_THAN_UNSIGNED ||
                    op == TAC_BINARY_GREATER_THAN || op == TAC_BINARY_GREATER_THAN_UNSIGNED;
        load_halves(g, x, y, t, 0);
        emit2(g, A32_CMP, a32_reg(T0), a32_reg(T1));
        load_halves(g, x, y, t, 1);
        op3(g, A32_SBC, T0, T0, a32_reg(T1), true);
        set_cond(g, T0, less ? (u ? A32_LO : A32_LT) : (u ? A32_HS : A32_GE));
        store_val(g, T0, dst);
        return;
    }
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        lo_op = A32_ADD, hi_op = A32_ADC, carry = true;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        lo_op = A32_SUB, hi_op = A32_SBC, carry = true;
        break;
    case TAC_BINARY_BITWISE_AND:
        lo_op = hi_op = A32_AND;
        break;
    case TAC_BINARY_BITWISE_OR:
        lo_op = hi_op = A32_ORR;
        break;
    case TAC_BINARY_BITWISE_XOR:
        lo_op = hi_op = A32_EOR;
        break;
    default:
        fatal_error("arm32: %s: bad long long operator %d", gen_name(g), op);
    }
    // Each word stored as it is computed: the flags carry over.
    load_halves(g, a, b, t, 0);
    op3(g, lo_op, T0, T0, a32_reg(T1), carry);
    store_word(g, T0, dst, 0);
    load_halves(g, a, b, t, 1);
    op3(g, hi_op, T0, T0, a32_reg(T1), false);
    store_word(g, T0, dst, 1);
}

void gen_ll_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    const Tac_Type *t = val_type(g, src);
    load_word(g, T0, src, t, 0);
    load_word(g, T1, src, t, 1);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        op3(g, A32_RSB, T0, T0, a32_imm(0), true);
        op3(g, A32_RSC, T1, T1, a32_imm(0), false);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A32_MVN, a32_reg(T0), a32_reg(T0));
        emit2(g, A32_MVN, a32_reg(T1), a32_reg(T1));
        break;
    case TAC_UNARY_NOT:
        op3(g, A32_ORR, T0, T0, a32_reg(T1), true);
        set_cond(g, T0, A32_EQ);
        store_val(g, T0, dst);
        return;
    default:
        fatal_error("arm32: %s: bad long long unary operator", gen_name(g));
    }
    store_pair(g, dst, T0, T1);
}

bool from_unsigned(Tac_InstructionKind kind)
{
    return kind == TAC_INSTRUCTION_UINT_TO_DOUBLE || kind == TAC_INSTRUCTION_UINT_TO_FLOAT ||
           kind == TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE;
}

// A conversion between long long and FP: the integer in r0:r1, a double in r0:r1 too
// and a float in r0, both ways.
void gen_ll_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    const char *name;
    if (a32_is_pair(st)) {
        bool u = from_unsigned(kind);
        if (a32_is_double(dt))
            name = u ? "__aeabi_ul2d" : "__aeabi_l2d";
        else
            name = u ? "__aeabi_ul2f" : "__aeabi_l2f";
        load_args(g, src, NULL, u ? &ull : &ll, false);
        bl(g, name);
        if (a32_is_double(dt))
            store_pair(g, dst, A32_R0, A32_R0 + 1);
        else
            store_val(g, A32_R0, dst);
        return;
    }
    bool u = a32_is_unsigned(dt);
    if (a32_is_double(st)) {
        name = u ? "__aeabi_d2ulz" : "__aeabi_d2lz";
        load_args(g, src, NULL, &dbl, false);
    } else {
        name = u ? "__aeabi_f2ulz" : "__aeabi_f2lz";
        load_as(g, A32_R0, src, st);
    }
    bl(g, name);
    store_pair(g, dst, A32_R0, A32_R0 + 1);
}
