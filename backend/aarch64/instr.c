//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers and its result stored back.
//
#include "codegen.h"
#include "internal.h"

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a64_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a64_size(t), a64_align(t));
        return;
    }
    if (a64_is_ld(t))
        fatal_error("aarch64: %s: long double is not implemented yet", gen_name(g));
    if (a64_is_fp(t)) {
        load_val(g, F0, src);
    } else if (src->kind == TAC_VAL_CONSTANT) {
        load_const_as(g, T0, src->u.constant, t);
    } else {
        load_val(g, T0, src);
    }
    store_val(g, a64_is_fp(t) ? F0 : T0, dst);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    case TAC_INSTRUCTION_COPY:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("aarch64: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
