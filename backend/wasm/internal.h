//
// WebAssembly backend internals: the state of one function's translation and the
// functions of the backend's files.
//
#ifndef WASM_INTERNAL_H
#define WASM_INTERNAL_H

#include "codegen.h"
#include "string_map.h"
#include "wasm_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

// A function's wasm signature, as the C ABI maps its type.
typedef struct {
    Wasm_ValType params[64];
    int nparams;
    Wasm_ValType result;
} Wasm_Sig;

typedef struct {
    Wasm_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    StringMap locals;            // name → its local's index + 1
    StringMap types;             // name → const Tac_Type * of a frame-resident name
} Gen;

//
// Types and locals (frame.c)
//
int wasm_type_size(const Tac_Type *t);
int wasm_type_align(const Tac_Type *t);
// The value type a scalar of type t is held in.
Wasm_ValType wasm_valtype(const Tac_Type *t);
// The signature of a function of type fun_type (a FUN_TYPE).
void wasm_signature(const Tac_Type *fun_type, Wasm_Sig *sig);
void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl);
void gen_done(Gen *g);
// The type of frame-resident name, or NULL.
const Tac_Type *var_type(const Gen *g, const char *name);
// The local holding name; fails when there is none.
int var_local(const Gen *g, const char *name);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);

//
// Symbols and assembly (emit.c)
//
// The assembler's name of TAC name `name`: main as clang names it.
const char *wasm_symbol(const Tac_TopLevel *program, const char *name);
// The declarations a translation unit starts with: the target's features and the
// signature of every function it defines or references.
void wasm_emit_unit_begin(FILE *out, const Tac_TopLevel *program);
void wasm_emit_func(FILE *out, const Wasm_Func *fn);
// main(void)'s two other names, as clang emits them: the main(argc, argv) that calls it
// and __main_void, which crt0 calls.
void wasm_emit_main_aliases(FILE *out);

#ifdef __cplusplus
}
#endif

#endif // WASM_INTERNAL_H
