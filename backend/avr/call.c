//
// Parameters, calls and returns: the avr-gcc ABI as clang implements it.
//
// Arguments are assigned left to right from r25 down, each taking the registers just
// below the previous one, its size rounded up to even; the first that would go below
// r8 goes on the stack, and so does every later one.  A structure is flattened into
// its members first (split_arg).  A variadic callee takes them all on the stack.
// Stack arguments lie in order above the return address, unaligned.
//
#include "internal.h"
#include "xalloc.h"

// A piece of an argument: clang passes a structure flattened, each top-level member
// an argument of its own (bit-fields by their storage unit, a nested structure, union
// or array whole), so a structure may even be split between registers and the stack.
typedef struct {
    int arg;  // the argument's index
    int off;  // the piece's offset in the argument
    int size; // its bytes
    int reg;  // the first (lowest) register, or 0 on the stack
    int stack; // the offset in the stack argument area
} Piece;

typedef struct {
    Piece *p;
    int n, max;
} Pieces;

static void add_piece(Pieces *v, int arg, int off, int size)
{
    if (v->n == v->max) {
        v->max   = v->max ? 2 * v->max : 16;
        Piece *p = xalloc(v->max * sizeof(Piece), __func__, __FILE__, __LINE__);
        for (int i = 0; i < v->n; i++)
            p[i] = v->p[i];
        xfree(v->p);
        v->p = p;
    }
    v->p[v->n++] = (Piece){ .arg = arg, .off = off, .size = size };
}

// The pieces of argument `arg` of type `t`: a scalar, a union or an opaque structure
// is one; a structure one per distinct member offset.
static void split_arg(Pieces *v, int arg, const Tac_Type *t)
{
    int size = avr_type_size(t);
    if (t->kind != TAC_TYPE_STRUCTURE || t->u.structure.is_union || !t->u.structure.members) {
        add_piece(v, arg, 0, size);
        return;
    }
    int start = 0;
    for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
        if (m->offset > start) {
            add_piece(v, arg, start, m->offset - start);
            start = m->offset;
        }
    }
    if (size > start)
        add_piece(v, arg, start, size - start);
}

// Place the pieces of `n` arguments of types `types`, from r25 down, each rounded up
// to even, until one does not fit above r8: from it on, all go on the stack.  Returns
// the bytes of stack arguments.
static int assign_args(const Tac_Type *const *types, int n, bool variadic, Pieces *v)
{
    for (int i = 0; i < n; i++)
        split_arg(v, i, types[i]);
    int reg = 26, stack = 0;
    bool on_stack = variadic;
    for (int i = 0; i < v->n; i++) {
        Piece *p = &v->p[i];
        int even = (p->size + 1) & ~1;
        if (!on_stack && reg - even >= 8) {
            reg -= even;
            p->reg = reg;
        } else {
            on_stack = true;
            p->stack = stack;
            stack += p->size;
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

// The pieces of the function's parameters.
static void param_pieces(const Gen *g, Pieces *v)
{
    int n                  = count_params(g);
    const Tac_Type **types = xalloc((n + 1) * sizeof(*types), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
        types[i++] = p->type;
    }
    assign_args(types, n, g->tl->u.function.variadic, v);
    xfree(types);
}

// The parameter of index `i`.
static const Tac_Param *nth_param(const Gen *g, int i)
{
    const Tac_Param *p = g->tl->u.function.params;
    while (i-- > 0)
        p = p->next;
    return p;
}

// Whether every piece of parameter `arg` came on the stack: it then lives there.
static bool all_on_stack(const Pieces *v, int arg)
{
    for (int i = 0; i < v->n; i++)
        if (v->p[i].arg == arg && v->p[i].reg)
            return false;
    return true;
}

void place_params(Gen *g)
{
    Pieces v = { 0 };
    param_pieces(g, &v);
    for (int i = 0; i < v.n; i++) {
        const Piece *p = &v.p[i];
        if (p->off == 0 && all_on_stack(&v, p->arg)) {
            const Tac_Param *param = nth_param(g, p->arg);
            place_stack_param(g, param->name, param->type, p->stack);
        }
    }
    xfree(v.p);
}

void store_params(Gen *g)
{
    Pieces v = { 0 };
    param_pieces(g, &v);
    for (int i = 0; i < v.n; i++) {
        const Piece *p   = &v.p[i];
        const char *name = nth_param(g, p->arg)->name;
        if (p->reg) {
            access_bytes(g, true, name, p->off, p->reg, p->size);
        } else if (!all_on_stack(&v, p->arg)) {
            // The stack part of a structure split by the register limit, copied into
            // its slot byte by byte.
            for (int k = 0; k < p->size; k++) {
                access_incoming(g, p->stack + k, 24);
                access_bytes(g, true, name, p->off + k, 24, 1);
            }
        }
    }
    xfree(v.p);
}

// The first register of a result of `size` bytes: r24, r22 or r18, the bytes in
// order from it; over 4 bytes always r18, a 5-byte structure in r22:r18 too.
static int result_reg(int size)
{
    return size <= 2 ? 24 : size <= 4 ? 22 : 18;
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
    const Tac_Val **args   = xalloc((n + 1) * sizeof(*args), __func__, __FILE__, __LINE__);
    int i                  = 0;
    for (const Tac_Val *a = in->u.fun_call.args; a; a = a->next, i++) {
        args[i]  = a;
        types[i] = val_type(g, a);
    }
    Pieces v  = { 0 };
    int stack = assign_args(types, n, ft && ft->u.fun_type.variadic, &v);

    // The stack pieces, the last first and each high byte first, so that they lie in
    // order and little-endian; then the register ones, straight from memory.
    for (i = v.n - 1; i >= 0; i--) {
        const Piece *p = &v.p[i];
        if (p->reg)
            continue;
        if (!avr_is_scalar(types[p->arg])) {
            for (int k = p->size - 1; k >= 0; k--) { // byte by byte, through r24
                access_bytes(g, false, args[p->arg]->u.var_name, p->off + k, 24, 1);
                emit1(g, AVR_PUSH, avr_reg(24));
            }
            continue;
        }
        int a = block_a(p->size);
        load_val(g, args[p->arg], a, p->size, EXT_TYPE);
        for (int k = p->size - 1; k >= 0; k--)
            emit1(g, AVR_PUSH, avr_reg(a + k));
    }
    for (i = 0; i < v.n; i++) {
        const Piece *p = &v.p[i];
        if (!p->reg)
            continue;
        if (!avr_is_scalar(types[p->arg]))
            access_bytes(g, false, args[p->arg]->u.var_name, p->off, p->reg, p->size);
        else // a char extended to its pair
            load_val(g, args[p->arg], p->reg, p->size == 1 ? 2 : p->size, EXT_TYPE);
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
        int size          = avr_type_size(t);
        if (!avr_is_scalar(t))
            access_bytes(g, true, dst->u.var_name, 0, result_reg(size), size);
        else
            store_val(g, dst, result_reg(size), size);
    }
    xfree(types);
    xfree(args);
    xfree(v.p);
}

void gen_return(Gen *g, const Tac_Val *v, bool last)
{
    if (v) {
        const Tac_Type *fn = g->tl->u.function.type;
        const Tac_Type *t  = fn ? fn->u.fun_type.ret_type : val_type(g, v);
        int size = avr_type_size(t);
        if (!avr_is_scalar(t) && size > 8) {
            // The frontend returns a larger one through the hidden pointer it returns
            // here; under this ABI the address does not come back.
        } else if (!avr_is_scalar(t)) {
            access_bytes(g, false, v->u.var_name, 0, result_reg(size), size);
        } else {
            if (size == 1)
                size = 2; // a char comes back extended to r25:r24, as avr-gcc does
            load_val(g, v, result_reg(size), size, EXT_TYPE);
        }
    }
    if (!last)
        emit1(g, AVR_RJMP, avr_label(g->exit));
}
