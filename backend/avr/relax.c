//
// Branch relaxation.  Neither clang's assembler nor ld.lld relaxes an AVR branch that
// is out of range, so the backend sizes its own: from the instruction sizes, each
// branch's distance to its label is known exactly.
//
#include "internal.h"
#include "xalloc.h"

static AVR_Op inverse(AVR_Op op)
{
    switch (op) {
    case AVR_BREQ:
        return AVR_BRNE;
    case AVR_BRNE:
        return AVR_BREQ;
    case AVR_BRLO:
        return AVR_BRSH;
    case AVR_BRSH:
        return AVR_BRLO;
    case AVR_BRLT:
        return AVR_BRGE;
    case AVR_BRGE:
        return AVR_BRLT;
    case AVR_BRMI:
        return AVR_BRPL;
    case AVR_BRPL:
        return AVR_BRMI;
    default:
        return AVR_NUM_OPS; // not a conditional branch
    }
}

// The byte address of every label of `fn`, from its start.
static void label_addresses(const AVR_Func *fn, StringMap *labels)
{
    int addr = 0;
    for (const AVR_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            map_insert(labels, b->label, addr, 0);
        for (const AVR_Instr *in = b->head; in; in = in->next)
            addr += avr_size[in->op];
    }
}

// One pass: rewrite each branch out of range; returns whether anything changed.
static bool relax_pass(AVR_Func *fn)
{
    StringMap labels;
    map_init(&labels);
    label_addresses(fn, &labels);
    bool changed = false;
    int addr     = 0;
    for (AVR_Block *b = fn->blocks; b; b = b->next) {
        for (AVR_Instr *in = b->head; in; in = in->next) {
            int here = addr;
            addr += avr_size[in->op];
            intptr_t target;
            if (in->opnd[0].kind != AVR_OPND_LABEL || !map_get(&labels, in->opnd[0].sym, &target))
                continue;
            int words = (int)(target - (here + 2)) / 2;
            if (in->op == AVR_RJMP && (words < -2048 || words > 2047)) {
                in->op  = AVR_JMP;
                changed = true;
            } else if (inverse(in->op) != AVR_NUM_OPS && (words < -64 || words > 63)) {
                // br!cc skip; rjmp target; skip:
                char skip[32];
                new_label(skip);
                AVR_Instr *jump = avr_insert_after(b, in, AVR_RJMP);
                jump->opnd[0]   = in->opnd[0];
                in->op          = inverse(in->op);
                in->opnd[0]     = avr_label(skip);
                avr_split_after(fn, b, jump, skip);
                changed = true;
                break; // the addresses are stale: start over
            }
        }
        if (changed)
            break;
    }
    map_destroy(&labels);
    return changed;
}

void avr_relax(AVR_Func *fn)
{
    while (relax_pass(fn))
        ;
}
