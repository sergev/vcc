//
// Register allocation for MMIX: the target side of backend/common/regalloc.c, in GCC's
// fixed model (internal.h).  Every scalar takes one register: there is no FP file and
// no pair.  A value live across no call may take the hole $15 or an argument register
// $16-$31, first; any value one of $0-$13, which a call keeps below its hole at no cost
// (the register stack saves them), so there is no prologue push.  The allocator numbers
// registers from 1, $0 being register 0.  After allocation, the compaction (phys_reg)
// shifts $14-$31 down to just above the highest of $0-$13 in use.
//
#include "regalloc.h"

#include <string.h>

#include "internal.h"

bool mmix_regalloc = true;

// Argument registers and the hole, then the registers a call keeps, + 1.
static const int int_pool[] = { 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                31, 32, 16, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                12, 13, 14 };

#define NPOOL ((int)(sizeof(int_pool) / sizeof(int_pool[0])))

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (!mmix_is_scalar(t) || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    return REGALLOC_INT;
}

static bool runtime_call(void *arg, const Flow *f, const Tac_Instruction *in, const Tac_Val **res)
{
    (void)arg;
    (void)f;
    (void)in;
    (void)res;
    return false; // no runtime helpers: multiply, divide and binary64 are instructions
}

// The stack builtins are expanded in place, through the scratch registers: no call.
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return mmix_stack_builtin(in);
}

// A parameter arrives in $i, kept by a call for i < 14.
// cppcheck-suppress constParameterCallback ; RegAlloc_Target's hook
static void param_hints(void *arg, StringMap *hints, StringMap *hints_hi)
{
    (void)hints_hi;
    const Gen *g = arg;
    int i        = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p && i < V_RJ; p = p->next, i++)
        if (mmix_is_scalar(p->type))
            map_insert(hints, p->name, i + 1, 0);
}

// Argument i in $(16+i), the result in the hole.
static void call_hints(void *arg, const Flow *f, const Tac_Instruction *in, int *hint)
{
    (void)arg;
    int i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a && i < MAX_REG_ARGS; a = a->next, i++) {
        int v = a->kind == TAC_VAL_VAR ? flow_var(f, a->u.var_name) : -1;
        if (v >= 0 && !hint[v])
            hint[v] = V_ARG0 + i + 1;
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    int v              = dst ? flow_var(f, dst->u.var_name) : -1;
    if (v >= 0 && !hint[v])
        hint[v] = V_HOLE + 1;
}

static void assign(void *arg, const char *name, int reg, int hi)
{
    (void)hi;
    Gen *g = arg;
    map_insert(&g->regs, name, reg, 0);
    if (reg - 1 < V_RJ && reg > g->P)
        g->P = reg; // one more than the highest of $0-$13
}

static void dead_param(void *arg, const char *name)
{
    map_insert(&((Gen *)arg)->dead, name, 1, 0);
}

void gen_regalloc(Gen *g)
{
    RegAlloc_Target desc = {
        .int_pool     = int_pool,
        .nint         = NPOOL,
        .int_narg     = V_LAST - V_HOLE + 1,
        .ret_int      = 1,
        .arg          = g,
        .classify     = classify,
        .runtime_call = runtime_call,
        .param_hints  = param_hints,
        .call_hints   = call_hints,
        .inline_call  = inline_call,
        .assign       = assign,
        .dead_param   = dead_param,
    };
    regalloc(&desc, g->tl);
}
