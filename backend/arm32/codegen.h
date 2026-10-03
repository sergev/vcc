#ifndef ARM32_CODEGEN_H
#define ARM32_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Code generation options (genarm32 flags); every one is on by default but the frame
// pointer.
extern bool arm32_regalloc;      // registers for scalar variables (else all in slots)
extern bool arm32_frame_pointer; // the frame addressed from r11, a frame record in it

// Translate one TAC toplevel to ARM32 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t arm32; the module header goes out with it.
void arm32_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // ARM32_CODEGEN_H
