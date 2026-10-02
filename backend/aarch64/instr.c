//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers and its result stored back.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// Local label for TAC label `%N`: `.LN`.
static char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, ".L");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
}

static void gen_label(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    a64_new_block(g->fn, l);
    xfree(l);
}

static void gen_jump(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    emit1(g, A64_B, a64_sym(l, 0));
    xfree(l);
}

// Branch to `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    char *l = label_name(target);
    if (a64_is_fp(t)) {
        // A NaN is not zero: unordered leaves Z clear.
        load_val(g, F0, cond);
        emit2(g, A64_FCMP, a64_reg(F0, a64_width(t)), a64_fzero());
        emit2(g, A64_BCOND, a64_cond(if_zero ? A64_EQ : A64_NE), a64_sym(l, 0));
    } else {
        load_val(g, T0, cond);
        emit2(g, if_zero ? A64_CBZ : A64_CBNZ, a64_reg(T0, a64_width(t)), a64_sym(l, 0));
    }
    xfree(l);
}

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a64_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a64_size(t), a64_align(t));
        return;
    }
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    if (a64_is_fp(t)) {
        load_val(g, F0, src);
    } else {
        load_int_as(g, T0, src, t);
    }
    store_val(g, a64_is_fp(t) ? F0 : T0, dst);
}

// The register view of an operation on type `t`: W up to 32 bits, else X.
static A64_Width int_width(const Tac_Type *t)
{
    return a64_size(t) <= 4 ? A64_W : A64_X;
}

// An integer conversion.  A store truncates to the destination's width; a load
// extends by the source's own type, so an extension is explicit only where that
// differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type) or into 64 bits, a zero extension of a signed one.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    int ssize    = a64_size(st);
    A64_Width dw = int_width(dt);
    load_val(g, T0, src);
    A64_Operand d = a64_reg(T0, dw), s = a64_reg(T0, A64_W);
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND) {
        if (ssize == 1)
            emit2(g, A64_SXTB, d, s);
        else if (ssize == 2)
            emit2(g, A64_SXTH, d, s);
        else if (ssize == 4 && dw == A64_X)
            emit2(g, A64_SXTW, d, s);
    } else if (kind == TAC_INSTRUCTION_ZERO_EXTEND) {
        if (ssize == 1)
            emit2(g, A64_UXTB, s, s);
        else if (ssize == 2)
            emit2(g, A64_UXTH, s, s);
        // A W register's upper half is already zero.
    }
    store_val(g, T0, dst);
}

// A floating-point negation, or `!` (equal to zero, a NaN is not).
static void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    A64_Operand f = a64_reg(F0, a64_width(t));
    load_val(g, F0, in->u.unary.src);
    if (in->u.unary.op == TAC_UNARY_NOT) {
        emit2(g, A64_FCMP, f, a64_fzero());
        emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(A64_EQ));
        store_val(g, T0, in->u.unary.dst);
        return;
    }
    if (in->u.unary.op != TAC_UNARY_NEGATE && in->u.unary.op != TAC_UNARY_NEGATE_DOUBLE)
        fatal_error("aarch64: %s: bad floating-point unary operator", gen_name(g));
    emit2(g, A64_FNEG, f, f);
    store_val(g, F0, in->u.unary.dst);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    if (a64_is_fp(t)) {
        gen_fp_unary(g, in, t);
        return;
    }
    A64_Width w = int_width(t);
    load_int_as(g, T0, in->u.unary.src, t);
    A64_Operand r = a64_reg(T0, w);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit2(g, A64_NEG, r, r);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A64_MVN, r, r);
        break;
    case TAC_UNARY_NOT:
        emit2(g, A64_CMP, r, a64_imm(0));
        emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(A64_EQ));
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("aarch64: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, T0, in->u.unary.dst);
}

// Whether operator `op` is unsigned whatever its operands' types: they may differ in
// signedness, once copy propagation has removed a cast.
static bool unsigned_op(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER_UNSIGNED:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        return true;
    default:
        return false;
    }
}

// The condition of comparison `op`, or -1 when it is not one.
static int compare_cond(Tac_BinaryOperator op, bool is_unsigned)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A64_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A64_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return is_unsigned ? A64_LO : A64_LT;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A64_LS : A64_LE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return is_unsigned ? A64_HI : A64_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A64_HS : A64_GE;
    default:
        return -1;
    }
}

// d = a op b for an integer operator, all at view `w`.  The sources are read before d
// is written; d is not a or b only for the remainder.
static void gen_int_binop(Gen *g, Tac_BinaryOperator op, bool is_unsigned, A64_Width w, int d,
                          int a, int b)
{
    A64_Operand rd = a64_reg(d, w), ra = a64_reg(a, w), rb = a64_reg(b, w);
    int cond = compare_cond(op, is_unsigned);
    if (cond >= 0) {
        emit2(g, A64_CMP, ra, rb);
        emit2(g, A64_CSET, a64_reg(d, A64_W), a64_cond(cond));
        return;
    }
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        emit3(g, A64_ADD, rd, ra, rb);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        emit3(g, A64_SUB, rd, ra, rb);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        emit3(g, A64_MUL, rd, ra, rb);
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
        emit3(g, is_unsigned ? A64_UDIV : A64_SDIV, rd, ra, rb);
        break;
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED: {
        // a - (a / b) * b
        A64_Operand q = a64_reg(T2, w);
        emit3(g, is_unsigned ? A64_UDIV : A64_SDIV, q, ra, rb);
        emit4(g, A64_MSUB, rd, q, rb, ra);
        break;
    }
    case TAC_BINARY_BITWISE_AND:
        emit3(g, A64_AND, rd, ra, rb);
        break;
    case TAC_BINARY_BITWISE_OR:
        emit3(g, A64_ORR, rd, ra, rb);
        break;
    case TAC_BINARY_BITWISE_XOR:
        emit3(g, A64_EOR, rd, ra, rb);
        break;
    case TAC_BINARY_LEFT_SHIFT:
        emit3(g, A64_LSL, rd, ra, rb);
        break;
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        emit3(g, is_unsigned ? A64_LSR : A64_ASR, rd, ra, rb);
        break;
    default:
        fatal_error("aarch64: floating-point operator %d on integers", op);
    }
}

// A floating-point operator: arithmetic leaves its result in v16, a comparison 0/1 in
// w9.  fcmp sets C and V for unordered operands, so mi and ls are false for a NaN where
// lt and le would not be.
static void gen_fp_binary(Gen *g, const Tac_Instruction *in, const Tac_Type *t)
{
    A64_Width w    = a64_width(t);
    A64_Operand fa = a64_reg(F0, w), fb = a64_reg(F1, w);
    load_val(g, F0, in->u.binary.src1);
    load_val(g, F1, in->u.binary.src2);
    A64_Op op = A64_RET;
    int cond  = -1;
    switch (in->u.binary.op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_DOUBLE:
        op = A64_FADD;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_DOUBLE:
        op = A64_FSUB;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_DOUBLE:
        op = A64_FMUL;
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_DOUBLE:
        op = A64_FDIV;
        break;
    case TAC_BINARY_EQUAL:
        cond = A64_EQ;
        break;
    case TAC_BINARY_NOT_EQUAL:
        cond = A64_NE;
        break;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_DOUBLE:
        cond = A64_MI;
        break;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
        cond = A64_LS;
        break;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_DOUBLE:
        cond = A64_GT;
        break;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
        cond = A64_GE;
        break;
    default:
        fatal_error("aarch64: %s: bad floating-point operator %d", gen_name(g), in->u.binary.op);
    }
    if (cond >= 0) {
        emit2(g, A64_FCMP, fa, fb);
        emit2(g, A64_CSET, a64_reg(T0, A64_W), a64_cond(cond));
        store_val(g, T0, in->u.binary.dst);
        return;
    }
    emit3(g, op, fa, fa, fb);
    store_val(g, F0, in->u.binary.dst);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    if (a64_is_fp(t)) {
        gen_fp_binary(g, in, t);
        return;
    }
    // A shift count may be of another width: it is used at the shifted value's.
    Tac_BinaryOperator op = in->u.binary.op;
    bool shift            = op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
                            op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
    load_int_as(g, T0, in->u.binary.src1, t);
    load_int_as(g, T1, in->u.binary.src2, shift ? val_type(g, in->u.binary.src2) : t);
    gen_int_binop(g, op, t->kind == TAC_TYPE_POINTER || unsigned_op(op), int_width(t), T0, T0, T1);
    store_val(g, T0, in->u.binary.dst);
}

// An int/FP or float/double conversion.  To an integer it truncates toward zero, into
// the destination's width (a narrower one is truncated by the store); the signedness of
// an integer source is the conversion's (its own type may differ, once copy propagation
// has removed a cast).
static void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (a64_is_ld(st) || a64_is_ld(dt))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    bool sfp = a64_is_fp(st), dfp = a64_is_fp(dt);
    int s = sfp ? F0 : T0, d = dfp ? F1 : T1;
    load_val(g, s, src);
    A64_Op op;
    if (sfp && dfp) {
        op = A64_FCVT;
    } else if (dfp) {
        bool u = kind == TAC_INSTRUCTION_UINT_TO_DOUBLE || kind == TAC_INSTRUCTION_UINT_TO_FLOAT;
        op     = u ? A64_UCVTF : A64_SCVTF;
    } else {
        op = a64_is_unsigned(dt) ? A64_FCVTZU : A64_FCVTZS;
    }
    emit2(g, op, a64_reg(d, dfp ? a64_width(dt) : int_width(dt)),
          a64_reg(s, sfp ? a64_width(st) : int_width(st)));
    store_val(g, d, dst);
}

// dst = &src, of a named object or function.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    int base;
    int64_t off;
    name_addr(g, src->u.var_name, T0, &base, &off);
    gen_addr(g, T0, base, off);
    store_val(g, T0, dst);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL:
        gen_label(g, in->u.label.name);
        break;
    case TAC_INSTRUCTION_JUMP:
        gen_jump(g, in->u.jump.target);
        break;
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                      in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    case TAC_INSTRUCTION_COPY:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_SIGN_EXTEND:
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_int_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, in->kind);
        break;
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst, in->kind);
        break;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        gen_get_address(g, in->u.get_address.src, in->u.get_address.dst);
        break;
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        gen_call(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("aarch64: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
