//
// Calls, parameters and return values: the System V psABI.  An integer or pointer goes
// in the next of rdi, rsi, rdx, rcx, r8, r9, a float or double in the next of
// xmm0-xmm7; once a class runs out, its values go on the stack, each in an 8-byte
// slot, the first at the lowest address.  The stack arguments are stored into an
// outgoing area at the bottom of the frame, so rsp stays fixed and 16-byte aligned.
// A long double goes on the stack in a 16-byte aligned slot.  A narrow argument or
// result is extended to 32 bits by the sender (clang relies on it) and again by the
// receiver, a store truncating and a load extending by type.  The result comes back
// in rax, xmm0, or st(0) for a long double.
//
// A struct or union of up to 16 bytes goes by its eightbytes' classes
// (tac_sysv64_class): each INTEGER one in the next general register, each SSE one in
// the next xmm register, all of them or, when they do not all fit, the whole struct on
// the stack, the registers staying free for later arguments.  A MEMORY struct, or one
// of a long double, goes on the stack.  A struct result comes back in rax/rdx and
// xmm0/xmm1 by the same classes, in st(0) for a struct of a long double, or else
// through memory whose address the caller passes in rdi as a hidden first argument,
// the callee returning it in rax.
//
// A variadic call differs only in %al, an upper bound on the xmm registers used.  A
// variadic function saves the argument registers into a register save area, and
// va_start (__va_start, expanded here) points a va_list at it.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

static const int int_regs[6] = { X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9 };

// Where one argument goes: up to two registers (an aggregate one per eightbyte, by its
// class `cls`), or the stack at byte offset `stack`.
typedef struct {
    int reg[2]; // reg[0] -1 on the stack
    int cls;
    int stack;
} ArgLoc;

typedef struct {
    int next_int, next_sse, stack;
} ArgState;

// The registers of an aggregate of class `cls` from the next free ones, when it is
// passed in registers and they all fit.
static bool aggregate_regs(ArgState *s, int cls, ArgLoc *a)
{
    if (cls == TAC_SYSV64_MEMORY || cls == TAC_SYSV64_X87)
        return false;
    int need_int = 0, need_sse = 0;
    for (int i = 0; i < 2; i++) {
        need_int += TAC_SYSV64_EIGHTBYTE(cls, i) == TAC_SYSV64_INTEGER;
        need_sse += TAC_SYSV64_EIGHTBYTE(cls, i) == TAC_SYSV64_SSE;
    }
    if (s->next_int + need_int > 6 || s->next_sse + need_sse > 8)
        return false;
    a->cls = cls;
    for (int i = 0; i < 2; i++) {
        int e = TAC_SYSV64_EIGHTBYTE(cls, i);
        a->reg[i] = e == TAC_SYSV64_INTEGER ? int_regs[s->next_int++]
                    : e == TAC_SYSV64_SSE   ? X86_XMM0 + s->next_sse++
                                            : -1;
    }
    return true;
}

// A long double always goes on the stack, in a 16-byte aligned slot; so does an
// aggregate not in registers, whole, in slots of 8 bytes, 16-byte aligned when it is.
static ArgLoc classify(ArgState *s, const Tac_Type *t)
{
    ArgLoc a = { { -1, -1 }, 0, 0 };
    if (x86_is_aggregate(t) && aggregate_regs(s, tac_sysv64_class(t), &a))
        return a;
    if (x86_is_ld(t) || x86_is_aggregate(t)) {
        int align = x86_align(t) > 8 ? 16 : 8;
        s->stack  = (s->stack + align - 1) / align * align;
        a.stack   = s->stack;
        s->stack += (x86_size(t) + 7) / 8 * 8;
    } else if (x86_is_fp(t) && s->next_sse < 8) {
        a.reg[0] = X86_XMM0 + s->next_sse++;
    } else if (!x86_is_fp(t) && s->next_int < 6) {
        a.reg[0] = int_regs[s->next_int++];
    } else {
        a.stack = s->stack;
        s->stack += 8;
    }
    return a;
}

// The registers a result of class `cls` comes back in: rax and rdx, xmm0 and xmm1.
static void result_regs(int cls, int reg[2])
{
    int next_int = 0, next_sse = 0;
    for (int i = 0; i < 2; i++) {
        int e  = TAC_SYSV64_EIGHTBYTE(cls, i);
        reg[i] = e == TAC_SYSV64_INTEGER ? (next_int++ ? X86_RDX : X86_RAX)
                 : e == TAC_SYSV64_SSE   ? X86_XMM0 + next_sse++
                                         : -1;
    }
}

static const Tac_Type t_uchar = { .kind = TAC_TYPE_UCHAR }, t_ushort = { .kind = TAC_TYPE_USHORT },
                      t_uint = { .kind = TAC_TYPE_UINT }, t_ulong = { .kind = TAC_TYPE_ULONG },
                      t_float = { .kind = TAC_TYPE_FLOAT }, t_double = { .kind = TAC_TYPE_DOUBLE };

// The pieces of an eightbyte holding `size` bytes (1-8): power-of-two sizes from the
// largest down, at ascending offsets.  Returns their number.
static int pieces(int size, int piece[4])
{
    int n = 0;
    for (int p = 8; p > 0; p /= 2) {
        if (size >= p) {
            piece[n++] = p;
            size -= p;
        }
    }
    return n;
}

static const Tac_Type *piece_type(int size)
{
    return size == 1 ? &t_uchar : size == 2 ? &t_ushort : size == 4 ? &t_uint : &t_ulong;
}

// Load the eightbyte at byte `off` of the aggregate at `base` (borrowed), of class `e`
// and `size` bytes, into `reg`, reading none past its end: a general register is put
// together from the pieces, the highest first, through r11.
static void load_eightbyte(Gen *g, int reg, int e, X86_Operand base, int off, int size)
{
    if (size > 8)
        size = 8;
    if (e == TAC_SYSV64_SSE) {
        load_mem(g, reg, size > 4 ? &t_double : &t_float, mem_at(base, off));
        return;
    }
    int piece[4], n = pieces(size, piece);
    int at = size;
    for (int i = n - 1; i >= 0; i--) {
        at -= piece[i];
        if (i == n - 1) {
            load_mem(g, reg, piece_type(piece[i]), mem_at(base, off + at));
            continue;
        }
        emit2(g, X86_SHL, X86_Q, x86_imm(piece[i] * 8), x86_reg(reg, X86_Q));
        load_mem(g, T2, piece_type(piece[i]), mem_at(base, off + at));
        emit2(g, X86_OR, X86_Q, x86_reg(T2, X86_Q), x86_reg(reg, X86_Q));
    }
}

// Store `reg`, the eightbyte at byte `off` of the aggregate at `base` (borrowed), of
// class `e` and `size` bytes, writing none past its end: a general register goes in
// pieces, the lowest first, shifted down after each.
static void store_eightbyte(Gen *g, int reg, int e, X86_Operand base, int off, int size)
{
    if (size > 8)
        size = 8;
    if (e == TAC_SYSV64_SSE) {
        store_mem(g, reg, size > 4 ? &t_double : &t_float, mem_at(base, off));
        return;
    }
    int piece[4], n = pieces(size, piece);
    int at = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0)
            emit2(g, X86_SHR, X86_Q, x86_imm(piece[i - 1] * 8), x86_reg(reg, X86_Q));
        store_mem(g, reg, piece_type(piece[i]), mem_at(base, off + at));
        at += piece[i];
    }
}

// Move aggregate `name` of type `t` and class `cls` between memory and registers.
static void load_aggregate(Gen *g, const int reg[2], int cls, const char *name, const Tac_Type *t)
{
    X86_Operand base = name_mem(g, name, 0);
    for (int i = 0; i < 2 && reg[i] >= 0; i++)
        load_eightbyte(g, reg[i], TAC_SYSV64_EIGHTBYTE(cls, i), base, 8 * i, x86_size(t) - 8 * i);
    xfree(base.sym);
}

static void store_aggregate(Gen *g, const int reg[2], int cls, X86_Operand base, const Tac_Type *t)
{
    for (int i = 0; i < 2 && reg[i] >= 0; i++)
        store_eightbyte(g, reg[i], TAC_SYSV64_EIGHTBYTE(cls, i), base, 8 * i, x86_size(t) - 8 * i);
    xfree(base.sym);
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

// Whether a result of type `t` is written through the address the caller passes in
// rdi: a MEMORY struct or union.
static bool struct_result(const Tac_Type *t)
{
    return t && x86_is_aggregate(t) && tac_sysv64_class(t) == TAC_SYSV64_MEMORY;
}

// Whether a result of type `t` comes back in st(0): a long double, or a struct of one.
static bool x87_result(const Tac_Type *t)
{
    return t && (x86_is_ld(t) || (x86_is_aggregate(t) && tac_sysv64_class(t) == TAC_SYSV64_X87));
}

// A variadic function saves rdi-r9 and, when %al says any xmm register carries an
// argument, xmm0-xmm7 into the 176-byte register save area, where va_arg finds them.
static void save_varargs(Gen *g)
{
    g->va.save = alloc_slot(g, NULL, NULL, 176, 16);
    for (int i = 0; i < 6; i++)
        emit2(g, X86_MOV, X86_Q, x86_reg(int_regs[i], X86_Q), x86_mem(X86_RBP, g->va.save + 8 * i));
    char skip[32];
    new_label(skip);
    emit2(g, X86_TEST, X86_B, x86_reg(X86_RAX, X86_B), x86_reg(X86_RAX, X86_B));
    emit1(g, X86_J, X86_Q, x86_label(skip))->cond = X86_CC_E;
    for (int i = 0; i < 8; i++)
        emit2(g, X86_MOVSD, X86_Q, x86_xmm(X86_XMM0 + i), x86_mem(X86_RBP, g->va.save + 48 + 16 * i));
    x86_new_block(g->fn, skip);
}

// va_start(ap), a call of __va_start(ap): fill the va_list
// { gp_offset, fp_offset, overflow_arg_area, reg_save_area }.
static void gen_va_start(Gen *g, const Tac_Instruction *in)
{
    if (!g->va.save)
        fatal_error("x86: %s: va_start in a function without ...", gen_name(g));
    if (!in->u.fun_call.args || in->u.fun_call.args->next)
        fatal_error("x86: %s: __va_start takes one argument", gen_name(g));
    load_val(g, T0, in->u.fun_call.args);
    emit2(g, X86_MOV, X86_L, x86_imm(g->va.gp), x86_mem(T0, 0));
    emit2(g, X86_MOV, X86_L, x86_imm(g->va.fp), x86_mem(T0, 4));
    emit2(g, X86_LEA, X86_Q, x86_mem(X86_RBP, g->va.overflow), x86_reg(T1, X86_Q));
    emit2(g, X86_MOV, X86_Q, x86_reg(T1, X86_Q), x86_mem(T0, 8));
    emit2(g, X86_LEA, X86_Q, x86_mem(X86_RBP, g->va.save), x86_reg(T1, X86_Q));
    emit2(g, X86_MOV, X86_Q, x86_reg(T1, X86_Q), x86_mem(T0, 16));
}

// Each parameter gets a slot: one passed in a register is stored there at its own
// width, which truncates; one on the stack is read where the caller put it, above the
// return address.  A load extends by type, so a narrow argument is re-extended whatever
// the caller left in the upper bits.
void gen_params(Gen *g)
{
    ArgState s = { 0 };
    if (g->tl->u.function.variadic)
        save_varargs(g);
    if (struct_result(ret_type(g->tl->u.function.type))) {
        g->ret_ptr = alloc_slot(g, NULL, NULL, 8, 8);
        emit2(g, X86_MOV, X86_Q, x86_reg(X86_RDI, X86_Q), x86_mem(X86_RBP, g->ret_ptr));
        s.next_int = 1;
    }
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("x86: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(&s, t);
        if (a.reg[0] >= 0) {
            int off = alloc_slot(g, p->name, t, x86_size(t), x86_align(t));
            if (x86_is_aggregate(t))
                store_aggregate(g, a.reg, a.cls, x86_mem(X86_RBP, off), t);
            else
                store_mem(g, a.reg[0], t, x86_mem(X86_RBP, off));
        } else {
            place_slot(g, p->name, t, 16 + a.stack);
        }
    }
    g->va.gp       = 8 * s.next_int;
    g->va.fp       = 48 + 16 * s.next_sse;
    g->va.overflow = 16 + s.stack;
}


// The type an argument is passed as: the declared parameter type of an integer, when
// there is one (a constant's own kind may differ), else its own.
static const Tac_Type *arg_type(const Tac_Type *t, const Tac_Type *want)
{
    if (want && !x86_is_fp(t) && !x86_is_fp(want) && !x86_is_ld(want) && !x86_is_aggregate(want))
        return want;
    return t;
}

// Load argument `v`, passed as type `as`, into register `reg`.
static void load_arg(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as)
{
    if (x86_is_xmm(reg))
        load_val(g, reg, v);
    else
        load_int_as(g, reg, v, as);
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    if (!in->u.fun_call.indirect && strcmp(in->u.fun_call.fun_name, "__va_start") == 0) {
        gen_va_start(g, in);
        return;
    }
    const Tac_Type *ft = in->u.fun_call.fun_type;
    const Tac_Val *dst = in->u.fun_call.dst;
    const Tac_Type *rt = ret_type(ft);
    if (!rt && dst)
        rt = val_type(g, dst);
    // The stack arguments first, through rax or xmm14; then the registers, straight
    // from memory.
    for (int pass = 0; pass < 2; pass++) {
        const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
        ArgState s           = { .next_int = struct_result(rt) };
        for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next) {
            const Tac_Type *t  = val_type(g, v);
            const Tac_Type *as = arg_type(t, want);
            ArgLoc a           = classify(&s, t);
            if (want)
                want = want->next;
            if (pass == 1 && a.reg[0] >= 0 && x86_is_aggregate(t)) {
                load_aggregate(g, a.reg, a.cls, v->u.var_name, t);
            } else if (pass == 1 && a.reg[0] >= 0) {
                load_arg(g, a.reg[0], v, as);
            } else if (pass == 0 && a.reg[0] < 0 && x86_is_aggregate(t)) {
                gen_memcopy(g, x86_mem(X86_RSP, a.stack), name_mem(g, v->u.var_name, 0),
                            x86_size(t), x86_align(t));
            } else if (pass == 0 && x86_is_ld(t)) {
                gen_ld_copy(g, v, x86_mem(X86_RSP, a.stack));
            } else if (pass == 0 && a.reg[0] < 0) {
                int r = x86_is_fp(t) ? F0 : T0;
                load_arg(g, r, v, as);
                if (x86_is_fp(t))
                    store_mem(g, r, t, x86_mem(X86_RSP, a.stack));
                else
                    emit2(g, X86_MOV, X86_Q, x86_reg(r, X86_Q), x86_mem(X86_RSP, a.stack));
            }
        }
        if (s.stack > g->outgoing)
            g->outgoing = s.stack;
        // A variadic or unprototyped callee is told how many xmm registers carry
        // arguments.
        if (pass == 1 && (!ft || ft->u.fun_type.variadic))
            gen_li(g, X86_RAX, X86_L, s.next_sse);
    }
    // The result's address in rdi: the destination, or a slot for an unused one.
    if (struct_result(rt)) {
        X86_Operand m = dst ? name_mem(g, dst->u.var_name, 0)
                            : x86_mem(X86_RBP, alloc_slot(g, NULL, NULL, x86_size(rt),
                                                          x86_align(rt)));
        emit2(g, X86_LEA, X86_Q, m, x86_reg(X86_RDI, X86_Q));
    }
    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T2, &fp);
        emit1(g, X86_CALL, X86_Q, x86_indirect(T2));
    } else {
        emit1(g, X86_CALL, X86_Q, x86_label(in->u.fun_call.fun_name));
    }
    // A result in st(0) must be popped even when unused.
    if (struct_result(rt))
        return;
    if (!dst) {
        if (x87_result(rt))
            emit1(g, X86_FSTP, X86_Q, x86_st(0));
        return;
    }
    const Tac_Type *t = val_type(g, dst);
    if (x87_result(t)) {
        emit1(g, X86_FSTPT, X86_Q, name_mem(g, dst->u.var_name, 0));
    } else if (x86_is_aggregate(t)) {
        int cls = tac_sysv64_class(t), reg[2];
        result_regs(cls, reg);
        store_aggregate(g, reg, cls, name_mem(g, dst->u.var_name, 0), t);
    } else {
        store_val(g, x86_is_fp(t) ? X86_XMM0 : X86_RAX, dst);
    }
}

// The result in rax, extended to 32 bits when narrower (clang relies on it), xmm0,
// st(0), the registers of its class, or memory.
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (x86_is_aggregate(t) && g->ret_ptr) {
            // Copied through the address that came in rdi, which goes back in rax.
            emit2(g, X86_MOV, X86_Q, x86_mem(X86_RBP, g->ret_ptr), x86_reg(T1, X86_Q));
            gen_memcopy(g, x86_mem(T1, 0), name_mem(g, v->u.var_name, 0), x86_size(t),
                        x86_align(t));
            emit2(g, X86_MOV, X86_Q, x86_mem(X86_RBP, g->ret_ptr), x86_reg(X86_RAX, X86_Q));
        } else if (struct_result(t)) {
            fatal_error("x86: %s: a struct result without its address", gen_name(g));
        } else if (x86_is_ld(t)) {
            gen_ld_load(g, v);
        } else if (x87_result(t)) {
            emit1(g, X86_FLDT, X86_Q, name_mem(g, v->u.var_name, 0));
        } else if (x86_is_aggregate(t)) {
            int cls = tac_sysv64_class(t), reg[2];
            result_regs(cls, reg);
            load_aggregate(g, reg, cls, v->u.var_name, t);
        } else if (x86_is_fp(t)) {
            load_val(g, X86_XMM0, v);
        } else {
            load_int_as(g, X86_RAX, v, rt && !x86_is_fp(rt) && !x86_is_ld(rt) ? rt : t);
        }
    }
    gen_epilogue(g);
}
