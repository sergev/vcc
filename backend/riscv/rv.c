//
// RISC-V IR: allocation, release and operand constructors.
//
#include "rv.h"

#include <stdlib.h>

#include "xalloc.h"

const char *const rv_mnemonic[RV_NUM_OPS] = {
#define RV_MNEM(op, mnem) [RV_##op] = mnem,
    RV_OPS(RV_MNEM)
#undef RV_MNEM
};

Rv_Func *rv_new_func(const char *name, bool global)
{
    Rv_Func *fn = xalloc(sizeof(Rv_Func), __func__, __FILE__, __LINE__);
    fn->name    = xstrdup(name);
    fn->global  = global;
    rv_new_block(fn, NULL);
    return fn;
}

Rv_Block *rv_new_block(Rv_Func *fn, const char *label)
{
    Rv_Block *b = xalloc(sizeof(Rv_Block), __func__, __FILE__, __LINE__);
    b->label    = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

Rv_Instr *rv_append_to(Rv_Block *b, Rv_Op op)
{
    Rv_Instr *in = xalloc(sizeof(Rv_Instr), __func__, __FILE__, __LINE__);
    in->op       = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

Rv_Instr *rv_append(Rv_Func *fn, Rv_Op op)
{
    return rv_append_to(fn->tail, op);
}

void rv_free_func(Rv_Func *fn)
{
    Rv_Block *b = fn->blocks;
    while (b) {
        Rv_Block *bnext = b->next;
        Rv_Instr *in    = b->head;
        while (in) {
            Rv_Instr *next = in->next;
            for (int i = 0; i < 3; i++)
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

bool rv_is_freg(int reg)
{
    return reg >= RV_F0 && reg < RV_VREG;
}

Rv_Operand rv_reg(int reg)
{
    return (Rv_Operand){ .kind = RV_OPND_REG, .reg = reg };
}

Rv_Operand rv_imm(int64_t imm)
{
    return (Rv_Operand){ .kind = RV_OPND_IMM, .imm = imm };
}

Rv_Operand rv_sym(const char *sym, int64_t offset)
{
    return (Rv_Operand){ .kind = RV_OPND_SYM, .sym = xstrdup(sym), .imm = offset };
}

Rv_Operand rv_mem(int base, int64_t offset)
{
    return (Rv_Operand){ .kind = RV_OPND_MEM, .reg = base, .imm = offset };
}
