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
    a32_new_block(g->fn, l);
    xfree(l);
}

// A branch to TAC label `tac` when condition `cond` holds.
void gen_branch(Gen *g, int cond, const char *tac)
{
    char *l                              = label_name(tac);
    emit1(g, A32_B, a32_sym(l, 0))->cond = cond;
    xfree(l);
}

// Branch to `target` when `cond` is zero (or nonzero).  ARM state has no cbz: a
// cmp, or for a long long an orrs of its words.
static void gen_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *target)
{
    const Tac_Type *t = val_type(g, cond);
    if (a32_is_fp(t)) {
        fp_test_zero(g, cond); // a NaN is not zero: unordered leaves Z clear
    } else if (a32_is_pair(t)) {
        int lo = use_word(g, T0, cond, t, 0);
        emit3(g, A32_ORR, a32_reg(T0), a32_reg(lo), a32_reg(use_word(g, T1, cond, t, 1)))
            ->set_flags = true;
    } else {
        emit2(g, A32_CMP, a32_reg(use_val(g, T0, cond)), a32_imm(0));
    }
    gen_branch(g, if_zero ? A32_EQ : A32_NE, target);
}

// dst = src, for any type: an aggregate copied as bytes; into a register, loaded or
// moved there; from a register, stored; else an 8-byte scalar as two words, any other
// through r12.  A float or double in memory needs no VFP register to move.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a32_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T0, &sbase, &soff);
        name_addr(g, dst->u.var_name, T1, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a32_size(t), a32_align(t));
        return;
    }
    int dr = var_reg(g, dst), sr = var_reg(g, src);
    const Tac_Type *st = val_type(g, src);
    if (dr >= 0 && var_reg_hi(g, dst) >= 0) {
        WordLoad w[2] = { { dr, src, t, 0 }, { var_reg_hi(g, dst), src, t, 1 } };
        load_words(g, w, 2);
        return;
    }
    if (dr >= 0 && a32_is_vfp(dr) == a32_is_fp(st)) {
        load_as(g, dr, src, t);
        if (!a32_is_vfp(dr) && src->kind == TAC_VAL_VAR && st->kind != t->kind)
            gen_canon(g, dr, dr, t);
        return;
    }
    if (sr >= 0 && var_reg_hi(g, src) >= 0) {
        store_pair(g, dst, sr, var_reg_hi(g, src));
        return;
    }
    if (sr >= 0 && dr < 0 && a32_is_vfp(sr) == a32_is_fp(t)) {
        store_val(g, sr, dst);
        return;
    }
    if (a32_size(t) == 8) {
        load_word(g, T0, src, t, 0);
        load_word(g, T1, src, t, 1);
        store_pair(g, dst, T0, T1);
        return;
    }
    load_as(g, T0, src, t);
    store_val(g, T0, dst);
}

// The type stored through pointer value `ptr`, or NULL when not known.
static const Tac_Type *pointee(Gen *g, const Tac_Val *ptr)
{
    const Tac_Type *t = val_type(g, ptr);
    return t->kind == TAC_TYPE_POINTER ? t->u.pointer.target_type : NULL;
}

// Store value `src` of type `t` at base + off: an aggregate copied from its object, an
// 8-byte scalar as two words, any other through r12; a large offset through r10.
// `base` is not r12 or r10.
static void store_to(Gen *g, const Tac_Val *src, const Tac_Type *t, int base, int64_t off)
{
    if (a32_is_aggregate(t)) {
        int sbase;
        int64_t soff;
        name_addr(g, src->u.var_name, T2, &sbase, &soff);
        gen_memcopy(g, base, off, sbase, soff, a32_size(t), a32_align(t));
        return;
    }
    int r = var_reg(g, src);
    if (r >= 0 && a32_is_vfp(r) && a32_is_fp(t) &&
        a32_is_double(t) == a32_is_double(val_type(g, src))) {
        store_mem(g, r, t, base, off, T2);
        return;
    }
    if (a32_size(t) == 8) {
        for (int half = 0; half < 2; half++) {
            int w = use_word(g, T0, src, t, half);
            emit2(g, A32_STR, a32_reg(w), mem(g, A32_STR, base, off + 4 * half, T2));
        }
        return;
    }
    store_mem(g, use_as(g, T0, src, t), t, base, off, T2);
}

// Load the value of type `t` at base + off into variable `dst`.  `base` is not lr,
// nor r10 for an aggregate.
static void load_from(Gen *g, const Tac_Val *dst, const Tac_Type *t, int base, int64_t off)
{
    if (a32_is_aggregate(t)) {
        int dbase;
        int64_t doff;
        name_addr(g, dst->u.var_name, T2, &dbase, &doff);
        gen_memcopy(g, dbase, doff, base, off, a32_size(t), a32_align(t));
        return;
    }
    int r = var_reg(g, dst), hi = var_reg_hi(g, dst);
    if (hi >= 0) {
        // The word in the base register last.
        for (int k = 0; k < 2; k++) {
            int half = (k == 0) == (base == r);
            int reg  = half ? hi : r;
            emit2(g, A32_LDR, a32_reg(reg), mem(g, A32_LDR, base, off + 4 * half, reg));
        }
        return;
    }
    if (r >= 0 && a32_is_vfp(r) == a32_is_fp(t)) {
        load_mem(g, r, t, base, off);
        return;
    }
    if (a32_size(t) == 8) {
        emit2(g, A32_LDR, a32_reg(T1), mem(g, A32_LDR, base, off + 4, T1));
        emit2(g, A32_LDR, a32_reg(T0), mem(g, A32_LDR, base, off, T0));
        store_pair(g, dst, T0, T1);
        return;
    }
    load_mem(g, T0, t, base, off);
    store_val(g, T0, dst);
}

// dst = *src_ptr.
static void gen_load(Gen *g, const Tac_Val *src_ptr, const Tac_Val *dst)
{
    load_from(g, dst, val_type(g, dst), use_val(g, T0, src_ptr), 0);
}

// *dst_ptr = src, in the width of the pointee (or of src when that is not known).
static void gen_store(Gen *g, const Tac_Val *src, const Tac_Val *dst_ptr)
{
    const Tac_Type *t = pointee(g, dst_ptr);
    if (!t || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE ||
        (a32_is_aggregate(t) && !a32_is_aggregate(val_type(g, src))))
        t = val_type(g, src);
    store_to(g, src, t, use_val(g, T1, dst_ptr), 0);
}

// dst = ptr + index * scale (bytes): a shifted index for a power of two, else the index
// multiplied first.  A long long index counts by its low word.
static void gen_add_ptr(Gen *g, const Tac_Instruction *in)
{
    int scale          = in->u.add_ptr.scale;
    const Tac_Val *idx = in->u.add_ptr.index;
    const Tac_Type *it = val_type(g, idx);
    const Tac_Val *dst = in->u.add_ptr.dst;
    int d              = def_reg(g, T0, dst);
    if (idx->kind == TAC_VAL_CONSTANT) {
        int64_t i = (int32_t)(uint32_t)const_bits(idx->u.constant, it);
        gen_addr(g, d, use_val(g, T0, in->u.add_ptr.ptr), i * scale);
        store_val(g, d, dst);
        return;
    }
    int shift = 0;
    while ((1 << shift) < scale)
        shift++;
    int i = a32_is_pair(it) ? use_word(g, T1, idx, it, 0) : use_val(g, T1, idx);
    if ((1 << shift) != scale) {
        gen_li(g, T0, scale);
        emit3(g, A32_MUL, a32_reg(T1), a32_reg(i), a32_reg(T0));
        i     = T1;
        shift = 0;
    }
    int p = use_val(g, T0, in->u.add_ptr.ptr);
    emit3(g, A32_ADD, a32_reg(d), a32_reg(p), shift ? a32_shift(i, A32_SHIFT_LSL, shift) : a32_reg(i));
    store_val(g, d, dst);
}

// dst = a - b, a byte count.
static void gen_ptr_diff(Gen *g, const Tac_Instruction *in)
{
    int a = use_val(g, T0, in->u.ptr_diff.ptr_a), b = use_val(g, T1, in->u.ptr_diff.ptr_b);
    int d = def_reg(g, T0, in->u.ptr_diff.dst);
    emit3(g, A32_SUB, a32_reg(d), a32_reg(a), a32_reg(b));
    store_val(g, d, in->u.ptr_diff.dst);
}

// The scalar type at byte `offset` of aggregate type `t`, or NULL.  Of several union
// members there, one of `size` bytes is preferred, else the first.
static const Tac_Type *scalar_at(const Tac_Type *t, int offset, int size)
{
    if (!t)
        return NULL;
    if (t->kind == TAC_TYPE_ARRAY) {
        int esize = a32_size(t->u.array.elem_type);
        return esize > 0 ? scalar_at(t->u.array.elem_type, offset % esize, size) : NULL;
    }
    if (t->kind != TAC_TYPE_STRUCTURE)
        return offset == 0 ? t : NULL;
    const Tac_Type *first = NULL;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (offset < m->offset || offset >= m->offset + a32_size(m->type))
            continue;
        const Tac_Type *s = scalar_at(m->type, offset - m->offset, size);
        if (s && a32_size(s) == size)
            return s;
        if (!first)
            first = s;
    }
    return first;
}

// Member store: aggregate `dst` at byte `offset` = src.  A constant takes the width of
// the member there (its own kind may be wider); a byte copy is one byte.
static void gen_copy_to_offset(Gen *g, const Tac_Val *src, const char *dst, int offset, bool byte)
{
    static const Tac_Type uchar = { .kind = TAC_TYPE_UCHAR };
    const Tac_Type *t           = val_type(g, src);
    if (byte) {
        t = &uchar;
    } else if (src->kind == TAC_VAL_CONSTANT) {
        const Tac_Type *m = scalar_at(name_type(g, dst), offset, a32_size(t));
        if (m && a32_is_fp(m) == a32_is_fp(t) && !a32_is_aggregate(m))
            t = m;
    }
    int base;
    int64_t off;
    name_addr(g, dst, T1, &base, &off);
    store_to(g, src, t, base, off + offset);
}

// Member load: dst = aggregate `src` at byte `offset`.
static void gen_copy_from_offset(Gen *g, const char *src, int offset, const Tac_Val *dst, bool byte)
{
    const Tac_Type *t = val_type(g, dst);
    if (byte && a32_size(t) != 1)
        fatal_error("arm32: %s: byte copy into %s", gen_name(g), dst->u.var_name);
    int base;
    int64_t off;
    name_addr(g, src, T0, &base, &off);
    load_from(g, dst, t, base, off + offset);
}

// dst = &src, of a named object or function.
static void gen_get_address(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    int d = def_reg(g, T0, dst);
    int base;
    int64_t off;
    name_addr(g, src->u.var_name, d, &base, &off);
    gen_addr(g, d, base, off);
    store_val(g, d, dst);
}

// An integer conversion.  A store truncates to the destination's width; a loaded value
// is extended by the source's own type, so an extension is explicit only where that
// differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type), a zero extension of a signed one.  A long long's
// high word is the low one's sign, or zero, by the conversion's kind.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (src->kind == TAC_VAL_CONSTANT) {
        gen_copy(g, src, dst); // converted as it is loaded
        return;
    }
    if (a32_is_pair(st) && a32_is_pair(dt)) {
        gen_copy(g, src, dst);
        return;
    }
    if (a32_is_pair(st)) {
        store_val(g, use_word(g, T0, src, st, 0), dst);
        return;
    }
    int r     = use_val(g, T0, src);
    int ssize = a32_size(st);
    A32_Op op = A32_EPILOGUE;
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND && a32_is_unsigned(st))
        op = ssize == 1 ? A32_SXTB : ssize == 2 ? A32_SXTH : op;
    else if (kind == TAC_INSTRUCTION_ZERO_EXTEND && !a32_is_unsigned(st))
        op = ssize == 1 ? A32_UXTB : ssize == 2 ? A32_UXTH : op;
    if (op != A32_EPILOGUE) {
        int d = a32_is_pair(dt) ? T0 : def_reg(g, T0, dst);
        emit2(g, op, a32_reg(d), a32_reg(r));
        r = d;
    }
    if (!a32_is_pair(dt)) {
        store_val(g, r, dst);
        return;
    }
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND)
        emit2(g, A32_MOV, a32_reg(T1), a32_shift(r, A32_SHIFT_ASR, 31));
    else
        gen_li(g, T1, 0);
    store_pair(g, dst, r, T1);
}

// A conversion between an integer and FP.
static void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    if (a32_is_pair(val_type(g, src)) || a32_is_pair(val_type(g, dst))) {
        gen_ll_fp_convert(g, src, dst, kind);
        return;
    }
    gen_fp_convert32(g, src, dst, kind);
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

int compare_cond(Tac_BinaryOperator op, bool is_unsigned)
{
    switch (op) {
    case TAC_BINARY_EQUAL:
        return A32_EQ;
    case TAC_BINARY_NOT_EQUAL:
        return A32_NE;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return is_unsigned ? A32_LO : A32_LT;
    case TAC_BINARY_LESS_OR_EQUAL:
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A32_LS : A32_LE;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return is_unsigned ? A32_HI : A32_GT;
    case TAC_BINARY_GREATER_OR_EQUAL:
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return is_unsigned ? A32_HS : A32_GE;
    default:
        return -1;
    }
}

bool unsigned_operation(const Tac_Type *t, Tac_BinaryOperator op)
{
    return t->kind == TAC_TYPE_POINTER || unsigned_op(op);
}

void set_cond(Gen *g, int reg, int cond)
{
    gen_li(g, reg, 0);
    emit2(g, A32_MOV, a32_reg(reg), a32_imm(1))->cond = cond;
}

A32_Operand operand2(Gen *g, A32_Op *op, const Tac_Val *v, const Tac_Type *t, int scratch)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        uint32_t c = (uint32_t)const_bits(v->u.constant, t);
        if (a32_operand2_imm(c))
            return a32_imm(c);
        // cmp and cmn set the same flags for c and -c, but for 0 and 1 << 31, which
        // are modified immediates.
        A32_Op alt  = *op;
        uint32_t ac = -c;
        if (*op == A32_ADD)
            alt = A32_SUB;
        else if (*op == A32_SUB)
            alt = A32_ADD;
        else if (*op == A32_CMP)
            alt = A32_CMN;
        else if (*op == A32_AND)
            alt = A32_BIC, ac = ~c;
        if (alt != *op && a32_operand2_imm(ac)) {
            *op = alt;
            return a32_imm(ac);
        }
    }
    return a32_reg(use_as(g, scratch, v, t));
}

static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.unary.src);
    if (a32_is_pair(t)) {
        gen_ll_unary(g, in);
        return;
    }
    if (a32_is_fp(t)) {
        gen_fp_unary(g, in);
        return;
    }
    A32_Operand a = a32_reg(use_as(g, T0, in->u.unary.src, t));
    int d         = def_reg(g, T0, in->u.unary.dst);
    switch (in->u.unary.op) {
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        emit3(g, A32_RSB, a32_reg(d), a, a32_imm(0));
        break;
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        emit2(g, A32_MVN, a32_reg(d), a);
        break;
    case TAC_UNARY_NOT:
        emit2(g, A32_CMP, a, a32_imm(0));
        set_cond(g, d, A32_EQ);
        break;
    case TAC_UNARY_NEGATE_DOUBLE:
        fatal_error("arm32: %s: NEGATE_DOUBLE of an integer", gen_name(g));
    }
    store_val(g, d, in->u.unary.dst);
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *t = val_type(g, in->u.binary.src1);
    if (a32_is_pair(t)) {
        gen_ll_binary(g, in);
        return;
    }
    if (a32_is_fp(t)) {
        gen_fp_binary(g, in);
        return;
    }
    Tac_BinaryOperator op = in->u.binary.op;
    const Tac_Val *b      = in->u.binary.src2;
    bool u                = unsigned_operation(t, op);
    const Tac_Val *dst    = in->u.binary.dst;
    int dr                = def_reg(g, T0, dst);
    A32_Operand d = a32_reg(dr), a = a32_reg(use_as(g, T0, in->u.binary.src1, t)), rb;
    int cond = compare_cond(op, u);
    if (cond >= 0) {
        A32_Op o       = A32_CMP;
        A32_Operand ob = operand2(g, &o, b, t, T1);
        emit2(g, o, a, ob);
        set_cond(g, dr, cond);
        store_val(g, dr, dst);
        return;
    }
    A32_Op o;
    switch (op) {
    case TAC_BINARY_ADD:
    case TAC_BINARY_ADD_UNSIGNED:
        o = A32_ADD;
        break;
    case TAC_BINARY_SUBTRACT:
    case TAC_BINARY_SUBTRACT_UNSIGNED:
        o = A32_SUB;
        break;
    case TAC_BINARY_BITWISE_AND:
        o = A32_AND;
        break;
    case TAC_BINARY_BITWISE_OR:
        o = A32_ORR;
        break;
    case TAC_BINARY_BITWISE_XOR:
        o = A32_EOR;
        break;
    case TAC_BINARY_MULTIPLY:
    case TAC_BINARY_MULTIPLY_UNSIGNED:
        rb = a32_reg(use_as(g, T1, b, t));
        emit3(g, A32_MUL, d, a, rb);
        store_val(g, dr, dst);
        return;
    case TAC_BINARY_DIVIDE:
    case TAC_BINARY_DIVIDE_UNSIGNED:
        rb = a32_reg(use_as(g, T1, b, t));
        emit3(g, u ? A32_UDIV : A32_SDIV, d, a, rb);
        store_val(g, dr, dst);
        return;
    case TAC_BINARY_REMAINDER:
    case TAC_BINARY_REMAINDER_UNSIGNED:
        // a - (a / b) * b, the quotient in r10
        rb = a32_reg(use_as(g, T1, b, t));
        emit3(g, u ? A32_UDIV : A32_SDIV, a32_reg(T2), a, rb);
        emit4(g, A32_MLS, d, a32_reg(T2), rb, a);
        store_val(g, dr, dst);
        return;
    case TAC_BINARY_LEFT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT:
    case TAC_BINARY_RIGHT_SHIFT_LOGICAL: {
        // The count at its own type.  A constant one beyond the width is undefined:
        // its low five bits are used.
        A32_Op s           = op == TAC_BINARY_LEFT_SHIFT ? A32_LSL : u ? A32_LSR : A32_ASR;
        const Tac_Type *ct = val_type(g, b);
        if (b->kind == TAC_VAL_CONSTANT) {
            int n = (int)(const_bits(b->u.constant, ct) & 31);
            if (n)
                emit3(g, s, d, a, a32_imm(n));
            else
                move_reg(g, dr, a.reg, t);
        } else {
            rb = a32_reg(a32_is_pair(ct) ? use_word(g, T1, b, ct, 0) : use_val(g, T1, b));
            emit3(g, s, d, a, rb);
        }
        store_val(g, dr, dst);
        return;
    }
    default:
        fatal_error("arm32: %s: bad integer operator %d", gen_name(g), op);
    }
    A32_Operand ob = operand2(g, &o, b, t, T1);
    emit3(g, o, d, a, ob);
    store_val(g, dr, dst);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL:
        gen_label(g, in->u.label.name);
        break;
    case TAC_INSTRUCTION_JUMP:
        gen_branch(g, A32_AL, in->u.jump.target);
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
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        gen_fp_convert(g, in->u.int_to_double.src, in->u.int_to_double.dst, in->kind);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        gen_get_address(g, in->u.get_address.src, in->u.get_address.dst);
        break;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        gen_load(g, in->u.load.src_ptr, in->u.load.dst);
        break;
    case TAC_INSTRUCTION_STORE:
    case TAC_INSTRUCTION_STORE_BYTE:
        gen_store(g, in->u.store.src, in->u.store.dst_ptr);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        gen_add_ptr(g, in);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        gen_ptr_diff(g, in);
        break;
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.ptr_to_char_ptr.src, in->u.ptr_to_char_ptr.dst);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
        gen_copy_to_offset(g, in->u.copy_to_offset.src, in->u.copy_to_offset.dst,
                           in->u.copy_to_offset.offset,
                           in->kind == TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET);
        break;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        gen_copy_from_offset(g, in->u.copy_from_offset.src, in->u.copy_from_offset.offset,
                             in->u.copy_from_offset.dst,
                             in->kind == TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET);
        break;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        break;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        break;
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        gen_call(g, in);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("arm32: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
