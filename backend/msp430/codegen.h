#ifndef MSP_CODEGEN_H
#define MSP_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to MSP430 assembly on `out`.  `program` heads the whole
// translation unit, lowered with -t msp430.
void msp430_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // MSP_CODEGEN_H
