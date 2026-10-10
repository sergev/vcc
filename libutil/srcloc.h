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

#include <setjmp.h>
#include <stdarg.h>
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
// The name of the running program, for a diagnostic with no location; a main sets it
// from argv[0]. NULL leaves the name out.
//
extern const char *diag_progname;

//
// Print "file:line:col: kind: ", leaving out what is unknown. With no location at
// all, print "program: kind: " instead.
//
void diag_print_prefix(FILE *f, SrcLoc loc, const char *kind);

//
// Print one diagnostic line: the prefix, the message and a newline. The kind is
// "error", "warning", "note" or "internal compiler error". Every message follows the
// style of docs/Technical_Reference.md, "Diagnostics".
//
void diag_vreport(SrcLoc loc, const char *kind, const char *fmt, va_list ap);
void diag_error(SrcLoc loc, const char *fmt, ...);
void diag_warning(SrcLoc loc, const char *fmt, ...);
void diag_note(SrcLoc loc, const char *fmt, ...);

//
// Error recovery. A pass that can go on after an error points diag_recover at a
// jmp_buf of its own; diag_fatal() reports at diag_loc, counts the error and jumps
// there. With no recovery point, or once diag_max_errors errors have been reported
// (0 means no limit), it exits with status 1 instead.
//
extern int diag_errors;
extern int diag_max_errors;
extern jmp_buf *diag_recover;

#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void diag_fatal_v(const char *fmt, va_list ap);

//
// The second half of diag_fatal_v(), for an error already reported (with its notes):
// count it, then jump to the recovery point or exit.
//
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void diag_unwind(void);

//
// Jump to the recovery point without a message, for an error that only follows
// from one already reported; with no recovery point, exit.
//
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void diag_abandon(void);

//
// Count an error that was reported without unwinding, and stop when there are
// too many.
//
void diag_count_error(void);

//
// Report a broken invariant of the compiler itself, at diag_loc, and exit with
// status 2 (a user error exits with 1), so the driver can tell the two apart.
//
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void internal_error(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
