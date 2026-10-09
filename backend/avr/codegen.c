//
// TAC → AVR IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "internal.h"
#include "xalloc.h"

bool avr_regalloc = true;
bool avr_peephole = true;

static void count_use(int var, void *arg)
{
    ((int *)arg)[var]++;
}

// Allocate (when `alloc`, with Y among the registers when `y_free`) and lay out the
// frame.
static void prepare(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl, bool alloc,
                    bool y_free)
{
    gen_init(g, program, tl, alloc);
    g->y_free = y_free;
    if (alloc)
        gen_regalloc(g);
    place_params(g);
    layout_frame(g);
}

// The registers of the function's result, as a mask: r24 up for 2 bytes, r22 for 4,
// r18 for 8, none through a hidden pointer.
static uint32_t result_regs(const Tac_TopLevel *tl)
{
    const Tac_Type *ft = tl->u.function.type;
    const Tac_Type *rt = ft ? ft->u.fun_type.ret_type : NULL;
    if (!rt)
        return ~0u;
    int size = rt->kind == TAC_TYPE_VOID ? 0 : avr_type_size(rt);
    if (size == 0 || size > 8)
        return 0;
    int lo = size <= 2 ? 24 : size <= 4 ? 22 : 18;
    return (uint32_t)(((1ull << 26) - 1) & ~((1ull << lo) - 1));
}

static bool needs_frame(const Gen *g)
{
    return g->frame_size > 0 || g->stack_args || g->moves_sp;
}

static void gen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    Gen g;
    prepare(&g, program, tl, avr_regalloc, avr_regalloc && !avr_moves_sp(tl));
    if (g.alloc && needs_frame(&g) && (g.var_regs >> AVR_Y & 3)) {
        // Y is the frame pointer after all.
        gen_done(&g);
        prepare(&g, program, tl, true, false);
    }
    if (g.alloc && !frame_is_near(&g)) {
        // Slots past Y+63 the scratch-free selection cannot reach: all in memory.
        gen_done(&g);
        prepare(&g, program, tl, false, false);
    }
    g.frameless = !needs_frame(&g);
    g.fn->result = result_regs(tl);
    Flow *flow  = NULL;
    if (avr_peephole) {
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
    g.vol = false;
    if (avr_peephole)
        avr_peephole_pass(g.fn);
    gen_frame(&g);
    if (avr_peephole)
        avr_peephole_frame(g.fn);
    avr_relax(g.fn);
    avr_emit_func(out, g.fn);
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
