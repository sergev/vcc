//
// TAC → AVR IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

AVR_Instr *emit0(Gen *g, AVR_Op op)
{
    return avr_append(g->fn, op);
}

AVR_Instr *emit1(Gen *g, AVR_Op op, AVR_Operand a)
{
    AVR_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

AVR_Instr *emit2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b)
{
    AVR_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

// The value of integer constant `c`, and its size in bytes.
static uint64_t const_int(const Gen *g, const Tac_Const *c, int *size)
{
    switch (c->kind) {
    case TAC_CONST_SCHAR:
        *size = 1;
        return (uint64_t)(int64_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        *size = 1;
        return c->u.uchar_val;
    case TAC_CONST_INT:
        *size = 2;
        return (uint64_t)c->u.int_val;
    case TAC_CONST_UINT:
        *size = 2;
        return c->u.uint_val;
    case TAC_CONST_LONG:
        *size = 4;
        return (uint64_t)c->u.long_val;
    case TAC_CONST_ULONG:
        *size = 4;
        return c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        *size = 8;
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        *size = 8;
        return c->u.ulong_long_val;
    default:
        fatal_error("avr: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

// The first (lowest) register of a result of `size` bytes: r24, r25:r24, r25:r22 or
// r25:r18, little-endian.
static int result_reg(int size)
{
    return size == 1 ? 24 : 26 - (size + 1) / 2 * 2;
}

static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("avr: %s: returning a variable is not implemented yet", gen_name(g));
        int size;
        uint64_t value = const_int(g, v->u.constant, &size);
        int reg        = result_reg(size);
        for (int i = 0; i < size; i++)
            emit2(g, AVR_LDI, avr_reg(reg + i), avr_imm((value >> (8 * i)) & 0xff));
    }
    emit0(g, AVR_RET);
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("avr: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = avr_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("avr: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    avr_emit_func(out, g.fn);
    avr_free_func(g.fn);
}

void avr_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        avr_emit_header(out); // a new translation unit
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the linker resolves undefined names
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("avr: static data is not implemented yet");
    }
}
