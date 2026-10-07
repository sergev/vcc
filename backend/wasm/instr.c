//
// Instruction selection: each TAC instruction becomes stack code that pushes its
// operands, computes, and pops the result into its destination.  A destination in
// memory takes its address first, under the value: begin_dst before the operands,
// end_dst after them.  A narrow integer is kept extended in its i32 (signed or zero
// by its type), so a load of one needs no fixing and a truncation does it.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

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

// The value type a constant of this kind is held in.
static Wasm_ValType const_valtype(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_LONG_LONG:
    case TAC_CONST_ULONG_LONG:
        return WASM_I64;
    case TAC_CONST_FLOAT:
        return WASM_F32;
    case TAC_CONST_DOUBLE:
        return WASM_F64;
    default:
        return WASM_I32;
    }
}

static Wasm_Instr *emit(Gen *g, Wasm_Op op)
{
    return wasm_append(g->fn, op);
}

static void emit_imm(Gen *g, Wasm_Op op, int64_t imm)
{
    emit(g, op)->imm = imm;
}

static void push_int(Gen *g, Wasm_ValType t, int64_t v)
{
    if (t == WASM_I64)
        emit_imm(g, WASM_I64_CONST, v);
    else
        emit_imm(g, WASM_I32_CONST, (int32_t)v);
}

// Push constant c as a value of type t.
static void push_const(Gen *g, const Tac_Const *c, Wasm_ValType t)
{
    switch (t) {
    case WASM_I32:
    case WASM_I64:
        push_int(g, t, const_int(c));
        return;
    case WASM_F32: {
        float f = (float)const_double(c);
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        emit_imm(g, WASM_F32_CONST, bits);
        return;
    }
    case WASM_F64: {
        double d = const_double(c);
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        emit_imm(g, WASM_F64_CONST, (int64_t)bits);
        return;
    }
    case WASM_VOID:
        break;
    }
    fatal_error("wasm: %s: a constant of no type", g->fn->name);
}

//
// Variables: a local, or a static object in memory.
//
static bool is_local(const Gen *g, const char *name)
{
    return var_type(g, name) != NULL;
}

// The type of variable v, wherever it lives.
static const Tac_Type *type_of(const Gen *g, const char *name)
{
    const Tac_Type *t = var_type(g, name);
    if (!t)
        t = global_type(g, name);
    if (!t)
        fatal_error("wasm: %s: %s has no type", g->fn->name, name);
    return t;
}

static Wasm_Op load_op(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
        return WASM_I32_LOAD8_S;
    case TAC_TYPE_UCHAR:
        return WASM_I32_LOAD8_U;
    case TAC_TYPE_SHORT:
        return WASM_I32_LOAD16_S;
    case TAC_TYPE_USHORT:
        return WASM_I32_LOAD16_U;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
        return WASM_I64_LOAD;
    case TAC_TYPE_FLOAT:
        return WASM_F32_LOAD;
    case TAC_TYPE_DOUBLE:
        return WASM_F64_LOAD;
    default:
        return WASM_I32_LOAD;
    }
}

static Wasm_Op store_op(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
        return WASM_I32_STORE8;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
        return WASM_I32_STORE16;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
        return WASM_I64_STORE;
    case TAC_TYPE_FLOAT:
        return WASM_F32_STORE;
    case TAC_TYPE_DOUBLE:
        return WASM_F64_STORE;
    default:
        return WASM_I32_STORE;
    }
}

// A load or store of static object `name`: its address is the relocated offset.
static void emit_static_access(Gen *g, Wasm_Op op, const char *name)
{
    emit(g, op)->sym = xstrdup(wasm_symbol(g->program, name));
}

// Push value v, of type t.
static void push_val(Gen *g, const Tac_Val *v, Wasm_ValType t)
{
    if (v->kind == TAC_VAL_CONSTANT) {
        push_const(g, v->u.constant, t);
        return;
    }
    const char *name = v->u.var_name;
    if (is_local(g, name)) {
        emit_imm(g, WASM_LOCAL_GET, var_local(g, name));
        return;
    }
    emit_imm(g, WASM_I32_CONST, 0);
    emit_static_access(g, load_op(type_of(g, name)), name);
}

// The value type of value v; a constant's is its kind's.
static Wasm_ValType val_valtype(const Gen *g, const Tac_Val *v)
{
    if (v->kind == TAC_VAL_CONSTANT)
        return const_valtype(v->u.constant);
    return wasm_valtype(type_of(g, v->u.var_name));
}

// The type of variable v, or NULL for a constant.
static const Tac_Type *val_type(const Gen *g, const Tac_Val *v)
{
    return v->kind == TAC_VAL_VAR ? type_of(g, v->u.var_name) : NULL;
}

static void check_dst(const Gen *g, const Tac_Val *dst)
{
    if (dst->kind != TAC_VAL_VAR)
        fatal_error("wasm: %s: a constant destination", g->fn->name);
}

// Before the value of `dst` is computed: the address of a destination in memory.
static void begin_dst(Gen *g, const Tac_Val *dst)
{
    check_dst(g, dst);
    if (!is_local(g, dst->u.var_name))
        emit_imm(g, WASM_I32_CONST, 0);
}

// Pop the value on top of the stack into `dst`.
static void end_dst(Gen *g, const Tac_Val *dst)
{
    const char *name = dst->u.var_name;
    if (is_local(g, name))
        emit_imm(g, WASM_LOCAL_SET, var_local(g, name));
    else
        emit_static_access(g, store_op(type_of(g, name)), name);
}

static bool is_signed_type(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_SHORT:
    case TAC_TYPE_INT:
    case TAC_TYPE_LONG:
    case TAC_TYPE_LONG_LONG:
        return true;
    default:
        return false;
    }
}

// Bring an i32 holding a value of type t to its canonical form: a narrow type
// extended from its width, by its signedness.
static void narrow(Gen *g, const Tac_Type *t)
{
    int size = wasm_type_size(t);
    if (size >= 4 || t->kind == TAC_TYPE_VOID)
        return;
    if (is_signed_type(t)) {
        emit(g, size == 1 ? WASM_I32_EXTEND8_S : WASM_I32_EXTEND16_S);
    } else {
        emit_imm(g, WASM_I32_CONST, size == 1 ? 0xff : 0xffff);
        emit(g, WASM_I32_AND);
    }
}

// The canonical value of constant v as a value of type t.
static int64_t narrow_const(int64_t v, const Tac_Type *t)
{
    switch (wasm_type_size(t)) {
    case 1:
        return is_signed_type(t) ? (int8_t)v : (uint8_t)v;
    case 2:
        return is_signed_type(t) ? (int16_t)v : (uint16_t)v;
    case 4:
        return is_signed_type(t) ? (int32_t)v : (uint32_t)v;
    default:
        return v;
    }
}

//
// Integer width conversions: truncation, sign and zero extension.
//
static void gen_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, bool sign)
{
    begin_dst(g, dst);
    const Tac_Type *dt = type_of(g, dst->u.var_name);
    Wasm_ValType dvt   = wasm_valtype(dt);
    if (src->kind == TAC_VAL_CONSTANT) {
        push_int(g, dvt, narrow_const(const_int(src->u.constant), dt));
        end_dst(g, dst);
        return;
    }
    const Tac_Type *st = val_type(g, src);
    Wasm_ValType svt   = wasm_valtype(st);
    push_val(g, src, svt);
    // A zero extension of a signed narrow value drops its sign bits first.
    if (!sign && is_signed_type(st) && wasm_type_size(st) < 4) {
        emit_imm(g, WASM_I32_CONST, wasm_type_size(st) == 1 ? 0xff : 0xffff);
        emit(g, WASM_I32_AND);
    }
    if (svt == WASM_I64 && dvt == WASM_I32)
        emit(g, WASM_I32_WRAP_I64);
    else if (svt == WASM_I32 && dvt == WASM_I64)
        emit(g, sign ? WASM_I64_EXTEND_I32_S : WASM_I64_EXTEND_I32_U);
    if (wasm_type_size(dt) < wasm_type_size(st) || svt != dvt)
        narrow(g, dt);
    end_dst(g, dst);
}

//
// Unary and binary operators on integers.
//
static void gen_unary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *src = in->u.unary.src, *dst = in->u.unary.dst;
    begin_dst(g, dst);
    Wasm_ValType t = val_valtype(g, src);
    bool i64       = t == WASM_I64;
    switch (in->u.unary.op) {
    case TAC_UNARY_COMPLEMENT:
    case TAC_UNARY_COMPLEMENT_UNSIGNED:
        push_val(g, src, t);
        push_int(g, t, -1);
        emit(g, i64 ? WASM_I64_XOR : WASM_I32_XOR);
        break;
    case TAC_UNARY_NEGATE:
    case TAC_UNARY_NEGATE_UNSIGNED:
        push_int(g, t, 0);
        push_val(g, src, t);
        emit(g, i64 ? WASM_I64_SUB : WASM_I32_SUB);
        break;
    case TAC_UNARY_NOT:
        push_val(g, src, t);
        emit(g, i64 ? WASM_I64_EQZ : WASM_I32_EQZ);
        break;
    default:
        fatal_error("wasm: %s: unary operator %d is not supported yet", g->fn->name,
                    in->u.unary.op);
    }
    narrow(g, type_of(g, dst->u.var_name));
    end_dst(g, dst);
}

// The i32 and i64 opcodes of an integer binary operator; false when it has none.
static bool int_binop(Tac_BinaryOperator op, Wasm_Op *op32, Wasm_Op *op64)
{
    switch (op) {
#define OP(tac, w)            \
    case TAC_BINARY_##tac:    \
        *op32 = WASM_I32_##w; \
        *op64 = WASM_I64_##w; \
        return true;
        OP(ADD, ADD)
        OP(ADD_UNSIGNED, ADD)
        OP(SUBTRACT, SUB)
        OP(SUBTRACT_UNSIGNED, SUB)
        OP(MULTIPLY, MUL)
        OP(MULTIPLY_UNSIGNED, MUL)
        OP(DIVIDE, DIV_S)
        OP(DIVIDE_UNSIGNED, DIV_U)
        OP(REMAINDER, REM_S)
        OP(REMAINDER_UNSIGNED, REM_U)
        OP(EQUAL, EQ)
        OP(NOT_EQUAL, NE)
        OP(LESS_THAN, LT_S)
        OP(LESS_OR_EQUAL, LE_S)
        OP(GREATER_THAN, GT_S)
        OP(GREATER_OR_EQUAL, GE_S)
        OP(LESS_THAN_UNSIGNED, LT_U)
        OP(LESS_OR_EQUAL_UNSIGNED, LE_U)
        OP(GREATER_THAN_UNSIGNED, GT_U)
        OP(GREATER_OR_EQUAL_UNSIGNED, GE_U)
        OP(BITWISE_AND, AND)
        OP(BITWISE_OR, OR)
        OP(BITWISE_XOR, XOR)
        OP(LEFT_SHIFT, SHL)
        OP(RIGHT_SHIFT, SHR_S)
        OP(RIGHT_SHIFT_LOGICAL, SHR_U)
#undef OP
    default:
        return false;
    }
}

static bool is_shift(Tac_BinaryOperator op)
{
    return op == TAC_BINARY_LEFT_SHIFT || op == TAC_BINARY_RIGHT_SHIFT ||
           op == TAC_BINARY_RIGHT_SHIFT_LOGICAL;
}

static void gen_binary(Gen *g, const Tac_Instruction *in)
{
    const Tac_Val *a = in->u.binary.src1, *b = in->u.binary.src2, *dst = in->u.binary.dst;
    Tac_BinaryOperator op = in->u.binary.op;
    Wasm_Op op32, op64;
    if (!int_binop(op, &op32, &op64))
        fatal_error("wasm: %s: binary operator %d is not supported yet", g->fn->name, op);
    begin_dst(g, dst);
    // The operands' type: the left one's, but the right one's when only it is a
    // variable; a shift's count has a type of its own.
    Wasm_ValType t = val_valtype(g, a);
    if (a->kind == TAC_VAL_CONSTANT && b->kind == TAC_VAL_VAR && !is_shift(op))
        t = val_valtype(g, b);
    push_val(g, a, t);
    if (is_shift(op) && b->kind == TAC_VAL_VAR) {
        // The count has a type of its own.
        Wasm_ValType ct = val_valtype(g, b);
        push_val(g, b, ct);
        if (ct == WASM_I64 && t == WASM_I32)
            emit(g, WASM_I32_WRAP_I64);
        else if (ct == WASM_I32 && t == WASM_I64)
            emit(g, WASM_I64_EXTEND_I32_U);
    } else {
        push_val(g, b, t);
    }
    emit(g, t == WASM_I64 ? op64 : op32);
    narrow(g, type_of(g, dst->u.var_name));
    end_dst(g, dst);
}

//
// Control: jumps by the skeleton's rules (structure.c).
//
static void gen_cond_jump(Gen *g, const Tac_Val *cond, const char *target, bool if_zero)
{
    Wasm_ValType t = val_valtype(g, cond);
    gen_branch_setup(g, target);
    push_val(g, cond, t);
    if (t == WASM_I64) {
        emit(g, WASM_I64_EQZ);
        if (!if_zero)
            emit(g, WASM_I32_EQZ);
    } else if (if_zero) {
        emit(g, WASM_I32_EQZ);
    }
    gen_branch(g, target, true);
}

//
// Calls.
//
static void gen_call(Gen *g, const Tac_Instruction *in, bool noreturn)
{
    const Tac_Val *dst       = in->u.fun_call.dst;
    const Tac_Type *fun_type = in->u.fun_call.fun_type;
    if (in->u.fun_call.indirect)
        fatal_error("wasm: %s: indirect calls are not supported yet", g->fn->name);
    if (!fun_type)
        fatal_error("wasm: %s: call of %s with no type", g->fn->name, in->u.fun_call.fun_name);
    if (dst)
        begin_dst(g, dst);
    const Tac_Type *p = fun_type->u.fun_type.param_types;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next) {
        if (p) {
            push_val(g, a, wasm_valtype(p));
            p = p->next;
        } else if (fun_type->u.fun_type.variadic) {
            fatal_error("wasm: %s: variable arguments are not supported yet", g->fn->name);
        } else {
            push_val(g, a, val_valtype(g, a)); // no prototype: the argument's own type
        }
    }
    if (fun_type->u.fun_type.variadic)
        fatal_error("wasm: %s: variadic calls are not supported yet", g->fn->name);
    emit(g, WASM_CALL)->sym = xstrdup(wasm_symbol(g->program, in->u.fun_call.fun_name));
    Wasm_ValType result     = wasm_valtype(fun_type->u.fun_type.ret_type);
    if (noreturn)
        emit(g, WASM_UNREACHABLE);
    else if (dst)
        end_dst(g, dst);
    else if (result != WASM_VOID)
        emit(g, WASM_DROP);
}

void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_RETURN:
        if (in->u.return_.src)
            push_val(g, in->u.return_.src, g->fn->result);
        else if (g->fn->result != WASM_VOID)
            push_int(g, g->fn->result, 0); // `return;` in a function with a result
        emit(g, WASM_RETURN);
        return;
    case TAC_INSTRUCTION_COPY:
        begin_dst(g, in->u.copy.dst);
        push_val(g, in->u.copy.src, wasm_valtype(type_of(g, in->u.copy.dst->u.var_name)));
        end_dst(g, in->u.copy.dst);
        return;
    case TAC_INSTRUCTION_SIGN_EXTEND:
        gen_int_convert(g, in->u.sign_extend.src, in->u.sign_extend.dst, true);
        return;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        gen_int_convert(g, in->u.zero_extend.src, in->u.zero_extend.dst, false);
        return;
    case TAC_INSTRUCTION_TRUNCATE:
        gen_int_convert(g, in->u.truncate.src, in->u.truncate.dst, true);
        return;
    case TAC_INSTRUCTION_UNARY:
        gen_unary(g, in);
        return;
    case TAC_INSTRUCTION_BINARY:
        gen_binary(g, in);
        return;
    case TAC_INSTRUCTION_LABEL:
        return; // a block boundary: structure.c
    case TAC_INSTRUCTION_JUMP:
        gen_branch_setup(g, in->u.jump.target);
        gen_branch(g, in->u.jump.target, false);
        return;
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
        gen_cond_jump(g, in->u.jump_if_zero.condition, in->u.jump_if_zero.target, true);
        return;
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        gen_cond_jump(g, in->u.jump_if_not_zero.condition, in->u.jump_if_not_zero.target, false);
        return;
    case TAC_INSTRUCTION_FUN_CALL:
        gen_call(g, in, false);
        return;
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        gen_call(g, in, true);
        return;
    default:
        break;
    }
    fatal_error("wasm: %s: TAC %s is not supported yet", g->fn->name,
                tac_instruction_name(in->kind));
}
