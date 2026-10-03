//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers and its result stored back.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"
#include "xalloc.h"

// dst = src, for any type: an aggregate copied as bytes, an 8-byte scalar as two
// words, any other through r12.  A float or double needs no VFP register to move.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (a32_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, T0, &sbase, &soff);
        name_addr(g, dst->u.var_name, T1, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, a32_size(t), a32_align(t));
        return;
    }
    if (a32_size(t) == 8) {
        load_word(g, T0, src, t, 0);
        load_word(g, T1, src, t, 1);
        store_pair(g, dst, T0, T1);
        return;
    }
    load_as(g, T0, src, t);
    store_val(g, T0, dst);
}

// An integer conversion.  A store truncates to the destination's width; a loaded value
// is extended by the source's own type, so an extension is explicit only where that
// differs: a sign extension of a narrow unsigned source (copy propagation may have
// removed its cast to a signed type), a zero extension of a signed one.  A long long's
// high word is the low one's sign, or zero, by the conversion's kind.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (src->kind == TAC_VAL_CONSTANT) {
        gen_copy(g, src, dst); // converted as it is loaded
        return;
    }
    if (a32_is_pair(st)) {
        load_word(g, T0, src, st, 0);
        if (a32_is_pair(dt)) {
            load_word(g, T1, src, st, 1);
            store_pair(g, dst, T0, T1);
        } else {
            store_val(g, T0, dst);
        }
        return;
    }
    load_val(g, T0, src);
    int ssize = a32_size(st);
    A32_Op op = A32_EPILOGUE;
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND)
        op = ssize == 1 ? A32_SXTB : ssize == 2 ? A32_SXTH : op;
    else if (kind == TAC_INSTRUCTION_ZERO_EXTEND)
        op = ssize == 1 ? A32_UXTB : ssize == 2 ? A32_UXTH : op;
    if (op != A32_EPILOGUE)
        emit2(g, op, a32_reg(T0), a32_reg(T0));
    if (!a32_is_pair(dt)) {
        store_val(g, T0, dst);
        return;
    }
    if (kind == TAC_INSTRUCTION_SIGN_EXTEND)
        emit2(g, A32_MOV, a32_reg(T1), a32_shift(T0, A32_SHIFT_ASR, 31));
    else
        gen_li(g, T1, 0);
    store_pair(g, dst, T0, T1);
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
    case TAC_INSTRUCTION_SIGN_EXTEND:
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_int_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, in->kind);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("arm32: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
