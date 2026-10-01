#ifndef RISCV_CODEGEN_H
#define RISCV_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to RISC-V assembly on `out`.  `program` heads the whole
// translation unit.
void riscv_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // RISCV_CODEGEN_H
