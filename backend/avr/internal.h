//
// AVR code generator internals (the avr-gcc ABI; see backend/avr/Plan.md).
//
// Registers: r0 is scratch (__tmp_reg__), r1 is zero at every call and return
// (__zero_reg__).  Arguments go from r25 down to r8, results in r24, r25:r24, r25:r22
// or r25:r18.  r18-r27 and r30-r31 are call-clobbered, r2-r17 and r28-r29 call-saved;
// Y (r29:r28) is the frame pointer, X (r27:r26) and Z (r31:r30) the pointer scratch of
// instruction selection.
//
#ifndef AVR_INTERNAL_H
#define AVR_INTERNAL_H

#include "avr_ir.h"
#include "tac.h"

typedef struct {
    AVR_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
AVR_Instr *emit0(Gen *g, AVR_Op op);
AVR_Instr *emit1(Gen *g, AVR_Op op, AVR_Operand a);
AVR_Instr *emit2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b);

#endif // AVR_INTERNAL_H
