//
// Calls, parameters and return values: the System V psABI.
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
