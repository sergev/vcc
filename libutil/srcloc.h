//
// Source locations and the prefix of a diagnostic.
//
// The scanner stamps each token with its file, line and column; the parser copies
// the location of a node's token into the AST, and the later passes point diag_loc
// at the node they are checking, so that fatal_error() can say where the error is:
//
//      file.c:12:7: error: message
//
#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *file; // interned by srcloc_intern(), or NULL
    int line;         // 1-based; 0 means unknown
    int col;          // 1-based; 0 means unknown
} SrcLoc;

//
// The location of the construct being processed, for diagnostics.
//
extern SrcLoc diag_loc;

//
// Make loc the current location, when it is known; return the one it replaces,
// for the caller to restore when it is done with the node:
//
//      SrcLoc saved = diag_enter(node->loc);
//      ...
//      diag_loc = saved;
//
SrcLoc diag_enter(SrcLoc loc);

//
// Return a copy of the name that lives until the program exits: the same pointer
// for the same name, so AST nodes may share it and compare it by address.
//
const char *srcloc_intern(const char *name);

//
// Print "file:line:col: kind: ", leaving out what is unknown.
//
void diag_print_prefix(FILE *f, SrcLoc loc, const char *kind);

#ifdef __cplusplus
}
#endif
