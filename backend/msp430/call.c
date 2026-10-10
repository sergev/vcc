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
// takes its last named argument and all the variable ones on the stack; the named ones
// before (the hidden result pointer counts) go by the rules above.  (clang puts every
// argument of a variadic call on the stack.)  Stack arguments lie in order above the
// return address, 2-aligned.
//
#include <string.h>

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

// Assign `n` arguments of types `types`, those from index `stack_from` on all to the
// stack; returns the bytes of stack arguments.
static int assign_args(const Tac_Type *const *types, int n, int stack_from, ArgLoc *locs)
{
    int next = 12, stack = 0;
    bool used_stack = false;
    for (int i = 0; i < n; i++) {
        ArgLoc *l = &locs[i];
        *l        = (ArgLoc){ 0 };
        l->agg    = !msp_is_scalar(types[i]);
        l->parts      = l->agg ? 1 : msp_words(types[i]);
        bool variadic = i >= stack_from;
        int left      = 16 - next;
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
            internal_error("msp430: %s: no type for %s", gen_name(g), p->name);
        types[i++] = p->type;
    }
    // A variadic function's last named parameter starts the stack.
    assign_args(types, *n, g->tl->u.function.variadic ? *n - 1 : *n, locs);
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
        if ((!locs[i].agg || is_byref(g, p->name)) && all_on_stack(&locs[i]) &&
            !var_reg(g, p->name, 0))
            place_stack_param(g, p->name, p->type, locs[i].stack[0]);
    xfree(locs);
}

// The parameters into place: first those in memory, a structure's address into the first
// word of its slot (layout_frame makes it big enough); then those in registers, all at
// once, from the registers and the stack; then each structure copied in through r15,
// from the address in its slot or among the stack arguments.
void store_params(Gen *g)
{
    int n;
    ArgLoc *locs = param_locs(g, &n);
    Move m[MAX_PARTS * 4 + 4];
    int k = 0, i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        const ArgLoc *l = &locs[i];
        if (l->agg) {
            if (l->reg[0])
                emit2(g, MSP_MOV, msp_reg(l->reg[0]), slot_at(g, p->name, 0));
            continue;
        }
        if (map_get(&g->dead, p->name, NULL))
            continue;
        bool in_reg = var_reg(g, p->name, 0) != 0;
        if (!in_reg && all_on_stack(l))
            continue; // it lives where it came
        if (!in_reg && msp_type_size(p->type) == 1) {
            emit2b(g, MSP_MOV, msp_reg(l->reg[0]), mem_at(g, p->name, 0));
            continue;
        }
        for (int j = 0; j < l->parts; j++) {
            Msp_Operand src = l->reg[j] ? msp_reg(l->reg[j]) : incoming_at(g, l->stack[j]);
            if (in_reg)
                m[k++] = (Move){ mem_at(g, p->name, 2 * j), src, false };
            else
                emit2(g, MSP_MOV, src, mem_at(g, p->name, 2 * j));
        }
    }
    parallel_moves(g, m, k);
    i = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        const ArgLoc *l = &locs[i];
        if (!l->agg || is_byref(g, p->name))
            continue;
        emit2(g, MSP_MOV, l->reg[0] ? mem_at(g, p->name, 0) : incoming_at(g, l->stack[0]),
              msp_reg(MSP_SCRATCH));
        copy_ptr(g, true, MSP_SCRATCH, p->name, 0, msp_type_size(p->type),
                 msp_type_align(p->type));
    }
    xfree(locs);
}

// The types and locations of the arguments of call `in`, whose types `type_of` gives,
// `n` of them; free the result.
typedef const Tac_Type *(*TypeOf)(const Gen *g, const void *arg, const Tac_Val *v);

static ArgLoc *call_locs(const Gen *g, const Tac_Instruction *in, TypeOf type_of,
                         const void *arg, int *n, int *stack)
{
    const Tac_Type *ft = in->u.fun_call.fun_type;
    *n                 = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        (*n)++;
    const Tac_Type **types = xalloc((*n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    ArgLoc *locs           = xalloc((*n + 1) * sizeof(*locs), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next)
        types[i++] = type_of(g, arg, a);
    // A variadic callee's last named argument starts the stack: count the named ones,
    // and the hidden result pointer in front of them.
    int stack_from = *n;
    if (ft && ft->u.fun_type.variadic) {
        const Tac_Type *ret = ft->u.fun_type.ret_type;
        stack_from          = ret && !msp_is_scalar(ret) ? 1 : 0;
        for (const Tac_Type *p = ft->u.fun_type.param_types; p; p = p->next)
            stack_from++;
        stack_from--;
    }
    *stack = assign_args(types, *n, stack_from, locs);
    xfree(types);
    return locs;
}

static const Tac_Type *gen_type(const Gen *g, const void *arg, const Tac_Val *v)
{
    (void)arg;
    return val_type(g, v);
}

int call_stack_size(const Gen *g, const Tac_Instruction *in)
{
    int n, stack;
    xfree(call_locs(g, in, gen_type, NULL, &n, &stack));
    return stack;
}

bool msp_stack_builtin(const Tac_Instruction *in)
{
    const char *name = in->u.fun_call.fun_name;
    return !in->u.fun_call.indirect &&
           (strcmp(name, "__builtin_alloca") == 0 || strcmp(name, "__builtin_stack_save") == 0 ||
            strcmp(name, "__builtin_stack_restore") == 0);
}

bool msp_moves_sp(const Tac_TopLevel *tl)
{
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_FUN_CALL && msp_stack_builtin(in))
            return true;
    return false;
}

// save: dst = SP; restore: SP = arg; alloca: SP -= (arg + 1) & -2, dst = SP plus the
// outgoing area, known before selection (layout_frame).  Through r15 alone; the frame
// is from r4, and the epilogue puts SP back from it.
static void gen_stack_builtin(Gen *g, const Tac_Instruction *in)
{
    const char *name   = in->u.fun_call.fun_name;
    const Tac_Val *dst = in->u.fun_call.dst;
    Msp_Operand r15 = msp_reg(MSP_SCRATCH), sp = msp_reg(MSP_SP);
    if (strcmp(name, "__builtin_stack_save") == 0) {
        emit2(g, MSP_MOV, sp, r15);
    } else if (strcmp(name, "__builtin_stack_restore") == 0) {
        emit2(g, MSP_MOV, val_word(g, in->u.fun_call.args, 0), sp);
        return;
    } else {
        emit2(g, MSP_MOV, val_word(g, in->u.fun_call.args, 0), r15);
        emit1(g, MSP_INC, msp_reg(MSP_SCRATCH));
        emit2(g, MSP_BIC, msp_imm(1), msp_reg(MSP_SCRATCH));
        emit2(g, MSP_SUB, msp_reg(MSP_SCRATCH), sp);
        emit2(g, MSP_MOV, msp_reg(MSP_SP), msp_reg(MSP_SCRATCH));
        if (g->out_size)
            emit2(g, MSP_ADD, msp_imm(g->out_size), msp_reg(MSP_SCRATCH));
    }
    if (dst)
        emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), mem_at(g, dst->u.var_name, 0));
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    if (msp_stack_builtin(in)) {
        gen_stack_builtin(g, in);
        return;
    }
    int n, stack;
    ArgLoc *locs = call_locs(g, in, gen_type, NULL, &n, &stack);

    // The stack parts first: they write only the outgoing area.
    int i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l = &locs[i];
        if (l->agg) {
            if (!l->reg[0]) {
                address_of(g, msp_reg(MSP_SCRATCH), a->u.var_name, 0);
                emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), msp_indexed(MSP_SP, NULL, l->stack[0]));
            }
            continue;
        }
        if (msp_type_size(val_type(g, a)) == 1 && !l->reg[0]) {
            load_val(g, a, MSP_SCRATCH, 1, EXT_TYPE); // a char goes extended
            emit2(g, MSP_MOV, msp_reg(MSP_SCRATCH), msp_indexed(MSP_SP, NULL, l->stack[0]));
            continue;
        }
        for (int j = 0; j < l->parts; j++)
            if (!l->reg[j])
                emit2(g, MSP_MOV, val_word(g, a, j), msp_indexed(MSP_SP, NULL, l->stack[j]));
    }

    // Then the registers at once, and the target of an indirect call into r11 with them
    // (call reads an SP-relative operand after it pushes).
    Move m[MAX_PARTS + 1];
    int k = 0;
    i     = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l = &locs[i];
        if (l->agg)
            continue;
        bool byte = msp_type_size(val_type(g, a)) == 1;
        for (int j = 0; j < l->parts; j++)
            if (l->reg[j])
                m[k++] = (Move){ msp_reg(l->reg[j]), val_word(g, a, j), byte };
    }
    if (in->u.fun_call.indirect)
        m[k++] = (Move){ msp_reg(11), mem_at(g, in->u.fun_call.fun_name, 0), false };
    parallel_moves(g, m, k);
    i = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l = &locs[i];
        if (!l->reg[0])
            continue;
        if (l->agg) {
            address_of(g, msp_reg(l->reg[0]), a->u.var_name, 0);
            continue;
        }
        // A char goes extended: mov.b from memory zero-extends, but not from a register.
        const Tac_Type *t = val_type(g, a);
        if (msp_type_size(t) != 1)
            continue;
        if (ext_sign(t, EXT_TYPE))
            emit1(g, MSP_SXT, msp_reg(l->reg[0]));
        else if (a->kind == TAC_VAL_VAR && var_reg(g, a->u.var_name, 0))
            emit2b(g, MSP_MOV, msp_reg(l->reg[0]), msp_reg(l->reg[0]));
    }

    // The call reads only the registers its arguments are in.
    unsigned args = 1;
    for (i = 0; i < n; i++)
        for (int j = 0; j < locs[i].parts; j++)
            if (locs[i].reg[j])
                args |= 1u << locs[i].reg[j];
    Msp_Instr *call = in->u.fun_call.indirect
                          ? emit1(g, MSP_CALL, msp_reg(11))
                          : emit1(g, MSP_CALL, msp_imm_sym(in->u.fun_call.fun_name, 0));
    call->args = args;

    const Tac_Val *dst = in->u.fun_call.dst;
    if (dst) {
        const Tac_Type *t = val_type(g, dst);
        if (!msp_is_scalar(t))
            internal_error("msp430: %s: a structure result not through a pointer", gen_name(g));
        store_val(g, dst, 12, msp_words(t));
    }
    xfree(locs);
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        // A structure result comes as the hidden pointer; ours is not passed back.
        const Tac_Type *t = val_type(g, v);
        if (msp_is_scalar(t))
            load_val(g, v, 12, msp_words(t), EXT_TYPE);
    }
    if (!last)
        emit1(g, MSP_JMP, msp_label(g->exit));
}

const Tac_Type *flow_val_type(const Gen *g, const Flow *f, const Tac_Val *v)
{
    int var = v->kind == TAC_VAL_VAR ? flow_var(f, v->u.var_name) : -1;
    return var >= 0 && f->types[var] ? f->types[var] : val_type(g, v);
}

void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi)
{
    int n;
    ArgLoc *locs = param_locs(g, &n);
    int i        = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next, i++) {
        const ArgLoc *l = &locs[i];
        if (l->agg || l->parts > 2)
            continue;
        if (l->reg[0])
            map_insert(hints, p->name, l->reg[0], 0);
        if (l->parts == 2 && l->reg[1])
            map_insert(hints_hi, p->name, l->reg[1], 0);
    }
    xfree(locs);
}

static const Tac_Type *flow_type(const Gen *g, const void *arg, const Tac_Val *v)
{
    return flow_val_type(g, arg, v);
}

void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint)
{
    int n, stack;
    ArgLoc *locs = call_locs(g, in, flow_type, f, &n, &stack);
    int i        = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        const ArgLoc *l = &locs[i];
        int var         = a->kind == TAC_VAL_VAR ? flow_var(f, a->u.var_name) : -1;
        if (var < 0 || l->agg || l->parts > 2)
            continue;
        if (l->reg[0] && !hint[var])
            hint[var] = l->reg[0];
        if (l->parts == 2 && l->reg[1] && !hint[var + f->nvars])
            hint[var + f->nvars] = l->reg[1];
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    int var            = dst ? flow_var(f, dst->u.var_name) : -1;
    if (var >= 0 && f->types[var] && msp_is_scalar(f->types[var])) {
        int size = msp_type_size(f->types[var]);
        if (size <= 4 && !hint[var])
            hint[var] = 12;
        if (size == 4 && !hint[var + f->nvars])
            hint[var + f->nvars] = 13;
    }
    xfree(locs);
}

const Tac_Val *instr_dst(const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_BINARY:
        return in->u.binary.dst;
    case TAC_INSTRUCTION_UNARY:
        return in->u.unary.dst;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        return in->u.load.dst;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        return in->u.get_address.dst;
    case TAC_INSTRUCTION_ADD_PTR:
        return in->u.add_ptr.dst;
    case TAC_INSTRUCTION_PTR_DIFF:
        return in->u.ptr_diff.dst;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        return in->u.copy_from_offset.dst;
    case TAC_INSTRUCTION_COPY:
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
    case TAC_INSTRUCTION_SIGN_EXTEND:
    case TAC_INSTRUCTION_ZERO_EXTEND:
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        return in->u.copy.dst; // every conversion begins {src, dst}
    default:
        return NULL;
    }
}

bool is_byref(const Gen *g, const char *name)
{
    return map_get(&g->byref, name, NULL);
}

static bool is_var(const Tac_Val *v, const char *name)
{
    return v && v->kind == TAC_VAL_VAR && strcmp(v->u.var_name, name) == 0;
}

// Whether structure parameter `name` is only read in the body: a member copied out, or
// the whole copied into another object.
static bool only_read(const Gen *g, const char *name)
{
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next) {
        if (is_var(instr_dst(in), name))
            return false;
        switch (in->kind) {
        case TAC_INSTRUCTION_GET_ADDRESS:
        case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
        case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
            if (is_var(in->u.get_address.src, name))
                return false;
            continue;
        case TAC_INSTRUCTION_COPY_TO_OFFSET:
        case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
            if (strcmp(in->u.copy_to_offset.dst, name) == 0 ||
                is_var(in->u.copy_to_offset.src, name))
                return false;
            continue;
        case TAC_INSTRUCTION_RETURN:
            if (is_var(in->u.return_.src, name))
                return false;
            continue;
        default:
            continue;
        }
    }
    return true;
}

// Whether the body may change memory other than its own frame: a call, a store through a
// pointer, a write to a global.
static bool writes_outside(const Gen *g)
{
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next) {
        switch (in->kind) {
        case TAC_INSTRUCTION_FUN_CALL:
        case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        case TAC_INSTRUCTION_STORE:
        case TAC_INSTRUCTION_STORE_BYTE:
            return true;
        case TAC_INSTRUCTION_COPY_TO_OFFSET:
        case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
            if (in->u.copy_to_offset.dst[0] != '%')
                return true;
            break;
        default: {
            const Tac_Val *d = instr_dst(in);
            if (d && d->kind == TAC_VAL_VAR && d->u.var_name[0] != '%')
                return true;
            break;
        }
        }
    }
    return false;
}

void find_byref_params(Gen *g)
{
    if (g->tl->u.function.variadic || writes_outside(g))
        return;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next)
        if (p->type && !msp_is_scalar(p->type) && only_read(g, p->name))
            map_insert(&g->byref, p->name, 1, 0);
}
