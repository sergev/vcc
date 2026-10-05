//
// MMIX code generator internals (the MMIXware ABI as GCC implements it; see
// backend/mmix/Plan.md).
//
// Registers: a call is pushj $X,f, which renames the register file: the callee's $0 is
// the caller's $(X+1), the arguments arrive in $0..., and the caller's $0..$(X-1) come
// back intact after pop, with the result in $X.  $32..$255 are global: $254 is the stack
// pointer, $251 the structure-result address, $255 scratch, and the linker allocates
// base registers below $247.  The frame is addressed off $254; there is no frame pointer.
//
#ifndef MMIX_INTERNAL_H
#define MMIX_INTERNAL_H

#include "mmix_ir.h"
#include "tac.h"

typedef struct {
    Mmix_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
Mmix_Instr *emit0(Gen *g, Mmix_Op op);
Mmix_Instr *emit1(Gen *g, Mmix_Op op, Mmix_Operand a);
Mmix_Instr *emit2(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b);
Mmix_Instr *emit3(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b, Mmix_Operand c);

// Load the 64-bit constant `value` into register `reg`, a wyde at a time.
void gen_const(Gen *g, int reg, uint64_t value);

#endif // MMIX_INTERNAL_H
