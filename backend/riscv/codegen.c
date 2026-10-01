//
// TAC → RISC-V IR → assembly, one toplevel at a time.
//
#include "codegen.h"

#include <stdlib.h>
#include <string.h>

#include "rv.h"
#include "xalloc.h"

typedef struct {
    Rv_Func *fn;
    const Tac_TopLevel *tl;
} Gen;

// Local label for TAC label `%N`: `.LN`.
static char *label_name(const char *tac)
{
    size_t len = strlen(tac);
    char *s    = xalloc(len + 3, __func__, __FILE__, __LINE__);
    strcpy(s, ".L");
    strcat(s, tac[0] == '%' ? tac + 1 : tac);
    return s;
}

static void gen_epilogue(Gen *g)
{
    rv_append(g->fn, RV_RET);
}

static void gen_instr(Gen *g, const Tac_Instruction *in)
{
    switch (in->kind) {
    case TAC_INSTRUCTION_LABEL: {
        char *l = label_name(in->u.label.name);
        rv_new_block(g->fn, l);
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_JUMP: {
        char *l = label_name(in->u.jump.target);
        rv_append(g->fn, RV_J)->opnd[0] = rv_sym(l, 0);
        xfree(l);
        break;
    }
    case TAC_INSTRUCTION_RETURN:
        if (in->u.return_.src)
            fatal_error("riscv: %s: return of a value not implemented", g->tl->u.function.name);
        gen_epilogue(g);
        break;
    default:
        fatal_error("riscv: %s: %s not implemented", g->tl->u.function.name,
                    tac_instruction_name(in->kind));
    }
}

static void gen_function(const Tac_TopLevel *tl, FILE *out)
{
    Gen g = { rv_new_func(tl->u.function.name, tl->u.function.global), tl };
    const Tac_Instruction *last = NULL;
    for (const Tac_Instruction *in = tl->u.function.body; in; in = in->next) {
        gen_instr(&g, in);
        last = in;
    }
    if (!last || last->kind != TAC_INSTRUCTION_RETURN)
        gen_epilogue(&g); // falling off the end
    rv_emit_func(out, g.fn);
    rv_free_func(g.fn);
}

void riscv_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    (void)program;
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        gen_function(tl, out);
        break;
    case TAC_TOPLEVEL_EXTERN:
        break; // the assembler resolves undefined names at link time
    case TAC_TOPLEVEL_STATIC_VARIABLE:
        fatal_error("riscv: static variable %s not implemented", tl->u.static_variable.name);
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        fatal_error("riscv: static constant %s not implemented", tl->u.static_constant.name);
    }
}
