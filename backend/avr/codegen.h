#ifndef AVR_CODEGEN_H
#define AVR_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to AVR assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t avr; the module header goes out with it.
void avr_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // AVR_CODEGEN_H
