//
// TAC → MSP430 IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "internal.h"
#include "xalloc.h"

bool msp430_regalloc = true;
bool msp430_peephole = true;

static void count_use(int var, void *arg)
{
    ((int *)arg)[var]++;
}

// The registers of the function's result, as a mask: r12 up, one per word; none for a
// structure, which goes through the hidden pointer.
static unsigned result_regs(const Tac_TopLevel *tl)
{
    const Tac_Type *ft = tl->u.function.type;
    const Tac_Type *rt = ft ? ft->u.fun_type.ret_type : NULL;
    if (!rt)
        return 0xf000u;
    if (rt->kind == TAC_TYPE_VOID || !msp_is_scalar(rt))
        return 0;
    return ((1u << msp_words(rt)) - 1) << 12;
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    gen_init(&g, program, tl);
    if (msp430_regalloc)
        gen_regalloc(&g);
    if (msp430_peephole)
        find_byref_params(&g);
    place_params(&g);
    layout_frame(&g);
    Flow *flow = NULL;
    if (msp430_peephole) {
        g.flow = flow = flow_build(tl);
        g.uses        = xalloc((flow->nvars + 1) * sizeof(int), __func__, __FILE__, __LINE__);
        memset(g.uses, 0, (flow->nvars + 1) * sizeof(int));
        for (int i = 0; i < flow->ninstrs; i++)
            flow_uses(flow, flow->instrs[i], count_use, g.uses);
    }
    store_params(&g);
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        g.vol = in->is_volatile;
        if (gen_compare_branch(&g, in, in->next))
            in = in->next;
        else
            gen_instr(&g, in, in->next == NULL);
    }
    g.vol           = false;
    unsigned result = result_regs(tl);
    if (msp430_peephole)
        msp_peephole_pass(g.fn, result);
    gen_frame(&g);
    if (msp430_peephole)
        msp_peephole_frame(g.fn, result);
    msp_relax(g.fn);
    msp_emit_func(out, g.fn);
    gen_done(&g);
    if (flow) {
        xfree(g.uses);
        flow_free(flow);
    }
    for (const Tac_StaticLocal *s = tl->u.function.static_locals; s; s = s->next)
        emit_static_variable(out, program, s->name, false, s->type, s->init_list, false,
                             s->alignment);
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

void msp430_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
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
        emit_static_variable(out, program, tl->u.static_variable.name,
                             tl->u.static_variable.global, tl->u.static_variable.type,
                             tl->u.static_variable.init_list, false,
                             declared_alignment(program, tl->u.static_variable.name));
        break;
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        emit_static_variable(out, program, tl->u.static_constant.name, false,
                             tl->u.static_constant.type, tl->u.static_constant.init, true, 0);
        break;
    }
}
