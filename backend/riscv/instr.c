//
// Instruction selection: one TAC instruction at a time, operands through scratch
// registers.
//
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "xalloc.h"

// Local label for TAC label `%N`: `.LN`.
static char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, ".L");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
}

static void gen_label(Gen *g, const char *tac)
{
    char *l = label_name(tac);
    rv_new_block(g->fn, l);
    xfree(l);
}

static void gen_jump(Gen *g, Rv_Op op, int reg, const char *tac)
{
    char *l      = label_name(tac);
    Rv_Instr *in = rv_append(g->fn, op);
    if (op == RV_J) {
        in->opnd[0] = rv_sym(l, 0);
    } else {
        in->opnd[0] = rv_reg(reg);
        in->opnd[1] = rv_sym(l, 0);
    }
    xfree(l);
}

// dst = src, for any type.
static void gen_copy(Gen *g, const Tac_Val *src, const Tac_Val *dst)
{
    const Tac_Type *t = val_type(g, dst);
    if (rv_is_aggregate(t)) {
        int sbase, dbase;
        int64_t soff, doff;
        name_addr(g, src->u.var_name, RV_T3, &sbase, &soff);
        name_addr(g, dst->u.var_name, RV_T4, &dbase, &doff);
        gen_memcopy(g, dbase, doff, sbase, soff, rv_size(t), rv_align(t));
        return;
    }
    int reg = rv_is_fp(t) ? RV_F0 : RV_T0;
    load_val(g, reg, src);
    store_val(g, reg, dst);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL:
        gen_label(g, in->u.label.name);
        break;
    case TAC_INSTRUCTION_JUMP:
        gen_jump(g, RV_J, 0, in->u.jump.target);
        break;
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    case TAC_INSTRUCTION_COPY:
        gen_copy(g, in->u.copy.src, in->u.copy.dst);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // the slot is laid out with the frame
    default:
        fatal_error("riscv: %s: %s not implemented", gen_name(g), tac_instruction_name(in->kind));
    }
}
