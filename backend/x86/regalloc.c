//
// Register allocation for x86-64: the target side of backend/common/regalloc.c.  A
// value not live across a call may take an argument register, rdi, rsi, rdx, rcx, r8
// or r9, or any of xmm0-xmm13: selection never uses them as scratch, and only a call,
// a divide (rdx) or a shift by a variable (rcx) writes them.  A value live across one
// takes rbx, r12-r15, or rbp without a frame pointer; the psABI preserves no xmm
// register, so an FP value live across a call stays in its slot.  rax, r10, r11, xmm14
// and xmm15 are selection's scratch, and a long double never gets a register.
//
#include "regalloc.h"

#include "codegen.h"
#include "internal.h"

// Argument registers, then callee-saved; a value live across a call starts at NARG.
static const int int_pool[] = { X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8,  X86_R9,
                                X86_RBX, X86_R12, X86_R13, X86_R14, X86_R15, X86_RBP };
static const int fp_pool[]  = { X86_XMM0 + 0,  X86_XMM0 + 1,  X86_XMM0 + 2,  X86_XMM0 + 3,
                                X86_XMM0 + 4,  X86_XMM0 + 5,  X86_XMM0 + 6,  X86_XMM0 + 7,
                                X86_XMM0 + 8,  X86_XMM0 + 9,  X86_XMM0 + 10, X86_XMM0 + 11,
                                X86_XMM0 + 12, X86_XMM0 + 13 };

#define NINT ((int)(sizeof(int_pool) / sizeof(int_pool[0])))
#define NFP  ((int)(sizeof(fp_pool) / sizeof(fp_pool[0])))

typedef struct {
    Gen *g;
    const Flow *flow;
    bool used[X86_VREG];
} Target;

static RegAlloc_Class classify(void *arg, const Tac_Type *t)
{
    (void)arg;
    if (x86_is_aggregate(t) || x86_is_ld(t) || t->kind == TAC_TYPE_VOID ||
        t->kind == TAC_TYPE_FUN_TYPE)
        return REGALLOC_NONE;
    return x86_is_fp(t) ? REGALLOC_FP : REGALLOC_INT;
}

// The type of an operand: a tracked variable's, a global's or a constant's.
static const Tac_Type *operand_type(const void *arg, const Tac_Val *v)
{
    const Target *t = arg;
    int var         = v->kind == TAC_VAL_VAR ? flow_var(t->flow, v->u.var_name) : -1;
    return var >= 0 ? t->flow->types[var] : val_type(t->g, v);
}

static bool makes_clobber(void *arg, const Flow *f, const Tac_Instruction *in,
                          const Tac_Val **res)
{
    Target *t = arg;
    t->flow   = f;
    return clobbers_regs(in, operand_type, t, res);
}

// alloca and the other stack builtins: in place, through rax alone (call.c).
static bool inline_call(void *arg, const Tac_Instruction *in)
{
    (void)arg;
    return x86_stack_builtin(in);
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
        .nint         = g->frame_pointer ? NINT - 1 : NINT, // rbp last
        .int_narg     = 6,
        .fp_pool      = fp_pool,
        .nfp          = NFP,
        .fp_narg      = NFP, // no callee-saved xmm
        .ret_int      = 0,   // rax is not in the pool
        .ret_fp       = X86_XMM0,
        .arg          = &target,
        .classify     = classify,
        .runtime_call = makes_clobber,
        .param_hints  = get_param_hints,
        .call_hints   = get_call_hints,
        .assign       = assign,
        .dead_param   = dead_param,
        .inline_call  = inline_call,
    };
    regalloc(&desc, g->tl);

    for (int i = 6; i < NINT; i++)
        if (target.used[int_pool[i]])
            g->saved_reg[g->nsaved++] = int_pool[i];
}
