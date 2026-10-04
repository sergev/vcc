//
// AVR IR: allocation, release and operand constructors.
//
#include "avr_ir.h"

#include "xalloc.h"

const char *const avr_mnemonic[AVR_NUM_OPS] = {
#define AVR_MNEM(op, mnem, size) [AVR_##op] = mnem,
    AVR_OPS(AVR_MNEM)
#undef AVR_MNEM
};

const int avr_size[AVR_NUM_OPS] = {
#define AVR_SIZE(op, mnem, size) [AVR_##op] = size,
    AVR_OPS(AVR_SIZE)
#undef AVR_SIZE
};

AVR_Func *avr_new_func(const char *name, bool global)
{
    AVR_Func *fn = xalloc(sizeof(AVR_Func), __func__, __FILE__, __LINE__);
    fn->name     = xstrdup(name);
    fn->global   = global;
    fn->result   = ~0u;
    avr_new_block(fn, NULL);
    return fn;
}

AVR_Block *avr_new_block(AVR_Func *fn, const char *label)
{
    AVR_Block *b = xalloc(sizeof(AVR_Block), __func__, __FILE__, __LINE__);
    b->label     = label ? xstrdup(label) : NULL;
    if (fn->tail)
        fn->tail->next = b;
    else
        fn->blocks = b;
    fn->tail = b;
    return b;
}

AVR_Instr *avr_append(AVR_Func *fn, AVR_Op op)
{
    return avr_append_to(fn->tail, op);
}

AVR_Instr *avr_append_to(AVR_Block *b, AVR_Op op)
{
    AVR_Instr *in = xalloc(sizeof(AVR_Instr), __func__, __FILE__, __LINE__);
    in->op        = op;
    if (b->tail)
        b->tail->next = in;
    else
        b->head = in;
    b->tail = in;
    return in;
}

AVR_Instr *avr_insert_after(AVR_Block *b, AVR_Instr *in, AVR_Op op)
{
    AVR_Instr *n = xalloc(sizeof(AVR_Instr), __func__, __FILE__, __LINE__);
    n->op        = op;
    n->next      = in->next;
    in->next     = n;
    if (b->tail == in)
        b->tail = n;
    return n;
}

AVR_Block *avr_split_after(AVR_Func *fn, AVR_Block *b, AVR_Instr *in, const char *label)
{
    AVR_Block *n = xalloc(sizeof(AVR_Block), __func__, __FILE__, __LINE__);
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

void avr_free_instr(AVR_Instr *in)
{
    for (int i = 0; i < AVR_MAX_OPERANDS; i++)
        xfree(in->opnd[i].sym);
    xfree(in);
}

void avr_free_func(AVR_Func *fn)
{
    AVR_Block *b = fn->blocks;
    while (b) {
        AVR_Block *bnext = b->next;
        AVR_Instr *in    = b->head;
        while (in) {
            AVR_Instr *next = in->next;
            avr_free_instr(in);
            in = next;
        }
        xfree(b->label);
        xfree(b);
        b = bnext;
    }
    xfree(fn->name);
    xfree(fn);
}

AVR_Operand avr_reg(int reg)
{
    return (AVR_Operand){ .kind = AVR_OPND_REG, .reg = reg };
}

AVR_Operand avr_imm(int64_t imm)
{
    return (AVR_Operand){ .kind = AVR_OPND_IMM, .imm = imm };
}

AVR_Operand avr_sym(AVR_Modifier mod, const char *sym, int64_t off)
{
    return (AVR_Operand){ .kind = AVR_OPND_SYM, .mod = mod, .sym = xstrdup(sym), .imm = off };
}

AVR_Operand avr_ptr(int ptr, AVR_PtrMode mode)
{
    return (AVR_Operand){ .kind = AVR_OPND_PTR, .reg = ptr, .mode = mode };
}

AVR_Operand avr_disp(int ptr, int q)
{
    return (AVR_Operand){ .kind = AVR_OPND_DISP, .reg = ptr, .imm = q };
}

AVR_Operand avr_label(const char *sym)
{
    return (AVR_Operand){ .kind = AVR_OPND_LABEL, .sym = xstrdup(sym) };
}
