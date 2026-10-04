//
// TAC → x86-64 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

X86_Instr *emit0(Gen *g, X86_Op op, X86_Width width)
{
    return x86_append(g->fn, op, width);
}

X86_Instr *emit1(Gen *g, X86_Op op, X86_Width width, X86_Operand a)
{
    X86_Instr *in = emit0(g, op, width);
    in->opnd[0]   = a;
    return in;
}

X86_Instr *emit2(Gen *g, X86_Op op, X86_Width width, X86_Operand src, X86_Operand dst)
{
    X86_Instr *in = emit1(g, op, width, src);
    in->opnd[1]   = dst;
    return in;
}

// Zero is `xor` (shorter, and the flags are never live across a constant load); a
// 64-bit constant is `movl` when it fits 32 bits zero-extended, `movq` when it fits
// them sign-extended, and otherwise `movabsq`.
void gen_li(Gen *g, int reg, X86_Width width, int64_t imm)
{
    if (width == X86_Q && (uint64_t)imm <= UINT32_MAX)
        width = X86_L;
    if (imm == 0)
        emit2(g, X86_XOR, X86_L, x86_reg(reg, X86_L), x86_reg(reg, X86_L));
    else if (width != X86_Q || x86_imm32(imm))
        emit2(g, X86_MOV, width, x86_imm(width == X86_L ? (int32_t)imm : imm), x86_reg(reg, width));
    else
        emit2(g, X86_MOVABS, X86_Q, x86_imm(imm), x86_reg(reg, X86_Q));
}

// The value of integer constant `c`, and its width.
static int64_t const_int(const Gen *g, const Tac_Const *c, X86_Width *width)
{
    *width = X86_L;
    switch (c->kind) {
    case TAC_CONST_INT:
        return (int32_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_SCHAR:
        return (int8_t)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_LONG:
        *width = X86_Q;
        return c->u.long_val;
    case TAC_CONST_ULONG:
        *width = X86_Q;
        return (int64_t)c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        *width = X86_Q;
        return c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        *width = X86_Q;
        return (int64_t)c->u.ulong_long_val;
    default:
        fatal_error("x86: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("x86: %s: returning a variable is not implemented yet", gen_name(g));
        X86_Width width;
        int64_t value = const_int(g, v->u.constant, &width);
        gen_li(g, X86_RAX, width, value);
    }
    emit0(g, X86_RET, X86_Q);
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("x86: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = x86_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("x86: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    x86_emit_func(out, g.fn);
    x86_free_func(g.fn);
}

void x86_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        x86_emit_header(out); // a new translation unit
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("x86: static data is not implemented yet");
    }
}
