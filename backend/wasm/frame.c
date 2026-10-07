//
// Types, signatures, and where a function's names live: a scalar in a wasm local.
//
#include <string.h>

#include "internal.h"
#include "xalloc.h"

int wasm_type_size(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
        return 2;
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_FLOAT:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return 4;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_DOUBLE:
        return 8;
    case TAC_TYPE_LONG_DOUBLE:
        return 16;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * wasm_type_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 4;
}

int wasm_type_align(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY:
        return wasm_type_align(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.alignment > 0 ? t->u.structure.alignment : 1;
    default:
        return wasm_type_size(t);
    }
}

Wasm_ValType wasm_valtype(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
        return WASM_I64;
    case TAC_TYPE_FLOAT:
        return WASM_F32;
    case TAC_TYPE_DOUBLE:
        return WASM_F64;
    case TAC_TYPE_VOID:
        return WASM_VOID;
    case TAC_TYPE_LONG_DOUBLE:
    case TAC_TYPE_ARRAY:
    case TAC_TYPE_STRUCTURE:
        fatal_error("wasm: no value type for an aggregate or a long double");
    default:
        return WASM_I32;
    }
}

static void add_param(Wasm_Sig *sig, Wasm_ValType t)
{
    if (sig->nparams >= (int)(sizeof(sig->params) / sizeof(sig->params[0])))
        fatal_error("wasm: too many parameters");
    sig->params[sig->nparams++] = t;
}

void wasm_signature(const Tac_Type *fun_type, Wasm_Sig *sig)
{
    memset(sig, 0, sizeof(*sig));
    for (const Tac_Type *p = fun_type->u.fun_type.param_types; p; p = p->next)
        add_param(sig, wasm_valtype(p));
    // The variable arguments come in a buffer, by its address.
    if (fun_type->u.fun_type.variadic)
        add_param(sig, WASM_I32);
    sig->result = wasm_valtype(fun_type->u.fun_type.ret_type);
}

void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl)
{
    memset(g, 0, sizeof(*g));
    g->program = program;
    g->tl      = tl;
    map_init(&g->locals);
    map_init(&g->types);
    g->fn = wasm_new_func(wasm_symbol(program, tl->u.function.name), tl->u.function.global);

    // The parameters are the first locals, in order.
    Wasm_Func *fn = g->fn;
    int n         = 0;
    for (const Tac_Param *p = tl->u.function.params; p; p = p->next)
        n++;
    if (tl->u.function.variadic)
        n++;
    fn->params = xalloc((n + 1) * sizeof(Wasm_ValType), __func__, __FILE__, __LINE__);
    for (const Tac_Param *p = tl->u.function.params; p; p = p->next) {
        if (!p->type)
            fatal_error("wasm: %s: parameter %s has no type", fn->name, p->name);
        map_insert(&g->types, p->name, (intptr_t)p->type, 0);
        map_insert(&g->locals, p->name, fn->nparams + 1, 0);
        fn->params[fn->nparams++] = wasm_valtype(p->type);
    }
    if (tl->u.function.variadic)
        fn->params[fn->nparams++] = WASM_I32;
    fn->result = WASM_VOID;
    if (tl->u.function.type)
        fn->result = wasm_valtype(tl->u.function.type->u.fun_type.ret_type);

    // Every other frame-resident name: an automatic local or a temporary.
    for (const Tac_Param *p = tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("wasm: %s: local %s has no type", fn->name, p->name);
        map_insert(&g->types, p->name, (intptr_t)p->type, 0);
        map_insert(&g->locals, p->name, wasm_add_local(fn, wasm_valtype(p->type)) + 1, 0);
    }
}

void gen_done(Gen *g)
{
    map_destroy(&g->locals);
    map_destroy(&g->types);
    wasm_free_func(g->fn);
    g->fn = NULL;
}

const Tac_Type *var_type(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->types, name, &v) ? (const Tac_Type *)v : NULL;
}

int var_local(const Gen *g, const char *name)
{
    intptr_t v;
    if (!map_get(&g->locals, name, &v))
        fatal_error("wasm: %s: %s is not a local", g->fn->name, name);
    return (int)v - 1;
}

const Tac_Type *global_type(const Gen *g, const char *name)
{
    for (const Tac_StaticLocal *s = g->tl->u.function.static_locals; s; s = s->next)
        if (strcmp(s->name, name) == 0)
            return s->type;
    for (const Tac_TopLevel *t = g->program; t; t = t->next) {
        switch (t->kind) {
        case TAC_TOPLEVEL_STATIC_VARIABLE:
            if (strcmp(t->u.static_variable.name, name) == 0)
                return t->u.static_variable.type;
            break;
        case TAC_TOPLEVEL_STATIC_CONSTANT:
            if (strcmp(t->u.static_constant.name, name) == 0)
                return t->u.static_constant.type;
            break;
        case TAC_TOPLEVEL_EXTERN:
            if (strcmp(t->u.extern_.name, name) == 0)
                return t->u.extern_.type;
            break;
        case TAC_TOPLEVEL_FUNCTION:
            if (strcmp(t->u.function.name, name) == 0)
                return t->u.function.type;
            break;
        }
    }
    return NULL;
}
