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
    return msp_append_to(fn->tail, op);
}

Msp_Instr *msp_insert_after(Msp_Block *b, Msp_Instr *in, Msp_Op op)
{
    Msp_Instr *n = xalloc(sizeof(Msp_Instr), __func__, __FILE__, __LINE__);
    n->op        = op;
    n->next      = in->next;
    in->next     = n;
    if (b->tail == in)
        b->tail = n;
    return n;
}

Msp_Block *msp_split_after(Msp_Func *fn, Msp_Block *b, Msp_Instr *in, const char *label)
{
    Msp_Block *n = xalloc(sizeof(Msp_Block), __func__, __FILE__, __LINE__);
    n->label     = xstrdup(label);
    n->head      = in->next;
    n->tail      = in->next ? b->tail : NULL;
    in->next     = NULL;
    b->tail      = in;
    n->next      = b->next;
    b->next      = n;
    if (fn->tail == b)
        fn->tail = n;
    return n;
}

Msp_Instr *msp_append_to(Msp_Block *b, Msp_Op op)
{
    Msp_Instr *in = xalloc(sizeof(Msp_Instr), __func__, __FILE__, __LINE__);
    in->op        = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void msp_free_instr(Msp_Instr *in)
{
    for (int i = 0; i < MSP_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

Msp_Op msp_inverse(Msp_Op op)
{
    switch (op) {
    case MSP_JEQ:
        return MSP_JNE;
    case MSP_JNE:
        return MSP_JEQ;
    case MSP_JLO:
        return MSP_JHS;
    case MSP_JHS:
        return MSP_JLO;
    case MSP_JL:
        return MSP_JGE;
    case MSP_JGE:
        return MSP_JL;
    default:
        return MSP_NUM_OPS;
    }
}

void msp_free_func(Msp_Func *fn)
{
    Msp_Block *b = fn->blocks;
    while (b) {
        Msp_Block *bnext = b->next;
        Msp_Instr *in    = b->head;
        while (in) {
            Msp_Instr *next = in->next;
            msp_free_instr(in);
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

Msp_Operand msp_copy(const Msp_Operand *o)
{
    Msp_Operand c = *o;
    c.sym         = dup_sym(o->sym);
    return c;
}

int64_t msp_imm_value(int64_t imm, bool byte)
{
    return byte ? (int64_t)(int8_t)imm : (int64_t)(int16_t)imm;
}

bool msp_zero_indexed(const Msp_Operand *o)
{
    return o->kind == MSP_OPND_INDEXED && !o->sym && o->imm == 0 && !o->incoming &&
           o->reg != MSP_PC && o->reg != MSP_SR && o->reg != MSP_CG;
}

// The extension words an operand of `in` takes, in bytes; `source` for an operand in a
// source (As) field, where 0(rN) is printed as @rN.
static int ext_size(const Msp_Instr *in, const Msp_Operand *o, bool source)
{
    switch (o->kind) {
    case MSP_OPND_INDEXED:
        return source && msp_zero_indexed(o) ? 0 : 2;
    case MSP_OPND_ABS:
        return 2;
    case MSP_OPND_IMM:
        if (o->sym)
            return 2;
        switch (msp_imm_value(o->imm, in->byte)) {
        case 4:
        case 8:
            // GNU as never pushes these through the constant generator (the CPU4
            // erratum); clang does, a word shorter.
            return in->op == MSP_PUSH ? 2 : 0;
        case 0:
        case 1:
        case 2:
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
        return 2 + ext_size(in, &in->opnd[0], true) + ext_size(in, &in->opnd[1], false);
    case MSP_FORM_SINGLE:
        return 2 + ext_size(in, &in->opnd[0], true);
    case MSP_FORM_DST:
        return 2 + ext_size(in, &in->opnd[0], false);
    case MSP_FORM_SRC: // br 0(rN) stays: clang has no `br @rN`
        return 2 + ext_size(in, &in->opnd[0], false);
    case MSP_FORM_TWICE:
        return 2 + ext_size(in, &in->opnd[0], true) + ext_size(in, &in->opnd[0], false);
    case MSP_FORM_JUMP:
    case MSP_FORM_NONE:
        return 2;
    }
    return 2;
}
