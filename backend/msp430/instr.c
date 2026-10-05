//
// Instruction selection: one TAC instruction at a time, naive.  Copies, loads and
// stores move memory to memory; an operation loads its first operand into block A
// (r12 up), takes the second from memory or as an immediate, and stores the result.
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

void call_helper(Gen *g, const char *name)
{
    emit1(g, MSP_CALL, msp_imm_sym(name, 0));
}

void gen_set_on(Gen *g, Msp_Op cond)
{
    char done[32];
    new_label(done);
    emit2(g, MSP_MOV, msp_imm(1), msp_reg(11));
    emit1(g, cond, msp_label(done));
    emit1(g, MSP_CLR, msp_reg(11));
    gen_label_block(g, done);
}

// dst = src: memory to memory, word by word; an aggregate copied.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    int size          = msp_type_size(t);
    if (!msp_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, src->u.var_name, 0, size, msp_type_align(t));
        return;
    }
    if (msp_type_size(val_type(g, src)) != size) {
        load_val(g, src, 12, msp_words(t), EXT_TYPE);
        store_val(g, dst, 12, msp_words(t));
        return;
    }
    for (int i = 0; i < msp_words(t); i++) {
        Msp_Instr *in = emit2(g, MSP_MOV, val_word(g, src, i), val_word(g, dst, i));
        in->byte      = size == 1;
    }
}

// dst = the address of named object `name`.
static void gen_get_address(Gen *g, const char *name, const Tac_Val *dst)
{
    address_of(g, 12, name, 0);
    store_val(g, dst, 12, 1);
}

// A width conversion: the low words of src, or src extended as `ext` says.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext)
{
    int n = msp_words(val_type(g, dst));
    load_val(g, src, 12, n, ext);
    store_val(g, dst, 12, n);
}

// op src+i, reg+i for each word: `first` on the low word, `rest` up the chain.
static void chain(Gen *g, Msp_Op first, Msp_Op rest, const Tac_Val *src, int reg, int n,
                  bool byte)
{
    for (int i = 0; i < n; i++) {
        Msp_Instr *in = emit2(g, i == 0 ? first : rest, val_word(g, src, i), msp_reg(reg + i));
        in->byte      = byte;
    }
}

// The zero flag of value `v` (an integer or pointer): set when every bit is zero.
static void test_zero(Gen *g, const Tac_Val *v)
{
    int n = msp_words(val_type(g, v));
    load_val(g, v, 12, n, EXT_TYPE);
    for (int i = 1; i < n; i++)
        emit2(g, MSP_BIS, msp_reg(12 + i), msp_reg(12));
    emit1(g, MSP_TST, msp_reg(12));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    if (msp_is_fp(val_type(g, src))) {
        gen_fp_unary(g, in);
        return;
    }
    int n = msp_words(val_type(g, dst));
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        load_val(g, src, 12, n, EXT_TYPE);
        for (int i = 0; i < n; i++)
            emit1(g, MSP_INV, msp_reg(12 + i));
        emit1(g, MSP_INC, msp_reg(12));
        for (int i = 1; i < n; i++)
            emit1(g, MSP_ADC, msp_reg(12 + i));
        store_val(g, dst, 12, n);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        load_val(g, src, 12, n, EXT_TYPE);
        for (int i = 0; i < n; i++)
            emit1(g, MSP_INV, msp_reg(12 + i));
        store_val(g, dst, 12, n);
        break;
    case TAC_UNARY_NOT:
        test_zero(g, src);
        gen_set_on(g, MSP_JEQ);
        store_val(g, dst, 11, 1);
        break;
    default:
        fatal_error("msp430: %s: unary operator %d is not implemented", gen_name(g),
                    in->u.unary.op);
    }
}

typedef enum { SHL, SHR, SAR } Shift;

// One bit of shift `sh` over r12..r12+n-1.
static void shift_step(Gen *g, Shift sh, int n)
{
    if (sh == SHL) {
        emit1(g, MSP_RLA, msp_reg(12));
        for (int i = 1; i < n; i++)
            emit1(g, MSP_RLC, msp_reg(12 + i));
        return;
    }
    if (sh == SAR) {
        emit1(g, MSP_RRA, msp_reg(12 + n - 1));
    } else {
        emit0(g, MSP_CLRC);
        emit1(g, MSP_RRC, msp_reg(12 + n - 1));
    }
    for (int i = n - 2; i >= 0; i--)
        emit1(g, MSP_RRC, msp_reg(12 + i));
}

// `count` (r11) steps of shift `sh`; none when it is zero.
static void shift_loop(Gen *g, Shift sh, int n)
{
    char loop[32], done[32];
    new_label(loop);
    new_label(done);
    emit1(g, MSP_TST, msp_reg(11));
    emit1(g, MSP_JEQ, msp_label(done));
    gen_label_block(g, loop);
    shift_step(g, sh, n);
    emit1(g, MSP_DEC, msp_reg(11));
    emit1(g, MSP_JNE, msp_label(loop));
    gen_label_block(g, done);
}

// r12..r12+n-1 shifted by constant k: whole words moved first, then bit by bit.
static void shift_const(Gen *g, Shift sh, int n, int k)
{
    int words = k / 16;
    if (words >= n) {
        if (sh == SAR) {
            words = n - 1;
            k     = 16 * n - 1;
        } else {
            for (int i = 0; i < n; i++)
                emit1(g, MSP_CLR, msp_reg(12 + i));
            return;
        }
    }
    if (words > 0) {
        if (sh == SHL) {
            for (int i = n - 1; i >= words; i--)
                emit2(g, MSP_MOV, msp_reg(12 + i - words), msp_reg(12 + i));
            for (int i = 0; i < words; i++)
                emit1(g, MSP_CLR, msp_reg(12 + i));
        } else {
            for (int i = 0; i < n - words; i++)
                emit2(g, MSP_MOV, msp_reg(12 + i + words), msp_reg(12 + i));
            extend_regs(g, 12, n - words, n, sh == SAR);
        }
    }
    int bits = k % 16;
    if (bits > 3) {
        emit2(g, MSP_MOV, msp_imm(bits), msp_reg(11));
        shift_loop(g, sh, n);
        return;
    }
    for (int i = 0; i < bits; i++)
        shift_step(g, sh, n);
}

static void gen_shift(Gen *g, const Tac_Instruction *in, Shift sh)
{
    const Tac_Val *count = in->u.binary.src2;
    int n                = msp_words(val_type(g, in->u.binary.dst));
    load_val(g, in->u.binary.src1, 12, n, EXT_TYPE);
    if (count->kind == TAC_VAL_CONSTANT) {
        shift_const(g, sh, n, (int)(const_bits(count->u.constant) & 0xff));
    } else {
        load_val(g, count, 11, 1, EXT_TYPE);
        shift_loop(g, sh, n);
    }
    store_val(g, in->u.binary.dst, 12, n);
}

// A multiply, divide or remainder through the runtime, the __mspabi_ helpers GCC's code
// calls: operands in r12 and r13, or r13:r12 and r15:r14, or for 64 bits r11:r8 (the
// prologue then saves r8-r10) and r15:r12; the result in r12 up.
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
    int row;
    switch (in->u.binary.op) {
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        row = 0;
        break;
    case TAC_BINARY_DIVIDE:
        row = 1;
        break;
    case TAC_BINARY_DIVIDE_UNSIGNED:
        row = 2;
        break;
    case TAC_BINARY_REMAINDER:
        row = 3;
        break;
    default:
        row = 4;
        break;
    }
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    if (n == 4) {
        load_val(g, a, 8, 4, EXT_TYPE);
        load_val(g, b, 12, 4, EXT_TYPE);
    } else {
        load_val(g, a, 12, n, EXT_TYPE);
        load_val(g, b, n == 1 ? 13 : 14, n, EXT_TYPE);
    }
    call_helper(g, names[row][n == 1 ? 0 : n == 2 ? 1 : 2]);
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

// Jump to `target` when a (in r12 up, n words) and b stand in relation `c`; fall
// through otherwise.  The high words decide first, signed or not; the rest unsigned.
static void compare_jump(Gen *g, const Tac_Val *b, int n, bool byte, Cond c, bool is_unsigned,
                         const char *target)
{
    char skip[32];
    new_label(skip);
    bool used_skip = false;
    for (int i = n - 1; i >= 0; i--) {
        Msp_Instr *cmp = emit2(g, MSP_CMP, val_word(g, b, i), msp_reg(12 + i));
        cmp->byte      = byte;
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

static void gen_compare(Gen *g, const Tac_Instruction *in, Cond c, bool swap, const Tac_Type *t)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2;
    if (swap) {
        const Tac_Val *x = a;
        a                = b;
        b                = x;
    }
    int n            = msp_words(t);
    bool is_unsigned = unsigned_compare(in->u.binary.op) || t->kind == TAC_TYPE_POINTER;
    load_val(g, a, 12, n, EXT_TYPE);
    char done[32];
    new_label(done);
    emit2(g, MSP_MOV, msp_imm(1), msp_reg(11));
    compare_jump(g, b, n, msp_type_size(t) == 1, c, is_unsigned, done);
    emit1(g, MSP_CLR, msp_reg(11));
    gen_label_block(g, done);
    store_val(g, in->u.binary.dst, 11, 1);
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
    int n     = msp_words(val_type(g, in->u.binary.dst));
    bool byte = msp_type_size(val_type(g, in->u.binary.dst)) == 1;
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
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        arith_helper(g, in, n);
        return;
    default:
        break;
    }
    load_val(g, in->u.binary.src1, 12, n, EXT_TYPE);
    const Tac_Val *b = in->u.binary.src2;
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        chain(g, MSP_ADD, MSP_ADDC, b, 12, n, byte);
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        chain(g, MSP_SUB, MSP_SUBC, b, 12, n, byte);
        break;
    case TAC_BINARY_BITWISE_AND:
        chain(g, MSP_AND, MSP_AND, b, 12, n, byte);
        break;
    case TAC_BINARY_BITWISE_OR:
        chain(g, MSP_BIS, MSP_BIS, b, 12, n, byte);
        break;
    case TAC_BINARY_BITWISE_XOR:
        chain(g, MSP_XOR, MSP_XOR, b, 12, n, byte);
        break;
    default:
        fatal_error("msp430: %s: binary operator %d is not implemented", gen_name(g), op);
    }
    store_val(g, in->u.binary.dst, 12, n);
}

int instr_out_size(const Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        return call_stack_size(g, in);
    case TAC_INSTRUCTION_BINARY: {
        // A binary64 comparison takes its second operand on the stack.
        const Tac_Type *t = operand_type(g, in->u.binary.src1, in->u.binary.src2);
        return msp_is_fp(t) ? fp_out_size(in->u.binary.op, msp_type_size(t)) : 0;
    }
    default:
        return 0;
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

// dst = *ptr, `size` bytes: memory to memory through the pointer in r15, as much as
// the destination holds (the pointee may be wider, a row of a 2-D array) and its rest
// zeroed; an aggregate copied from r14 to r15.
static void gen_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, int size)
{
    const Tac_Type *t = val_type(g, dst);
    if (!msp_is_scalar(t)) {
        load_val(g, ptr, 14, 1, EXT_TYPE);
        address_of(g, 15, dst->u.var_name, 0);
        copy_bytes(g, size, msp_type_align(t));
        return;
    }
    int dsize = msp_type_size(t);
    load_val(g, ptr, 15, 1, EXT_TYPE);
    if (size == 1 || dsize == 1) {
        emit2b(g, MSP_MOV, msp_ind(15), mem_at(g, dst->u.var_name, 0));
        if (dsize > 1)
            emit1b(g, MSP_CLR, mem_at(g, dst->u.var_name, 1));
        for (int i = 1; i < dsize / 2; i++)
            emit1(g, MSP_CLR, mem_at(g, dst->u.var_name, 2 * i));
        return;
    }
    for (int i = 0; i < dsize / 2; i++) {
        if (2 * i < size)
            emit2(g, MSP_MOV, i == 0 ? msp_ind(15) : msp_indexed(15, NULL, 2 * i),
                  mem_at(g, dst->u.var_name, 2 * i));
        else
            emit1(g, MSP_CLR, mem_at(g, dst->u.var_name, 2 * i));
    }
}

// *ptr = src, `size` bytes: memory to memory through the pointer in r15; an aggregate
// copied from r14 to r15.
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, int size)
{
    if (src->kind == TAC_VAL_VAR && !msp_is_scalar(val_type(g, src))) {
        load_val(g, ptr, 15, 1, EXT_TYPE);
        address_of(g, 14, src->u.var_name, 0);
        copy_bytes(g, size, msp_type_align(val_type(g, src)));
        return;
    }
    if (size > msp_type_size(val_type(g, src)) && size > 1) {
        // A narrower value into a wider pointee: extended in registers first.
        int n = size / 2;
        load_val(g, src, 12, n, EXT_TYPE);
        load_val(g, ptr, 15, 1, EXT_TYPE);
        for (int i = 0; i < n; i++)
            emit2(g, MSP_MOV, msp_reg(12 + i), msp_indexed(15, NULL, 2 * i));
        return;
    }
    load_val(g, ptr, 15, 1, EXT_TYPE);
    if (size == 1) {
        emit2b(g, MSP_MOV, val_word(g, src, 0), msp_indexed(15, NULL, 0));
        return;
    }
    for (int i = 0; i < size / 2; i++)
        emit2(g, MSP_MOV, val_word(g, src, i), msp_indexed(15, NULL, 2 * i));
}

// dst = ptr + index * scale: the index scaled in r12, by shifts for a power of two and
// a multiply otherwise, then the pointer added.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index;
    int scale            = in->u.add_ptr.scale;
    if (index->kind == TAC_VAL_CONSTANT) {
        int off = (int)(const_bits(index->u.constant) * (uint64_t)scale);
        load_val(g, in->u.add_ptr.ptr, 12, 1, EXT_TYPE);
        if (off & 0xffff)
            emit2(g, MSP_ADD, msp_imm(off & 0xffff), msp_reg(12));
    } else {
        load_val(g, index, 12, 1, EXT_TYPE);
        int k = 0;
        while (k < 15 && (1 << k) < scale)
            k++;
        if ((1 << k) == scale) {
            for (int i = 0; i < k; i++)
                emit1(g, MSP_RLA, msp_reg(12));
        } else {
            emit2(g, MSP_MOV, msp_imm(scale), msp_reg(13));
            call_helper(g, "__mspabi_mpyi");
        }
        emit2(g, MSP_ADD, val_word(g, in->u.add_ptr.ptr, 0), msp_reg(12));
    }
    store_val(g, in->u.add_ptr.dst, 12, 1);
}

// dst = a - b, two byte pointers.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    load_val(g, in->u.ptr_diff.ptr_a, 12, 1, EXT_TYPE);
    emit2(g, MSP_SUB, val_word(g, in->u.ptr_diff.ptr_b, 0), msp_reg(12));
    store_val(g, in->u.ptr_diff.dst, 12, 1);
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
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        if (in->u.get_address.src->kind != TAC_VAL_VAR)
            fatal_error("msp430: %s: the address of a constant", gen_name(g));
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
