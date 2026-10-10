//
// Register allocation for MSP430: the target side of backend/common/regalloc.c, with
// the 16-bit register as its unit.  An int, a pointer or a char takes one register, a
// long or a float two, not necessarily adjacent; a long long or a double stays in
// memory.  A value not live across a call or a helper may take r12-r14 or r11, first;
// any other value one of r10-r4, call-saved, which the prologue then pushes.  r15 is
// the selection's scratch register, never allocated.  A function with a helper that
// takes its first operand in r8-r11 keeps r8-r10 free of variables.
//
#include "regalloc.h"

#include "internal.h"

static const int int_pool[]    = { 12, 13, 14, 11, 10, 9, 8, 7, 6, 5, 4 };
static const int int_pool_r8[] = { 12, 13, 14, 11, 7, 6, 5, 4 };

#define NPOOL(p) ((int)(sizeof(p) / sizeof(p[0])))

typedef struct {
    Gen *g;
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (!msp_is_scalar(t) || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    int size = msp_type_size(t);
    return size <= 2 ? REGALLOC_INT : size == 4 ? REGALLOC_PAIR : REGALLOC_NONE;
}

static bool runtime_call(void *arg, const Flow *f, const Tac_Instruction *in, const Tac_Val **res)
{
    (void)f;
    bool r8;
    if (!uses_helper(((Target *)arg)->g, in, &r8))
        return false;
    *res = instr_dst(in);
    return true;
}

// alloca and the other stack builtins: in place, through r15 alone (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return msp_stack_builtin(in);
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
    map_insert(&((Target *)arg)->g->regs, name, reg | hi << 8, 0);
}

// Its register may hold another parameter on entry: store_params leaves it alone.
static void dead_param(void *arg, const char *name)
{
    map_insert(&((Target *)arg)->g->dead, name, 1, 0);
}

void gen_regalloc(Gen *g)
{
    for (const Tac_Instruction *in = g->tl->u.function.body; in && !g->no_r8; in = in->next) {
        bool r8;
        if (uses_helper(g, in, &r8))
            g->no_r8 = r8;
    }
    Target target        = { .g = g };
    RegAlloc_Target desc = {
        .int_pool     = g->no_r8 ? int_pool_r8 : int_pool,
        // r4, the last of either pool, is the frame pointer in a frame from it.
        .nint         = (g->no_r8 ? NPOOL(int_pool_r8) : NPOOL(int_pool)) - (g->fp ? 1 : 0),
        .int_narg     = 4,
        .ret_int      = 12,
        .ret_int_hi   = 13,
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
