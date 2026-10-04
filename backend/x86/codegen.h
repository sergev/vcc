#ifndef X86_CODEGEN_H
#define X86_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to x86-64 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t x86_64; the module header goes out with it.
void x86_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // X86_CODEGEN_H
