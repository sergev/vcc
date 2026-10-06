//
// MMIX IR: allocation, release and operand constructors.
//
#include "mmix_ir.h"

#include "xalloc.h"

const char *const mmix_mnemonic[MMIX_NUM_OPS] = {
#define MMIX_MNEM(op, mnem, form) [MMIX_##op] = mnem,
    MMIX_OPS(MMIX_MNEM)
#undef MMIX_MNEM
};

const Mmix_Form mmix_form[MMIX_NUM_OPS] = {
#define MMIX_FORM(op, mnem, form) [MMIX_##op] = MMIX_FORM_##form,
    MMIX_OPS(MMIX_FORM)
#undef MMIX_FORM
};

const char *const mmix_special_name[MMIX_NUM_SPECIALS] = {
#define MMIX_SPECIAL_NAME(name, num) [num] = #name,
    MMIX_SPECIALS(MMIX_SPECIAL_NAME)
#undef MMIX_SPECIAL_NAME
};

Mmix_Func *mmix_new_func(const char *name, bool global)
{
    Mmix_Func *fn = xalloc(sizeof(Mmix_Func), __func__, __FILE__, __LINE__);
    fn->name      = xstrdup(name);
    fn->global    = global;
    mmix_new_block(fn, NULL);
    return fn;
}

Mmix_Block *mmix_new_block(Mmix_Func *fn, const char *label)
{
    Mmix_Block *b = xalloc(sizeof(Mmix_Block), __func__, __FILE__, __LINE__);
    b->label      = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

Mmix_Instr *mmix_append(Mmix_Func *fn, Mmix_Op op)
{
    return mmix_append_to(fn->tail, op);
}

Mmix_Instr *mmix_append_to(Mmix_Block *b, Mmix_Op op)
{
    Mmix_Instr *in = xalloc(sizeof(Mmix_Instr), __func__, __FILE__, __LINE__);
    in->op         = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void mmix_free_func(Mmix_Func *fn)
{
    Mmix_Block *b = fn->blocks;
    while (b) {
        Mmix_Block *bnext = b->next;
        Mmix_Instr *in    = b->head;
        while (in) {
            Mmix_Instr *next = in->next;
            for (int i = 0; i < MMIX_MAX_OPERANDS; i++)
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

Mmix_Operand mmix_reg(int reg)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_REG, .reg = reg };
}

Mmix_Operand mmix_imm(int64_t imm)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_IMM, .imm = imm };
}

Mmix_Operand mmix_wyde(unsigned wyde)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_WYDE, .imm = wyde };
}

Mmix_Operand mmix_sym(const char *sym, int64_t off)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_SYM, .imm = off, .sym = xstrdup(sym) };
}

Mmix_Operand mmix_label(const char *label)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_LABEL, .sym = xstrdup(label) };
}

Mmix_Operand mmix_special(Mmix_Special sr)
{
    return (Mmix_Operand){ .kind = MMIX_OPND_SPECIAL, .reg = sr };
}
