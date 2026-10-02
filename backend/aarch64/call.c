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
        load_val(g, a64_is_fp(t) ? A64_V0 : A64_X0, v);
    }
    gen_epilogue(g);
}
