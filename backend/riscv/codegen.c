//
// TAC → RISC-V IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <string.h>

#include "internal.h"
#include "xalloc.h"

bool riscv_regalloc = true;
bool riscv_peephole = true;
bool riscv_frame_pointer = false;
int riscv_xlen           = 8;

// Numbers the double literals of a translation unit: .LC0, .LC1, ...
static int const_seq;

int riscv_const_label(Gen *g, uint64_t bits)
{
    for (int i = 0; i < g->nconsts; i++)
        if (g->const_bits[i] == bits)
            return g->const_label[i];
    if (g->nconsts == g->consts_cap) {
        g->consts_cap   = g->consts_cap ? 2 * g->consts_cap : 8;
        uint64_t *bits_ = xalloc(g->consts_cap * sizeof(uint64_t), __func__, __FILE__, __LINE__);
        int *label      = xalloc(g->consts_cap * sizeof(int), __func__, __FILE__, __LINE__);
        if (g->nconsts) {
            memcpy(bits_, g->const_bits, g->nconsts * sizeof(uint64_t));
            memcpy(label, g->const_label, g->nconsts * sizeof(int));
        }
        xfree(g->const_bits);
        xfree(g->const_label);
        g->const_bits  = bits_;
        g->const_label = label;
    }
    g->const_bits[g->nconsts]  = bits;
    g->const_label[g->nconsts] = const_seq++;
    return g->const_label[g->nconsts++];
}

static void emit_consts(FILE *out, const Gen *g)
{
    if (g->nconsts == 0)
        return;
    fprintf(out, "    .section .rodata\n    .p2align 3\n");
    for (int i = 0; i < g->nconsts; i++)
        fprintf(out, ".LC%d:\n    .word   0x%08x\n    .word   0x%08x\n", g->const_label[i],
                (unsigned)g->const_bits[i], (unsigned)(g->const_bits[i] >> 32));
}

// Save slots for the callee-saved registers in use, then a register or a slot for
// every parameter and local.  An ALLOCATE_LOCAL may ask for more room or alignment
// than the type.
static void layout_frame(Gen *g)
{
    for (int i = 0; i < g->nsaved; i++)
        g->saved_off[i] = alloc_slot(g, NULL, NULL, 8, 8);

    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    gen_params(g);
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
        if (!p->type)
            fatal_error("riscv: %s: no type for %s", gen_name(g), p->name);
        int reg = assigned_reg(g, p->name);
        if (reg) {
            place_reg(g, p->name, p->type, reg, assigned_reg_hi(g, p->name));
            continue;
        }
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
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_FUN_CALL && rv_stack_builtin(in))
            g.moves_sp = true;
    if (riscv_regalloc)
        gen_regalloc(&g);
    layout_frame(&g);
    if (g.moves_sp)
        reserve_outgoing(&g);
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        g.fn->volatile_access = in->is_volatile;
        gen_instr(&g, in);
        last = in;
    }
    g.fn->volatile_access = false;
    if (!last || (last->kind != TAC_INSTRUCTION_RETURN && last->kind != TAC_INSTRUCTION_JUMP))
        gen_epilogue(&g); // falling off the end
    gen_prologue(&g);
    if (riscv_peephole)
        rv_peephole(g.fn);
    rv_emit_func(out, g.fn);
    emit_consts(out, &g);
    gen_done(&g);
    for (const Tac_StaticLocal *s = tl->u.function.static_locals; s; s = s->next)
        emit_static_variable(out, s->name, false, s->type, s->init_list, false, s->alignment);
}

// The strictest _Alignas among the declarations of static variable `name`.
static int declared_alignment(const Tac_TopLevel *program, const char *name)
{
    int a = 0;
    for (const Tac_TopLevel *t = program; t; t = t->next)
        if (t->kind == TAC_TOPLEVEL_STATIC_VARIABLE && strcmp(t->u.static_variable.name, name) == 0 &&
            t->u.static_variable.alignment > a)
            a = t->u.static_variable.alignment;
    return a;
}

void riscv_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    if (tl == program)
        const_seq = 0; // a new translation unit
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
