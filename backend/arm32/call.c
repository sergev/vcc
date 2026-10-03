//
// Calls, parameters and return values: AAPCS-VFP.
//
#include "codegen.h"
#include "internal.h"

static const Tac_Type *ret_type(const Tac_Type *fun_type)
{
    return fun_type && fun_type->kind == TAC_TYPE_FUN_TYPE ? fun_type->u.fun_type.ret_type : NULL;
}

void gen_params(Gen *g)
{
    if (g->tl->u.function.params)
        fatal_error("arm32: %s: parameters are not implemented yet", gen_name(g));
}

// An integer in r0 (the callee extends a narrow one), a long long in r0:r1, a float in
// s0, a double in d0 (a constant's bits through r0 and r1, which need no frame).
void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t  = val_type(g, v);
        const Tac_Type *rt = ret_type(g->tl->u.function.type);
        if (!rt || rt->kind == TAC_TYPE_VOID)
            rt = t;
        if (a32_is_aggregate(rt))
            fatal_error("arm32: %s: returning a structure is not implemented yet", gen_name(g));
        if (a32_is_fp(rt) && v->kind == TAC_VAL_CONSTANT)
            load_fp_const(g, A32_S0, v->u.constant, rt, A32_R0, A32_R0 + 1);
        else if (a32_is_fp(rt))
            load_val(g, A32_S0, v);
        else if (a32_is_pair(rt)) {
            load_word(g, A32_R0, v, rt, 0);
            load_word(g, A32_R0 + 1, v, rt, 1);
        } else {
            load_as(g, A32_R0, v, rt);
        }
    }
    gen_epilogue(g);
}
