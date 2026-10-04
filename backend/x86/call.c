//
// Calls, parameters and return values: the System V psABI.  An integer or pointer goes
// in the next of rdi, rsi, rdx, rcx, r8, r9; once they run out, on the stack in an
// 8-byte slot, the first at the lowest address.  The stack arguments are stored into
// an outgoing area at the bottom of the frame, so rsp stays fixed and 16-byte aligned.
// A narrow argument or result is extended to 32 bits by the sender (clang relies on
// it) and again by the receiver, a store truncating and a load extending by type.
//
#include "codegen.h"
#include "internal.h"

static const int int_regs[6] = { X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9 };

// Each parameter gets a slot: one passed in a register is stored there at its own
// width, which truncates; one on the stack is read where the caller put it, above the
// return address.  A load extends by type, so a narrow argument is re-extended whatever
// the caller left in the upper bits.
void gen_params(Gen *g)
{
    int next_int = 0, stack = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        const Tac_Type *t = p->type;
        if (!t)
            fatal_error("x86: %s: no type for %s", gen_name(g), p->name);
        if (x86_is_fp(t) || x86_is_ld(t) || x86_is_aggregate(t))
            fatal_error("x86: %s: a parameter of this type is not implemented yet", gen_name(g));
        if (next_int < 6) {
            int off = alloc_slot(g, p->name, t, x86_size(t), x86_align(t));
            store_mem(g, int_regs[next_int++], t, x86_mem(X86_RBP, off));
        } else {
            place_slot(g, p->name, t, 16 + stack);
            stack += 8;
        }
    }
}

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

// The type an integer argument is passed as: the declared parameter type, when there
// is one (a constant's own kind may differ), else its own.
static const Tac_Type *arg_type(const Tac_Type *t, const Tac_Type *want)
{
    if (want && !x86_is_fp(want) && !x86_is_ld(want) && !x86_is_aggregate(want))
        return want;
    return t;
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft   = in->u.fun_call.fun_type;
    const Tac_Type *want = ft ? ft->u.fun_type.param_types : NULL;
    const Tac_Val *dst   = in->u.fun_call.dst;
    // The stack arguments first, through rax; then the registers, straight from memory.
    int next_int = 0, stack = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v; v = v->next) {
        const Tac_Type *t = val_type(g, v);
        if (x86_is_fp(t) || x86_is_ld(t) || x86_is_aggregate(t))
            fatal_error("x86: %s: an argument of this type is not implemented yet", gen_name(g));
        const Tac_Type *as = arg_type(t, want);
        if (want)
            want = want->next;
        if (next_int < 6) {
            next_int++;
            continue;
        }
        load_int_as(g, T0, v, as);
        emit2(g, X86_MOV, X86_Q, x86_reg(T0, X86_Q), x86_mem(X86_RSP, stack));
        stack += 8;
    }
    if (stack > g->outgoing)
        g->outgoing = stack;
    want     = ft ? ft->u.fun_type.param_types : NULL;
    next_int = 0;
    for (const Tac_Val *v = in->u.fun_call.args; v && next_int < 6; v = v->next) {
        load_int_as(g, int_regs[next_int++], v, arg_type(val_type(g, v), want));
        if (want)
            want = want->next;
    }
    // A variadic or unprototyped callee is told how many xmm registers carry arguments.
    if (!ft || ft->u.fun_type.variadic)
        gen_li(g, X86_RAX, X86_L, 0);
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
    if (x86_is_fp(t) || x86_is_ld(t) || x86_is_aggregate(t))
        fatal_error("x86: %s: a result of this type is not implemented yet", gen_name(g));
    store_val(g, X86_RAX, dst);
}

// The result in rax, extended to 32 bits when narrower (clang relies on it).
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (x86_is_fp(t) || x86_is_ld(t) || x86_is_aggregate(t))
            fatal_error("x86: %s: returning this type is not implemented yet", gen_name(g));
        load_int_as(g, X86_RAX, v, rt && !x86_is_fp(rt) && !x86_is_ld(rt) ? rt : t);
    }
    gen_epilogue(g);
}
