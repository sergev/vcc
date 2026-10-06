//
// TAC → MMIX IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "flow.h"
#include "internal.h"
#include "xalloc.h"

bool mmix_peephole_on = true;

static void count_use(int var, void *arg)
{
    ((int *)arg)[var]++;
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    if (mmix_regalloc)
        gen_regalloc(&g);
    layout_frame(&g);
    if (mmix_peephole_on) {
        g.flow = flow_build(tl);
        g.uses = xalloc((g.flow->nvars + 1) * sizeof(int), __func__, __FILE__, __LINE__);
        memset(g.uses, 0, (g.flow->nvars + 1) * sizeof(int));
        for (int i = 0; i < g.flow->ninstrs; i++)
            flow_uses(g.flow, g.flow->instrs[i], count_use, g.uses);
    }
    copy_byref_params(&g);
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        if (gen_fused(&g, in)) {
            if (!in->next)
                break; // a tail call that ends the function
            in = in->next;
        } else
            gen_instr(&g, in, in->next == NULL);
    }
    if (g.flow) {
        flow_free(g.flow);
        xfree(g.uses);
        g.flow = NULL;
        g.uses = NULL;
    }
    gen_frame(&g);
    if (mmix_peephole_on) {
        const Slot *f = find_slot(&g, FROUND_SLOT);
        mmix_peephole(g.fn, f ? f->off : -1);
    }
    mmix_emit_func(out, g.fn);
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

void mmix_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        gen_unit_begin(); // a new translation unit
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(program, tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the linker resolves undefined names
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
