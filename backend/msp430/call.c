//
// Parameters, calls and returns: the MSP430 EABI as GCC implements it.
//
// Arguments are assigned left to right to r12, r13, r14 and r15, a 16-bit value (a char
// extended) one register, a 32-bit one the next two, a 64-bit one all four.  One that
// does not fit goes on the stack, and the later ones still take the registers left,
// with one exception: a 32-bit value with only r15 left is split, its low word in r15
// and its high word on the stack, unless something has already gone on the stack (it
// then goes there whole).  A structure or union goes by reference: the caller passes the
// address of its own object, uncopied, as a pointer argument, and the callee copies the
// object into its frame on entry, before anything can change it.  (clang copies the
// object onto the stack instead; the two do not interoperate there.)  A variadic callee
// takes every argument on the stack.  Stack arguments lie in order above the return
// address, 2-aligned.
//
#include "internal.h"
#include "xalloc.h"

enum { MAX_PARTS = 4 };

// Where an argument goes, word by word; an aggregate is its address, one word.
typedef struct {
    bool agg;
    int parts;            // words of a scalar
    int reg[MAX_PARTS];   // its register, or 0 on the stack
    int stack[MAX_PARTS]; // its offset in the stack argument area
} ArgLoc;

// Assign `n` arguments of types `types`; returns the bytes of stack arguments.
static int assign_args(const Tac_Type *const *types, int n, bool variadic, ArgLoc *locs)
{
    int next = 12, stack = 0;
    bool used_stack = false;
    for (int i = 0; i < n; i++) {
        ArgLoc *l = &locs[i];
        *l        = (ArgLoc){ 0 };
        l->agg    = !msp_is_scalar(types[i]);
        l->parts  = l->agg ? 1 : msp_words(types[i]);
        int left  = 16 - next;
        bool regs = !variadic && l->parts <= left;
        if (!variadic && !used_stack && l->parts == 2 && left == 1) {
            l->reg[0]   = next++;
            l->stack[1] = stack;
            stack += 2;
            used_stack = true;
            continue;
        }
        for (int j = 0; j < l->parts; j++) {
            if (regs) {
                l->reg[j] = next++;
            } else {
                l->stack[j] = stack;
                stack += 2;
            }
        }
        if (!regs)
            used_stack = true;
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

// The locations of the function's parameters, `n` of them; free the result.
static ArgLoc *param_locs(const Gen *g, int *n)
{
    *n                     = count_params(g);
    const Tac_Type **types = xalloc((*n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    ArgLoc *locs           = xalloc((*n + 1) * sizeof(*locs), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("msp430: %s: no type for %s", gen_name(g), p->name);
        types[i++] = p->type;
    }
    assign_args(types, *n, g->tl->u.function.variadic, locs);
    xfree(types);
    return locs;
}

// Whether every word of a scalar argument is on the stack: it then lives there.
static bool all_on_stack(const ArgLoc *l)
{
    for (int j = 0; j < l->parts; j++)
        if (l->reg[j])
            return false;
    return true;
}

void place_params(Gen *g)
{
    int n;
    ArgLoc *locs = param_locs(g, &n);
    int i        = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++)
        if (!locs[i].agg && all_on_stack(&locs[i]))
            place_stack_param(g, p->name, p->type, locs[i].stack[0]);
    xfree(locs);
}

// The register parameters go to their slots first, a structure's address into the first
// word of its slot (layout_frame makes it big enough); then each structure is copied
// in, through r12-r15, from the address in its slot or among the stack arguments.
void store_params(Gen *g)
{
    int n;
    ArgLoc *locs = param_locs(g, &n);
    int i        = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        const ArgLoc *l = &locs[i];
        if (l->agg) {
            if (l->reg[0])
                emit2(g, MSP_MOV, msp_reg(l->reg[0]), mem_at(g, p->name, 0));
            continue;
        }
        if (all_on_stack(l))
            continue;
        if (msp_type_size(p->type) == 1) {
            emit2b(g, MSP_MOV, msp_reg(l->reg[0]), mem_at(g, p->name, 0));
            continue;
        }
        for (int j = 0; j < l->parts; j++) {
            Msp_Operand src = l->reg[j] ? msp_reg(l->reg[j]) : incoming_at(l->stack[j]);
            emit2(g, MSP_MOV, src, mem_at(g, p->name, 2 * j));
        }
    }
    i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        const ArgLoc *l = &locs[i];
        if (!l->agg)
            continue;
        emit2(g, MSP_MOV, l->reg[0] ? mem_at(g, p->name, 0) : incoming_at(l->stack[0]),
              msp_reg(14));
        address_of(g, 15, p->name, 0);
        copy_bytes(g, msp_type_size(p->type), msp_type_align(p->type));
    }
    xfree(locs);
}

// The types and locations of the arguments of call `in`, `n` of them; free both.
static ArgLoc *call_locs(const Gen *g, const Tac_Instruction *in, int *n, int *stack)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    *n                 = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        (*n)++;
    const Tac_Type **types = xalloc((*n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    ArgLoc *locs           = xalloc((*n + 1) * sizeof(*locs), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        types[i++] = val_type(g, a);
    *stack = assign_args(types, *n, ft && ft->u.fun_type.variadic, locs);
    xfree(types);
    return locs;
}

int call_stack_size(const Gen *g, const Tac_Instruction *in)
{
    int n, stack;
    xfree(call_locs(g, in, &n, &stack));
    return stack;
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    int n, stack;
    ArgLoc *locs = call_locs(g, in, &n, &stack);

    // The stack parts first, then the registers, straight from memory: nothing lives
    // in a register across the moves.
    int i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l   = &locs[i];
        const Tac_Type *t = val_type(g, a);
        if (l->agg) {
            if (!l->reg[0]) {
                address_of(g, 11, a->u.var_name, 0);
                emit2(g, MSP_MOV, msp_reg(11), msp_indexed(MSP_SP, NULL, l->stack[0]));
            }
            continue;
        }
        if (msp_type_size(t) == 1 && !l->reg[0]) {
            load_val(g, a, 11, 1, EXT_TYPE); // a char goes extended
            emit2(g, MSP_MOV, msp_reg(11), msp_indexed(MSP_SP, NULL, l->stack[0]));
            continue;
        }
        for (int j = 0; j < l->parts; j++)
            if (!l->reg[j])
                emit2(g, MSP_MOV, val_word(g, a, j), msp_indexed(MSP_SP, NULL, l->stack[j]));
    }
    i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l = &locs[i];
        if (!l->reg[0])
            continue;
        if (l->agg) {
            address_of(g, l->reg[0], a->u.var_name, 0);
            continue;
        }
        if (msp_type_size(val_type(g, a)) == 1) {
            load_val(g, a, l->reg[0], 1, EXT_TYPE);
            continue;
        }
        for (int j = 0; j < l->parts; j++)
            if (l->reg[j])
                emit2(g, MSP_MOV, val_word(g, a, j), msp_reg(l->reg[j]));
    }

    if (in->u.fun_call.indirect) {
        // Not through an SP-relative operand: call pushes before it reads one.
        emit2(g, MSP_MOV, mem_at(g, in->u.fun_call.fun_name, 0), msp_reg(11));
        emit1(g, MSP_CALL, msp_reg(11));
    } else {
        emit1(g, MSP_CALL, msp_imm_sym(in->u.fun_call.fun_name, 0));
    }

    const Tac_Val *dst = in->u.fun_call.dst;
    if (dst) {
        const Tac_Type *t = val_type(g, dst);
        if (!msp_is_scalar(t))
            fatal_error("msp430: %s: a structure result not through a pointer", gen_name(g));
        store_val(g, dst, 12, msp_words(t));
    }
    xfree(locs);
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        // A structure result comes as the hidden pointer, which goes back in r12.
        const Tac_Type *t = val_type(g, v);
        if (msp_is_scalar(t))
            load_val(g, v, 12, msp_words(t), EXT_TYPE);
    }
    if (!last)
        emit1(g, MSP_JMP, msp_label(g->exit));
}
