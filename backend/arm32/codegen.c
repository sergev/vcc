//
// TAC → ARM32 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "internal.h"

bool arm32_regalloc      = true;
bool arm32_frame_pointer = false;

// With r11, the saved r4-r9 in use first, just below it; then a register or a slot for
// every parameter and local.  An ALLOCATE_LOCAL may ask for more room or alignment than
// the type.
static void layout_frame(Gen *g)
{
    g->locals_size = g->sp_frame ? 0 : 4 * __builtin_popcount(g->saved_core);
    gen_params(g);
    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("arm32: %s: no type for %s", gen_name(g), p->name);
        int hi, reg = assigned_reg(g, p->name, &hi);
        if (reg >= 0) {
            place_reg(g, p->name, p->type, reg, hi);
            continue;
        }
        int size  = a32_size(p->type);
        int align = a32_align(p->type);
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

// The function, its frame addressed from sp when it can be (`sp_frame`); false when not.
static bool gen_body(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl, bool sp_frame)
{
    gen_init(g, program, tl);
    g->sp_frame = sp_frame;
    if (arm32_regalloc)
        gen_regalloc(g);
    layout_frame(g);
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(g, in);
        last = in;
    }
    if (!last || (last->kind != TAC_INSTRUCTION_RETURN && last->kind != TAC_INSTRUCTION_JUMP))
        gen_epilogue(g); // falling off the end
    return gen_prologue(g);
}

// Addressed from sp unless asked for r11, or some offset does not fit from sp: then
// again, with r11.
static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    if (!gen_body(&g, program, tl, !arm32_frame_pointer)) {
        gen_done(&g);
        gen_body(&g, program, tl, false);
    }
    a32_emit_func(out, g.fn);
    gen_done(&g);
    for (const Tac_StaticLocal *s = tl->u.function.static_locals; s; s = s->next)
        emit_static_variable(out, s->name, false, s->type, s->init_list, false, s->alignment);
}

// The strictest _Alignas among the declarations of static variable `name`.
static int declared_alignment(const Tac_TopLevel *program, const char *name)
{
    int a = 0;
    for (const Tac_TopLevel *t = program; t; t = t->next)
        if (t->kind == TAC_TOPLEVEL_STATIC_VARIABLE &&
            strcmp(t->u.static_variable.name, name) == 0 && t->u.static_variable.alignment > a)
            a = t->u.static_variable.alignment;
    return a;
}

void arm32_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        a32_emit_header(out); // a new translation unit
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
        if (tac_static_superseded(program, tl))
            break;
        emit_static_variable(out, tl->u.static_variable.name, tl->u.static_variable.global,
                             tl->u.static_variable.type, tl->u.static_variable.init_list, false,
                             declared_alignment(program, tl->u.static_variable.name));
        break;
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        emit_static_variable(out, tl->u.static_constant.name, false, tl->u.static_constant.type,
                             tl->u.static_constant.init, true, 0);
        break;
    }
}
