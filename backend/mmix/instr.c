//
// Instruction selection: one TAC instruction at a time, naive.  An operation loads its
// operands into the scratch registers, operates, and stores the result.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

// dst = src: a scalar loaded by its own type and stored in the destination's width, an
// aggregate copied.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!mmix_is_scalar(t)) {
        copy_named(g, dst->u.var_name, 0, src->u.var_name, 0, mmix_type_size(t),
                   mmix_type_align(t));
        return;
    }
    load_val(g, src, REG_A);
    store_val(g, REG_A, dst);
}

int instr_out_size(const Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        return call_stack_size(g, in);
    default:
        return 0;
    }
}

void gen_instr(Gen *g, const Tac_Instruction *in, bool last)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src, last);
        break;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("mmix: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
