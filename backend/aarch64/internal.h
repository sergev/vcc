//
// AArch64 code generator internals (AAPCS64; see backend/aarch64/Plan.md).
//
// Registers: x0-x7/v0-v7 carry arguments and results, x8 the indirect result address;
// x9-x15, x16/x17 and v16-v31 are scratch; x19-x28 and the low halves of v8-v15 are
// callee-saved; x18 is never used; x29 is the frame pointer, x30 the link register.
//
#ifndef AARCH64_INTERNAL_H
#define AARCH64_INTERNAL_H

#include "a64.h"
#include "tac.h"

typedef struct {
    A64_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
A64_Instr *emit1(Gen *g, A64_Op op, A64_Operand a);
A64_Instr *emit2(Gen *g, A64_Op op, A64_Operand a, A64_Operand b);
A64_Instr *emit3(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c);
// reg = imm, at `width` (the value truncated to it).
void gen_li(Gen *g, int reg, A64_Width width, int64_t imm);

#endif // AARCH64_INTERNAL_H
