//
// Source locations and the prefix of a diagnostic.
//
#include "srcloc.h"

#include <stdlib.h>
#include <string.h>

SrcLoc diag_loc;

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
    }
    fprintf(f, "%s: ", kind);
}
