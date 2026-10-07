//
// Instruction selection: each TAC instruction becomes stack code that pushes its
// operands, computes, and pops the result into its destination.
//
#include <string.h>

#include "internal.h"

// The value of an integer constant, sign- or zero-extended from its own width.
static int64_t const_int(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_INT:
        return (int32_t)c->u.int_val;
    case TAC_CONST_LONG:
        return (int32_t)c->u.long_val;
    case TAC_CONST_LONG_LONG:
        return c->u.long_long_val;
    case TAC_CONST_UINT:
        return (uint32_t)c->u.uint_val;
    case TAC_CONST_ULONG:
        return (uint32_t)c->u.ulong_val;
    case TAC_CONST_ULONG_LONG:
        return (int64_t)c->u.ulong_long_val;
    case TAC_CONST_SCHAR:
        return (signed char)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_FLOAT:
        return (int64_t)c->u.float_val;
    case TAC_CONST_DOUBLE:
        return (int64_t)c->u.double_val;
    case TAC_CONST_LONG_DOUBLE:
        break;
    }
    fatal_error("wasm: a long double constant as an integer");
}

static double const_double(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_FLOAT:
        return c->u.float_val;
    case TAC_CONST_DOUBLE:
        return c->u.double_val;
    case TAC_CONST_LONG_DOUBLE:
        fatal_error("wasm: a long double constant as a double");
    case TAC_CONST_ULONG_LONG:
        return (double)c->u.ulong_long_val;
    case TAC_CONST_UINT:
    case TAC_CONST_ULONG:
    case TAC_CONST_UCHAR:
        return (double)(uint64_t)const_int(c);
    default:
        return (double)const_int(c);
    }
}

// Push constant c as a value of type t.
static void push_const(Gen *g, const Tac_Const *c, Wasm_ValType t)
{
    Wasm_Instr *in;
    switch (t) {
    case WASM_I32:
        in      = wasm_append(g->fn, WASM_I32_CONST);
        in->imm = (int32_t)const_int(c);
        return;
    case WASM_I64:
        in      = wasm_append(g->fn, WASM_I64_CONST);
        in->imm = const_int(c);
        return;
    case WASM_F32: {
        float f = (float)const_double(c);
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        in      = wasm_append(g->fn, WASM_F32_CONST);
        in->imm = bits;
        return;
    }
    case WASM_F64: {
        double d = const_double(c);
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        in      = wasm_append(g->fn, WASM_F64_CONST);
        in->imm = (int64_t)bits;
        return;
    }
    case WASM_VOID:
        break;
    }
    fatal_error("wasm: %s: a constant of no type", g->fn->name);
}

static void local_op(Gen *g, Wasm_Op op, int local)
{
    wasm_append(g->fn, op)->imm = local;
}

// Push value v, of type t.
static void push_val(Gen *g, const Tac_Val *v, Wasm_ValType t)
{
    if (v->kind == TAC_VAL_CONSTANT)
        push_const(g, v->u.constant, t);
    else
        local_op(g, WASM_LOCAL_GET, var_local(g, v->u.var_name));
}

// Pop the value on top of the stack into variable v.
static void pop_to(Gen *g, const Tac_Val *v)
{
    if (v->kind != TAC_VAL_VAR)
        fatal_error("wasm: %s: a constant destination", g->fn->name);
    local_op(g, WASM_LOCAL_SET, var_local(g, v->u.var_name));
}

// The value type of variable v.
static Wasm_ValType val_type(Gen *g, const Tac_Val *v)
{
    const Tac_Type *t = var_type(g, v->u.var_name);
    if (!t)
        fatal_error("wasm: %s: %s has no type", g->fn->name, v->u.var_name);
    return wasm_valtype(t);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        if (in->u.return_.src)
            push_val(g, in->u.return_.src, g->fn->result);
        wasm_append(g->fn, WASM_RETURN);
        return;
    case TAC_INSTRUCTION_COPY:
        push_val(g, in->u.copy.src, val_type(g, in->u.copy.dst));
        pop_to(g, in->u.copy.dst);
        return;
    default:
        break;
    }
    fatal_error("wasm: %s: TAC %s is not supported yet", g->fn->name,
                tac_instruction_name(in->kind));
}
