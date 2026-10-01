//
// TAC → RISC-V IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include "internal.h"

// Give every parameter and local a slot.  An ALLOCATE_LOCAL may ask for more room or
// alignment than the type.
static void layout_frame(Gen *g)
{
    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    gen_params(g);
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("riscv: %s: no type for %s", gen_name(g), p->name);
        int size  = rv_size(p->type);
        int align = rv_align(p->type);
        intptr_t v;
        if (map_get(&allocs, p->name, &v)) {
            const Tac_Instruction *in = (const Tac_Instruction *)v;
            if (in->u.allocate_local.size > size)
                size = in->u.allocate_local.size;
            if (in->u.allocate_local.alignment > align)
                align = in->u.allocate_local.alignment;
        }
        alloc_slot(g, p->name, p->type, size, align);
    }
    map_destroy(&allocs);
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    layout_frame(&g);
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || (last->kind != TAC_INSTRUCTION_RETURN && last->kind != TAC_INSTRUCTION_JUMP))
        gen_epilogue(&g); // falling off the end
    gen_prologue(&g);
    rv_emit_func(out, g.fn);
    gen_done(&g);
}

void riscv_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
        fatal_error("riscv: static variable %s not implemented", tl->u.static_variable.name);
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("riscv: static constant %s not implemented", tl->u.static_constant.name);
    }
}
