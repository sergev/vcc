#ifndef MMIX_CODEGEN_H
#define MMIX_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to MMIX assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t mmix.
void mmix_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

// Allocate registers (default), or keep every variable in memory (--no-regalloc).
extern bool mmix_regalloc;
// Fuse instructions in selection and run the peephole pass (default), or not
// (--no-peephole).
extern bool mmix_peephole_on;
// Address the frame from $253, set to $254 by the prologue, and restore $254 from it in
// the epilogue (--frame-pointer).
extern bool mmix_frame_pointer;

#ifdef __cplusplus
}
#endif

#endif // MMIX_CODEGEN_H
