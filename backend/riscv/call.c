//
// Calls, parameters and return values.
//
#include "internal.h"

void gen_params(Gen *g)
{
    if (g->tl->u.function.params)
        fatal_error("riscv: %s: parameters not implemented", gen_name(g));
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    (void)in;
    fatal_error("riscv: %s: calls not implemented", gen_name(g));
}

void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        const Tac_Type *t = val_type(g, v);
        if (rv_is_aggregate(t))
            fatal_error("riscv: %s: struct return not implemented", gen_name(g));
        load_val(g, rv_is_fp(t) ? RV_FA0 : RV_A0, v);
    }
    gen_epilogue(g);
}
