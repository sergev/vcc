#ifndef MSP_CODEGEN_H
#define MSP_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Allocate registers (on by default); off, every variable lives in memory.
extern bool msp430_regalloc;
// Run the peephole pass and fuse compares with branches (on by default).
extern bool msp430_peephole;
// Address every frame from r4, the frame pointer, set to SP after the prologue (off by
// default: from SP, r4 a register like the others).
extern bool msp430_frame_pointer;

// Translate one TAC toplevel to MSP430 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t msp430.
void msp430_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // MSP_CODEGEN_H
