//
// MSP430 IR: allocation, release, operand constructors and instruction sizes.
//
#include "msp_ir.h"

#include "xalloc.h"

const char *const msp_mnemonic[MSP_NUM_OPS] = {
#define MSP_MNEM(op, mnem, form) [MSP_##op] = mnem,
    MSP_OPS(MSP_MNEM)
#undef MSP_MNEM
};

const Msp_Form msp_form[MSP_NUM_OPS] = {
#define MSP_FORM(op, mnem, form) [MSP_##op] = MSP_FORM_##form,
    MSP_OPS(MSP_FORM)
#undef MSP_FORM
};

Msp_Func *msp_new_func(const char *name, bool global)
{
    Msp_Func *fn = xalloc(sizeof(Msp_Func), __func__, __FILE__, __LINE__);
    fn->name     = xstrdup(name);
    fn->global   = global;
    msp_new_block(fn, NULL);
    return fn;
}

Msp_Block *msp_new_block(Msp_Func *fn, const char *label)
{
    Msp_Block *b = xalloc(sizeof(Msp_Block), __func__, __FILE__, __LINE__);
    b->label     = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

Msp_Instr *msp_append(Msp_Func *fn, Msp_Op op)
{
    Msp_Block *b  = fn->tail;
    Msp_Instr *in = xalloc(sizeof(Msp_Instr), __func__, __FILE__, __LINE__);
    in->op        = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void msp_free_func(Msp_Func *fn)
{
    Msp_Block *b = fn->blocks;
    while (b) {
        Msp_Block *bnext = b->next;
        Msp_Instr *in    = b->head;
        while (in) {
            Msp_Instr *next = in->next;
            for (int i = 0; i < MSP_MAX_OPERANDS; i++)
                xfree(in->opnd[i].sym);
            xfree(in);
            in = next;
        }
        xfree(b->label);
        xfree(b);
        b = bnext;
    }
    xfree(fn->name);
    xfree(fn);
}

static char *dup_sym(const char *sym)
{
    return sym ? xstrdup(sym) : NULL;
}

Msp_Operand msp_reg(int reg)
{
    return (Msp_Operand){ .kind = MSP_OPND_REG, .reg = reg };
}

Msp_Operand msp_indexed(int reg, const char *sym, int64_t off)
{
    return (Msp_Operand){ .kind = MSP_OPND_INDEXED, .reg = reg, .sym = dup_sym(sym), .imm = off };
}

Msp_Operand msp_abs(const char *sym, int64_t off)
{
    return (Msp_Operand){ .kind = MSP_OPND_ABS, .sym = dup_sym(sym), .imm = off };
}

Msp_Operand msp_ind(int reg)
{
    return (Msp_Operand){ .kind = MSP_OPND_IND, .reg = reg };
}

Msp_Operand msp_postinc(int reg)
{
    return (Msp_Operand){ .kind = MSP_OPND_POSTINC, .reg = reg };
}

Msp_Operand msp_imm(int64_t imm)
{
    return (Msp_Operand){ .kind = MSP_OPND_IMM, .imm = imm };
}

Msp_Operand msp_imm_sym(const char *sym, int64_t off)
{
    return (Msp_Operand){ .kind = MSP_OPND_IMM, .sym = xstrdup(sym), .imm = off };
}

Msp_Operand msp_label(const char *sym)
{
    return (Msp_Operand){ .kind = MSP_OPND_LABEL, .sym = xstrdup(sym) };
}

int64_t msp_imm_value(int64_t imm, bool byte)
{
    return byte ? (int64_t)(int8_t)imm : (int64_t)(int16_t)imm;
}

// The extension words an operand takes, in bytes.
static int ext_size(const Msp_Operand *o, bool byte)
{
    switch (o->kind) {
    case MSP_OPND_INDEXED:
    case MSP_OPND_ABS:
        return 2;
    case MSP_OPND_IMM:
        if (o->sym)
            return 2;
        switch (msp_imm_value(o->imm, byte)) {
        case 0:
        case 1:
        case 2:
        case 4:
        case 8:
        case -1:
            return 0;
        default:
            return 2;
        }
    default:
        return 0;
    }
}

int msp_instr_size(const Msp_Instr *in)
{
    switch (msp_form[in->op]) {
    case MSP_FORM_DOUBLE:
        return 2 + ext_size(&in->opnd[0], in->byte) + ext_size(&in->opnd[1], in->byte);
    case MSP_FORM_SINGLE:
    case MSP_FORM_DST:
    case MSP_FORM_SRC:
        return 2 + ext_size(&in->opnd[0], in->byte);
    case MSP_FORM_TWICE:
        return 2 + 2 * ext_size(&in->opnd[0], in->byte);
    case MSP_FORM_JUMP:
    case MSP_FORM_NONE:
        return 2;
    }
    return 2;
}
