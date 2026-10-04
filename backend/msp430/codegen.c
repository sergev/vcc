//
// TAC → MSP430 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

Msp_Instr *emit0(Gen *g, Msp_Op op)
{
    return msp_append(g->fn, op);
}

Msp_Instr *emit1(Gen *g, Msp_Op op, Msp_Operand a)
{
    Msp_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

Msp_Instr *emit2(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b)
{
    Msp_Instr *in = emit1(g, op, a);
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
        fatal_error("msp430: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

// A result goes in r12, r13:r12 or r15:r12, low word first; a char in r12, extended.
static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("msp430: %s: returning a variable is not implemented yet",
                        gen_name(g));
        int size;
        uint64_t value = const_int(g, v->u.constant, &size);
        if (size == 1)
            value = v->u.constant->kind == TAC_CONST_SCHAR ? (uint64_t)(int64_t)(int8_t)value
                                                           : (uint8_t)value;
        for (int i = 0; i < (size + 1) / 2; i++)
            emit2(g, MSP_MOV, msp_imm((int64_t)((value >> (16 * i)) & 0xffff)),
                  msp_reg(12 + i));
    }
    emit0(g, MSP_RET);
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("msp430: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = msp_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("msp430: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    msp_emit_func(out, g.fn);
    msp_free_func(g.fn);
}

void msp430_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the linker resolves undefined names
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("msp430: static data is not implemented yet");
    }
}
