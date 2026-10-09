//
// Register allocation for RISC-V: the target side of backend/common/regalloc.c.
// Candidates get callee-saved registers, s1-s11 or fs0-fs11, so they survive calls and
// keep clear of the scratch registers.  A value not live across a call may also take
// the argument registers a0-a7 and fa0-fa7, first: selection never uses them as
// scratch, and only a call (or a long double operation, a runtime call) writes them.
// A long long on rv32 is a pair.
//
#include "regalloc.h"

#include "internal.h"

// Argument registers, then callee-saved; a value live across a call starts at NARG.
static const int int_pool[] = { RV_A0,     RV_A0 + 1, RV_A0 + 2, RV_A0 + 3, RV_A0 + 4,
                                RV_A0 + 5, RV_A0 + 6, RV_A7,     RV_S1,     RV_S2,
                                RV_S2 + 1, RV_S2 + 2, RV_S2 + 3, RV_S2 + 4, RV_S2 + 5,
                                RV_S2 + 6, RV_S2 + 7, RV_S2 + 8, RV_S11 };
static const int fp_pool[]  = { RV_FA0,     RV_FA0 + 1, RV_FA0 + 2, RV_FA0 + 3, RV_FA0 + 4,
                                RV_FA0 + 5, RV_FA0 + 6, RV_FA0 + 7, RV_F0 + 8,  RV_F0 + 9,
                                RV_F0 + 18, RV_F0 + 19, RV_F0 + 20, RV_F0 + 21, RV_F0 + 22,
                                RV_F0 + 23, RV_F0 + 24, RV_F0 + 25, RV_F0 + 26, RV_F0 + 27 };

#define NINT ((int)(sizeof(int_pool) / sizeof(int_pool[0])))
#define NFP  ((int)(sizeof(fp_pool) / sizeof(fp_pool[0])))
#define NARG 8

typedef struct {
    Gen *g;
    const Flow *flow;
    bool used[RV_VREG];
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (rv_is_aggregate(t) || (rv_is_pair(t) && !rv_is_ll(t)) || t->kind == TAC_TYPE_LONG_DOUBLE ||
        t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    if (rv_is_fp(t))
        return REGALLOC_FP;
    return rv_is_ll(t) ? REGALLOC_PAIR : REGALLOC_INT;
}

// The type of an operand: a tracked variable's, a global's or a constant's.
static const Tac_Type *operand_type(const void *arg, const Tac_Val *v)
{
    const Target *t = arg;
    int var         = v->kind == TAC_VAL_VAR ? flow_var(t->flow, v->u.var_name) : -1;
    return var >= 0 ? t->flow->types[var] : val_type(t->g, v);
}

static bool makes_runtime_call(void *arg, const Flow *f, const Tac_Instruction *in,
                               const Tac_Val **res)
{
    Target *t = arg;
    t->flow   = f;
    return runtime_call(in, operand_type, t, res);
}

// alloca and the other stack builtins: in place, through t0 alone (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return rv_stack_builtin(in);
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
    Target *t = arg;
    map_insert(&t->g->regs, name, reg, 0);
    t->used[reg] = true;
    if (hi) {
        map_insert(&t->g->regs_hi, name, hi, 0);
        t->used[hi] = true;
    }
}

// Its register may hold another parameter on entry: gen_params leaves it alone.
static void dead_param(void *arg, const char *name)
{
    map_insert(&((Target *)arg)->g->dead, name, 1, 0);
}

void gen_regalloc(Gen *g)
{
    Target target        = { .g = g };
    RegAlloc_Target desc = {
        .int_pool     = int_pool,
        .nint         = NINT,
        .int_narg     = NARG,
        .fp_pool      = fp_pool,
        .nfp          = NFP,
        .fp_narg      = NARG,
        .ret_int      = RV_A0,
        .ret_int_hi   = RV_A0 + 1,
        .ret_fp       = RV_FA0,
        .arg          = &target,
        .classify     = classify,
        .runtime_call = makes_runtime_call,
        .param_hints  = get_param_hints,
        .call_hints   = get_call_hints,
        .assign       = assign,
        .dead_param   = dead_param,
        .inline_call  = inline_call,
    };
    regalloc(&desc, g->tl);

    for (int r = 0; r < RV_VREG; r++)
        if (target.used[r] && (r == RV_S1 || (r >= RV_S2 && r <= RV_S11) || r == RV_F0 + 8 ||
                               r == RV_F0 + 9 || (r >= RV_F0 + 18 && r <= RV_F0 + 27)))
            g->saved_reg[g->nsaved++] = r;
}
