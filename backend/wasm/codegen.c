//
// TAC → WebAssembly IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "internal.h"

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        gen_instr(&g, in);
    wasm_emit_func(out, g.fn);
    gen_done(&g);
    if (strcmp(tl->u.function.name, "main") == 0 && !tl->u.function.params)
        wasm_emit_main_aliases(out);
}

void wasm_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        wasm_emit_unit_begin(out, program);
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // declared by wasm_emit_unit_begin; the linker resolves it
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("wasm: static data is not supported yet");
    }
}
