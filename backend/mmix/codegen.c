//
// TAC → MMIX IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

Mmix_Instr *emit0(Gen *g, Mmix_Op op)
{
    return mmix_append(g->fn, op);
}

Mmix_Instr *emit1(Gen *g, Mmix_Op op, Mmix_Operand a)
{
    Mmix_Instr *in = emit0(g, op);
    in->opnd[0]    = a;
    return in;
}

Mmix_Instr *emit2(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b)
{
    Mmix_Instr *in = emit1(g, op, a);
    in->opnd[1]    = b;
    return in;
}

Mmix_Instr *emit3(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b, Mmix_Operand c)
{
    Mmix_Instr *in = emit2(g, op, a, b);
    in->opnd[2]    = c;
    return in;
}

// setl sets the low wyde and clears the rest; incml, incmh and inch add the others.
// The shortest sequence for every constant is K9's.
void gen_const(Gen *g, int reg, uint64_t value)
{
    static const Mmix_Op inc[4] = { MMIX_SETL, MMIX_INCML, MMIX_INCMH, MMIX_INCH };
    emit2(g, MMIX_SETL, mmix_reg(reg), mmix_wyde(value & 0xffff));
    for (int i = 1; i < 4; i++) {
        unsigned w = (value >> (16 * i)) & 0xffff;
        if (w)
            emit2(g, inc[i], mmix_reg(reg), mmix_wyde(w));
    }
}

// The value of integer constant `c`, extended to 64 bits as its type is.
static uint64_t const_int(const Gen *g, const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_SCHAR:
        return (uint64_t)(int64_t)(int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_INT:
        return (uint64_t)(int64_t)(int32_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_LONG:
        return (uint64_t)c->u.long_val;
    case TAC_CONST_ULONG:
        return c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        return c->u.ulong_long_val;
    default:
        fatal_error("mmix: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

// The result goes in the callee's $0, which pop 1,0 hands back in the caller's hole.
static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("mmix: %s: returning a variable is not implemented yet", gen_name(g));
        gen_const(g, 0, const_int(g, v->u.constant));
    }
    emit2(g, MMIX_POP, mmix_imm(v ? 1 : 0), mmix_imm(0));
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("mmix: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = mmix_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("mmix: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    mmix_emit_func(out, g.fn);
    mmix_free_func(g.fn);
}

void mmix_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the linker resolves undefined names
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("mmix: static data is not implemented yet");
    }
}
