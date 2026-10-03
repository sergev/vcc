#ifndef ARM32_CODEGEN_H
#define ARM32_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to ARM32 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t arm32; the module header goes out with it.
void arm32_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // ARM32_CODEGEN_H
