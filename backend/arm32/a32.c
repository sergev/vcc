//
// ARM32 IR: allocation, release, operand constructors and the immediate encoder.
//
#include "a32.h"

#include "xalloc.h"

const char *const a32_mnemonic[A32_NUM_OPS] = {
#define A32_MNEM(op, mnem) [A32_##op] = mnem,
    A32_OPS(A32_MNEM)
#undef A32_MNEM
};

A32_Func *a32_new_func(const char *name, bool global)
{
    A32_Func *fn = xalloc(sizeof(A32_Func), __func__, __FILE__, __LINE__);
    fn->name     = xstrdup(name);
    fn->global   = global;
    a32_new_block(fn, NULL);
    return fn;
}

A32_Block *a32_new_block(A32_Func *fn, const char *label)
{
    A32_Block *b = xalloc(sizeof(A32_Block), __func__, __FILE__, __LINE__);
    b->label     = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

A32_Instr *a32_append(A32_Func *fn, A32_Op op)
{
    A32_Block *b  = fn->tail;
    A32_Instr *in = xalloc(sizeof(A32_Instr), __func__, __FILE__, __LINE__);
    in->op        = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

void a32_free_func(A32_Func *fn)
{
    A32_Block *b = fn->blocks;
    while (b) {
        A32_Block *bnext = b->next;
        A32_Instr *in    = b->head;
        while (in) {
            A32_Instr *next = in->next;
            for (int i = 0; i < A32_MAX_OPERANDS; i++)
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

bool a32_is_vfp(int reg)
{
    return reg >= A32_S0 && reg < A32_VREG;
}

bool a32_operand2_imm(uint32_t v)
{
    for (int rot = 0; rot < 32; rot += 2) {
        // v rotated left by rot undoes a rotation right by rot.
        uint32_t x = rot ? (v << rot) | (v >> (32 - rot)) : v;
        if (x <= 0xff)
            return true;
    }
    return false;
}

static A32_Operand reg_at(int reg, A32_Width width)
{
    return (A32_Operand){ .kind = A32_OPND_REG, .reg = reg, .width = width, .reg2 = -1 };
}

A32_Operand a32_reg(int reg)
{
    return reg_at(reg, A32_CORE);
}

A32_Operand a32_sreg(int reg)
{
    return reg_at(reg, A32_S);
}

A32_Operand a32_dreg(int reg)
{
    return reg_at(reg, A32_D);
}

A32_Operand a32_imm(int64_t imm)
{
    return (A32_Operand){ .kind = A32_OPND_IMM, .imm = imm, .reg2 = -1 };
}

static A32_Operand sym(const char *name, int64_t offset, A32_Reloc reloc)
{
    return (A32_Operand){
        .kind = A32_OPND_SYM, .sym = xstrdup(name), .imm = offset, .sub = reloc, .reg2 = -1
    };
}

A32_Operand a32_sym(const char *name, int64_t offset)
{
    return sym(name, offset, A32_RELOC_NONE);
}

A32_Operand a32_lower16(const char *name, int64_t offset)
{
    return sym(name, offset, A32_RELOC_LOWER16);
}

A32_Operand a32_upper16(const char *name, int64_t offset)
{
    return sym(name, offset, A32_RELOC_UPPER16);
}

static A32_Operand mem(int base, int64_t offset, A32_MemMode mode)
{
    return (A32_Operand){
        .kind = A32_OPND_MEM, .reg = base, .imm = offset, .sub = mode, .reg2 = -1
    };
}

A32_Operand a32_mem(int base, int64_t offset)
{
    return mem(base, offset, A32_MEM_OFFSET);
}

A32_Operand a32_mem_pre(int base, int64_t offset)
{
    return mem(base, offset, A32_MEM_PRE);
}

A32_Operand a32_mem_post(int base, int64_t offset)
{
    return mem(base, offset, A32_MEM_POST);
}

A32_Operand a32_mem_index(int base, int index, bool negative, int shift)
{
    A32_Operand o = mem(base, shift, A32_MEM_OFFSET);
    o.reg2        = index;
    o.negative    = negative;
    return o;
}

A32_Operand a32_shift(int reg, A32_Shift shift, int amount)
{
    return (A32_Operand){
        .kind = A32_OPND_SHIFT, .reg = reg, .imm = amount, .sub = shift, .reg2 = -1
    };
}

A32_Operand a32_shift_reg(int reg, A32_Shift shift, int amount_reg)
{
    A32_Operand o = a32_shift(reg, shift, 0);
    o.reg2        = amount_reg;
    return o;
}

A32_Operand a32_reglist(unsigned mask)
{
    return (A32_Operand){ .kind = A32_OPND_REGLIST, .imm = mask, .reg2 = -1 };
}
