//
// Parameters, calls and returns: the avr-gcc ABI as clang implements it.
//
// Arguments are assigned left to right from r25 down, each taking the registers just
// below the previous one, its size rounded up to even; the first that would go below
// r8 goes on the stack, and so does every later one.  A structure is flattened into
// its members first (split_arg).  A variadic callee takes them all on the stack.
// Stack arguments lie in order above the return address, unaligned.
//
#include <string.h>

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
            internal_error("avr: %s: no type for %s", gen_name(g), p->name);
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
        if (!p->reg)
            g->stack_args = true;
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
    intptr_t dead;

    // Into memory first, while every incoming register still holds its argument.
    for (int i = 0; i < v.n; i++) {
        const Piece *p   = &v.p[i];
        const char *name = nth_param(g, p->arg)->name;
        Regs vr;
        if (var_regs(g, name, &vr))
            continue;
        if (p->reg) {
            access_bytes(g, true, name, p->off, p->reg, p->size);
        } else if (!all_on_stack(&v, p->arg)) {
            // The stack part of a structure split by the register limit, copied into
            // its slot byte by byte.
            for (int k = 0; k < p->size; k++) {
                access_incoming(g, p->stack + k, AVR_TMP);
                access_bytes(g, true, name, p->off + k, AVR_TMP, 1);
            }
        }
    }

    // Then the register variables, all at once; those that came on the stack last.
    int d[64], s[64], m = 0;
    for (int i = 0; i < v.n; i++) {
        const Piece *p   = &v.p[i];
        const char *name = nth_param(g, p->arg)->name;
        Regs vr;
        if (!p->reg || !var_regs(g, name, &vr) || map_get(&g->dead, name, &dead))
            continue;
        for (int k = 0; k < vr.n && k < p->size; k++) {
            d[m]   = vr.r[k];
            s[m++] = p->reg + k;
        }
    }
    // cppcheck-suppress uninitvar ; only the first m entries are read, all set above
    parallel_move(g, d, s, m);
    for (int i = 0; i < v.n; i++) {
        const Piece *p   = &v.p[i];
        const char *name = nth_param(g, p->arg)->name;
        Regs vr;
        if (p->reg || !var_regs(g, name, &vr) || map_get(&g->dead, name, &dead))
            continue;
        for (int k = 0; k < vr.n; k++)
            access_incoming(g, p->stack + k, vr.r[k]);
    }
    xfree(v.p);
}

// The first register of a result of `size` bytes: r24, r22 or r18, the bytes in
// order from it; over 4 bytes always r18, a 5-byte structure in r22:r18 too.
static int result_reg(int size)
{
    return size <= 2 ? 24 : size <= 4 ? 22 : 18;
}

// SP = Z, with interrupts held off between the halves (the I flag is restored before
// the low half, which the next instruction still completes).
static void write_sp_z(Gen *g)
{
    emit2(g, AVR_IN, avr_reg(AVR_TMP), avr_sym(AVR_MOD_NONE, "__SREG__", 0));
    emit0(g, AVR_CLI);
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_H__", 0), avr_reg(AVR_Z + 1));
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SREG__", 0), avr_reg(AVR_TMP));
    emit2(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_L__", 0), avr_reg(AVR_Z));
}

// Z = SP.
static void read_sp_z(Gen *g)
{
    emit2(g, AVR_IN, avr_reg(AVR_Z), avr_sym(AVR_MOD_NONE, "__SP_L__", 0));
    emit2(g, AVR_IN, avr_reg(AVR_Z + 1), avr_sym(AVR_MOD_NONE, "__SP_H__", 0));
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
    read_sp_z(g);
    if (n <= Y_MAX) {
        emit2(g, AVR_ADIW, avr_reg(AVR_Z), avr_imm(n));
    } else {
        emit2(g, AVR_SUBI, avr_reg(AVR_Z), avr_imm(-n & 0xff));
        emit2(g, AVR_SBCI, avr_reg(AVR_Z + 1), avr_imm(((unsigned)-n >> 8) & 0xff));
    }
    write_sp_z(g);
}

bool avr_stack_builtin(const Tac_Instruction *in)
{
    const char *name = in->u.fun_call.fun_name;
    return !in->u.fun_call.indirect &&
           (strcmp(name, "__builtin_alloca") == 0 || strcmp(name, "__builtin_stack_save") == 0 ||
            strcmp(name, "__builtin_stack_restore") == 0);
}

bool avr_moves_sp(const Tac_TopLevel *tl)
{
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_FUN_CALL && avr_stack_builtin(in))
            return true;
    return false;
}

// save: dst = SP; restore: SP = arg; alloca: SP -= arg, dst = SP + 1 (SP points below
// the last byte).  Arguments are pushed, so there is no outgoing area to keep below
// the memory.  Through X, Z and r0 alone; the epilogue resets SP from Y.
static void gen_stack_builtin(Gen *g, const Tac_Instruction *in)
{
    const char *name   = in->u.fun_call.fun_name;
    const Tac_Val *dst = in->u.fun_call.dst;
    if (strcmp(name, "__builtin_stack_save") == 0) {
        read_sp_z(g);
    } else if (strcmp(name, "__builtin_stack_restore") == 0) {
        load_val(g, in->u.fun_call.args, AVR_Z, 2, EXT_TYPE);
        write_sp_z(g);
        return;
    } else {
        load_val(g, in->u.fun_call.args, AVR_X, 2, EXT_TYPE);
        read_sp_z(g);
        emit2(g, AVR_SUB, avr_reg(AVR_Z), avr_reg(AVR_X));
        emit2(g, AVR_SBC, avr_reg(AVR_Z + 1), avr_reg(AVR_X + 1));
        write_sp_z(g);
        emit2(g, AVR_ADIW, avr_reg(AVR_Z), avr_imm(1));
    }
    if (dst)
        store_val(g, dst, AVR_Z, 2);
}

void gen_call(Gen *g, const Tac_Instruction *in)
{
    if (avr_stack_builtin(in)) {
        gen_stack_builtin(g, in);
        return;
    }
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

    // Variables in the call-saved registers an argument takes are saved around it:
    // the callee keeps the argument there, not the variable.
    int low = 18;
    for (i = 0; i < v.n; i++)
        if (v.p[i].reg && v.p[i].reg < low)
            low = v.p[i].reg;
    uint32_t saved = save_var_regs(g, low, 17);

    // The stack pieces, the last first and each high byte first, so that they lie in
    // order and little-endian; then the register ones, all at once.
    for (i = v.n - 1; i >= 0; i--) {
        const Piece *p = &v.p[i];
        if (p->reg)
            continue;
        if (!avr_is_scalar(types[p->arg])) {
            for (int k = p->size - 1; k >= 0; k--) { // byte by byte, through r0
                access_bytes(g, false, args[p->arg]->u.var_name, p->off + k, AVR_TMP, 1);
                emit1(g, AVR_PUSH, avr_reg(AVR_TMP));
            }
            continue;
        }
        // Straight from a variable's registers; through r0 from memory, r26 for a
        // constant: no register an argument is still to be read from.
        const Tac_Val *a = args[p->arg];
        Regs vr;
        bool in_regs = val_regs(g, a, &vr);
        for (int k = p->size - 1; k >= 0; k--) {
            int r = AVR_TMP;
            if (in_regs) {
                r = vr.r[k];
            } else if (a->kind == TAC_VAL_CONSTANT) {
                int b = (int)(const_bits(a->u.constant) >> (8 * k) & 0xff);
                r     = b ? AVR_X : AVR_ZERO;
                if (b)
                    emit2(g, AVR_LDI, avr_reg(AVR_X), avr_imm(b));
            } else {
                access_mem(g, false, a->u.var_name, k, &r, 1, 0);
            }
            emit1(g, AVR_PUSH, avr_reg(r));
        }
    }
    Load *loads = xalloc((v.n + 1) * sizeof(Load), __func__, __FILE__, __LINE__);
    int nl      = 0;
    for (i = 0; i < v.n; i++) {
        const Piece *p = &v.p[i];
        if (!p->reg || !avr_is_scalar(types[p->arg]))
            continue;
        // A char extended to its pair.
        loads[nl++] = (Load){ args[p->arg], regs_range(p->reg, p->size == 1 ? 2 : p->size),
                              EXT_TYPE };
    }
    Tac_Val fp = { .kind = TAC_VAL_VAR, .u.var_name = in->u.fun_call.fun_name };
    if (in->u.fun_call.indirect)
        loads[nl++] = (Load){ &fp, regs_range(AVR_Z, 2), EXT_TYPE };
    load_vals(g, loads, nl);
    xfree(loads);
    for (i = 0; i < v.n; i++) { // structure pieces, from memory
        const Piece *p = &v.p[i];
        if (!p->reg || avr_is_scalar(types[p->arg]))
            continue;
        Regs r = regs_range(p->reg, p->size);
        access_mem(g, false, args[p->arg]->u.var_name, p->off, r.r, p->size,
                   in->u.fun_call.indirect ? 3u << AVR_Z : 0);
    }

    if (in->u.fun_call.indirect)
        emit0(g, AVR_ICALL);
    else
        emit1(g, AVR_CALL, avr_label(in->u.fun_call.fun_name));
    if (stack)
        release_stack(g, stack);
    restore_var_regs(g, saved);

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

const Tac_Type *flow_val_type(const Gen *g, const Flow *f, const Tac_Val *v)
{
    int var = v->kind == TAC_VAL_VAR ? flow_var(f, v->u.var_name) : -1;
    return var >= 0 && f->types[var] ? f->types[var] : val_type(g, v);
}

// The register a scalar of `size` bytes in a register piece at `reg` starts in, and
// the high pair of a 4-byte one.
static void piece_hints(const Piece *p, const Tac_Type *t, int *lo, int *hi)
{
    *lo = *hi = 0;
    if (!p->reg || !avr_is_scalar(t) || p->size > 4)
        return;
    *lo = p->reg;
    if (p->size > 2)
        *hi = p->reg + 2;
}

void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi)
{
    Pieces v = { 0 };
    param_pieces(g, &v);
    for (int i = 0; i < v.n; i++) {
        const Tac_Param *p = nth_param(g, v.p[i].arg);
        int lo, hi;
        piece_hints(&v.p[i], p->type, &lo, &hi);
        if (lo)
            map_insert(hints, p->name, lo, 0);
        if (hi)
            map_insert(hints_hi, p->name, hi, 0);
    }
    xfree(v.p);
}

void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint)
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
        types[i] = flow_val_type(g, f, a);
    }
    Pieces v = { 0 };
    assign_args(types, n, ft && ft->u.fun_type.variadic, &v);
    for (i = 0; i < v.n; i++) {
        const Tac_Val *a = args[v.p[i].arg];
        int var          = a->kind == TAC_VAL_VAR ? flow_var(f, a->u.var_name) : -1;
        int lo, hi;
        piece_hints(&v.p[i], types[v.p[i].arg], &lo, &hi);
        if (var < 0 || !lo)
            continue;
        if (!hint[var])
            hint[var] = lo;
        if (hi && !hint[var + f->nvars])
            hint[var + f->nvars] = hi;
    }
    const Tac_Val *dst = in->u.fun_call.dst;
    int var            = dst ? flow_var(f, dst->u.var_name) : -1;
    if (var >= 0 && f->types[var] && avr_is_scalar(f->types[var])) {
        int size = avr_type_size(f->types[var]);
        if (size <= 4 && !hint[var])
            hint[var] = result_reg(size);
        if (size > 2 && size <= 4 && !hint[var + f->nvars])
            hint[var + f->nvars] = result_reg(size) + 2;
    }
    xfree(types);
    xfree(args);
    xfree(v.p);
}
