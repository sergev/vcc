//
// Instruction selection: one TAC instruction at a time, naive.  Operands are loaded
// into register blocks A and B (block_a, block_b), byte-serial chains compute in place,
// and the result is stored back.
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

// op a+i, b+i for each byte: `first` on the low byte, `rest` up the chain.
static void chain(Gen *g, AVR_Op first, AVR_Op rest, int a, int b, int n)
{
    for (int i = 0; i < n; i++)
        emit2(g, i == 0 ? first : rest, avr_reg(a + i), avr_reg(b + i));
}

// The zero flag of a..a+n-1: set when every byte is zero.
static void test_zero(Gen *g, int a, int n)
{
    for (int i = 0; i < n; i++)
        emit2(g, i == 0 ? AVR_CP : AVR_CPC, avr_reg(a + i), avr_reg(AVR_ZERO));
}

void gen_set_on(Gen *g, AVR_Op br)
{
    char done[32];
    new_label(done);
    emit2(g, AVR_LDI, avr_reg(24), avr_imm(1));
    emit1(g, br, avr_label(done));
    emit1(g, AVR_CLR, avr_reg(24));
    gen_label_block(g, done);
}

// dst = src, a scalar.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!avr_is_scalar(t))
        fatal_error("avr: %s: copying an aggregate is not implemented yet", gen_name(g));
    int size = avr_type_size(t);
    int a    = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    store_val(g, dst, a, size);
}

// dst = the address of named object `name`: Y+q for a slot, lo8/hi8 of a data
// symbol, pm_lo8/pm_hi8 of a function (a word address in flash).
static void gen_get_address(Gen *g, const char *name, const Tac_Val *dst)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        emit2(g, AVR_MOVW, avr_reg(24), avr_reg(AVR_Y));
        if (s->q <= Y_MAX) {
            emit2(g, AVR_ADIW, avr_reg(24), avr_imm(s->q));
        } else {
            emit2(g, AVR_SUBI, avr_reg(24), avr_imm(-s->q & 0xff));
            emit2(g, AVR_SBCI, avr_reg(25), avr_imm((-s->q >> 8) & 0xff));
        }
    } else {
        bool fn = is_function(g, name);
        emit2(g, AVR_LDI, avr_reg(24), avr_sym(fn ? AVR_MOD_PM_LO8 : AVR_MOD_LO8, name, 0));
        emit2(g, AVR_LDI, avr_reg(25), avr_sym(fn ? AVR_MOD_PM_HI8 : AVR_MOD_HI8, name, 0));
    }
    store_val(g, dst, 24, 2);
}

// A width conversion: the low bytes of src, or src extended as `ext` says.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext)
{
    int size = avr_type_size(val_type(g, dst));
    int a    = block_a(size);
    load_val(g, src, a, size, ext);
    store_val(g, dst, a, size);
}

// a..a+n-1 = -(a..a+n-1): the complement plus one, as com on the high bytes, neg on
// the low one, and its borrow carried up by sbci 0xff (a is an upper register).
static void negate(Gen *g, int a, int n)
{
    for (int i = n - 1; i > 0; i--)
        emit1(g, AVR_COM, avr_reg(a + i));
    emit1(g, AVR_NEG, avr_reg(a));
    for (int i = 1; i < n; i++)
        emit2(g, AVR_SBCI, avr_reg(a + i), avr_imm(0xff));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (avr_is_fp(val_type(g, src))) {
        gen_fp_unary(g, in);
        return;
    }
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED: {
        int n = avr_type_size(val_type(g, dst)), a = block_a(n);
        load_val(g, src, a, n, EXT_TYPE);
        negate(g, a, n);
        store_val(g, dst, a, n);
        break;
    }
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED: {
        int n = avr_type_size(val_type(g, dst)), a = block_a(n);
        load_val(g, src, a, n, EXT_TYPE);
        for (int i = 0; i < n; i++)
            emit1(g, AVR_COM, avr_reg(a + i));
        store_val(g, dst, a, n);
        break;
    }
    case TAC_UNARY_NOT: {
        int n = avr_type_size(val_type(g, src)), a = block_a(n);
        load_val(g, src, a, n, EXT_TYPE);
        test_zero(g, a, n);
        gen_set_on(g, AVR_BREQ);
        store_val(g, dst, 24, 1);
        break;
    }
    default:
        fatal_error("avr: %s: unary operator %d is not implemented yet", gen_name(g),
                    in->u.unary.op);
    }
}

typedef enum { SHL, SHR, SAR } Shift;

// One bit of shift `sh` over a..a+n-1.
static void shift_step(Gen *g, Shift sh, int a, int n)
{
    if (sh == SHL) {
        emit1(g, AVR_LSL, avr_reg(a));
        for (int i = 1; i < n; i++)
            emit1(g, AVR_ROL, avr_reg(a + i));
    } else {
        emit1(g, sh == SAR ? AVR_ASR : AVR_LSR, avr_reg(a + n - 1));
        for (int i = n - 2; i >= 0; i--)
            emit1(g, AVR_ROR, avr_reg(a + i));
    }
}

// a..a+n-1 shifted by constant k: whole bytes moved first, then bit by bit.
static void shift_const(Gen *g, Shift sh, int a, int n, int k)
{
    int bytes = k / 8;
    if (bytes >= n) {
        if (sh == SAR) {
            bytes = n - 1;
            k     = 8 * n - 1;
        } else {
            for (int i = 0; i < n; i++)
                emit2(g, AVR_MOV, avr_reg(a + i), avr_reg(AVR_ZERO));
            return;
        }
    }
    if (bytes > 0) {
        if (sh == SHL) {
            for (int i = n - 1; i >= bytes; i--)
                emit2(g, AVR_MOV, avr_reg(a + i), avr_reg(a + i - bytes));
            for (int i = 0; i < bytes; i++)
                emit2(g, AVR_MOV, avr_reg(a + i), avr_reg(AVR_ZERO));
        } else {
            for (int i = 0; i < n - bytes; i++)
                emit2(g, AVR_MOV, avr_reg(a + i), avr_reg(a + i + bytes));
            extend_regs(g, a, n - bytes, n, sh == SAR);
        }
    }
    for (int i = 0; i < k % 8; i++)
        shift_step(g, sh, a, n);
}

// a..a+n-1 shifted by the count in r26: a loop of bit steps, `dec` + `brpl` around it.
static void shift_var(Gen *g, Shift sh, int a, int n)
{
    char loop[32], check[32];
    new_label(loop);
    new_label(check);
    emit1(g, AVR_RJMP, avr_label(check));
    gen_label_block(g, loop);
    shift_step(g, sh, a, n);
    gen_label_block(g, check);
    emit1(g, AVR_DEC, avr_reg(AVR_X));
    emit1(g, AVR_BRPL, avr_label(loop));
}

static void gen_shift(Gen *g, const Tac_Instruction *in, Shift sh)
{
    const Tac_Val *count = in->u.binary.src2;
    int n                = avr_type_size(val_type(g, in->u.binary.dst));
    int a                = block_a(n);
    load_val(g, in->u.binary.src1, a, n, EXT_TYPE);
    if (count->kind == TAC_VAL_CONSTANT) {
        shift_const(g, sh, a, n, (int)(const_bits(count->u.constant) & 0xff));
    } else {
        load_val(g, count, AVR_X, 1, EXT_TYPE);
        shift_var(g, sh, a, n);
    }
    store_val(g, in->u.binary.dst, a, n);
}

// r25:r24 = r25:r24 * r23:r22, the low 16 bits from three mul; r1 cleared after.
static void mul16(Gen *g)
{
    emit2(g, AVR_MUL, avr_reg(24), avr_reg(22));
    emit2(g, AVR_MOVW, avr_reg(20), avr_reg(AVR_TMP));
    emit2(g, AVR_MUL, avr_reg(24), avr_reg(23));
    emit2(g, AVR_ADD, avr_reg(21), avr_reg(AVR_TMP));
    emit2(g, AVR_MUL, avr_reg(25), avr_reg(22));
    emit2(g, AVR_ADD, avr_reg(21), avr_reg(AVR_TMP));
    emit1(g, AVR_CLR, avr_reg(AVR_ZERO));
    emit2(g, AVR_MOVW, avr_reg(24), avr_reg(20));
}

// The type `ptr` points to, or NULL when unknown.
static const Tac_Type *pointee(const Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// dst = *ptr, `size` bytes (a scalar): the pointer in Z, the bytes through Z+i.
static void gen_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, int size)
{
    if (size > 8)
        fatal_error("avr: %s: loading an aggregate is not implemented yet", gen_name(g));
    int a = block_a(size);
    load_val(g, ptr, AVR_Z, 2, EXT_TYPE);
    for (int i = 0; i < size; i++)
        emit2(g, AVR_LDD, avr_reg(a + i), avr_disp(AVR_Z, i));
    store_val(g, dst, a, size);
}

// *ptr = src, `size` bytes (a scalar): the value first, then the pointer in Z.
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, int size)
{
    if (size > 8)
        fatal_error("avr: %s: storing an aggregate is not implemented yet", gen_name(g));
    int a = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    load_val(g, ptr, AVR_Z, 2, EXT_TYPE);
    for (int i = 0; i < size; i++)
        emit2(g, AVR_STD, avr_disp(AVR_Z, i), avr_reg(a + i));
}

// The width of a load or store through `ptr` of value `v`: the pointee's when known,
// else the value's.
static int access_size(const Gen *g, const Tac_Val *ptr, const Tac_Val *v)
{
    const Tac_Type *t = pointee(g, ptr);
    if (!t || !avr_is_scalar(t) || t->kind == TAC_TYPE_VOID)
        t = val_type(g, v);
    return avr_type_size(t);
}

// dst = ptr + index * scale: the index scaled in r25:r24, by shifts for a power of two
// and a multiply otherwise, then the pointer added from r23:r22.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index;
    int scale            = in->u.add_ptr.scale;
    if (index->kind == TAC_VAL_CONSTANT) {
        int off = (int)(const_bits(index->u.constant) * (uint64_t)scale);
        load_val(g, in->u.add_ptr.ptr, 24, 2, EXT_TYPE);
        gen_li(g, 22, off & 0xff);
        gen_li(g, 23, (off >> 8) & 0xff);
    } else {
        load_val(g, index, 24, 2, EXT_TYPE);
        int k = 0;
        while (k < 15 && (1 << k) < scale)
            k++;
        if ((1 << k) == scale) {
            for (int i = 0; i < k; i++) {
                emit1(g, AVR_LSL, avr_reg(24));
                emit1(g, AVR_ROL, avr_reg(25));
            }
        } else {
            gen_li(g, 22, scale & 0xff);
            gen_li(g, 23, (scale >> 8) & 0xff);
            mul16(g);
        }
        load_val(g, in->u.add_ptr.ptr, 22, 2, EXT_TYPE);
    }
    chain(g, AVR_ADD, AVR_ADC, 24, 22, 2);
    store_val(g, in->u.add_ptr.dst, 24, 2);
}

// dst = a - b, two byte pointers.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    load_val(g, in->u.ptr_diff.ptr_a, 24, 2, EXT_TYPE);
    load_val(g, in->u.ptr_diff.ptr_b, 22, 2, EXT_TYPE);
    chain(g, AVR_SUB, AVR_SBC, 24, 22, 2);
    store_val(g, in->u.ptr_diff.dst, 24, 2);
}

// Member `offset` of aggregate `name` = src, a scalar of `size` bytes.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *name, int offset, int size)
{
    if (size > 8)
        fatal_error("avr: %s: copying an aggregate member is not implemented yet", gen_name(g));
    int a = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    access_bytes(g, true, name, offset, a, size);
}

// dst = member `offset` of aggregate `name`, a scalar of `size` bytes.
static void gen_copy_from_offset(Gen *g, const char *name, int offset, const Tac_Val *dst,
                                 int size)
{
    if (size > 8)
        fatal_error("avr: %s: copying an aggregate member is not implemented yet", gen_name(g));
    int a = block_a(size);
    access_bytes(g, false, name, offset, a, size);
    store_val(g, dst, a, size);
}

// Call runtime helper `name`.
static void call_helper(Gen *g, const char *name)
{
    emit1(g, AVR_CALL, avr_label(name));
}

// A divide or remainder through the libgcc helpers: the quotient in B and the
// remainder in A for 1 to 4 bytes (the special contracts), the result in r25:r18 of
// the ordinary ABI for 8; returns the register of the result.
static int divide(Gen *g, int n, bool is_unsigned, bool rem)
{
    static const char *const names[2][4] = {
        { "__divmodqi4", "__divmodhi4", "__divmodsi4", NULL },
        { "__udivmodqi4", "__udivmodhi4", "__udivmodsi4", NULL },
    };
    if (n == 8) {
        call_helper(g, rem ? (is_unsigned ? "__umoddi3" : "__moddi3")
                           : (is_unsigned ? "__udivdi3" : "__divdi3"));
        return 18;
    }
    call_helper(g, names[is_unsigned][n == 1 ? 0 : n / 2]);
    if (n == 1)
        return rem ? 25 : 24;
    return rem ? block_a(n) : block_b(n);
}

// A conditional branch, and the operand order of its compare: a < b is cp a, b and
// brlt; a > b is cp b, a and brlt.
typedef struct {
    AVR_Op br;
    bool swap;
} Cond;

static bool compare_cond(Tac_BinaryOperator op, bool is_unsigned, Cond *c)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        *c = (Cond){ AVR_BREQ, false };
        return true;
    case TAC_BINARY_NOT_EQUAL:
        *c = (Cond){ AVR_BRNE, false };
        return true;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        *c = (Cond){ is_unsigned ? AVR_BRLO : AVR_BRLT, false };
        return true;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        *c = (Cond){ is_unsigned ? AVR_BRSH : AVR_BRGE, false };
        return true;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        *c = (Cond){ is_unsigned ? AVR_BRLO : AVR_BRLT, true };
        return true;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        *c = (Cond){ is_unsigned ? AVR_BRSH : AVR_BRGE, true };
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

static void gen_compare(Gen *g, const Tac_Instruction *in, Cond c, const Tac_Type *t)
{
    int n = avr_type_size(t), a = block_a(n), b = block_b(n);
    load_val(g, in->u.binary.src1, a, n, EXT_TYPE);
    load_val(g, in->u.binary.src2, b, n, EXT_TYPE);
    if (c.swap)
        chain(g, AVR_CP, AVR_CPC, b, a, n);
    else
        chain(g, AVR_CP, AVR_CPC, a, b, n);
    gen_set_on(g, c.br);
    store_val(g, in->u.binary.dst, 24, 1);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Type *t     = operand_type(g, in->u.binary.src1, in->u.binary.src2);
    if (avr_is_fp(t)) {
        gen_fp_binary(g, in);
        return;
    }
    Cond c;
    if (compare_cond(op, unsigned_compare(op) || t->kind == TAC_TYPE_POINTER, &c)) {
        gen_compare(g, in, c, t);
        return;
    }
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
    default:
        break;
    }
    int n = avr_type_size(val_type(g, in->u.binary.dst)), a = block_a(n), b = block_b(n);
    load_val(g, in->u.binary.src1, a, n, EXT_TYPE);
    load_val(g, in->u.binary.src2, b, n, EXT_TYPE);
    int result = a;
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        chain(g, AVR_ADD, AVR_ADC, a, b, n);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        chain(g, AVR_SUB, AVR_SBC, a, b, n);
        break;
    case TAC_BINARY_BITWISE_AND:
        chain(g, AVR_AND, AVR_AND, a, b, n);
        break;
    case TAC_BINARY_BITWISE_OR:
        chain(g, AVR_OR, AVR_OR, a, b, n);
        break;
    case TAC_BINARY_BITWISE_XOR:
        chain(g, AVR_EOR, AVR_EOR, a, b, n);
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        if (n == 1) {
            emit2(g, AVR_MUL, avr_reg(24), avr_reg(22));
            emit2(g, AVR_MOV, avr_reg(24), avr_reg(AVR_TMP));
            emit1(g, AVR_CLR, avr_reg(AVR_ZERO));
        } else if (n == 2) {
            mul16(g);
        } else {
            call_helper(g, n == 4 ? "__mulsi3" : "__muldi3");
        }
        break;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        result = divide(g, n, op == TAC_BINARY_DIVIDE_UNSIGNED || op == TAC_BINARY_REMAINDER_UNSIGNED,
                        op == TAC_BINARY_REMAINDER || op == TAC_BINARY_REMAINDER_UNSIGNED);
        break;
    default:
        fatal_error("avr: %s: binary operator %d is not implemented yet", gen_name(g), op);
    }
    store_val(g, in->u.binary.dst, result, n);
}

// Branch to TAC label `target` when `cond` is zero (or nonzero).
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    if (avr_is_fp(t)) {
        gen_fp_test(g, cond);
    } else {
        int n = avr_type_size(t), a = block_a(n);
        load_val(g, cond, a, n, EXT_TYPE);
        test_zero(g, a, n);
    }
    char *l = label_name(target);
    emit1(g, if_zero ? AVR_BREQ : AVR_BRNE, avr_label(l));
    xfree(l);
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
        emit1(g, AVR_RJMP, avr_label(l));
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                      in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        if (in->u.get_address.src->kind != TAC_VAL_VAR)
            fatal_error("avr: %s: the address of a constant", gen_name(g));
        gen_get_address(g, in->u.get_address.src->u.var_name, in->u.get_address.dst);
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
        gen_ptr_diff(g, in);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset,
                           avr_type_size(val_type(g, in->u.copy_to_offset.src)));
        break;
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset, 1);
        break;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
        gen_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                             in->u.copy_from_offset.dst,
                             avr_type_size(val_type(g, in->u.copy_from_offset.dst)));
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
    default:
        fatal_error("avr: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
