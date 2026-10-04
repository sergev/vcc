//
// TAC → AVR IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    if (tl->u.function.static_locals)
        fatal_error("avr: %s: static data is not implemented yet", gen_name(&g));
    place_params(&g);
    layout_frame(&g);
    store_params(&g);
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        gen_instr(&g, in, in->next == NULL);
    gen_frame(&g);
    avr_relax(g.fn);
    avr_emit_func(out, g.fn);
    gen_done(&g);
}

void avr_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program) { // a new translation unit
        avr_emit_header(out);
        gen_unit_begin();
    }
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the linker resolves undefined names
    case TAC_TOPLEVEL_STATIC_VARIABLE:
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("avr: static data is not implemented yet");
    }
}
