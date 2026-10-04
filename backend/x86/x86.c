//
// x86-64 IR: allocation, release and operand constructors.
//
#include "x86.h"

#include "xalloc.h"

const char *const x86_mnemonic[X86_NUM_OPS] = {
#define X86_MNEM(op, mnem, form) [X86_##op] = mnem,
    X86_OPS(X86_MNEM)
#undef X86_MNEM
};

const int x86_form[X86_NUM_OPS] = {
#define X86_FORM(op, mnem, form) [X86_##op] = form,
    X86_OPS(X86_FORM)
#undef X86_FORM
};

const char *const x86_cond_name[16] = { "o", "no", "b",  "ae", "e", "ne", "be", "a",
                                        "s", "ns", "p", "np", "l", "ge", "le", "g" };

X86_Func *x86_new_func(const char *name, bool global)
{
    X86_Func *fn = xalloc(sizeof(X86_Func), __func__, __FILE__, __LINE__);
    fn->name     = xstrdup(name);
    fn->global   = global;
    x86_new_block(fn, NULL);
    return fn;
}

X86_Block *x86_new_block(X86_Func *fn, const char *label)
{
    X86_Block *b = xalloc(sizeof(X86_Block), __func__, __FILE__, __LINE__);
    b->label     = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

X86_Instr *x86_append(X86_Func *fn, X86_Op op, X86_Width width)
{
    X86_Block *b  = fn->tail;
    X86_Instr *in   = xalloc(sizeof(X86_Instr), __func__, __FILE__, __LINE__);
    in->op          = op;
    in->width       = width;
    in->is_volatile = fn->volatile_access;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void x86_free_func(X86_Func *fn)
{
    X86_Block *b = fn->blocks;
    while (b) {
        X86_Block *bnext = b->next;
        X86_Instr *in    = b->head;
        while (in) {
            X86_Instr *next = in->next;
            for (int i = 0; i < X86_MAX_OPERANDS; i++)
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

bool x86_is_xmm(int reg)
{
    return reg >= X86_XMM0 && reg < X86_XMM0 + 16;
}

bool x86_is_st(int reg)
{
    return reg >= X86_ST0 && reg < X86_ST0 + 8;
}

bool x86_imm32(int64_t v)
{
    return v >= INT32_MIN && v <= INT32_MAX;
}

static X86_Operand operand(X86_OperandKind kind)
{
    X86_Operand o = { 0 };
    o.kind        = kind;
    o.reg         = -1;
    o.index       = -1;
    return o;
}

X86_Operand x86_reg(int reg, X86_Width width)
{
    X86_Operand o = operand(X86_OPND_REG);
    o.reg         = reg;
    o.width       = width;
    return o;
}

X86_Operand x86_xmm(int reg)
{
    return x86_reg(reg, X86_Q);
}

X86_Operand x86_st(int n)
{
    return x86_reg(X86_ST0 + n, X86_Q);
}

X86_Operand x86_imm(int64_t imm)
{
    X86_Operand o = operand(X86_OPND_IMM);
    o.imm         = imm;
    return o;
}

X86_Operand x86_mem(int base, int64_t disp)
{
    X86_Operand o = operand(X86_OPND_MEM);
    o.reg         = base;
    o.imm         = disp;
    return o;
}

X86_Operand x86_mem_index(int base, int index, int scale, int64_t disp)
{
    X86_Operand o = x86_mem(base, disp);
    o.index       = index;
    o.scale       = scale;
    return o;
}

X86_Operand x86_rip(const char *sym, int64_t disp)
{
    X86_Operand o = operand(X86_OPND_RIP);
    o.sym         = xstrdup(sym);
    o.imm         = disp;
    return o;
}

X86_Operand x86_label(const char *sym)
{
    X86_Operand o = operand(X86_OPND_LABEL);
    o.sym         = xstrdup(sym);
    return o;
}

X86_Operand x86_indirect(int reg)
{
    X86_Operand o = x86_reg(reg, X86_Q);
    o.kind        = X86_OPND_INDIRECT;
    return o;
}
