//
// Instruction selection, one TAC instruction at a time, on the operands where they live:
// every MSP430 operand may be a register or memory, so `d = a + b` is `mov a, d; add b,
// d` and a compare is a `cmp` of the operands in place.  r15 is the one scratch
// register.  An operation with a helper moves its operands into the helper's registers
// all at once, and its result out of them.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

// The assembler label of TAC label `tac`: .L<name>, its % dropped.
static char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, ".L");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
}

// The type an operation on `a` and `b` works in: a variable's, else a constant's.
static const Tac_Type *operand_type(const Gen *g, const Tac_Val *a, const Tac_Val *b)
{
    return val_type(g, a->kind == TAC_VAL_VAR || !b ? a : b);
}

static int val_size(const Gen *g, const Tac_Val *v)
{
    return msp_type_size(val_type(g, v));
}

// The first n words of value `v` (a char's byte), into w; free them with free_words.
static void get_words(const Gen *g, const Tac_Val *v, int n, Msp_Operand *w)
{
    for (int i = 0; i < n; i++)
        w[i] = val_word(g, v, i);
}

static void free_words(Msp_Operand *w, int n)
{
    for (int i = 0; i < n; i++)
        xfree(w[i].sym);
}

// Whether some word of `a` is some word of `b`.
static bool overlap(const Msp_Operand *a, int na, const Msp_Operand *b, int nb)
{
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (same_opnd(&a[i], &b[j]))
                return true;
    return false;
}

// d[i] = s[i], at once.
static void move_words(Gen *g, const Msp_Operand *d, const Msp_Operand *s, int n, bool byte)
{
    Move m[4];
    for (int i = 0; i < n; i++)
        m[i] = (Move){ msp_copy(&d[i]), msp_copy(&s[i]), byte };
    parallel_moves(g, m, n);
}

void call_helper(Gen *g, const char *name)
{
    emit1(g, MSP_CALL, msp_imm_sym(name, 0));
}

void gen_set_on(Gen *g, Msp_Op cond, const Tac_Val *d)
{
    // mov sets no flags: the result is written after the comparison.
    int n     = msp_words(val_type(g, d));
    bool byte = val_size(g, d) == 1;
    for (int i = 1; i < n; i++)
        emit1(g, MSP_CLR, val_word(g, d, i));
    char done[32];
    new_label(done);
    emit2(g, MSP_MOV, msp_imm(1), val_word(g, d, 0))->byte = byte;
    emit1(g, cond, msp_label(done));
    emit1(g, MSP_CLR, val_word(g, d, 0))->byte = byte;
    gen_label_block(g, done);
}

// `tst o`, through r15 for an immediate.
static void test_word(Gen *g, Msp_Operand o, bool byte)
{
    if (o.kind == MSP_OPND_IMM) {
        emit2(g, MSP_MOV, o, msp_reg(MSP_SCRATCH))->byte = byte;
        o = msp_reg(MSP_SCRATCH);
    }
    emit1(g, MSP_TST, o)->byte = byte;
}

// `cmp b, a` (the flags of a - b), through r15 when a is an immediate.
static void compare_words(Gen *g, Msp_Operand b, Msp_Operand a, bool byte)
{
    if (a.kind == MSP_OPND_IMM) {
        emit2(g, MSP_MOV, a, msp_reg(MSP_SCRATCH))->byte = byte;
        a = msp_reg(MSP_SCRATCH);
    }
    emit2(g, MSP_CMP, b, a)->byte = byte;
}

// The zero flag of value `v` (an integer or pointer): set when every word is zero.
static void test_zero(Gen *g, const Tac_Val *v)
{
    int n     = msp_words(val_type(g, v));
    bool byte = val_size(g, v) == 1;
    if (n == 1) {
        test_word(g, val_word(g, v, 0), byte);
        return;
    }
    char nonzero[32];
    new_label(nonzero);
    for (int i = n - 1; i >= 1; i--) {
        test_word(g, val_word(g, v, i), false);
        emit1(g, MSP_JNE, msp_label(nonzero));
    }
    test_word(g, val_word(g, v, 0), false);
    gen_label_block(g, nonzero);
}

// dst = src: a move, or an aggregate copied.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext);

static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    int size          = msp_type_size(t);
    if (!msp_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, src->u.var_name, 0, size, msp_type_align(t));
        return;
    }
    if (val_size(g, src) != size) {
        gen_convert(g, src, dst, EXT_TYPE);
        return;
    }
    int n = msp_words(t);
    Msp_Operand d[4], s[4];
    get_words(g, dst, n, d);
    get_words(g, src, n, s);
    move_words(g, d, s, n, size == 1);
    free_words(d, n);
    free_words(s, n);
}

// A width conversion: the low words of src, or src extended as `ext` says.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext)
{
    const Tac_Type *st = val_type(g, src);
    int ssize = msp_type_size(st), dsize = val_size(g, dst);
    int n     = msp_words(val_type(g, dst));
    bool sign = ext_sign(st, ext);
    Msp_Operand d[4];
    get_words(g, dst, n, d);
    if (src->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_bits(src->u.constant);
        if (ssize < 8) {
            uint64_t mask = (1ull << (8 * ssize)) - 1;
            bits          = sign && (bits >> (8 * ssize - 1) & 1) ? bits | ~mask : bits & mask;
        }
        for (int i = 0; i < n; i++)
            emit2(g, MSP_MOV, msp_imm((int64_t)(i < 4 ? bits >> (16 * i) & 0xffff : 0)),
                  msp_copy(&d[i]))
                ->byte = dsize == 1;
    } else if (dsize == 1) {
        Msp_Operand s = val_word(g, src, 0);
        if (same_opnd(&s, &d[0]))
            xfree(s.sym);
        else
            emit2b(g, MSP_MOV, s, msp_copy(&d[0]));
    } else if (ssize == 1) {
        // A char widened in a register: mov.b clears the high byte.
        bool reg      = d[0].kind == MSP_OPND_REG;
        int r         = reg ? d[0].reg : MSP_SCRATCH;
        Msp_Operand s = val_word(g, src, 0);
        if (sign && same_opnd(&s, &d[0]))
            xfree(s.sym); // sxt reads only the low byte
        else
            emit2b(g, MSP_MOV, s, msp_reg(r));
        if (sign)
            emit1(g, MSP_SXT, msp_reg(r));
        if (!reg)
            emit2(g, MSP_MOV, msp_reg(r), msp_copy(&d[0]));
        extend_words(g, d, 1, n, sign);
    } else {
        int m = ssize / 2 < n ? ssize / 2 : n;
        Msp_Operand s[4];
        get_words(g, src, m, s);
        move_words(g, d, s, m, false);
        free_words(s, m);
        extend_words(g, d, m, n, sign);
    }
    free_words(d, n);
}

// dst = a op b, word by word: `first` on the low word, `rest` up the chain.  In place
// in dst, unless dst is where b is (a commutative op swaps a and b then); else in r15,
// each word pushed until all are done.
static void gen_arith(Gen *g, Msp_Op first, Msp_Op rest, bool commutative, const Tac_Val *a,
                      const Tac_Val *b, const Tac_Val *dst)
{
    int n     = msp_words(val_type(g, dst));
    bool byte = val_size(g, dst) == 1;
    Msp_Operand d[4], wa[4], wb[4];
    get_words(g, dst, n, d);
    get_words(g, a, n, wa);
    get_words(g, b, n, wb);
    if (overlap(d, n, wb, n) && commutative && !overlap(d, n, wa, n)) {
        const Tac_Val *t = a;
        a = b, b = t;
        free_words(wa, n);
        free_words(wb, n);
        get_words(g, a, n, wa);
        get_words(g, b, n, wb);
    }
    if (!overlap(d, n, wb, n)) {
        move_words(g, d, wa, n, byte);
        for (int i = 0; i < n; i++)
            emit2(g, i == 0 ? first : rest, msp_copy(&wb[i]), msp_copy(&d[i]))->byte = byte;
    } else if (n == 1) {
        emit2(g, MSP_MOV, msp_copy(&wa[0]), msp_reg(MSP_SCRATCH))->byte = byte;
        emit2(g, first, msp_copy(&wb[0]), msp_reg(MSP_SCRATCH))->byte = byte;
        emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), msp_copy(&d[0]))->byte = byte;
    } else {
        // mov and push leave the carry alone.
        int bias = g->sp_bias;
        for (int i = 0; i < n; i++) {
            g->sp_bias = bias + 2 * i;
            emit2(g, MSP_MOV, val_word(g, a, i), msp_reg(MSP_SCRATCH));
            emit2(g, i == 0 ? first : rest, val_word(g, b, i), msp_reg(MSP_SCRATCH));
            emit1(g, MSP_PUSH, msp_reg(MSP_SCRATCH));
        }
        for (int i = n - 1; i >= 0; i--) {
            emit1(g, MSP_POP, msp_reg(MSP_SCRATCH));
            g->sp_bias = bias + 2 * i;
            emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), val_word(g, dst, i));
        }
        g->sp_bias = bias;
    }
    free_words(d, n);
    free_words(wa, n);
    free_words(wb, n);
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (msp_is_fp(val_type(g, src))) {
        gen_fp_unary(g, in);
        return;
    }
    int n     = msp_words(val_type(g, dst));
    bool byte = val_size(g, dst) == 1;
    Msp_Operand d[4], s[4];
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        get_words(g, dst, n, d);
        get_words(g, src, n, s);
        move_words(g, d, s, n, byte);
        for (int i = 0; i < n; i++)
            emit1(g, MSP_INV, msp_copy(&d[i]))->byte = byte;
        if (in->u.unary.op == TAC_UNARY_NEGATE || in->u.unary.op == TAC_UNARY_NEGATE_UNSIGNED) {
            emit1(g, MSP_INC, msp_copy(&d[0]))->byte = byte;
            for (int i = 1; i < n; i++)
                emit1(g, MSP_ADC, msp_copy(&d[i]));
        }
        free_words(d, n);
        free_words(s, n);
        break;
    case TAC_UNARY_NOT:
        test_zero(g, src);
        gen_set_on(g, MSP_JEQ, dst);
        break;
    default:
        internal_error("msp430: %s: unary operator %d is not implemented", gen_name(g),
                       in->u.unary.op);
    }
}

typedef enum { SHL, SHR, SAR } Shift;

// One bit of shift `sh` over d[0..n-1].
static void shift_step(Gen *g, Shift sh, const Msp_Operand *d, int n, bool byte)
{
    if (sh == SHL) {
        emit1(g, MSP_RLA, msp_copy(&d[0]))->byte = byte;
        for (int i = 1; i < n; i++)
            emit1(g, MSP_RLC, msp_copy(&d[i]));
        return;
    }
    if (sh == SAR) {
        emit1(g, MSP_RRA, msp_copy(&d[n - 1]))->byte = byte;
    } else {
        emit0(g, MSP_CLRC);
        emit1(g, MSP_RRC, msp_copy(&d[n - 1]))->byte = byte;
    }
    for (int i = n - 2; i >= 0; i--)
        emit1(g, MSP_RRC, msp_copy(&d[i]));
}

// `count` (r15) steps of shift `sh`; none when it is zero.
static void shift_loop(Gen *g, Shift sh, const Msp_Operand *d, int n, bool byte)
{
    char loop[32], done[32];
    new_label(loop);
    new_label(done);
    emit1(g, MSP_TST, msp_reg(MSP_SCRATCH));
    emit1(g, MSP_JEQ, msp_label(done));
    gen_label_block(g, loop);
    shift_step(g, sh, d, n, byte);
    emit1(g, MSP_DEC, msp_reg(MSP_SCRATCH));
    emit1(g, MSP_JNE, msp_label(loop));
    gen_label_block(g, done);
}

// d[0..n-1] shifted by constant k: whole words moved first, then bit by bit.
static void shift_const(Gen *g, Shift sh, const Msp_Operand *d, int n, bool byte, int k)
{
    int words = byte ? 0 : k / 16;
    if (byte && k >= 8)
        k = sh == SAR ? 7 : 8;
    if (words >= n) {
        if (sh == SAR) {
            words = n - 1;
            k     = 16 * n - 1;
        } else {
            for (int i = 0; i < n; i++)
                emit1(g, MSP_CLR, msp_copy(&d[i]));
            return;
        }
    }
    if (words > 0) {
        if (sh == SHL) {
            for (int i = n - 1; i >= words; i--)
                emit2(g, MSP_MOV, msp_copy(&d[i - words]), msp_copy(&d[i]));
            for (int i = 0; i < words; i++)
                emit1(g, MSP_CLR, msp_copy(&d[i]));
        } else {
            for (int i = 0; i < n - words; i++)
                emit2(g, MSP_MOV, msp_copy(&d[i + words]), msp_copy(&d[i]));
            extend_words(g, d, n - words, n, sh == SAR);
        }
    }
    int bits = k % 16;
    if (byte && sh == SHL && bits >= 8) {
        emit1b(g, MSP_CLR, msp_copy(&d[0]));
        return;
    }
    if (bits > 3) {
        emit2(g, MSP_MOV, msp_imm(bits), msp_reg(MSP_SCRATCH));
        shift_loop(g, sh, d, n, byte);
        return;
    }
    for (int i = 0; i < bits; i++)
        shift_step(g, sh, d, n, byte);
}

static void gen_shift(Gen *g, const Tac_Instruction *in, Shift sh)
{
    const Tac_Val *count = in->u.binary.src2, *dst = in->u.binary.dst;
    int n                = msp_words(val_type(g, dst));
    bool byte            = val_size(g, dst) == 1;
    // The count first: dst may be where it lives.
    if (count->kind != TAC_VAL_CONSTANT)
        emit2(g, MSP_MOV, val_word(g, count, 0), msp_reg(MSP_SCRATCH))->byte =
            val_size(g, count) == 1;
    Msp_Operand d[4], s[4];
    get_words(g, dst, n, d);
    get_words(g, in->u.binary.src1, n, s);
    move_words(g, d, s, n, byte);
    if (count->kind == TAC_VAL_CONSTANT)
        shift_const(g, sh, d, n, byte, (int)(const_bits(count->u.constant) & 0xff));
    else
        shift_loop(g, sh, d, n, byte);
    free_words(d, n);
    free_words(s, n);
}

// A 16-bit multiply by a constant, in canonical signed digits: digit[i] of `v` is -1, 0
// or 1, no two adjacent nonzero, `top` the highest nonzero one; the product negated at
// the end when `neg`. The cost is one instruction a digit position, plus two for `neg`.
typedef struct {
    signed char digit[17];
    int top;
    bool neg;
} MulPlan;

static int naf(unsigned v, signed char *digit)
{
    int top = -1;
    for (int i = 0; v; i++, v >>= 1) {
        digit[i] = 0;
        if (v & 1) {
            digit[i] = (v & 3) == 3 ? -1 : 1;
            v -= digit[i];
            top = i;
        }
    }
    return top;
}

static int plan_cost(const MulPlan *p)
{
    int n = p->top + (p->neg ? 2 : 0);
    for (int i = 0; i < p->top; i++)
        n += p->digit[i] != 0;
    return n;
}

// The cheaper chain for a * v, v of 16 bits: plain binary, or signed digits when they
// fit 16 bits and cost less (they cost a shift more at the top, so binary wins for 3).
static MulPlan plan_for(unsigned v, bool neg)
{
    MulPlan bin = { .top = -1, .neg = neg }, csd = { .neg = neg };
    for (int i = 0; i < 16; i++)
        if ((bin.digit[i] = v >> i & 1))
            bin.top = i;
    csd.top = naf(v, csd.digit);
    return csd.top <= 15 && plan_cost(&csd) < plan_cost(&bin) ? csd : bin;
}

// The cheapest Horner chain starting at a +1 digit: for a * k, or for -(a * -k).
static MulPlan mul_plan(uint64_t k)
{
    k &= 0xffff;
    MulPlan pos = plan_for((unsigned)k, false);
    MulPlan neg = plan_for((unsigned)(0x10000 - k) & 0xffff, true);
    return plan_cost(&neg) < plan_cost(&pos) ? neg : pos;
}

// r15 = a * k, by Horner's rule over the signed digits: a is only read, so it may be
// anywhere.
static void mul_const(Gen *g, const Tac_Val *a, uint64_t k)
{
    MulPlan p = mul_plan(k);
    if (p.top < 0) {
        emit1(g, MSP_CLR, msp_reg(MSP_SCRATCH));
        return;
    }
    emit2(g, MSP_MOV, val_word(g, a, 0), msp_reg(MSP_SCRATCH));
    for (int i = p.top - 1; i >= 0; i--) {
        emit1(g, MSP_RLA, msp_reg(MSP_SCRATCH));
        if (p.digit[i])
            emit2(g, p.digit[i] > 0 ? MSP_ADD : MSP_SUB, val_word(g, a, 0),
                  msp_reg(MSP_SCRATCH));
    }
    if (p.neg) {
        emit1(g, MSP_INV, msp_reg(MSP_SCRATCH));
        emit1(g, MSP_INC, msp_reg(MSP_SCRATCH));
    }
}

// The constant factor of a 16-bit multiply done inline, its other operand in *a; or
// false.
static bool inline_multiply(const Gen *g, const Tac_Instruction *in, const Tac_Val **a,
                            uint64_t *k)
{
    Tac_BinaryOperator op = in->u.binary.op;
    if (op != TAC_BINARY_MULTIPLY && op != TAC_BINARY_MULTIPLY_UNSIGNED)
        return false;
    if (msp_type_size(val_type(g, in->u.binary.dst)) != 2)
        return false;
    const Tac_Val *x = in->u.binary.src1, *y = in->u.binary.src2;
    if (x->kind == TAC_VAL_CONSTANT) {
        const Tac_Val *t = x;
        x = y, y = t;
    }
    if (y->kind != TAC_VAL_CONSTANT || x->kind == TAC_VAL_CONSTANT)
        return false;
    *a = x;
    *k = const_bits(y->u.constant);
    return true;
}

// The row of the __mspabi_ multiply and divide helpers of `op`.
static int arith_row(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        return 0;
    case TAC_BINARY_DIVIDE:
        return 1;
    case TAC_BINARY_DIVIDE_UNSIGNED:
        return 2;
    case TAC_BINARY_REMAINDER:
        return 3;
    case TAC_BINARY_REMAINDER_UNSIGNED:
        return 4;
    default:
        return -1;
    }
}

// A multiply, divide or remainder through the runtime, the __mspabi_ helpers GCC's code
// calls: operands in r12 and r13, or r13:r12 and r15:r14, or for 64 bits r11:r8 and
// r15:r12; the result in r12 up.
static void arith_helper(Gen *g, const Tac_Instruction *in, int n)
{
    static const char *const names[][3] = {
        // 16, 32, 64 bits
        { "__mspabi_mpyi", "__mspabi_mpyl", "__mspabi_mpyll" },
        { "__mspabi_divi", "__mspabi_divli", "__mspabi_divlli" },
        { "__mspabi_divu", "__mspabi_divul", "__mspabi_divull" },
        { "__mspabi_remi", "__mspabi_remli", "__mspabi_remlli" },
        { "__mspabi_remu", "__mspabi_remul", "__mspabi_remull" },
    };
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    Load l[2]        = { { a, n == 4 ? 8 : 12, n, EXT_TYPE },
                         { b, n == 4 ? 12 : n == 1 ? 13 : 14, n, EXT_TYPE } };
    load_vals(g, l, 2);
    call_helper(g, names[arith_row(in->u.binary.op)][n == 1 ? 0 : n == 2 ? 1 : 2]);
    store_val(g, in->u.binary.dst, 12, n);
}

// A compare and the jump it feeds: the condition, after swapping the operands of > and
// <= (a > b is b < a).
typedef enum { C_EQ, C_NE, C_LT, C_GE } Cond;

static bool compare_cond(Tac_BinaryOperator op, Cond *c, bool *swap)
{
    *swap = false;
    switch (op) {
    case TAC_BINARY_EQUAL:
        *c = C_EQ;
        return true;
    case TAC_BINARY_NOT_EQUAL:
        *c = C_NE;
        return true;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        *c = C_LT;
        return true;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        *c = C_GE;
        return true;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        *c = C_LT, *swap = true;
        return true;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        *c = C_GE, *swap = true;
        return true;
    default:
        return false;
    }
}

static bool unsigned_compare(Tac_BinaryOperator op)
{
    return op == TAC_BINARY_LESS_THAN_UNSIGNED || op == TAC_BINARY_LESS_OR_EQUAL_UNSIGNED ||
           op == TAC_BINARY_GREATER_THAN_UNSIGNED || op == TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED;
}

// The jump of a one-word comparison in relation `c`.
static Msp_Op cond_jump(Cond c, bool is_unsigned)
{
    switch (c) {
    case C_EQ:
        return MSP_JEQ;
    case C_NE:
        return MSP_JNE;
    case C_LT:
        return is_unsigned ? MSP_JLO : MSP_JL;
    default:
        return is_unsigned ? MSP_JHS : MSP_JGE;
    }
}

// Jump to `target` when a and b (n words) stand in relation `c`; fall through
// otherwise.  The high words decide first, signed or not; the rest unsigned.
static void compare_jump(Gen *g, const Tac_Val *a, const Tac_Val *b, int n, bool byte, Cond c,
                         bool is_unsigned, const char *target)
{
    char skip[32];
    new_label(skip);
    bool used_skip = false;
    for (int i = n - 1; i >= 0; i--) {
        compare_words(g, val_word(g, b, i), val_word(g, a, i), byte);
        bool top = i == n - 1, last = i == 0;
        Msp_Op lt = top && !is_unsigned ? MSP_JL : MSP_JLO;
        Msp_Op ge = top && !is_unsigned ? MSP_JGE : MSP_JHS;
        switch (c) {
        case C_EQ:
            emit1(g, last ? MSP_JEQ : MSP_JNE, msp_label(last ? target : skip));
            used_skip |= !last;
            break;
        case C_NE:
            emit1(g, MSP_JNE, msp_label(target));
            break;
        case C_LT:
            emit1(g, lt, msp_label(target));
            if (!last) {
                emit1(g, MSP_JNE, msp_label(skip));
                used_skip = true;
            }
            break;
        case C_GE:
            if (last) {
                emit1(g, ge, msp_label(target));
            } else {
                emit1(g, lt, msp_label(skip));
                emit1(g, MSP_JNE, msp_label(target));
                used_skip = true;
            }
            break;
        }
    }
    if (used_skip)
        gen_label_block(g, skip);
}

// A one-word comparison a c b into flags, and the jump that tests them.  A constant a
// turns around, since cmp cannot write an immediate: k == b is b == k, k < b is
// b >= k+1, k >= b is b < k+1 (through r15 when k+1 overflows).
static Msp_Op compare_one(Gen *g, const Tac_Val *a, const Tac_Val *b, Cond c, bool is_unsigned,
                          bool byte)
{
    Msp_Operand wa = val_word(g, a, 0), wb = val_word(g, b, 0);
    if (wa.kind == MSP_OPND_IMM && !wa.sym && wb.kind != MSP_OPND_IMM) {
        int64_t k   = msp_imm_value(wa.imm, byte);
        int64_t max = is_unsigned ? (byte ? 0xff : 0xffff) : (byte ? 0x7f : 0x7fff);
        if (is_unsigned)
            k &= byte ? 0xff : 0xffff;
        if (c == C_EQ || c == C_NE || k < max) {
            Msp_Operand t = wa;
            wa            = wb;
            wb            = t;
            if (c == C_LT || c == C_GE) {
                wb.imm = k + 1;
                c      = c == C_LT ? C_GE : C_LT;
            }
        }
    }
    compare_words(g, wb, wa, byte);
    return cond_jump(c, is_unsigned);
}

static Cond inverse_cond(Cond c)
{
    return c == C_EQ ? C_NE : c == C_NE ? C_EQ : c == C_LT ? C_GE : C_LT;
}

static void gen_compare(Gen *g, const Tac_Instruction *in, Cond c, bool swap, const Tac_Type *t)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *dst = in->u.binary.dst;
    if (swap) {
        const Tac_Val *x = a;
        a                = b;
        b                = x;
    }
    int n            = msp_words(t);
    bool byte        = msp_type_size(t) == 1;
    bool is_unsigned = unsigned_compare(in->u.binary.op) || t->kind == TAC_TYPE_POINTER;
    if (n == 1) {
        gen_set_on(g, compare_one(g, a, b, c, is_unsigned, byte), dst);
        return;
    }
    char yes[32], done[32];
    new_label(yes);
    new_label(done);
    compare_jump(g, a, b, n, byte, c, is_unsigned, yes);
    bool dbyte = val_size(g, dst) == 1;
    emit1(g, MSP_CLR, val_word(g, dst, 0))->byte = dbyte;
    emit1(g, MSP_JMP, msp_label(done));
    gen_label_block(g, yes);
    emit2(g, MSP_MOV, msp_imm(1), val_word(g, dst, 0))->byte = dbyte;
    gen_label_block(g, done);
    for (int i = 1; i < msp_words(val_type(g, dst)); i++)
        emit1(g, MSP_CLR, val_word(g, dst, i));
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Type *t     = operand_type(g, in->u.binary.src1, in->u.binary.src2);
    if (msp_is_fp(t)) {
        gen_fp_binary(g, in);
        return;
    }
    Cond c;
    bool swap;
    if (compare_cond(op, &c, &swap)) {
        gen_compare(g, in, c, swap, t);
        return;
    }
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *d = in->u.binary.dst;
    switch (op) {
    case TAC_BINARY_LEFT_SHIFT:
        gen_shift(g, in, SHL);
        return;
    case TAC_BINARY_RIGHT_SHIFT:
        gen_shift(g, in, SAR);
        return;
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL:
        gen_shift(g, in, SHR);
        return;
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        gen_arith(g, MSP_ADD, MSP_ADDC, true, a, b, d);
        return;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        gen_arith(g, MSP_SUB, MSP_SUBC, false, a, b, d);
        return;
    case TAC_BINARY_BITWISE_AND:
        gen_arith(g, MSP_AND, MSP_AND, true, a, b, d);
        return;
    case TAC_BINARY_BITWISE_OR:
        gen_arith(g, MSP_BIS, MSP_BIS, true, a, b, d);
        return;
    case TAC_BINARY_BITWISE_XOR:
        gen_arith(g, MSP_XOR, MSP_XOR, true, a, b, d);
        return;
    default:
        break;
    }
    if (arith_row(op) < 0)
        internal_error("msp430: %s: binary operator %d is not implemented", gen_name(g), op);
    const Tac_Val *x;
    uint64_t k;
    if (inline_multiply(g, in, &x, &k)) {
        mul_const(g, x, k);
        emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), val_word(g, d, 0));
        return;
    }
    arith_helper(g, in, msp_words(val_type(g, d)));
}

int instr_out_size(const Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        return msp_stack_builtin(in) ? 0 : call_stack_size(g, in);
    case TAC_INSTRUCTION_BINARY: {
        // A binary64 comparison takes its second operand on the stack.
        const Tac_Type *t = operand_type(g, in->u.binary.src1, in->u.binary.src2);
        return msp_is_fp(t) ? fp_out_size(in->u.binary.op, msp_type_size(t)) : 0;
    }
    default:
        return 0;
    }
}

// log2 of `scale`, or -1 when it is no power of two.
static int scale_shift(int scale)
{
    for (int k = 0; k < 15; k++)
        if (1 << k == scale)
            return k;
    return -1;
}

bool uses_helper(const Gen *g, const Tac_Instruction *in, bool *r8)
{
    *r8 = false;
    switch (in->kind) {
    case TAC_INSTRUCTION_BINARY: {
        const Tac_Type *t = operand_type(g, in->u.binary.src1, in->u.binary.src2);
        int size          = msp_type_size(t);
        if (msp_is_fp(t)) {
            *r8 = size == 8 && fp_arith(in->u.binary.op, size);
            return true;
        }
        const Tac_Val *x;
        uint64_t k;
        if (arith_row(in->u.binary.op) < 0 || inline_multiply(g, in, &x, &k))
            return false;
        *r8 = msp_type_size(val_type(g, in->u.binary.dst)) == 8;
        return true;
    }
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        return true;
    case TAC_INSTRUCTION_ADD_PTR:
        return false;
    default:
        return false;
    }
}

// The type `ptr` points to, or NULL when unknown.
static const Tac_Type *pointee(const Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// The width of a load or store through `ptr` of value `v`: the pointee's when known,
// else the value's.
static int access_size(const Gen *g, const Tac_Val *ptr, const Tac_Val *v)
{
    const Tac_Type *t = pointee(g, ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (t->kind == TAC_TYPE_STRUCTURE && t->u.structure.size == 0))
        t = val_type(g, v);
    return msp_type_size(t);
}

// The register pointer `p` is in: its own, or r15 loaded with it.
static int ptr_reg(Gen *g, const Tac_Val *p)
{
    if (p->kind == TAC_VAL_VAR) {
        int r = var_reg(g, p->u.var_name, 0);
        if (r)
            return r;
    }
    emit2(g, MSP_MOV, val_word(g, p, 0), msp_reg(MSP_SCRATCH));
    return MSP_SCRATCH;
}

// dst = *ptr, `size` bytes: as much as the destination holds (the pointee may be wider,
// a row of a 2-D array) and its rest zeroed.
static void gen_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, int size)
{
    const Tac_Type *t = val_type(g, dst);
    int base          = ptr_reg(g, ptr);
    if (!msp_is_scalar(t)) {
        copy_ptr(g, true, base, dst->u.var_name, 0, size, msp_type_align(t));
        return;
    }
    int dsize = msp_type_size(t), n = msp_words(t);
    Msp_Operand d[4];
    get_words(g, dst, n, d);
    if (size == 1 || dsize == 1) {
        emit2b(g, MSP_MOV, msp_ind(base), msp_copy(&d[0]));
        if (dsize > 1 && d[0].kind != MSP_OPND_REG)
            emit1b(g, MSP_CLR, high_byte(&d[0]));
        for (int i = 1; i < n; i++)
            emit1(g, MSP_CLR, msp_copy(&d[i]));
        free_words(d, n);
        return;
    }
    // The word into the pointer's own register last.
    int last = -1;
    for (int i = 0; i < n; i++)
        if (d[i].kind == MSP_OPND_REG && d[i].reg == base)
            last = i;
    for (int k = 0; k <= n; k++) {
        int i = k < n ? k : last;
        if ((k < n && i == last) || i < 0)
            continue;
        if (2 * i < size)
            emit2(g, MSP_MOV, msp_indexed(base, NULL, 2 * i), msp_copy(&d[i]));
        else
            emit1(g, MSP_CLR, msp_copy(&d[i]));
    }
    free_words(d, n);
}

// *ptr = src, `size` bytes.
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, int size)
{
    const Tac_Type *t = val_type(g, src);
    int base          = ptr_reg(g, ptr);
    if (src->kind == TAC_VAL_VAR && !msp_is_scalar(t)) {
        copy_ptr(g, false, base, src->u.var_name, 0, size, msp_type_align(t));
        return;
    }
    int ssize = msp_type_size(t);
    if (size == 1) {
        emit2b(g, MSP_MOV, val_word(g, src, 0), msp_indexed(base, NULL, 0));
        return;
    }
    // A narrower value into a wider pointee is extended there.
    int n     = size / 2;
    bool sign = ext_sign(t, EXT_TYPE);
    Msp_Operand d[4];
    for (int i = 0; i < n; i++)
        d[i] = msp_indexed(base, NULL, 2 * i);
    int m;
    if (ssize == 1) {
        emit2b(g, MSP_MOV, val_word(g, src, 0), msp_copy(&d[0]));
        emit1b(g, MSP_CLR, msp_indexed(base, NULL, 1));
        if (sign)
            emit1(g, MSP_SXT, msp_copy(&d[0]));
        m = 1;
    } else {
        m = ssize / 2 < n ? ssize / 2 : n;
        for (int i = 0; i < m; i++)
            emit2(g, MSP_MOV, val_word(g, src, i), msp_copy(&d[i]));
    }
    extend_words(g, d, m, n, sign);
    free_words(d, n);
}

// dst = ptr + index * scale.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index, *p = in->u.add_ptr.ptr, *dst = in->u.add_ptr.dst;
    int scale            = in->u.add_ptr.scale;
    Msp_Operand d = val_word(g, dst, 0), wp = val_word(g, p, 0);
    if (index->kind == TAC_VAL_CONSTANT) {
        int off = (int)(const_bits(index->u.constant) * (uint64_t)scale);
        if (!same_opnd(&d, &wp))
            emit2(g, MSP_MOV, msp_copy(&wp), msp_copy(&d));
        // -2, -4 and -8 as a subtraction, whose constant the generator has.
        int neg = -off & 0xffff;
        if (neg == 2 || neg == 4 || neg == 8)
            emit2(g, MSP_SUB, msp_imm(neg), msp_copy(&d));
        else if (off & 0xffff)
            emit2(g, MSP_ADD, msp_imm(off & 0xffff), msp_copy(&d));
    } else if (scale_shift(scale) >= 0) {
        // The scaled index in dst when it is a register apart from the pointer, else r15.
        int k             = scale_shift(scale);
        bool in_place     = d.kind == MSP_OPND_REG && !same_opnd(&d, &wp);
        Msp_Operand r     = in_place ? msp_copy(&d) : msp_reg(MSP_SCRATCH);
        Msp_Operand index0 = val_word(g, index, 0);
        if (same_opnd(&index0, &r))
            xfree(index0.sym);
        else
            emit2(g, MSP_MOV, index0, msp_copy(&r));
        for (int i = 0; i < k; i++)
            emit1(g, MSP_RLA, msp_copy(&r));
        emit2(g, MSP_ADD, msp_copy(&wp), msp_copy(&r));
        if (!in_place)
            emit2(g, MSP_MOV, msp_copy(&r), msp_copy(&d));
        xfree(r.sym);
    } else {
        mul_const(g, index, scale);
        emit2(g, MSP_ADD, msp_copy(&wp), msp_reg(MSP_SCRATCH));
        emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), msp_copy(&d));
    }
    xfree(d.sym);
    xfree(wp.sym);
}

// Member `offset` of aggregate `name` = src, of `size` bytes.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *name, int offset, int size)
{
    const Tac_Type *t = val_type(g, src);
    if (src->kind == TAC_VAL_VAR && !msp_is_scalar(t)) {
        copy_named(g, name, offset, src->u.var_name, 0, size, msp_type_align(t));
        return;
    }
    if (size == 1) {
        emit2b(g, MSP_MOV, val_word(g, src, 0), mem_at(g, name, offset));
        return;
    }
    for (int i = 0; i < size / 2; i++)
        emit2(g, MSP_MOV, val_word(g, src, i), mem_at(g, name, offset + 2 * i));
}

// dst = member `offset` of aggregate `name`, of `size` bytes.
static void gen_copy_from_offset(Gen *g, const char *name, int offset, const Tac_Val *dst,
                                 int size)
{
    const Tac_Type *t = val_type(g, dst);
    if (!msp_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, name, offset, size, msp_type_align(t));
        return;
    }
    if (is_byref(g, name))
        load_byref(g, name);
    if (size == 1) {
        emit2b(g, MSP_MOV, mem_at(g, name, offset), mem_at(g, dst->u.var_name, 0));
        return;
    }
    for (int i = 0; i < size / 2; i++)
        emit2(g, MSP_MOV, mem_at(g, name, offset + 2 * i), mem_at(g, dst->u.var_name, 2 * i));
}

// Branch to TAC label `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    if (msp_is_fp(val_type(g, cond)))
        gen_fp_test(g, cond);
    else
        test_zero(g, cond);
    char *l = label_name(target);
    emit1(g, if_zero ? MSP_JEQ : MSP_JNE, msp_label(l));
    xfree(l);
}

bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (!g->uses || !next ||
        (next->kind != TAC_INSTRUCTION_JUMP_IF_ZERO && next->kind != TAC_INSTRUCTION_JUMP_IF_NOT_ZERO))
        return false;
    const Tac_Val *dst;
    if (in->kind == TAC_INSTRUCTION_BINARY)
        dst = in->u.binary.dst;
    else if (in->kind == TAC_INSTRUCTION_UNARY && in->u.unary.op == TAC_UNARY_NOT)
        dst = in->u.unary.dst;
    else
        return false;
    const Tac_Val *c = next->u.jump_if_zero.condition;
    if (c->kind != TAC_VAL_VAR || strcmp(c->u.var_name, dst->u.var_name) != 0)
        return false;
    int v = flow_var(g->flow, dst->u.var_name);
    if (v < 0 || g->uses[v] != 1 || flow_has(g->flow->in_memory, v))
        return false;
    bool if_zero = next->kind == TAC_INSTRUCTION_JUMP_IF_ZERO;
    char *l      = label_name(next->u.jump_if_zero.target);
    if (in->kind == TAC_INSTRUCTION_UNARY) {
        // if (!x): the jump on x's zero flag, the other way round.
        const Tac_Val *src = in->u.unary.src;
        if (msp_is_fp(val_type(g, src)))
            gen_fp_test(g, src);
        else
            test_zero(g, src);
        emit1(g, if_zero ? MSP_JNE : MSP_JEQ, msp_label(l));
        xfree(l);
        return true;
    }
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    const Tac_Type *t = operand_type(g, a, b);
    Cond cond;
    bool swap;
    if (msp_is_fp(t)) {
        Msp_Op jump = gen_fp_compare(g, in);
        if (jump == MSP_NUM_OPS) {
            xfree(l);
            return false; // nothing emitted
        }
        emit1(g, if_zero ? msp_inverse(jump) : jump, msp_label(l));
        xfree(l);
        return true;
    }
    if (!compare_cond(op, &cond, &swap)) {
        xfree(l);
        return false;
    }
    if (swap) {
        const Tac_Val *x = a;
        a = b, b = x;
    }
    if (if_zero)
        cond = inverse_cond(cond);
    int n            = msp_words(t);
    bool byte        = msp_type_size(t) == 1;
    bool is_unsigned = unsigned_compare(op) || t->kind == TAC_TYPE_POINTER;
    if (n == 1)
        emit1(g, compare_one(g, a, b, cond, is_unsigned, byte), msp_label(l));
    else
        compare_jump(g, a, b, n, byte, cond, is_unsigned, l);
    xfree(l);
    return true;
}

void gen_instr(Gen *g, const Tac_Instruction *in, bool last)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src, last);
        break;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_SIGN_EXTEND:
        gen_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, EXT_SIGN);
        break;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_convert(g, in->u.zero_extend.src, in->u.zero_extend.dst, EXT_ZERO);
        break;
    case TAC_INSTRUCTION_TRUNCATE:
        gen_convert(g, in->u.truncate.src, in->u.truncate.dst, EXT_TYPE);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst, in->kind);
        break;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_LABEL: {
        char *l = label_name(in->u.label.name);
        gen_label_block(g, l);
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP: {
        char *l = label_name(in->u.jump.target);
        emit1(g, MSP_JMP, msp_label(l));
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                      in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_JUMP_TABLE:
        internal_error("msp430: a jump table, which only coroutines make (wasm32)");
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        if (in->u.get_address.src->kind != TAC_VAL_VAR)
            internal_error("msp430: %s: the address of a constant", gen_name(g));
        address_of(g, val_word(g, in->u.get_address.dst, 0), in->u.get_address.src->u.var_name,
                   0);
        break;
    case TAC_INSTRUCTION_LOAD:
        gen_load(g, in->u.load.src_ptr, in->u.load.dst,
                 access_size(g, in->u.load.src_ptr, in->u.load.dst));
        break;
    case TAC_INSTRUCTION_LOAD_BYTE:
        gen_load(g, in->u.load.src_ptr, in->u.load.dst, 1);
        break;
    case TAC_INSTRUCTION_STORE:
        gen_store(g, in->u.store.src, in->u.store.dst_ptr,
                  access_size(g, in->u.store.dst_ptr, in->u.store.src));
        break;
    case TAC_INSTRUCTION_STORE_BYTE:
        gen_store(g, in->u.store.src, in->u.store.dst_ptr, 1);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        gen_add_ptr(g, in);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        gen_arith(g, MSP_SUB, MSP_SUBC, false, in->u.ptr_diff.ptr_a, in->u.ptr_diff.ptr_b,
                  in->u.ptr_diff.dst);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset,
                           msp_type_size(val_type(g, in->u.copy_to_offset.src)));
        break;
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset, 1);
        break;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
        gen_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                             in->u.copy_from_offset.dst,
                             msp_type_size(val_type(g, in->u.copy_from_offset.dst)));
        break;
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        gen_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                             in->u.copy_from_offset.dst, 1);
        break;
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        gen_call(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    }
}
