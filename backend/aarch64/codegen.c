//
// TAC → AArch64 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

A64_Instr *emit1(Gen *g, A64_Op op, A64_Operand a)
{
    A64_Instr *in = a64_append(g->fn, op);
    in->opnd[0]   = a;
    return in;
}

A64_Instr *emit2(Gen *g, A64_Op op, A64_Operand a, A64_Operand b)
{
    A64_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

A64_Instr *emit3(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c)
{
    A64_Instr *in = emit2(g, op, a, b);
    in->opnd[2]   = c;
    return in;
}

// A constant takes one `mov` when all but one of its 16-bit chunks are zero (movz), or
// all ones (movn); otherwise a movz or movn, whichever leaves fewer chunks, and a movk
// for each of the rest.
void gen_li(Gen *g, int reg, A64_Width width, int64_t imm)
{
    int n      = width == A64_W ? 2 : 4;
    uint64_t v = width == A64_W ? (uint32_t)imm : (uint64_t)imm;
    int zeros = 0, ones = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = (v >> (16 * i)) & 0xffff;
        zeros += c == 0;
        ones += c == 0xffff;
    }
    if (zeros >= n - 1 || ones >= n - 1) {
        emit2(g, A64_MOV, a64_reg(reg, width), a64_imm(width == A64_W ? (int32_t)v : (int64_t)v));
        return;
    }
    unsigned fill = ones > zeros ? 0xffff : 0;
    bool first    = true;
    for (int i = 0; i < n; i++) {
        unsigned c = (v >> (16 * i)) & 0xffff;
        if (c == fill)
            continue;
        A64_Op op = !first ? A64_MOVK : fill ? A64_MOVN : A64_MOVZ;
        A64_Instr *mv =
            emit2(g, op, a64_reg(reg, width), a64_imm(op == A64_MOVN ? ~c & 0xffff : c));
        if (i > 0)
            mv->opnd[2] = a64_lsl(16 * i);
        first = false;
    }
}

// The value of integer constant `c`, and the width of register it takes.
static int64_t const_int(const Gen *g, const Tac_Const *c, A64_Width *width)
{
    *width = A64_W;
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
        *width = A64_X;
        return c->u.long_val;
    case TAC_CONST_LONG_LONG:
        *width = A64_X;
        return c->u.long_long_val;
    case TAC_CONST_ULONG:
        *width = A64_X;
        return (int64_t)c->u.ulong_val;
    case TAC_CONST_ULONG_LONG:
        *width = A64_X;
        return (int64_t)c->u.ulong_long_val;
    default:
        fatal_error("aarch64: %s: constant kind %d is not implemented yet", gen_name(g), c->kind);
    }
}

static void gen_return(Gen *g, const Tac_Val *v)
{
    if (v) {
        if (v->kind != TAC_VAL_CONSTANT)
            fatal_error("aarch64: %s: returning a variable is not implemented yet", gen_name(g));
        A64_Width width;
        int64_t value = const_int(g, v->u.constant, &width);
        gen_li(g, A64_X0, width, value);
    }
    emit1(g, A64_RET, (A64_Operand){ 0 });
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        gen_return(g, in->u.return_.src);
        break;
    default:
        fatal_error("aarch64: %s: %s is not implemented yet", gen_name(g),
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { .fn      = a64_new_func(tl->u.function.name, tl->u.function.global),
              .program = program,
              .tl      = tl };
    if (tl->u.function.static_locals)
        fatal_error("aarch64: %s: static data is not implemented yet", gen_name(&g));
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_return(&g, NULL); // falling off the end
    a64_emit_func(out, g.fn);
    a64_free_func(g.fn);
}

void aarch64_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("aarch64: static data is not implemented yet");
    }
}
