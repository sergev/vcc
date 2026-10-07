#ifndef WASM_CODEGEN_H
#define WASM_CODEGEN_H

#include <stdio.h>

#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate one TAC toplevel to WebAssembly assembly (the LLVM assembler's syntax) on
// `out`.  `program` heads the whole translation unit, lowered with -t wasm32.
void wasm_codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // WASM_CODEGEN_H
