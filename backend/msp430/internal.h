//
// MSP430 code generator internals (the MSP430 EABI as clang implements it; see
// backend/msp430/Plan.md).
//
// Registers: r0 is PC, r1 SP, r2 SR (and a constant generator), r3 the other constant
// generator.  Arguments go in r12-r15, results in r12, r13:r12 or r15:r12.  r11-r15 are
// call-clobbered, r4-r10 call-saved.  The frame is addressed off SP; there is no frame
// pointer.
//
#ifndef MSP_INTERNAL_H
#define MSP_INTERNAL_H

#include "msp_ir.h"
#include "tac.h"

typedef struct {
    Msp_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
Msp_Instr *emit0(Gen *g, Msp_Op op);
Msp_Instr *emit1(Gen *g, Msp_Op op, Msp_Operand a);
Msp_Instr *emit2(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b);

#endif // MSP_INTERNAL_H
