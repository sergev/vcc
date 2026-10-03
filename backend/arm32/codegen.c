//
// TAC → ARM32 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

A32_Instr *emit0(Gen *g, A32_Op op)
{
    return a32_append(g->fn, op);
}

A32_Instr *emit1(Gen *g, A32_Op op, A32_Operand a)
{
    A32_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

A32_Instr *emit2(Gen *g, A32_Op op, A32_Operand a, A32_Operand b)
{
    A32_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

A32_Instr *emit3(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c)
{
    A32_Instr *in = emit2(g, op, a, b);
    in->opnd[2]   = c;
    return in;
}

// A constant takes one `mov` when it is a modified immediate, one `mvn` when its
// complement is, one `movw` when it fits 16 bits, and otherwise `movw` + `movt`.
void gen_li(Gen *g, int reg, uint32_t imm)
{
    if (a32_operand2_imm(imm)) {
        emit2(g, A32_MOV, a32_reg(reg), a32_imm(imm));
    } else if (a32_operand2_imm(~imm)) {
        emit2(g, A32_MVN, a32_reg(reg), a32_imm(~imm));
    } else {
        emit2(g, A32_MOVW, a32_reg(reg), a32_imm(imm & 0xffff));
        if (imm >> 16)
            emit2(g, A32_MOVT, a32_reg(reg), a32_imm(imm >> 16));
    }
}

// The value of integer constant `c`, 64 bits wide for a long long (`*pair` set).
static uint64_t const_int(const Gen *g, const Tac_Const *c, bool *pair)
{
    *pair = false;
    switch (c->kind) {
    case TAC_CONST_INT:
        return (uint32_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_SCHAR:
        return (uint32_t)(int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_LONG:
        return (uint32_t)c->u.long_val;
    case TAC_CONST_ULONG:
        return (uint32_t)c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        *pair = true;
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        *pair = true;
        return c->u.ulong_long_val;
    default:
        fatal_error("arm32: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("arm32: %s: returning a variable is not implemented yet", gen_name(g));
        bool pair;
        uint64_t value = const_int(g, v->u.constant, &pair);
        gen_li(g, A32_R0, (uint32_t)value);
        if (pair)
            gen_li(g, A32_R0 + 1, (uint32_t)(value >> 32));
    }
    emit1(g, A32_BX, a32_reg(A32_LR));
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("arm32: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = a32_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("arm32: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    a32_emit_func(out, g.fn);
    a32_free_func(g.fn);
}

void arm32_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        a32_emit_header(out); // a new translation unit
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("arm32: static data is not implemented yet");
    }
}
