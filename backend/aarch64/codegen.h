#ifndef AARCH64_CODEGEN_H
#define AARCH64_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Code generation options (genaarch64 flags); every one is on by default but the frame
// pointer.
extern bool aarch64_regalloc;      // registers for scalar variables (else all in slots)
extern bool aarch64_peephole;      // the peephole pass
extern bool aarch64_frame_pointer; // a frame record and x29 in every function
extern bool aarch64_linux;         // hosted Linux: .note.GNU-stack in each unit

// Translate one TAC toplevel to AArch64 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t aarch64.
void aarch64_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // AARCH64_CODEGEN_H
