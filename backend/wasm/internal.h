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
    struct Flow *flow;           // the body's CFG and liveness
    StringMap slots;             // name → its frame slot's offset + 1
    int frame_size;              // bytes of the frame on the shadow stack, 16-aligned
    int fp;                      // the local holding the frame's address
    StringMap labels;            // label → its basic block
    int nblocks;                 // basic blocks
    int cur;                     // the block being translated
    int state;                   // the dispatch loop's local: the block to go to
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
// The local holding name, or -1.
int find_local(const Gen *g, const char *name);
// The offset of name's frame slot, or -1.
int var_slot(const Gen *g, const char *name);
// The frame on the shadow stack: allocated on entry (where a parameter living in a
// slot is stored), released before each return.
void gen_prologue(Gen *g);
void gen_epilogue(Gen *g);
// The load and store of a scalar of type t.
Wasm_Op wasm_load_op(const Tac_Type *t);
Wasm_Op wasm_store_op(const Tac_Type *t);
// The type of static object `name` (a variable, constant or block-scope static of the
// unit, or an extern), or NULL.
const Tac_Type *global_type(const Gen *g, const char *name);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);

//
// Control flow (structure.c)
//
// Translate the function's body into its skeleton of blocks.
void gen_body(Gen *g);
// A jump to label `target`: what goes before its condition, then the branch.
void gen_branch_setup(Gen *g, const char *target);
void gen_branch(Gen *g, const char *target, bool conditional);

//
// Static data (data.c)
//
void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment);

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
