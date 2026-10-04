//
// Parameters, calls and returns: the avr-gcc ABI.
//
// Arguments are assigned left to right from r25 down, each taking the registers just
// below the previous one, its size rounded up to even; the first that would go below
// r8 goes on the stack, and so does every later one.  A variadic callee takes them all
// on the stack.  Stack arguments lie in order above the return address, unaligned.
//
#include "internal.h"
#include "xalloc.h"

typedef struct {
    int reg; // the first (lowest) register, or 0 on the stack
    int off; // the offset in the stack argument area
} ArgLoc;

// Assign `n` arguments of types `types`; returns the bytes of stack arguments.
static int assign_args(const Tac_Type *const *types, int n, bool variadic, ArgLoc *loc)
{
    int reg = 26, stack = 0;
    bool on_stack = variadic;
    for (int i = 0; i < n; i++) {
        int size = avr_type_size(types[i]);
        int even = (size + 1) & ~1;
        if (!on_stack && reg - even >= 8) {
            reg -= even;
            loc[i] = (ArgLoc){ .reg = reg };
        } else {
            on_stack = true;
            loc[i]   = (ArgLoc){ .off = stack };
            stack += size;
        }
    }
    return stack;
}

static int count_params(const Gen *g)
{
    int n = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next)
        n++;
    return n;
}

// The locations of the function's parameters, into `loc` (count_params of them).
static void param_locs(const Gen *g, ArgLoc *loc)
{
    int n                  = count_params(g);
    const Tac_Type **types = xalloc((n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
        types[i++] = p->type;
    }
    assign_args(types, n, g->tl->u.function.variadic, loc);
    xfree(types);
}

void place_params(Gen *g)
{
    int n       = count_params(g);
    ArgLoc *loc = xalloc((n + 1) * sizeof(*loc), __func__, __FILE__, __LINE__);
    param_locs(g, loc);
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++)
        if (!loc[i].reg)
            place_stack_param(g, p->name, p->type, loc[i].off);
    xfree(loc);
}

void store_params(Gen *g)
{
    int n       = count_params(g);
    ArgLoc *loc = xalloc((n + 1) * sizeof(*loc), __func__, __FILE__, __LINE__);
    param_locs(g, loc);
    int i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++)
        if (loc[i].reg)
            access_bytes(g, true, p->name, 0, loc[i].reg, avr_type_size(p->type));
    xfree(loc);
}

// SP += n, after a call: pop r0 for a few bytes, else through Z (the result registers
// are r18-r25).
static void release_stack(Gen *g, int n)
{
    if (n <= 4) {
        for (int i = 0; i < n; i++)
            emit1(g, AVR_POP, avr_reg(AVR_TMP));
        return;
    }
    emit2(g, AVR_IN, avr_reg(AVR_Z), avr_sym(AVR_MOD_NONE, "__SP_L__", 0));
    emit2(g, AVR_IN, avr_reg(AVR_Z + 1), avr_sym(AVR_MOD_NONE, "__SP_H__", 0));
    if (n <= Y_MAX) {
        emit2(g, AVR_ADIW, avr_reg(AVR_Z), avr_imm(n));
    } else {
        emit2(g, AVR_SUBI, avr_reg(AVR_Z), avr_imm(-n & 0xff));
        emit2(g, AVR_SBCI, avr_reg(AVR_Z + 1), avr_imm((-n >> 8) & 0xff));
    }
    emit2(g, AVR_IN, avr_reg(AVR_TMP), avr_sym(AVR_MOD_NONE, "__SREG__", 0));
    emit0(g, AVR_CLI);
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_H__", 0), avr_reg(AVR_Z + 1));
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SREG__", 0), avr_reg(AVR_TMP));
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_L__", 0), avr_reg(AVR_Z));
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    int n              = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        n++;
    const Tac_Type **types = xalloc((n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    ArgLoc *loc            = xalloc((n + 1) * sizeof(*loc), __func__, __FILE__, __LINE__);
    const Tac_Val **args   = xalloc((n + 1) * sizeof(*args), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        args[i]  = a;
        types[i] = val_type(g, a);
        if (!avr_is_scalar(types[i]))
            fatal_error("avr: %s: passing a structure is not implemented yet", gen_name(g));
    }
    int stack = assign_args(types, n, ft && ft->u.fun_type.variadic, loc);

    // The stack arguments, the last first and each high byte first, so that they lie
    // in order and little-endian; then the register ones, straight from memory.
    for (i = n - 1; i >= 0; i--) {
        if (loc[i].reg)
            continue;
        int size = avr_type_size(types[i]), a = block_a(size);
        load_val(g, args[i], a, size, EXT_TYPE);
        for (int k = size - 1; k >= 0; k--)
            emit1(g, AVR_PUSH, avr_reg(a + k));
    }
    for (i = 0; i < n; i++)
        if (loc[i].reg) {
            int size = avr_type_size(types[i]);
            load_val(g, args[i], loc[i].reg, size == 1 ? 2 : size, EXT_TYPE);
        }

    if (in->u.fun_call.indirect) {
        Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
        load_val(g, &fp, AVR_Z, 2, EXT_TYPE);
        emit0(g, AVR_ICALL);
    } else {
        emit1(g, AVR_CALL, avr_label(in->u.fun_call.fun_name));
    }
    if (stack)
        release_stack(g, stack);

    const Tac_Val *dst = in->u.fun_call.dst;
    if (dst) {
        const Tac_Type *t = val_type(g, dst);
        if (!avr_is_scalar(t))
            fatal_error("avr: %s: a structure result is not implemented yet", gen_name(g));
        int size = avr_type_size(t);
        store_val(g, dst, block_a(size), size);
    }
    xfree(types);
    xfree(loc);
    xfree(args);
}

// The first register of a result of `size` bytes: r24, r25:r24, r25:r22 or r25:r18.
static int result_reg(int size)
{
    return block_a(size);
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        const Tac_Type *fn = g->tl->u.function.type;
        const Tac_Type *t  = fn ? fn->u.fun_type.ret_type : val_type(g, v);
        if (!avr_is_scalar(t))
            fatal_error("avr: %s: returning a structure is not implemented yet", gen_name(g));
        int size = avr_type_size(t);
        if (size == 1)
            size = 2; // a char comes back extended to r25:r24, as avr-gcc does
        load_val(g, v, result_reg(size), size, EXT_TYPE);
    }
    if (!last)
        emit1(g, AVR_RJMP, avr_label(g->exit));
}
