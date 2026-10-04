//
// Instruction selection: one TAC instruction at a time, naive.  Operands are loaded
// into register blocks A and B (block_a, block_b), byte-serial chains compute in place,
// and the result is stored back.
//
#include "internal.h"

// dst = src, a scalar.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (!avr_is_scalar(t))
        fatal_error("avr: %s: copying an aggregate is not implemented yet", gen_name(g));
    int size = avr_type_size(t);
    int a    = block_a(size);
    load_val(g, src, a, size, EXT_TYPE);
    store_val(g, dst, a, size);
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
        fatal_error("avr: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
