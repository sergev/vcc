//
// Calls, parameters and return values: AAPCS64.
//
#include "codegen.h"
#include "internal.h"

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (a64_is_aggregate(t) || a64_is_ld(t))
            fatal_error("aarch64: %s: returning a %d-byte value is not implemented yet",
                        gen_name(g), a64_size(t));
        const Tac_Type *ft = g->tl->u.function.type;
        const Tac_Type *rt = ft && ft->kind == TAC_TYPE_FUN_TYPE ? ft->u.fun_type.ret_type : NULL;
        if (a64_is_fp(t))
            load_val(g, A64_V0, v);
        else
            load_int_as(g, A64_X0, v, rt && !a64_is_fp(rt) ? rt : t);
    }
    gen_epilogue(g);
}
