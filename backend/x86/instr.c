//
// Instruction selection: one TAC instruction at a time, its operands loaded into
// scratch registers, or used from memory or as an immediate where x86 allows.
//
#include "codegen.h"
#include "internal.h"

// dst = src, for a scalar.  A constant or a register is stored straight into memory.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (x86_is_fp(t) || x86_is_ld(t) || x86_is_aggregate(t))
        fatal_error("x86: %s: copying this type is not implemented yet", gen_name(g));
    X86_Width w     = x86_width_of(x86_size(t));
    X86_Operand mem = name_mem(g, dst->u.var_name, 0);
    if (src->kind == TAC_VAL_CONSTANT) {
        int64_t imm = const_as(src->u.constant, t);
        if (w != X86_Q || x86_imm32(imm)) {
            emit2(g, X86_MOV, w, x86_imm(w == X86_L ? (int32_t)imm : imm), mem);
            return;
        }
    }
    load_int_as(g, T0, src, t);
    store_mem(g, T0, t, mem);
}

// An integer conversion: the source loaded extended as the conversion says (its own
// type may differ in signedness, once copy propagation has removed a cast), stored at
// the destination's width, which truncates.
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst,
                            Tac_InstructionKind kind)
{
    const Tac_Type *st = val_type(g, src), *dt = val_type(g, dst);
    if (src->kind == TAC_VAL_CONSTANT || kind == TAC_INSTRUCTION_TRUNCATE) {
        gen_copy(g, src, dst);
        return;
    }
    X86_Width w   = x86_op_width(dt);
    X86_Operand m = name_mem(g, src->u.var_name, 0);
    bool sign     = kind == TAC_INSTRUCTION_SIGN_EXTEND;
    switch (x86_size(st)) {
    case 1:
        emit2(g, sign ? X86_MOVSB : X86_MOVZB, w, m, x86_reg(T0, w));
        break;
    case 2:
        emit2(g, sign ? X86_MOVSW : X86_MOVZW, w, m, x86_reg(T0, w));
        break;
    case 4:
        if (sign && w == X86_Q)
            emit2(g, X86_MOVSL, X86_Q, m, x86_reg(T0, X86_Q));
        else
            emit2(g, X86_MOV, X86_L, m, x86_reg(T0, X86_L)); // the upper half zero
        break;
    default:
        load_val(g, T0, src);
        break;
    }
    store_val(g, T0, dst);
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
        fatal_error("x86: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}
