//
// Source locations and the prefix of a diagnostic.
//
#include "srcloc.h"

#include <stdlib.h>
#include <string.h>

SrcLoc diag_loc;
const char *diag_progname;

SrcLoc diag_enter(SrcLoc loc)
{
    SrcLoc saved = diag_loc;
    if (loc.line > 0) {
        diag_loc = loc;
    }
    return saved;
}

//
// The interned names. They take plain malloc rather than xalloc: they must outlive
// the xfree_all() a test fixture calls between tests, and must not count as a leak
// in xreport_lost_memory(). A unit names only a few files, so a list will do.
//
typedef struct Name {
    struct Name *next;
    char text[1]; // dynamically sized
} Name;

static Name *names;

const char *srcloc_intern(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (const Name *n = names; n; n = n->next) {
        if (strcmp(n->text, name) == 0) {
            return n->text;
        }
    }
    size_t len = strlen(name);
    Name *n    = malloc(sizeof(Name) + len);
    if (!n) {
        abort();
    }
    memcpy(n->text, name, len + 1);
    n->next = names;
    names   = n;
    return n->text;
}

void diag_print_prefix(FILE *f, SrcLoc loc, const char *kind)
{
    if (loc.line > 0) {
        fprintf(f, "%s:%d:", loc.file ? loc.file : "<input>", loc.line);
        if (loc.col > 0) {
            fprintf(f, "%d:", loc.col);
        }
        fputc(' ', f);
    } else if (diag_progname) {
        const char *slash = strrchr(diag_progname, '/');
        fprintf(f, "%s: ", slash ? slash + 1 : diag_progname);
    }
    fprintf(f, "%s: ", kind);
}

void diag_vreport(SrcLoc loc, const char *kind, const char *fmt, va_list ap)
{
    diag_print_prefix(stderr, loc, kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void diag_error(SrcLoc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(loc, "error", fmt, ap);
    va_end(ap);
}

void diag_warning(SrcLoc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(loc, "warning", fmt, ap);
    va_end(ap);
}

void diag_note(SrcLoc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(loc, "note", fmt, ap);
    va_end(ap);
}

void internal_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(diag_loc, "internal compiler error", fmt, ap);
    va_end(ap);
    fprintf(stderr, "please report this bug\n");
    exit(2);
}
