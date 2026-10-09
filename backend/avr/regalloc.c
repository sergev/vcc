//
// Register allocation for AVR: the target side of backend/common/regalloc.c, with the
// even register pair as its unit.  An int, a pointer or a char takes one pair (a char
// wasting the high byte), a long or a float two, not necessarily adjacent; a long long
// stays in memory.  A value not live across a call, or across an instruction the
// naive selection does (uses_scratch), may take an argument pair r24-r18, first; any
// other value one of r16-r2, call-saved, which the prologue then pushes, and Y (r28)
// in a function to have no frame.  The upper ones come first among them, since
// r16-r31 take immediate operands.
//
#include "regalloc.h"

#include "internal.h"

static const int int_pool[]   = { 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2 };
static const int int_pool_y[] = { 24, 22, 20, 18, 16, 28, 14, 12, 10, 8, 6, 4, 2 };

#define NINT ((int)(sizeof(int_pool) / sizeof(int_pool[0])))

typedef struct {
    Gen *g;
    const Flow *flow;
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (!avr_is_scalar(t) || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    int size = avr_type_size(t);
    return size <= 2 ? REGALLOC_INT : size == 4 ? REGALLOC_PAIR : REGALLOC_NONE;
}

static const Tac_Type *operand_type(const void *arg, const Tac_Val *v)
{
    const Target *t = arg;
    return flow_val_type(t->g, t->flow, v);
}

static bool runtime_call(void *arg, const Flow *f, const Tac_Instruction *in, const Tac_Val **res)
{
    Target *t = arg;
    t->flow   = f;
    *res      = instr_dst(in);
    return uses_scratch(in, operand_type, t);
}

// alloca and the other stack builtins: in place, through X, Z and r0 (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return avr_stack_builtin(in);
}

static void get_param_hints(void *arg, StringMap *hints, StringMap *hints_hi)
{
    param_hints(((Target *)arg)->g, hints, hints_hi);
}

static void get_call_hints(void *arg, const Flow *f, const Tac_Instruction *in, int *hint)
{
    call_hints(((Target *)arg)->g, f, in, hint);
}

static void assign(void *arg, const char *name, int reg, int hi)
{
    Gen *g = ((Target *)arg)->g;
    map_insert(&g->regs, name, reg | hi << 8, 0);
    g->var_regs |= 3u << reg;
    if (hi)
        g->var_regs |= 3u << hi;
}

// Its registers may hold another parameter on entry: store_params leaves it alone.
static void dead_param(void *arg, const char *name)
{
    map_insert(&((Target *)arg)->g->dead, name, 1, 0);
}

void gen_regalloc(Gen *g)
{
    Target target        = { .g = g };
    RegAlloc_Target desc = {
        .int_pool     = g->y_free ? int_pool_y : int_pool,
        .nint         = g->y_free ? NINT + 1 : NINT,
        .int_narg     = 4,
        .ret_int      = 24,
        .arg          = &target,
        .classify     = classify,
        .runtime_call = runtime_call,
        .param_hints  = get_param_hints,
        .call_hints   = get_call_hints,
        .assign       = assign,
        .dead_param   = dead_param,
        .inline_call  = inline_call,
    };
    regalloc(&desc, g->tl);
}
