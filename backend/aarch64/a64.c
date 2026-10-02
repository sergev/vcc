//
// AArch64 IR: allocation, release and operand constructors.
//
#include "a64.h"

#include "xalloc.h"

const char *const a64_mnemonic[A64_NUM_OPS] = {
#define A64_MNEM(op, mnem) [A64_##op] = mnem,
    A64_OPS(A64_MNEM)
#undef A64_MNEM
};

A64_Func *a64_new_func(const char *name, bool global)
{
    A64_Func *fn = xalloc(sizeof(A64_Func), __func__, __FILE__, __LINE__);
    fn->name     = xstrdup(name);
    fn->global   = global;
    a64_new_block(fn, NULL);
    return fn;
}

A64_Block *a64_new_block(A64_Func *fn, const char *label)
{
    A64_Block *b = xalloc(sizeof(A64_Block), __func__, __FILE__, __LINE__);
    b->label     = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

A64_Instr *a64_append(A64_Func *fn, A64_Op op)
{
    return a64_append_to(fn->tail, op);
}

A64_Instr *a64_append_to(A64_Block *b, A64_Op op)
{
    A64_Instr *in = xalloc(sizeof(A64_Instr), __func__, __FILE__, __LINE__);
    in->op        = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void a64_free_func(A64_Func *fn)
{
    A64_Block *b = fn->blocks;
    while (b) {
        A64_Block *bnext = b->next;
        A64_Instr *in    = b->head;
        while (in) {
            A64_Instr *next = in->next;
            for (int i = 0; i < A64_MAX_OPERANDS; i++)
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

bool a64_is_fpreg(int reg)
{
    return reg >= A64_V0 && reg < A64_VREG;
}

A64_Operand a64_reg(int reg, A64_Width width)
{
    return (A64_Operand){ .kind = A64_OPND_REG, .reg = reg, .width = width };
}

A64_Operand a64_imm(int64_t imm)
{
    return (A64_Operand){ .kind = A64_OPND_IMM, .imm = imm };
}

A64_Operand a64_sym(const char *sym, int64_t offset)
{
    return (A64_Operand){ .kind = A64_OPND_SYM, .sym = xstrdup(sym), .imm = offset };
}

A64_Operand a64_lo12(const char *sym, int64_t offset)
{
    return (A64_Operand){ .kind = A64_OPND_SYM, .sym = xstrdup(sym), .imm = offset, .lo12 = true };
}

static A64_Operand mem(int base, int64_t offset, A64_MemMode mode)
{
    return (A64_Operand){
        .kind = A64_OPND_MEM, .reg = base, .width = A64_X, .imm = offset, .sub = mode
    };
}

A64_Operand a64_mem(int base, int64_t offset)
{
    return mem(base, offset, A64_MEM_OFFSET);
}

A64_Operand a64_mem_pre(int base, int64_t offset)
{
    return mem(base, offset, A64_MEM_PRE);
}

A64_Operand a64_mem_post(int base, int64_t offset)
{
    return mem(base, offset, A64_MEM_POST);
}

A64_Operand a64_shift(int reg, A64_Width width, A64_Shift shift, int amount)
{
    return (A64_Operand){
        .kind = A64_OPND_SHIFT, .reg = reg, .width = width, .imm = amount, .sub = shift
    };
}

A64_Operand a64_ext(int reg, A64_Width width, A64_Extend ext, int amount)
{
    return (
        A64_Operand){ .kind = A64_OPND_EXT, .reg = reg, .width = width, .imm = amount, .sub = ext };
}

A64_Operand a64_lsl(int amount)
{
    return (A64_Operand){ .kind = A64_OPND_LSL, .imm = amount };
}

A64_Operand a64_cond(A64_Cond cond)
{
    return (A64_Operand){ .kind = A64_OPND_COND, .sub = cond };
}
