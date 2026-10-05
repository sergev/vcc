#ifndef MMIX_CODEGEN_H
#define MMIX_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to MMIX assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t mmix.
void mmix_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // MMIX_CODEGEN_H
