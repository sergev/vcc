//
// Branch relaxation.  A jump reaches -512..+511 words, and the assemblers reject one out
// of range rather than relaxing it, so the backend sizes its own: from the instruction
// sizes, each jump's distance to its label is known exactly.
//
#include "internal.h"
#include "xalloc.h"

// The byte address of every label of `fn`, from its start.
static void label_addresses(const Msp_Func *fn, StringMap *labels)
{
    int addr = 0;
    for (const Msp_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            map_insert(labels, b->label, addr, 0);
        for (const Msp_Instr *in = b->head; in; in = in->next)
            addr += msp_instr_size(in);
    }
}

// Rewrite jump `in` of block `b`, out of range: `jmp L` becomes `br #L`; `jcc L`
// becomes `j!cc skip; br #L; skip:`, and `jn L` (which has no inverse)
// `jn far; jmp skip; far: br #L; skip:`.
static void relax_jump(Msp_Func *fn, Msp_Block *b, Msp_Instr *in)
{
    if (in->op == MSP_JMP) {
        in->op         = MSP_BR;
        Msp_Operand l  = in->opnd[0];
        in->opnd[0]    = msp_imm_sym(l.sym, 0);
        xfree(l.sym);
        return;
    }
    char skip[32];
    new_label(skip);
    Msp_Instr *br = msp_insert_after(b, in, MSP_BR);
    br->opnd[0]   = msp_imm_sym(in->opnd[0].sym, 0);
    xfree(in->opnd[0].sym);
    if (in->op == MSP_JN) {
        char far[32];
        new_label(far);
        in->opnd[0]   = msp_label(far);
        Msp_Instr *jp = msp_insert_after(b, in, MSP_JMP);
        jp->opnd[0]   = msp_label(skip);
        msp_split_after(fn, b, jp, far);
        msp_split_after(fn, b->next, br, skip);
        return;
    }
    in->op      = msp_inverse(in->op);
    in->opnd[0] = msp_label(skip);
    msp_split_after(fn, b, br, skip);
}

// One pass: rewrite the first jump out of range; returns whether there was one.
static bool relax_pass(Msp_Func *fn)
{
    StringMap labels;
    map_init(&labels);
    label_addresses(fn, &labels);
    bool changed = false;
    int addr     = 0;
    for (Msp_Block *b = fn->blocks; b && !changed; b = b->next) {
        for (Msp_Instr *in = b->head; in; in = in->next) {
            int here = addr;
            addr += msp_instr_size(in);
            intptr_t target;
            if (msp_form[in->op] != MSP_FORM_JUMP || !map_get(&labels, in->opnd[0].sym, &target))
                continue;
            int words = (int)(target - (here + 2)) / 2;
            if (words < -512 || words > 511) {
                relax_jump(fn, b, in);
                changed = true; // the addresses are stale: start over
                break;
            }
        }
    }
    map_destroy(&labels);
    return changed;
}

void msp_relax(Msp_Func *fn)
{
    while (relax_pass(fn))
        ;
}
