//
// Calls, parameters and return values: the System V psABI.
//
#include "codegen.h"
#include "internal.h"

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
