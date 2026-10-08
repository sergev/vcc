//
// Type-checking for coroutines (docs/Coroutines_in_C.md; backend/wasm/Plan.md §2.4, §4.2).
//
// A coroutine is a function symbol marked `coro` with its yield type Y; its C type
// stays `T f(params)`, so calls through co_init, co_alloca and an arena await check
// their arguments as any call does.  A frame type _Coro_frame(Y, T) is a struct tagged
// __co_frame that is never defined (the parser builds it); two are the same type when
// their Y and T are.  Every compile-time rule of §2.4 is here, but for the jumps past
// a co_alloca, which semantic/defer.c checks with those past a defer.
//
#include <string.h>

#include "semantic.h"
#include "symtab.h"
#include "target.h"
#include "typecheck.h"
#include "xalloc.h"

// The function being checked when it is a coroutine: its yield type, else NULL.
static Type *coro_yield;
static bool in_coro;

// Depth of deferred statements and of loop heads around the expression being checked.
int coro_defer_depth;
int coro_loop_head_depth;

// The call an arena await is checking: the one call of a coroutine allowed.
static const Expr *arena_call;

static void require_target(void)
{
    if (!target_config->coroutines)
        fatal_error("coroutines are not supported on target %s", target_config->name);
}

// A short name for the messages: co_init for __co_init.
static const char *op_name(CoOp op)
{
    return co_op_name[op] + 2;
}

bool is_frame_type(const Type *t)
{
    t = unalias(t);
    return t && t->kind == TYPE_STRUCT && t->u.struct_t.frame_yield;
}

// The frame type a co_frame(Y, T) * points to, or NULL for any other type.
static const Type *frame_target(const Type *t)
{
    t = unalias(t);
    if (!t || t->kind != TYPE_POINTER)
        return NULL;
    const Type *f = unalias(t->u.pointer.target);
    return is_frame_type(f) ? f : NULL;
}

bool same_frame_type(const Type *a, const Type *b)
{
    return compatible_type(a->u.struct_t.frame_yield, b->u.struct_t.frame_yield) &&
           compatible_type(a->u.struct_t.frame_result, b->u.struct_t.frame_result);
}

// A new co_frame(Y, T) *.
static Type *new_frame_pointer(const Type *y, const Type *t)
{
    Type *frame                    = new_type(TYPE_STRUCT, __func__, __FILE__, __LINE__);
    frame->u.struct_t.name         = xstrdup("__co_frame");
    frame->u.struct_t.frame_yield  = clone_type(y, __func__, __FILE__, __LINE__);
    frame->u.struct_t.frame_result = clone_type(t, __func__, __FILE__, __LINE__);
    Type *ptr                      = new_type(TYPE_POINTER, __func__, __FILE__, __LINE__);
    ptr->u.pointer.target          = frame;
    return ptr;
}

// Y or T of a coroutine or a frame: void, or an object type that is not an array.
static void check_value_type(const Type *t, const char *what)
{
    const Type *u = unalias(t);
    if (u->kind == TYPE_ARRAY)
        fatal_error("%s cannot be an array", what);
    if (u->kind == TYPE_FUNCTION)
        fatal_error("%s cannot be a function", what);
}

void check_frame_type(const Type *t)
{
    validate_type(t->u.struct_t.frame_yield);
    validate_type(t->u.struct_t.frame_result);
    check_value_type(t->u.struct_t.frame_yield, "The yield type of a co_frame");
    check_value_type(t->u.struct_t.frame_result, "The result type of a co_frame");
}

static FunctionSpec *coro_spec(const DeclSpec *spec)
{
    FunctionSpec *found = NULL;
    for (FunctionSpec *fs = spec ? spec->func_specs : NULL; fs; fs = fs->next) {
        if (fs->kind == FUNC_SPEC_CORO) {
            if (found)
                fatal_error("More than one _Coro in a declaration");
            found = fs;
        }
    }
    return found;
}

void reject_coro_spec(const DeclSpec *spec, const char *name)
{
    if (coro_spec(spec))
        fatal_error("_Coro on '%s', which is not a function", name);
}

const Type *check_coroutine_decl(const char *name, const DeclSpec *spec, const Type *fn_type)
{
    FunctionSpec *cs = coro_spec(spec);
    if (!cs)
        return NULL;
    require_target();
    if (strcmp(name, "main") == 0)
        fatal_error("main cannot be a coroutine");
    for (const FunctionSpec *fs = spec->func_specs; fs; fs = fs->next) {
        if (fs->kind == FUNC_SPEC_INLINE)
            fatal_error("Coroutine '%s' cannot be inline", name);
        if (fs->kind == FUNC_SPEC_NORETURN)
            fatal_error("Coroutine '%s' cannot be _Noreturn", name);
    }
    fn_type = unalias(fn_type);
    if (fn_type->u.function.variadic)
        fatal_error("Coroutine '%s' cannot be variadic", name);
    if (!fn_type->u.function.params)
        fatal_error("Coroutine '%s' needs a prototype: write (void) for no parameters", name);
    cs->yield_type = resolve_typedef_names(cs->yield_type);
    validate_type(cs->yield_type);
    check_value_type(cs->yield_type, "The yield type of a coroutine");
    return cs->yield_type;
}

void agree_coroutine(const Symbol *existing, const Type *yield, const char *name)
{
    if (!existing || existing->kind != SYM_FUNC)
        return;
    if (existing->u.func.coro != (yield != NULL) ||
        (yield && !compatible_type(existing->u.func.yield_type, yield)))
        fatal_error("Conflicting declarations for function %s", name);
}

void coro_begin_body(const Type *yield)
{
    in_coro    = yield != NULL;
    coro_yield = yield ? clone_type(yield, __func__, __FILE__, __LINE__) : NULL;
}

void coro_end_body(void)
{
    free_type(coro_yield);
    coro_yield = NULL;
    in_coro    = false;
}

// The coroutine a co_* operation or an await names, which must be a bare identifier.
static const Symbol *named_coroutine(Expr *e, const char *what)
{
    if (e->kind != EXPR_VAR)
        fatal_error("%s needs the name of a coroutine", what);
    const Symbol *sym = symtab_get(e->u.var);
    if (sym->kind != SYM_FUNC || !sym->u.func.coro)
        fatal_error("%s: '%s' is not a coroutine", what, e->u.var);
    // Typed as the function, so the translator can tell the callee (as for a call).
    free_type(e->type);
    e->type = clone_type(sym->type, __func__, __FILE__, __LINE__);
    return sym;
}

static const Type *result_of(const Symbol *sym)
{
    return unalias(sym->type)->u.function.return_type;
}

bool coroutine_call_allowed(const Expr *call)
{
    return call == arena_call;
}

void check_coroutine_name(const Symbol *sym)
{
    if (sym->kind == SYM_FUNC && sym->u.func.coro)
        fatal_error("Coroutine '%s' may only be named in co_init, co_alloca, co_sizeof, "
                    "co_alignof or await",
                    sym->name);
}

static void check_suspension(const char *what)
{
    require_target();
    if (!in_coro)
        fatal_error("%s outside a coroutine", what);
    if (coro_defer_depth > 0)
        fatal_error("%s inside a deferred statement", what);
}

// The yield types of an await and its awaiter must be the same: the suspensions are
// forwarded unchanged.
static void check_same_yield(const Type *y)
{
    if (unalias(y)->kind != unalias(coro_yield)->kind || !compatible_type(y, coro_yield))
        fatal_error("await of a coroutine with another yield type");
}

Expr *typecheck_yield(Expr *e)
{
    check_suspension("yield");
    bool is_void = unalias(coro_yield)->kind == TYPE_VOID;
    if (e->u.yield_expr) {
        if (is_void)
            fatal_error("yield with a value in a coroutine that yields void");
        e->u.yield_expr = coerce_for_assignment(typecheck_and_decay(e->u.yield_expr), coro_yield);
    } else if (!is_void) {
        fatal_error("yield without a value in a coroutine that yields a value");
    }
    free_type(e->type);
    e->type = new_type(TYPE_INT, __func__, __FILE__, __LINE__); // co_signal
    return e;
}

Expr *typecheck_await(Expr *e)
{
    check_suspension("await");
    Expr *op = e->u.await_expr;
    const Type *result;
    if (op->kind == EXPR_CALL && op->u.call.func->kind == EXPR_VAR) {
        const Symbol *sym = symtab_get_opt(op->u.call.func->u.var);
        if (sym && sym->kind == SYM_FUNC && sym->u.func.coro) {
            // The arena form: a call of the coroutine, checked as any call.
            check_same_yield(sym->u.func.yield_type);
            const Expr *outer = arena_call;
            arena_call        = op;
            op                = typecheck_and_decay(op);
            arena_call        = outer;
            e->u.await_expr   = op;
            free_type(e->type);
            e->type = clone_type(op->type, __func__, __FILE__, __LINE__);
            return e;
        }
    }
    // The explicit form: a frame the program made.
    op                = typecheck_and_decay(op);
    e->u.await_expr   = op;
    const Type *frame = frame_target(op->type);
    if (!frame)
        fatal_error("await needs a call of a coroutine or a co_frame pointer");
    check_same_yield(frame->u.struct_t.frame_yield);
    result = frame->u.struct_t.frame_result;
    free_type(e->type);
    e->type = clone_type(result, __func__, __FILE__, __LINE__);
    return e;
}

// A size in bytes, converted to size_t.
static Expr *size_argument(Expr *e, const char *what)
{
    e = typecheck_and_decay(e);
    if (!is_integer(e->type))
        fatal_error("%s must be an integer", what);
    return convert_to_kind(e, size_kind());
}

// co_init(storage, bytes, f, args...) and co_alloca(f, extra, args...): the arguments
// before f's own, then f's checked against its parameters.  Returns f's symbol.
static const Symbol *typecheck_start(Expr *e)
{
    CoOp op    = e->u.co_op.op;
    Expr *args = e->u.co_op.args;
    Expr *name;
    Expr *rest;
    if (op == CO_OP_INIT) {
        Expr *storage = args, *bytes = args->next;
        name          = bytes->next;
        rest          = name->next;
        storage->next = bytes->next = name->next = NULL;

        static const Type void_type = { .kind = TYPE_VOID };
        Type *void_ptr              = new_type(TYPE_POINTER, __func__, __FILE__, __LINE__);
        void_ptr->u.pointer.target  = clone_type(&void_type, __func__, __FILE__, __LINE__);
        storage                     = coerce_for_assignment(typecheck_and_decay(storage), void_ptr);
        free_type(void_ptr);
        bytes         = size_argument(bytes, "co_init: the size");
        storage->next = bytes;
        bytes->next   = name;
        args          = storage;
    } else {
        if (coro_loop_head_depth > 0)
            fatal_error("co_alloca in the head of a loop");
        name       = args;
        Expr *size = args->next;
        rest       = size->next;
        name->next = size->next = NULL;
        name->next = size_argument(size, "co_alloca: the extra size");
        args       = name;
    }
    const Symbol *sym = named_coroutine(name, op_name(op));
    Expr *tail        = name;
    while (tail->next)
        tail = tail->next;
    tail->next       = typecheck_call_args(unalias(sym->type), rest);
    e->u.co_op.args  = args;
    return sym;
}

Expr *typecheck_co_op(Expr *e)
{
    require_target();
    CoOp op = e->u.co_op.op;
    Type *type;
    switch (op) {
    case CO_OP_INIT:
    case CO_OP_ALLOCA: {
        const Symbol *sym = typecheck_start(e);
        type              = new_frame_pointer(sym->u.func.yield_type, result_of(sym));
        break;
    }
    case CO_OP_SIZEOF:
    case CO_OP_ALIGNOF:
        named_coroutine(e->u.co_op.args, op_name(op));
        type = new_type(size_kind(), __func__, __FILE__, __LINE__);
        break;
    default: {
        Expr *p           = typecheck_and_decay(e->u.co_op.args);
        e->u.co_op.args   = p;
        const Type *frame = frame_target(p->type);
        if (!frame)
            fatal_error("%s needs a co_frame pointer", op_name(op));
        if (op == CO_OP_VALUE) {
            if (unalias(frame->u.struct_t.frame_yield)->kind == TYPE_VOID)
                fatal_error("co_value of a coroutine that yields void");
            type = clone_type(frame->u.struct_t.frame_yield, __func__, __FILE__, __LINE__);
        } else if (op == CO_OP_RESULT) {
            if (unalias(frame->u.struct_t.frame_result)->kind == TYPE_VOID)
                fatal_error("co_result of a coroutine that returns void");
            type = clone_type(frame->u.struct_t.frame_result, __func__, __FILE__, __LINE__);
        } else {
            type = new_type(TYPE_INT, __func__, __FILE__, __LINE__); // co_status, or a truth
        }
        break;
    }
    }
    free_type(e->type);
    e->type = type;
    return e;
}
