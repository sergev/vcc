//
// WebAssembly IR: allocation, release and the opcode tables.
//
#include "wasm_ir.h"

#include <string.h>

#include "xalloc.h"

static const char *const mnemonic[WASM_NUM_OPS] = {
#define WASM_MNEM(op, name, form) [WASM_##op] = name,
    WASM_OPS(WASM_MNEM)
#undef WASM_MNEM
};

static const Wasm_Form form[WASM_NUM_OPS] = {
#define WASM_FORM(op, name, form) [WASM_##op] = WASM_FORM_##form,
    WASM_OPS(WASM_FORM)
#undef WASM_FORM
};

const char *wasm_op_name(Wasm_Op op)
{
    return mnemonic[op];
}

Wasm_Form wasm_op_form(Wasm_Op op)
{
    return form[op];
}

const char *wasm_valtype_name(Wasm_ValType t)
{
    switch (t) {
    case WASM_I32:
        return "i32";
    case WASM_I64:
        return "i64";
    case WASM_F32:
        return "f32";
    case WASM_F64:
        return "f64";
    case WASM_VOID:
        break;
    }
    return "";
}

Wasm_Func *wasm_new_func(const char *name, bool global)
{
    Wasm_Func *fn = xalloc(sizeof(Wasm_Func), __func__, __FILE__, __LINE__);
    fn->name      = xstrdup(name);
    fn->global    = global;
    return fn;
}

static void free_instr(Wasm_Instr *in)
{
    xfree(in->sym);
    xfree(in->table);
    xfree(in);
}

void wasm_free_func(Wasm_Func *fn)
{
    Wasm_Instr *in = fn->first;
    while (in) {
        Wasm_Instr *next = in->next;
        free_instr(in);
        in = next;
    }
    xfree(fn->params);
    xfree(fn->locals);
    xfree(fn->name);
    xfree(fn);
}

Wasm_Instr *wasm_append(Wasm_Func *fn, Wasm_Op op)
{
    Wasm_Instr *in = xalloc(sizeof(Wasm_Instr), __func__, __FILE__, __LINE__);
    in->op         = op;
    in->prev       = fn->last;
    if (fn->last)
        fn->last->next = in;
    else
        fn->first = in;
    fn->last = in;
    return in;
}

void wasm_remove(Wasm_Func *fn, Wasm_Instr *in)
{
    if (in->prev)
        in->prev->next = in->next;
    else
        fn->first = in->next;
    if (in->next)
        in->next->prev = in->prev;
    else
        fn->last = in->prev;
    free_instr(in);
}

int wasm_add_local(Wasm_Func *fn, Wasm_ValType t)
{
    Wasm_ValType *locals =
        xalloc((fn->nlocals + 1) * sizeof(Wasm_ValType), __func__, __FILE__, __LINE__);
    if (fn->nlocals)
        memcpy(locals, fn->locals, fn->nlocals * sizeof(Wasm_ValType));
    locals[fn->nlocals] = t;
    xfree(fn->locals);
    fn->locals = locals;
    return fn->nparams + fn->nlocals++;
}
