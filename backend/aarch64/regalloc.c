//
// Register allocation for AArch64: the target side of backend/common/regalloc.c.
// Candidates get callee-saved registers, x19-x28 or v8-v15 (whose low 64 bits, all a
// float or double needs, AAPCS64 preserves), so they survive calls and keep clear of
// the scratch registers.  A value not live across a call may also take the argument
// registers x0-x7 and v0-v7, first: selection never uses them as scratch, and only a
// call (or a long double operation, a runtime call) writes them.  A long double never
// gets a register.
//
#include "regalloc.h"

#include "internal.h"

// Argument registers, then callee-saved; a value live across a call starts at NARG.
static const int int_pool[] = { A64_X(0),  A64_X(1),  A64_X(2),  A64_X(3),  A64_X(4),  A64_X(5),
                                A64_X(6),  A64_X(7),  A64_X(19), A64_X(20), A64_X(21), A64_X(22),
                                A64_X(23), A64_X(24), A64_X(25), A64_X(26), A64_X(27), A64_X(28) };
static const int fp_pool[]  = { A64_V(0),  A64_V(1),  A64_V(2),  A64_V(3),  A64_V(4),  A64_V(5),
                                A64_V(6),  A64_V(7),  A64_V(8),  A64_V(9),  A64_V(10), A64_V(11),
                                A64_V(12), A64_V(13), A64_V(14), A64_V(15) };

#define NINT ((int)(sizeof(int_pool) / sizeof(int_pool[0])))
#define NFP  ((int)(sizeof(fp_pool) / sizeof(fp_pool[0])))
#define NARG 8

typedef struct {
    Gen *g;
    const Flow *flow;
    bool used[A64_VREG];
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (a64_is_aggregate(t) || a64_is_ld(t) || t->kind == TAC_TYPE_VOID ||
        t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    return a64_is_fp(t) ? REGALLOC_FP : REGALLOC_INT;
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

// alloca and the other stack builtins: in place, through x9 alone (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return a64_stack_builtin(in);
}

static void get_param_hints(void *arg, StringMap *hints, StringMap *hints_hi)
{
    (void)hints_hi;
    param_hints(((Target *)arg)->g, hints);
}

static void get_call_hints(void *arg, const Flow *f, const Tac_Instruction *in, int *hint)
{
    call_hints(((Target *)arg)->g, f, in, hint);
}

static void assign(void *arg, const char *name, int reg, int hi)
{
    Target *t = arg;
    (void)hi;
    map_insert(&t->g->regs, name, reg, 0);
    t->used[reg] = true;
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
        .ret_int      = A64_X0,
        .ret_fp       = A64_V0,
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

    // The general registers first, so that pairs are of one file.
    for (int r = A64_X19; r <= A64_X28; r++)
        if (target.used[r])
            g->saved_reg[g->nsaved++] = r;
    for (int r = A64_V(8); r <= A64_V(15); r++)
        if (target.used[r])
            g->saved_reg[g->nsaved++] = r;
}
