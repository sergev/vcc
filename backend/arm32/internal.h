//
// ARM32 code generator internals (AAPCS-VFP; see backend/arm32/Plan.md).
//
// Registers: r0-r3/s0-s15 (d0-d7) carry arguments and results; r12 and lr are the
// scratch registers of instruction selection, d14/d15 the VFP ones; r4-r11 and d8-d15
// are callee-saved; r11 is the frame pointer, r13 sp, r14 lr, r15 pc.
//
#ifndef ARM32_INTERNAL_H
#define ARM32_INTERNAL_H

#include "a32.h"
#include "tac.h"

typedef struct {
    A32_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
A32_Instr *emit0(Gen *g, A32_Op op);
A32_Instr *emit1(Gen *g, A32_Op op, A32_Operand a);
A32_Instr *emit2(Gen *g, A32_Op op, A32_Operand a, A32_Operand b);
A32_Instr *emit3(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c);
// reg = imm (32 bits).
void gen_li(Gen *g, int reg, uint32_t imm);

#endif // ARM32_INTERNAL_H
