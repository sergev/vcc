//
// Calls, parameters and return values: the System V psABI.  An integer or pointer goes
// in the next of rdi, rsi, rdx, rcx, r8, r9, a float or double in the next of
// xmm0-xmm7; once a class runs out, its values go on the stack, each in an 8-byte
// slot, the first at the lowest address.  The stack arguments are stored into an
// outgoing area at the bottom of the frame, so rsp stays fixed and 16-byte aligned.
// A narrow argument or result is extended to 32 bits by the sender (clang relies on
// it) and again by the receiver, a store truncating and a load extending by type.
// The result comes back in rax or xmm0.
//
#include "codegen.h"
#include "internal.h"

static const int int_regs[6] = { X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9 };

// Where one argument goes: a register, or the stack at byte offset `stack`.
typedef struct {
    int reg; // or -1
    int stack;
} ArgLoc;

typedef struct {
    int next_int, next_sse, stack;
} ArgState;

static ArgLoc classify(Gen *g, ArgState *s, const Tac_Type *t)
{
    if (x86_is_ld(t) || x86_is_aggregate(t))
        fatal_error("x86: %s: an argument of this type is not implemented yet", gen_name(g));
    ArgLoc a = { -1, 0 };
    if (x86_is_fp(t) && s->next_sse < 8)
        a.reg = X86_XMM0 + s->next_sse++;
    else if (!x86_is_fp(t) && s->next_int < 6)
        a.reg = int_regs[s->next_int++];
    if (a.reg < 0) {
        a.stack = s->stack;
        s->stack += 8;
    }
    return a;
}

// Each parameter gets a slot: one passed in a register is stored there at its own
// width, which truncates; one on the stack is read where the caller put it, above the
// return address.  A load extends by type, so a narrow argument is re-extended whatever
// the caller left in the upper bits.
void gen_params(Gen *g)
{
    ArgState s = { 0 };
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("x86: %s: no type for %s", gen_name(g), p->name);
        ArgLoc a = classify(g, &s, t);
        if (a.reg >= 0) {
            int off = alloc_slot(g, p->name, t, x86_size(t), x86_align(t));
            store_mem(g, a.reg, t, x86_mem(X86_RBP, off));
        } else {
            place_slot(g, p->name, t, 16 + a.stack);
        }
    }
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
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
    const Tac_Type *ft = in->u.fun_call.fun_type;
    const Tac_Val *dst = in->u.fun_call.dst;
    // The stack arguments first, through rax or xmm14; then the registers, straight
    // from memory.
    for (int pass = 0; pass < 2; pass++) {
        const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
        ArgState s           = { 0 };
        for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next) {
            const Tac_Type *t  = val_type(g, v);
            const Tac_Type *as = arg_type(t, want);
            ArgLoc a           = classify(g, &s, t);
            if (want)
                want = want->next;
            if (pass == 1 && a.reg >= 0) {
                load_arg(g, a.reg, v, as);
            } else if (pass == 0 && a.reg < 0) {
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
    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, T2, &fp);
        emit1(g, X86_CALL, X86_Q, x86_indirect(T2));
    } else {
        emit1(g, X86_CALL, X86_Q, x86_label(in->u.fun_call.fun_name));
    }
    if (!dst)
        return;
    const Tac_Type *t = val_type(g, dst);
    if (x86_is_ld(t) || x86_is_aggregate(t))
        fatal_error("x86: %s: a result of this type is not implemented yet", gen_name(g));
    store_val(g, x86_is_fp(t) ? X86_XMM0 : X86_RAX, dst);
}

// The result in rax, extended to 32 bits when narrower (clang relies on it), or xmm0.
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (x86_is_ld(t) || x86_is_aggregate(t))
            fatal_error("x86: %s: returning this type is not implemented yet", gen_name(g));
        if (x86_is_fp(t))
            load_val(g, X86_XMM0, v);
        else
            load_int_as(g, X86_RAX, v, rt && !x86_is_fp(rt) && !x86_is_ld(rt) ? rt : t);
    }
    gen_epilogue(g);
}
