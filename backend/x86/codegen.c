//
// TAC → x86-64 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "flow.h"
#include "internal.h"
#include "xalloc.h"

bool x86_regalloc      = true;
bool x86_frame_pointer = false;
bool x86_peephole      = true;

// The first slots for the callee-saved registers in use, where the prologue pushes
// them (without a frame pointer the first goes where rbp would); then a register or a
// slot for every parameter and local.  An ALLOCATE_LOCAL may ask for more room or
// alignment than the type.
static void layout_frame(Gen *g)
{
    for (int i = x86_frame_pointer ? 0 : 1; i < g->nsaved; i++)
        alloc_slot(g, NULL, NULL, 8, 8);
    gen_params(g);
    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("x86: %s: no type for %s", gen_name(g), p->name);
        int reg = assigned_reg(g, p->name);
        if (reg) {
            place_reg(g, p->name, p->type, reg);
            continue;
        }
        int size  = x86_size(p->type);
        int align = x86_align(p->type);
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

static void count_use(int var, void *arg)
{
    ((int *)arg)[var]++;
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    if (x86_regalloc)
        gen_regalloc(&g);
    layout_frame(&g);
    if (x86_peephole) {
        g.flow = flow_build(tl);
        g.uses = xalloc((g.flow->nvars + 1) * sizeof(int), __func__, __FILE__, __LINE__);
        memset(g.uses, 0, (g.flow->nvars + 1) * sizeof(int));
        for (int i = 0; i < g.flow->ninstrs; i++)
            flow_uses(g.flow, g.flow->instrs[i], count_use, g.uses);
    }
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        g.fn->volatile_access = in->is_volatile;
        if (gen_compare_branch(&g, in, in->next))
            in = in->next;
        else
            gen_instr(&g, in);
        last = in;
    }
    g.fn->volatile_access = false;
    if (!last || (last->kind != TAC_INSTRUCTION_RETURN && last->kind != TAC_INSTRUCTION_JUMP))
        gen_epilogue(&g); // falling off the end
    if (x86_peephole) {
        g.fn->result_regs = result_regs(&g, &g.fn->result_wide);
        x86_peephole_func(g.fn);
    }
    gen_prologue(&g);
    if (g.flow) {
        flow_free(g.flow);
        xfree(g.uses);
    }
    x86_emit_func(out, g.fn);
    emit_consts(&g, out);
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

void x86_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program) { // a new translation unit
        x86_emit_header(out);
        gen_unit_begin();
    }
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
