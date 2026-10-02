#ifndef AARCH64_CODEGEN_H
#define AARCH64_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to AArch64 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t aarch64.
void aarch64_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // AARCH64_CODEGEN_H
