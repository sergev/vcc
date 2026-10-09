//
// Register allocation for ARM32: the target side of backend/common/regalloc.c.
// Candidates get callee-saved registers, r4-r9 (and r11 when the frame is addressed
// from sp) or d8-d13, so they survive calls and
// keep clear of the scratch registers (r10, r12, lr, d14, d15).  A value not live
// across a call may also take the argument registers r0-r3 and d0-d7, first: selection
// never uses them as scratch, and only a call (or a runtime call) writes them.  A
// float takes a whole d register, in its even s half, so the allocator never sees an
// s register alone.  A long long is a pair of core registers.
//
// The allocator takes 0 for "no register", and r0 is register 0 here: registers are
// numbered from 1 on its side.
//
#include "regalloc.h"

#include "internal.h"

#define RA(r) ((r) + 1)

// Argument registers, then callee-saved; a value live across a call starts at the
// argument count.
static const int int_pool[] = { RA(A32_R0),     RA(A32_R0 + 1), RA(A32_R0 + 2), RA(A32_R0 + 3),
                                RA(A32_R4),     RA(A32_R4 + 1), RA(A32_R4 + 2), RA(A32_R4 + 3),
                                RA(A32_R4 + 4), RA(A32_R9),     RA(A32_FP) };
static const int fp_pool[]  = { RA(A32_DREG(0)),  RA(A32_DREG(1)),  RA(A32_DREG(2)),
                                RA(A32_DREG(3)),  RA(A32_DREG(4)),  RA(A32_DREG(5)),
                                RA(A32_DREG(6)),  RA(A32_DREG(7)),  RA(A32_DREG(8)),
                                RA(A32_DREG(9)),  RA(A32_DREG(10)), RA(A32_DREG(11)),
                                RA(A32_DREG(12)), RA(A32_DREG(13)) };

#define NINT ((int)(sizeof(int_pool) / sizeof(int_pool[0])))  // r11 last
#define NFP  ((int)(sizeof(fp_pool) / sizeof(fp_pool[0])))

typedef struct {
    Gen *g;
    const Flow *flow;
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (a32_is_aggregate(t) || t->kind == TAC_TYPE_VOID || t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    if (a32_is_fp(t))
        return REGALLOC_FP;
    return a32_is_pair(t) ? REGALLOC_PAIR : REGALLOC_INT;
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

// alloca and the other stack builtins: in place, through r12 alone (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return a32_stack_builtin(in);
}

static void get_param_hints(void *arg, StringMap *hints, StringMap *hints_hi)
{
    param_hints(((Target *)arg)->g, hints, hints_hi);
}

static void get_call_hints(void *arg, const Flow *f, const Tac_Instruction *in, int *hint)
{
    call_hints(((Target *)arg)->g, f, in, hint);
}

static void note_saved(Gen *g, int reg)
{
    if ((reg >= A32_R4 && reg <= A32_R9) || reg == A32_FP)
        g->saved_core |= 1u << reg;
    else if (reg >= A32_DREG(8) && reg <= A32_DREG(13))
        g->saved_vfp |= 1u << (reg - A32_S0) / 2;
}

static void assign(void *arg, const char *name, int reg, int hi)
{
    Gen *g = ((Target *)arg)->g;
    map_insert(&g->regs, name, reg, 0);
    note_saved(g, reg - 1);
    if (hi) {
        map_insert(&g->regs_hi, name, hi, 0);
        note_saved(g, hi - 1);
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
        .nint         = g->sp_frame ? NINT : NINT - 1,
        .int_narg     = 4,
        .fp_pool      = fp_pool,
        .nfp          = NFP,
        .fp_narg      = 8,
        .ret_int      = RA(A32_R0),
        .ret_int_hi   = RA(A32_R0 + 1),
        .ret_fp       = RA(A32_S0),
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
}
