//
// Instruction selection, one TAC instruction at a time, in two forms (internal.h).  The
// naive form loads its operands into register blocks A and B (block_a, block_b),
// computes there with byte-serial chains and stores the result.  With registers
// allocated, an instruction that uses_scratch says needs neither r18-r25 nor a helper
// computes in its destination's registers, or in the scratch Z and X when they are in
// memory or unsuited, taking operand bytes straight from registers, through r0 from
// memory, or as immediates.
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

// The operand an operation's type is read from: a variable, else the constant.
static const Tac_Val *typed_operand(const Tac_Val *a, const Tac_Val *b)
{
    return a->kind == TAC_VAL_VAR || !b ? a : b;
}

// The type an operation on `a` and `b` works in: a variable's, else a constant's.
static const Tac_Type *operand_type(const Gen *g, const Tac_Val *a, const Tac_Val *b)
{
    return val_type(g, typed_operand(a, b));
}

static bool pow2(int scale, int *k)
{
    for (*k = 0; *k < 15 && (1 << *k) < scale; (*k)++)
        ;
    return (1 << *k) == scale;
}

static bool is_shift(Tac_BinaryOperator op)
{
    return op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
           op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
}

static bool is_mul_div(Tac_BinaryOperator op)
{
    switch (op) {
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        return true;
    default:
        return false;
    }
}

// An aggregate copied by a loop counted in r25:r24.
static bool big_aggregate(const Tac_Type *t)
{
    return t && !avr_is_scalar(t) && avr_type_size(t) > 16;
}

// The conversions through a helper.
static bool fp_int_convert(Tac_InstructionKind kind)
{
    switch (kind) {
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
        return true;
    default:
        return false;
    }
}

bool uses_scratch(const Tac_Instruction *in, TypeOf type_of, const void *arg)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_BINARY: {
        const Tac_Type *t = type_of(arg, typed_operand(in->u.binary.src1, in->u.binary.src2));
        Tac_BinaryOperator op = in->u.binary.op;
        if (avr_is_fp(t) || avr_type_size(t) > 4 || is_mul_div(op))
            return true;
        return is_shift(op) && in->u.binary.src2->kind != TAC_VAL_CONSTANT;
    }
    case TAC_INSTRUCTION_UNARY:
        return avr_type_size(type_of(arg, in->u.unary.src)) > 4;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
        return big_aggregate(type_of(arg, instr_dst(in)));
    case TAC_INSTRUCTION_STORE:
        return big_aggregate(type_of(arg, in->u.store.src));
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
        return big_aggregate(type_of(arg, in->u.copy_to_offset.src));
    case TAC_INSTRUCTION_ADD_PTR: {
        int k;
        return in->u.add_ptr.index->kind != TAC_VAL_CONSTANT && !pow2(in->u.add_ptr.scale, &k);
    }
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        return avr_type_size(type_of(arg, in->u.jump_if_zero.condition)) > 4;
    default:
        return fp_int_convert(in->kind);
    }
}

const Tac_Val *instr_dst(const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_BINARY:
        return in->u.binary.dst;
    case TAC_INSTRUCTION_UNARY:
        return in->u.unary.dst;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        return in->u.load.dst;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        return in->u.get_address.dst;
    case TAC_INSTRUCTION_ADD_PTR:
        return in->u.add_ptr.dst;
    case TAC_INSTRUCTION_PTR_DIFF:
        return in->u.ptr_diff.dst;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        return in->u.copy_from_offset.dst;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
    case TAC_INSTRUCTION_SIGN_EXTEND:
    case TAC_INSTRUCTION_ZERO_EXTEND:
    case TAC_INSTRUCTION_TRUNCATE:
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
        return in->u.copy.dst; // every conversion begins {src, dst}
    default:
        return NULL;
    }
}

static const Tac_Type *gen_type_of(const void *arg, const Tac_Val *v)
{
    return val_type(arg, v);
}

//
// Shared by both forms
//

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

// reg = 1 when branch `br` would be taken on the flags as they are, else 0.
static void set_on(Gen *g, AVR_Op br, int reg)
{
    char done[32];
    new_label(done);
    emit2(g, AVR_LDI, avr_reg(reg), avr_imm(1));
    emit1(g, br, avr_label(done));
    emit1(g, AVR_CLR, avr_reg(reg));
    gen_label_block(g, done);
}

void gen_set_on(Gen *g, AVR_Op br)
{
    set_on(g, br, 24);
}

typedef enum { SHL, SHR, SAR } Shift;

// One bit of shift `sh` over r[0..n-1].
static void shift_step(Gen *g, Shift sh, const int *r, int n)
{
    if (sh == SHL) {
        emit1(g, AVR_LSL, avr_reg(r[0]));
        for (int i = 1; i < n; i++)
            emit1(g, AVR_ROL, avr_reg(r[i]));
    } else {
        emit1(g, sh == SAR ? AVR_ASR : AVR_LSR, avr_reg(r[n - 1]));
        for (int i = n - 2; i >= 0; i--)
            emit1(g, AVR_ROR, avr_reg(r[i]));
    }
}

// r[0..n-1] shifted by constant k: whole bytes moved first, then bit by bit.
static void shift_const(Gen *g, Shift sh, const int *r, int n, int k)
{
    int bytes = k / 8;
    if (bytes >= n) {
        if (sh == SAR) {
            bytes = n - 1;
            k     = 8 * n - 1;
        } else {
            for (int i = 0; i < n; i++)
                emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(AVR_ZERO));
            return;
        }
    }
    if (bytes > 0) {
        if (sh == SHL) {
            for (int i = n - 1; i >= bytes; i--)
                emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(r[i - bytes]));
            for (int i = 0; i < bytes; i++)
                emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(AVR_ZERO));
        } else {
            for (int i = 0; i < n - bytes; i++)
                emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(r[i + bytes]));
            extend_regs(g, r, n - bytes, n, sh == SAR);
        }
    }
    for (int i = 0; i < k % 8; i++)
        shift_step(g, sh, r, n);
}

static Shift shift_of(Tac_BinaryOperator op)
{
    return op == TAC_BINARY_LEFT_SHIFT ? SHL : op == TAC_BINARY_RIGHT_SHIFT ? SAR : SHR;
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
    return avr_type_size(t);
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

// The chain of an add, subtract or bitwise operator: the low byte's, then the rest's.
static bool arith_ops(Tac_BinaryOperator op, AVR_Op *first, AVR_Op *rest)
{
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        *first = AVR_ADD, *rest = AVR_ADC;
        return true;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        *first = AVR_SUB, *rest = AVR_SBC;
        return true;
    case TAC_BINARY_BITWISE_AND:
        *first = *rest = AVR_AND;
        return true;
    case TAC_BINARY_BITWISE_OR:
        *first = *rest = AVR_OR;
        return true;
    case TAC_BINARY_BITWISE_XOR:
        *first = *rest = AVR_EOR;
        return true;
    default:
        return false;
    }
}

// Aggregate dst = src, from X to Z.
static void copy_aggregate(Gen *g, const char *src, int src_off, const char *dst, int dst_off,
                           int size)
{
    address_of(g, AVR_X, src, src_off);
    address_of(g, AVR_Z, dst, dst_off);
    copy_bytes(g, size);
}

//
// The naive form
//

// dst = src; an aggregate copied from X to Z.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    int size          = avr_type_size(t);
    if (!avr_is_scalar(t)) {
        copy_aggregate(g, src->u.var_name, 0, dst->u.var_name, 0, size);
        return;
    }
    int a = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    store_val(g, dst, a, size);
}

// dst = the address of named object `name`: Y+q for a slot, lo8/hi8 of a data
// symbol, pm_lo8/pm_hi8 of a function (a word address in flash); in `reg` and the
// register above it, an upper pair (adiw only from r24 up).
static void address_into(Gen *g, const char *name, int reg)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        emit2(g, AVR_MOVW, avr_reg(reg), avr_reg(AVR_Y));
        if (s->q <= Y_MAX && reg >= 24) {
            emit2(g, AVR_ADIW, avr_reg(reg), avr_imm(s->q));
        } else {
            emit2(g, AVR_SUBI, avr_reg(reg), avr_imm(-s->q & 0xff));
            emit2(g, AVR_SBCI, avr_reg(reg + 1), avr_imm(((unsigned)-s->q >> 8) & 0xff));
        }
    } else {
        bool fn = is_function(g, name);
        emit2(g, AVR_LDI, avr_reg(reg), avr_sym(fn ? AVR_MOD_PM_LO8 : AVR_MOD_LO8, name, 0));
        emit2(g, AVR_LDI, avr_reg(reg + 1), avr_sym(fn ? AVR_MOD_PM_HI8 : AVR_MOD_HI8, name, 0));
    }
}

static void gen_get_address(Gen *g, const char *name, const Tac_Val *dst)
{
    Regs w;
    if (val_regs(g, dst, &w) && w.r[0] >= 16) {
        address_into(g, name, w.r[0]);
        return;
    }
    int reg = g->alloc ? AVR_Z : 24;
    address_into(g, name, reg);
    store_val(g, dst, reg, 2);
}

// A width conversion: the low bytes of src, or src extended as `ext` says.
static void gen_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext)
{
    int size = avr_type_size(val_type(g, dst));
    int a    = block_a(size);
    load_val(g, src, a, size, ext);
    store_val(g, dst, a, size);
}

// r[0..n-1] = -r[0..n-1]: the complement plus one, as com on the high bytes, neg on the
// low one, and its borrow carried up by sbci 0xff (upper registers only).
static void negate(Gen *g, const int *r, int n)
{
    for (int i = n - 1; i > 0; i--)
        emit1(g, AVR_COM, avr_reg(r[i]));
    emit1(g, AVR_NEG, avr_reg(r[0]));
    for (int i = 1; i < n; i++)
        emit2(g, AVR_SBCI, avr_reg(r[i]), avr_imm(0xff));
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
        int n = avr_type_size(val_type(g, dst));
        Regs a = regs_range(block_a(n), n);
        load_regs(g, src, &a, EXT_TYPE);
        negate(g, a.r, n);
        store_val(g, dst, a.r[0], n);
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
        internal_error("avr: %s: unary operator %d is not implemented", gen_name(g),
                       in->u.unary.op);
    }
}

// a..a+n-1 shifted by the count in r26: a loop of bit steps, `dec` + `brpl` around it.
static void shift_var(Gen *g, Shift sh, const int *r, int n)
{
    char loop[32], check[32];
    new_label(loop);
    new_label(check);
    emit1(g, AVR_RJMP, avr_label(check));
    gen_label_block(g, loop);
    shift_step(g, sh, r, n);
    gen_label_block(g, check);
    emit1(g, AVR_DEC, avr_reg(AVR_X));
    emit1(g, AVR_BRPL, avr_label(loop));
}

static void gen_shift(Gen *g, const Tac_Instruction *in, Shift sh)
{
    const Tac_Val *count = in->u.binary.src2;
    int n                = avr_type_size(val_type(g, in->u.binary.dst));
    Regs a               = regs_range(block_a(n), n);
    if (count->kind == TAC_VAL_CONSTANT) {
        load_regs(g, in->u.binary.src1, &a, EXT_TYPE);
        shift_const(g, sh, a.r, n, (int)(const_bits(count->u.constant) & 0xff));
    } else {
        load_two(g, in->u.binary.src1, a.r[0], n, count, AVR_X, 1);
        shift_var(g, sh, a.r, n);
    }
    store_val(g, in->u.binary.dst, a.r[0], n);
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

// dst = *ptr, `size` bytes: a scalar with the pointer in Z and the bytes through Z+i,
// an aggregate copied with the pointer in X.
static void gen_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, int size)
{
    if (!avr_is_scalar(val_type(g, dst))) {
        load_val(g, ptr, AVR_X, 2, EXT_TYPE);
        address_of(g, AVR_Z, dst->u.var_name, 0);
        copy_bytes(g, size);
        return;
    }
    int a = block_a(size);
    load_val(g, ptr, AVR_Z, 2, EXT_TYPE);
    for (int i = 0; i < size; i++)
        emit2(g, AVR_LDD, avr_reg(a + i), avr_disp(AVR_Z, i));
    store_val(g, dst, a, size);
}

// *ptr = src, `size` bytes: a scalar's value and the pointer in Z; an aggregate
// copied, the pointer in Z first (loading it may take X).
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, int size)
{
    if (src->kind == TAC_VAL_VAR && !avr_is_scalar(val_type(g, src))) {
        load_val(g, ptr, AVR_Z, 2, EXT_TYPE);
        address_of(g, AVR_X, src->u.var_name, 0);
        copy_bytes(g, size);
        return;
    }
    int a = block_a(size);
    load_two(g, src, a, size, ptr, AVR_Z, 2);
    for (int i = 0; i < size; i++)
        emit2(g, AVR_STD, avr_disp(AVR_Z, i), avr_reg(a + i));
}

// dst = ptr + index * scale: the index scaled in r25:r24, by shifts for a power of two
// and a multiply otherwise, then the pointer added from Z.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index;
    int scale            = in->u.add_ptr.scale, k;
    if (index->kind == TAC_VAL_CONSTANT) {
        int off = (int)(const_bits(index->u.constant) * (uint64_t)scale);
        load_val(g, in->u.add_ptr.ptr, 24, 2, EXT_TYPE);
        if (off & 0xffff) {
            emit2(g, AVR_SUBI, avr_reg(24), avr_imm(-off & 0xff));
            emit2(g, AVR_SBCI, avr_reg(25), avr_imm((-off >> 8) & 0xff));
        }
    } else {
        load_two(g, index, 24, 2, in->u.add_ptr.ptr, AVR_Z, 2);
        if (pow2(scale, &k)) {
            for (int i = 0; i < k; i++) {
                emit1(g, AVR_LSL, avr_reg(24));
                emit1(g, AVR_ROL, avr_reg(25));
            }
        } else {
            gen_li(g, 22, scale & 0xff);
            gen_li(g, 23, (scale >> 8) & 0xff);
            mul16(g);
        }
        chain(g, AVR_ADD, AVR_ADC, 24, AVR_Z, 2);
    }
    store_val(g, in->u.add_ptr.dst, 24, 2);
}

// dst = a - b, two byte pointers.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    load_two(g, in->u.ptr_diff.ptr_a, 24, 2, in->u.ptr_diff.ptr_b, 22, 2);
    chain(g, AVR_SUB, AVR_SBC, 24, 22, 2);
    store_val(g, in->u.ptr_diff.dst, 24, 2);
}

// Member `offset` of aggregate `name` = src, of `size` bytes.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *name, int offset, int size)
{
    if (src->kind == TAC_VAL_VAR && !avr_is_scalar(val_type(g, src))) {
        copy_aggregate(g, src->u.var_name, 0, name, offset, size);
        return;
    }
    int a = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    access_bytes(g, true, name, offset, a, size);
}

// dst = member `offset` of aggregate `name`, of `size` bytes.
static void gen_copy_from_offset(Gen *g, const char *name, int offset, const Tac_Val *dst,
                                 int size)
{
    if (!avr_is_scalar(val_type(g, dst))) {
        copy_aggregate(g, name, offset, dst->u.var_name, 0, size);
        return;
    }
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

// Variables in r10-r17, which an 8-byte operand B overwrites, are saved around it.
static uint32_t save_for_b(Gen *g, int n)
{
    return n == 8 ? save_var_regs(g, 10, 17) : 0;
}

// The flags of a comparison, in blocks A and B.
static void compare_naive(Gen *g, const Tac_Instruction *in, Cond c, const Tac_Type *t)
{
    int n = avr_type_size(t), a = block_a(n), b = block_b(n);
    uint32_t saved = save_for_b(g, n);
    load_two(g, in->u.binary.src1, a, n, in->u.binary.src2, b, n);
    if (c.swap)
        chain(g, AVR_CP, AVR_CPC, b, a, n);
    else
        chain(g, AVR_CP, AVR_CPC, a, b, n);
    restore_var_regs(g, saved);
}

static void gen_compare(Gen *g, const Tac_Instruction *in, Cond c, const Tac_Type *t)
{
    compare_naive(g, in, c, t);
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
    if (is_shift(op)) {
        gen_shift(g, in, shift_of(op));
        return;
    }
    int n = avr_type_size(val_type(g, in->u.binary.dst)), a = block_a(n), b = block_b(n);
    uint32_t saved = save_for_b(g, n);
    load_two(g, in->u.binary.src1, a, n, in->u.binary.src2, b, n);
    int result = a;
    AVR_Op first, rest;
    if (arith_ops(op, &first, &rest)) {
        chain(g, first, rest, a, b, n);
    } else {
        switch (op) {
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
            result = divide(g, n,
                            op == TAC_BINARY_DIVIDE_UNSIGNED || op == TAC_BINARY_REMAINDER_UNSIGNED,
                            op == TAC_BINARY_REMAINDER || op == TAC_BINARY_REMAINDER_UNSIGNED);
            break;
        default:
            internal_error("avr: %s: binary operator %d is not implemented", gen_name(g), op);
        }
    }
    restore_var_regs(g, saved);
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

//
// The scratch-free form
//

// Z, then X, byte by byte: where a value in memory is computed.
static Regs scratch_regs(int n)
{
    static const int r[4] = { AVR_Z, AVR_Z + 1, AVR_X, AVR_X + 1 };
    Regs s                = { .n = n };
    for (int i = 0; i < n; i++)
        s.r[i] = r[i];
    return s;
}

static bool all_upper(const Regs *w)
{
    for (int i = 0; i < w->n; i++)
        if (w->r[i] < 16)
            return false;
    return true;
}

static bool in_regs(const Regs *w, int r)
{
    for (int i = 0; i < w->n; i++)
        if (w->r[i] == r)
            return true;
    return false;
}

// Whether value `v` lives in any of the registers `w`.
static bool overlaps(const Gen *g, const Regs *w, const Tac_Val *v)
{
    Regs vr;
    if (!val_regs(g, v, &vr))
        return false;
    for (int i = 0; i < vr.n; i++)
        if (in_regs(w, vr.r[i]))
            return true;
    return false;
}

// Where `dst` of `n` bytes is computed: in its own registers, when it has them and
// (with `upper`) they all take immediates; else in the scratch.
static Regs work_regs(const Gen *g, const Tac_Val *dst, int n, bool upper, bool *own)
{
    Regs w;
    *own = val_regs(g, dst, &w) && w.n == n && (!upper || all_upper(&w));
    return *own ? w : scratch_regs(n);
}

static void finish(Gen *g, const Tac_Val *dst, const Regs *w, bool own)
{
    if (!own)
        store_regs(g, dst, w->r, w->n);
}

// An immediate-capable scratch register not among `w`, or -1.
static int free_upper(const Regs *w)
{
    static const int r[4] = { AVR_X, AVR_X + 1, AVR_Z, AVR_Z + 1 };
    for (int i = 0; i < 4; i++)
        if (!in_regs(w, r[i]))
            return r[i];
    return -1;
}

// The register holding byte i of `v`: its own; r1 for a zero byte; else `tmp` loaded,
// from memory (a slot of a value of up to 4 bytes is within Y+63, so no flags change),
// or with ldi (tmp an upper register then).
static int byte_of(Gen *g, const Tac_Val *v, int i, int tmp, uint32_t busy)
{
    if (i >= avr_type_size(val_type(g, v)))
        return AVR_ZERO;
    if (v->kind == TAC_VAL_CONSTANT) {
        int b = (int)(const_bits(v->u.constant) >> (8 * i) & 0xff);
        if (!b)
            return AVR_ZERO;
        emit2(g, AVR_LDI, avr_reg(tmp), avr_imm(b));
        return tmp;
    }
    Regs vr;
    if (val_regs(g, v, &vr))
        return vr.r[i];
    access_mem(g, false, v->u.var_name, i, &tmp, 1, busy);
    return tmp;
}

// No pointer register: a slot within Y+63 only.
#define NEAR (3u << AVR_X | 3u << AVR_Z)

// w (op)= constant `k`, an add, subtract or bitwise operator.
static void arith_const(Gen *g, Tac_BinaryOperator op, const Regs *w, uint64_t k)
{
    AVR_Op first, rest;
    if (!arith_ops(op, &first, &rest))
        internal_error("avr: %s: bad arithmetic operator %d", gen_name(g), op);
    int tmp = free_upper(w);
    if (first == AVR_ADD || first == AVR_SUB) {
        bool sub   = first == AVR_SUB;
        uint64_t m = sub ? k : 0 - k; // to subtract, with subi/sbci
        int j      = 0;
        while (j < w->n && ((all_upper(w) ? m : k) >> (8 * j) & 0xff) == 0)
            j++;
        for (int i = j; i < w->n; i++) {
            if (all_upper(w)) {
                emit2(g, i == j ? AVR_SUBI : AVR_SBCI, avr_reg(w->r[i]),
                      avr_imm((int)(m >> (8 * i) & 0xff)));
                continue;
            }
            int b = (int)(k >> (8 * i) & 0xff), r = AVR_ZERO;
            if (b) {
                emit2(g, AVR_LDI, avr_reg(tmp), avr_imm(b));
                r = tmp;
            }
            emit2(g, i == j ? first : rest, avr_reg(w->r[i]), avr_reg(r));
        }
        return;
    }
    for (int i = 0; i < w->n; i++) {
        int b = (int)(k >> (8 * i) & 0xff), r = w->r[i];
        if ((first == AVR_AND && b == 0xff) || (first != AVR_AND && b == 0))
            continue;
        if (first == AVR_AND && b == 0) {
            emit2(g, AVR_MOV, avr_reg(r), avr_reg(AVR_ZERO));
        } else if (first == AVR_EOR && b == 0xff) {
            emit1(g, AVR_COM, avr_reg(r));
        } else if (r >= 16 && first != AVR_EOR) {
            emit2(g, first == AVR_AND ? AVR_ANDI : AVR_ORI, avr_reg(r), avr_imm(b));
        } else if (tmp >= 0) {
            emit2(g, AVR_LDI, avr_reg(tmp), avr_imm(b));
            emit2(g, first, avr_reg(r), avr_reg(tmp));
        } else { // eor into a full scratch: the operand through r0
            emit2(g, AVR_MOV, avr_reg(AVR_TMP), avr_reg(r));
            emit2(g, AVR_LDI, avr_reg(r), avr_imm(b));
            emit2(g, AVR_EOR, avr_reg(r), avr_reg(AVR_TMP));
        }
    }
}

// dst = s1 op s2: an add, subtract or bitwise operator of `n` bytes.
static void clean_arith(Gen *g, Tac_BinaryOperator op, const Tac_Val *s1, const Tac_Val *s2,
                        const Tac_Val *dst, int n)
{
    AVR_Op first, rest;
    if (!arith_ops(op, &first, &rest))
        internal_error("avr: %s: bad arithmetic operator %d", gen_name(g), op);
    bool comm = first != AVR_SUB;
    if (comm && s1->kind == TAC_VAL_CONSTANT && s2->kind == TAC_VAL_VAR) {
        const Tac_Val *t = s1;
        s1 = s2, s2 = t;
    }
    bool own;
    Regs w = work_regs(g, dst, n, false, &own);
    if (own && overlaps(g, &w, s2)) {
        if (comm && !overlaps(g, &w, s1)) {
            const Tac_Val *t = s1;
            s1 = s2, s2 = t;
        } else {
            own = false;
            w   = scratch_regs(n);
        }
    }
    load_regs(g, s1, &w, EXT_TYPE);
    if (s2->kind == TAC_VAL_CONSTANT) {
        arith_const(g, op, &w, const_bits(s2->u.constant));
    } else {
        for (int i = 0; i < n; i++)
            emit2(g, i == 0 ? first : rest, avr_reg(w.r[i]),
                  avr_reg(byte_of(g, s2, i, AVR_TMP, NEAR)));
    }
    finish(g, dst, &w, own);
}

// dst = 1 when branch `br` would be taken on the flags as they are, else 0.
static void set_result(Gen *g, AVR_Op br, const Tac_Val *dst)
{
    Regs w;
    bool own = val_regs(g, dst, &w) && w.r[0] >= 16;
    int t    = own ? w.r[0] : AVR_X;
    set_on(g, br, t);
    if (own)
        for (int i = 1; i < w.n; i++)
            emit2(g, AVR_MOV, avr_reg(w.r[i]), avr_reg(AVR_ZERO));
    else
        store_val(g, dst, t, 1);
}

// The flags of a comparison, its operand bytes straight from registers.
static void compare_clean(Gen *g, Cond c, const Tac_Val *s1, const Tac_Val *s2, int n)
{
    for (int i = 0; i < n; i++) {
        int x = byte_of(g, s1, i, AVR_X, NEAR), y = byte_of(g, s2, i, AVR_X + 1, NEAR);
        if (c.swap)
            emit2(g, i == 0 ? AVR_CP : AVR_CPC, avr_reg(y), avr_reg(x));
        else
            emit2(g, i == 0 ? AVR_CP : AVR_CPC, avr_reg(x), avr_reg(y));
    }
}

static void clean_compare(Gen *g, Cond c, const Tac_Val *s1, const Tac_Val *s2, int n,
                          const Tac_Val *dst)
{
    compare_clean(g, c, s1, s2, n);
    set_result(g, c.br, dst);
}

// The zero flag of `v`: set when all of it is zero; an FP one tested without its sign.
static void clean_test(Gen *g, const Tac_Val *v)
{
    const Tac_Type *t = val_type(g, v);
    int n             = avr_type_size(t);
    if (avr_is_fp(t)) {
        int top = byte_of(g, v, 3, AVR_X, NEAR);
        if (top != AVR_X)
            emit2(g, AVR_MOV, avr_reg(AVR_X), avr_reg(top));
        emit2(g, AVR_ANDI, avr_reg(AVR_X), avr_imm(0x7f));
        n = 3;
    }
    for (int i = 0; i < n; i++)
        emit2(g, i == 0 ? AVR_CP : AVR_CPC, avr_reg(byte_of(g, v, i, AVR_X + 1, NEAR)),
              avr_reg(AVR_ZERO));
    if (avr_is_fp(t))
        emit2(g, AVR_CPC, avr_reg(AVR_X), avr_reg(AVR_ZERO));
}

static void clean_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    bool fp            = avr_is_fp(val_type(g, src));
    int n              = avr_type_size(val_type(g, dst));
    bool own;
    Regs w;
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
    case TAC_UNARY_NEGATE_DOUBLE:
        w = work_regs(g, dst, n, !fp, &own);
        if (fp && w.r[3] < 16) {
            own = false;
            w   = scratch_regs(n);
        }
        load_regs(g, src, &w, EXT_TYPE);
        if (fp)
            emit2(g, AVR_SUBI, avr_reg(w.r[3]), avr_imm(0x80));
        else
            negate(g, w.r, n);
        finish(g, dst, &w, own);
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        w = work_regs(g, dst, n, false, &own);
        load_regs(g, src, &w, EXT_TYPE);
        for (int i = 0; i < n; i++)
            emit1(g, AVR_COM, avr_reg(w.r[i]));
        finish(g, dst, &w, own);
        break;
    case TAC_UNARY_NOT:
        clean_test(g, src);
        set_result(g, AVR_BREQ, dst);
        break;
    default:
        internal_error("avr: %s: unary operator %d is not implemented", gen_name(g),
                       in->u.unary.op);
    }
}

static void clean_binary(Gen *g, const Tac_Instruction *in)
{
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *s1 = in->u.binary.src1, *s2 = in->u.binary.src2, *dst = in->u.binary.dst;
    const Tac_Type *t = operand_type(g, s1, s2);
    Cond c;
    if (compare_cond(op, unsigned_compare(op) || t->kind == TAC_TYPE_POINTER, &c)) {
        clean_compare(g, c, s1, s2, avr_type_size(t), dst);
        return;
    }
    int n = avr_type_size(val_type(g, dst));
    if (is_shift(op)) { // by a constant
        bool own;
        Regs w = work_regs(g, dst, n, false, &own);
        load_regs(g, s1, &w, EXT_TYPE);
        shift_const(g, shift_of(op), w.r, n, (int)(const_bits(s2->u.constant) & 0xff));
        finish(g, dst, &w, own);
        return;
    }
    clean_arith(g, op, s1, s2, dst, n);
}

// `n` bytes of aggregate or variable `name` from `off` = src, extended as `ext` says.
static void copy_to_mem(Gen *g, const Tac_Val *src, const char *name, int off, int n, Ext ext)
{
    const Tac_Type *st = val_type(g, src);
    int ss = avr_type_size(st), m = ss < n ? ss : n;
    int tmp = AVR_TMP;
    if (src->kind == TAC_VAL_CONSTANT) {
        uint64_t bits = const_extended(src->u.constant, ss, n, ext);
        for (int i = 0; i < n; i++) {
            int b = (int)(i < 8 ? bits >> (8 * i) & 0xff : 0), r = AVR_ZERO;
            if (b) {
                emit2(g, AVR_LDI, avr_reg(AVR_X), avr_imm(b));
                r = AVR_X;
            }
            access_mem(g, true, name, off + i, &r, 1, 3u << AVR_X);
        }
        return;
    }
    Regs vr;
    bool in = val_regs(g, src, &vr);
    if (in) {
        access_mem(g, true, name, off, vr.r, m, 0);
    } else {
        for (int i = 0; i < m; i++) {
            access_mem(g, false, src->u.var_name, i, &tmp, 1, 0);
            access_mem(g, true, name, off + i, &tmp, 1, 0);
        }
    }
    if (m >= n)
        return;
    int fill = AVR_ZERO;
    if (ext_sign(st, ext)) {
        if (in)
            emit2(g, AVR_MOV, avr_reg(AVR_TMP), avr_reg(vr.r[m - 1]));
        else
            access_mem(g, false, src->u.var_name, m - 1, &tmp, 1, 0);
        emit1(g, AVR_LSL, avr_reg(AVR_TMP));
        emit2(g, AVR_SBC, avr_reg(AVR_TMP), avr_reg(AVR_TMP));
        fill = AVR_TMP;
    }
    for (int i = m; i < n; i++)
        access_mem(g, true, name, off + i, &fill, 1, 0);
}

// dst = src converted: its low bytes, or extended as `ext` says.
static void clean_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Ext ext)
{
    const Tac_Type *t = val_type(g, dst);
    if (!avr_is_scalar(t)) {
        copy_aggregate(g, src->u.var_name, 0, dst->u.var_name, 0, avr_type_size(t));
        return;
    }
    Regs w;
    if (val_regs(g, dst, &w))
        load_regs(g, src, &w, ext);
    else
        copy_to_mem(g, src, dst->u.var_name, 0, avr_type_size(t), ext);
}

// dst = *ptr, `size` bytes, the pointer in Z.
static void clean_load(Gen *g, const Tac_Val *ptr, const Tac_Val *dst, int size)
{
    if (!avr_is_scalar(val_type(g, dst))) {
        gen_load(g, ptr, dst, size);
        return;
    }
    Regs z = scratch_regs(2), w;
    load_regs(g, ptr, &z, EXT_TYPE);
    if (val_regs(g, dst, &w)) {
        for (int i = 0; i < w.n; i++) {
            if (i < size)
                emit2(g, AVR_LDD, avr_reg(w.r[i]), avr_disp(AVR_Z, i));
            else
                emit2(g, AVR_MOV, avr_reg(w.r[i]), avr_reg(AVR_ZERO));
        }
        return;
    }
    int n = avr_type_size(val_type(g, dst));
    for (int i = 0; i < n; i++) {
        int r = AVR_ZERO;
        if (i < size) {
            emit2(g, AVR_LDD, avr_reg(AVR_TMP), avr_disp(AVR_Z, i));
            r = AVR_TMP;
        }
        access_mem(g, true, dst->u.var_name, i, &r, 1, 3u << AVR_Z);
    }
}

// *ptr = src, `size` bytes, the pointer in Z.
static void clean_store(Gen *g, const Tac_Val *src, const Tac_Val *ptr, int size)
{
    if (src->kind == TAC_VAL_VAR && !avr_is_scalar(val_type(g, src))) {
        gen_store(g, src, ptr, size);
        return;
    }
    Regs z = scratch_regs(2);
    load_regs(g, ptr, &z, EXT_TYPE);
    for (int i = 0; i < size; i++) {
        int r = byte_of(g, src, i, src->kind == TAC_VAL_CONSTANT ? AVR_X : AVR_TMP, 3u << AVR_Z);
        emit2(g, AVR_STD, avr_disp(AVR_Z, i), avr_reg(r));
    }
}

// dst = ptr + index * scale, the scale a power of two when the index is a variable.
static void clean_add_ptr(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *index = in->u.add_ptr.index, *ptr = in->u.add_ptr.ptr,
                  *dst   = in->u.add_ptr.dst;
    int scale            = in->u.add_ptr.scale, k;
    bool own;
    if (index->kind == TAC_VAL_CONSTANT) {
        int off = (int)(const_bits(index->u.constant) * (uint64_t)scale);
        Regs w  = work_regs(g, dst, 2, true, &own);
        load_regs(g, ptr, &w, EXT_TYPE);
        if (off & 0xffff) {
            emit2(g, AVR_SUBI, avr_reg(w.r[0]), avr_imm(-off & 0xff));
            emit2(g, AVR_SBCI, avr_reg(w.r[1]), avr_imm((-off >> 8) & 0xff));
        }
        finish(g, dst, &w, own);
        return;
    }
    pow2(scale, &k);
    Regs z = scratch_regs(2);
    load_regs(g, index, &z, EXT_TYPE);
    for (int i = 0; i < k; i++) {
        emit1(g, AVR_LSL, avr_reg(AVR_Z));
        emit1(g, AVR_ROL, avr_reg(AVR_Z + 1));
    }
    for (int i = 0; i < 2; i++)
        emit2(g, i == 0 ? AVR_ADD : AVR_ADC, avr_reg(AVR_Z + i),
              avr_reg(byte_of(g, ptr, i, ptr->kind == TAC_VAL_CONSTANT ? AVR_X : AVR_TMP, NEAR)));
    store_regs(g, dst, z.r, 2);
}

// dst = member `offset` of aggregate `name`, of `size` bytes.
static void clean_copy_from_offset(Gen *g, const char *name, int offset, const Tac_Val *dst,
                                   int size)
{
    if (!avr_is_scalar(val_type(g, dst))) {
        gen_copy_from_offset(g, name, offset, dst, size);
        return;
    }
    Regs w;
    if (val_regs(g, dst, &w)) {
        access_mem(g, false, name, offset, w.r, size < w.n ? size : w.n, 0);
        for (int i = size; i < w.n; i++)
            emit2(g, AVR_MOV, avr_reg(w.r[i]), avr_reg(AVR_ZERO));
        return;
    }
    int n = avr_type_size(val_type(g, dst));
    for (int i = 0; i < n; i++) {
        int r = AVR_ZERO;
        if (i < size) {
            r = AVR_TMP;
            access_mem(g, false, name, offset + i, &r, 1, 0);
        }
        access_mem(g, true, dst->u.var_name, i, &r, 1, 0);
    }
}

static void clean_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    clean_test(g, cond);
    char *l = label_name(target);
    emit1(g, if_zero ? AVR_BREQ : AVR_BRNE, avr_label(l));
    xfree(l);
}

// The scratch-free form of `in`, when uses_scratch says it has one.
static void gen_clean(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        clean_convert(g, in->u.copy.src, in->u.copy.dst, EXT_TYPE);
        break;
    case TAC_INSTRUCTION_SIGN_EXTEND:
        clean_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, EXT_SIGN);
        break;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        clean_convert(g, in->u.zero_extend.src, in->u.zero_extend.dst, EXT_ZERO);
        break;
    case TAC_INSTRUCTION_UNARY:
        clean_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        clean_binary(g, in);
        break;
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        clean_cond_jump(g, in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO, in->u.jump_if_zero.condition,
                        in->u.jump_if_zero.target);
        break;
    case TAC_INSTRUCTION_LOAD:
        clean_load(g, in->u.load.src_ptr, in->u.load.dst,
                   access_size(g, in->u.load.src_ptr, in->u.load.dst));
        break;
    case TAC_INSTRUCTION_LOAD_BYTE:
        clean_load(g, in->u.load.src_ptr, in->u.load.dst, 1);
        break;
    case TAC_INSTRUCTION_STORE:
        clean_store(g, in->u.store.src, in->u.store.dst_ptr,
                    access_size(g, in->u.store.dst_ptr, in->u.store.src));
        break;
    case TAC_INSTRUCTION_STORE_BYTE:
        clean_store(g, in->u.store.src, in->u.store.dst_ptr, 1);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        clean_add_ptr(g, in);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        clean_arith(g, TAC_BINARY_SUBTRACT, in->u.ptr_diff.ptr_a, in->u.ptr_diff.ptr_b,
                    in->u.ptr_diff.dst, 2);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET: {
        const Tac_Val *src = in->u.copy_to_offset.src;
        int size           = in->kind == TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET
                                 ? 1
                                 : avr_type_size(val_type(g, src));
        if (src->kind == TAC_VAL_VAR && !avr_is_scalar(val_type(g, src)))
            gen_copy_to_offset(g, src, in->u.copy_to_offset.dst, in->u.copy_to_offset.offset,
                               size);
        else
            copy_to_mem(g, src, in->u.copy_to_offset.dst, in->u.copy_to_offset.offset, size,
                        EXT_TYPE);
        break;
    }
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
        clean_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                               in->u.copy_from_offset.dst,
                               avr_type_size(val_type(g, in->u.copy_from_offset.dst)));
        break;
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        clean_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                               in->u.copy_from_offset.dst, 1);
        break;
    default:
        internal_error("avr: %s: no scratch-free form of %s", gen_name(g),
                       tac_instruction_name(in->kind));
    }
}

// Whether `in` takes the scratch-free form.
static bool is_clean(const Gen *g, const Tac_Instruction *in)
{
    if (!g->alloc || uses_scratch(in, gen_type_of, g))
        return false;
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
    case TAC_INSTRUCTION_LABEL:
    case TAC_INSTRUCTION_JUMP:
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        return false; // one form only
    default:
        return true;
    }
}

bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next)
{
    if (!g->uses || !next || in->kind != TAC_INSTRUCTION_BINARY ||
        (next->kind != TAC_INSTRUCTION_JUMP_IF_ZERO && next->kind != TAC_INSTRUCTION_JUMP_IF_NOT_ZERO))
        return false;
    const Tac_Val *c = next->u.jump_if_zero.condition, *dst = in->u.binary.dst;
    if (c->kind != TAC_VAL_VAR || strcmp(c->u.var_name, dst->u.var_name) != 0)
        return false;
    int v = flow_var(g->flow, dst->u.var_name);
    if (v < 0 || g->uses[v] != 1 || flow_has(g->flow->in_memory, v))
        return false;
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *s1 = in->u.binary.src1, *s2 = in->u.binary.src2;
    const Tac_Type *t = operand_type(g, s1, s2);
    AVR_Op br;
    if (avr_is_fp(t)) {
        br = gen_fp_compare(g, in);
        if (br == AVR_NUM_OPS)
            return false; // nothing emitted
    } else {
        Cond cond;
        if (!compare_cond(op, unsigned_compare(op) || t->kind == TAC_TYPE_POINTER, &cond))
            return false;
        if (is_clean(g, in))
            compare_clean(g, cond, s1, s2, avr_type_size(t));
        else
            compare_naive(g, in, cond, t);
        br = cond.br;
    }
    char *l = label_name(next->u.jump_if_zero.target);
    emit1(g, next->kind == TAC_INSTRUCTION_JUMP_IF_ZERO ? avr_inverse(br) : br, avr_label(l));
    xfree(l);
    return true;
}

void gen_instr(Gen *g, const Tac_Instruction *in, bool last)
{
    if (is_clean(g, in)) {
        gen_clean(g, in);
        return;
    }
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
            internal_error("avr: %s: the address of a constant", gen_name(g));
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
        internal_error("avr: %s: %s is not implemented", gen_name(g),
                       tac_instruction_name(in->kind));
    }
}
