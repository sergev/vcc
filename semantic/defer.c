//
// Checks for `defer` (docs/Coroutines_in_C.md, section 1).
//
// A deferred statement runs when control leaves the block holding the `defer`.  The
// compiler lowers it inline on every exit edge (translator/stmt.c), which needs the
// set of defers registered at any point to be known statically.  So:
//   - a deferred statement may only be left by completing it: no return, no goto out
//     of it, and nothing jumps into it (a break or continue that would leave it is
//     label_loops' error: the deferred statement is a barrier to it);
//   - no goto, case or default label may jump into a block past a defer in it.
// A goto may still leave blocks, or jump back over a defer of its own block: the
// translator runs the defers it leaves behind.
//
#include "defer.h"

#include "semantic.h"
#include "xalloc.h"

typedef struct {
    DeferScope *stack;  // blocks from the root down to the statement being walked
    int depth, cap;
    const Stmt *defer;  // innermost deferred statement being walked, or NULL
    int switch_base;    // depth at the innermost switch inside it, or -1
    StringMap *labels;  // label name -> DeferPos *
    bool check_gotos;   // second walk: labels known, check every goto
} Walk;

static void walk_stmt(Walk *w, const Stmt *s);

static void push(Walk *w, const Stmt *key)
{
    if (w->depth == w->cap) {
        w->cap          = w->cap ? 2 * w->cap : 16;
        DeferScope *new = xalloc(w->cap * sizeof *new, __func__, __FILE__, __LINE__);
        for (int i = 0; i < w->depth; i++)
            new[i] = w->stack[i];
        xfree(w->stack);
        w->stack = new;
    }
    w->stack[w->depth].key   = key;
    w->stack[w->depth].count = 0;
    w->depth++;
}

// A substatement: a block of its own, unless it is a compound statement, which is one.
static void walk_sub(Walk *w, const Stmt *s)
{
    if (!s)
        return;
    if (s->kind == STMT_COMPOUND) {
        walk_stmt(w, s);
        return;
    }
    push(w, s);
    walk_stmt(w, s);
    w->depth--;
}

static void record_label(Walk *w, const char *name)
{
    DeferPos *pos = xalloc(sizeof *pos, __func__, __FILE__, __LINE__);
    pos->depth    = w->depth;
    pos->scopes   = xalloc(w->depth * sizeof *pos->scopes + 1, __func__, __FILE__, __LINE__);
    pos->defer    = w->defer;
    for (int i = 0; i < w->depth; i++)
        pos->scopes[i] = w->stack[i];
    map_insert(w->labels, name, (intptr_t)pos, 0);
}

static void check_goto(const Walk *w, const char *name)
{
    intptr_t v;
    if (!map_get(w->labels, name, &v))
        return; // undefined: resolve_labels reports it
    const DeferPos *to = (const DeferPos *)v;
    if (to->defer != w->defer) {
        fatal_error("goto %s jumps into or out of a deferred statement", name);
    }
    int k = 0;
    while (k < w->depth && k < to->depth && w->stack[k].key == to->scopes[k].key)
        k++;
    for (int i = k; i < to->depth; i++) {
        if (to->scopes[i].count > 0)
            fatal_error("goto %s jumps into a block past a defer", name);
    }
    if (k > 0 && to->scopes[k - 1].count > w->stack[k - 1].count) {
        fatal_error("goto %s jumps forward past a defer", name);
    }
}

static void check_case(const Walk *w, const char *what)
{
    if (w->switch_base < 0) {
        fatal_error("'%s' label inside a deferred statement cannot belong to a switch "
                    "outside it",
                    what);
    }
    for (int i = w->switch_base; i < w->depth; i++) {
        if (w->stack[i].count > 0)
            fatal_error("'%s' label past a defer in its switch", what);
    }
}

static void walk_stmt(Walk *w, const Stmt *s)
{
    if (!s)
        return;
    switch (s->kind) {
    case STMT_COMPOUND:
        push(w, s);
        for (const DeclOrStmt *ds = s->u.compound; ds; ds = ds->next) {
            if (ds->kind == DECL_OR_STMT_STMT)
                walk_stmt(w, ds->u.stmt);
        }
        w->depth--;
        break;
    case STMT_IF:
        walk_sub(w, s->u.if_stmt.then_stmt);
        walk_sub(w, s->u.if_stmt.else_stmt);
        break;
    case STMT_WHILE:
        walk_sub(w, s->u.while_stmt.body);
        break;
    case STMT_DO_WHILE:
        walk_sub(w, s->u.do_while.body);
        break;
    case STMT_FOR:
        walk_sub(w, s->u.for_stmt.body);
        break;
    case STMT_SWITCH: {
        int base       = w->switch_base;
        w->switch_base = w->depth;
        walk_sub(w, s->u.switch_stmt.body);
        w->switch_base = base;
        break;
    }
    case STMT_LABELED:
        if (!w->check_gotos)
            record_label(w, s->u.labeled.label);
        walk_stmt(w, s->u.labeled.stmt);
        break;
    case STMT_CASE:
        if (!w->check_gotos)
            check_case(w, "case");
        walk_stmt(w, s->u.case_stmt.stmt);
        break;
    case STMT_DEFAULT:
        if (!w->check_gotos)
            check_case(w, "default");
        walk_stmt(w, s->u.default_stmt);
        break;
    case STMT_GOTO:
        if (w->check_gotos)
            check_goto(w, s->u.goto_label);
        break;
    case STMT_RETURN:
        if (w->defer)
            fatal_error("return inside a deferred statement");
        break;
    case STMT_DEFER: {
        // The deferred statement is walked as a root of its own: nothing leaves it.
        Walk inner = { NULL, 0, 0, s, -1, w->labels, w->check_gotos };
        walk_sub(&inner, s->u.defer_stmt);
        xfree(inner.stack);
        // Registered from here on, in the block holding it.
        if (w->depth > 0)
            w->stack[w->depth - 1].count++;
        break;
    }
    default:
        break;
    }
}

void defer_collect_labels(const Stmt *body, StringMap *labels)
{
    Walk w = { NULL, 0, 0, NULL, -1, labels, false };
    walk_stmt(&w, body);
    xfree(w.stack);
}

static void free_pos(intptr_t v)
{
    DeferPos *pos = (DeferPos *)v;
    xfree(pos->scopes);
    xfree(pos);
}

void defer_free_labels(StringMap *labels)
{
    map_destroy_free(labels, free_pos);
}

// Does the statement hold a defer anywhere?  Most functions have none, and need no walk.
static bool has_defer(const Stmt *s)
{
    if (!s)
        return false;
    switch (s->kind) {
    case STMT_DEFER:
        return true;
    case STMT_COMPOUND:
        for (const DeclOrStmt *ds = s->u.compound; ds; ds = ds->next) {
            if (ds->kind == DECL_OR_STMT_STMT && has_defer(ds->u.stmt))
                return true;
        }
        return false;
    case STMT_IF:
        return has_defer(s->u.if_stmt.then_stmt) || has_defer(s->u.if_stmt.else_stmt);
    case STMT_WHILE:
        return has_defer(s->u.while_stmt.body);
    case STMT_DO_WHILE:
        return has_defer(s->u.do_while.body);
    case STMT_FOR:
        return has_defer(s->u.for_stmt.body);
    case STMT_SWITCH:
        return has_defer(s->u.switch_stmt.body);
    case STMT_LABELED:
        return has_defer(s->u.labeled.stmt);
    case STMT_CASE:
        return has_defer(s->u.case_stmt.stmt);
    case STMT_DEFAULT:
        return has_defer(s->u.default_stmt);
    default:
        return false;
    }
}

void check_defers(const ExternalDecl *ast)
{
    if (!ast || ast->kind != EXTERNAL_DECL_FUNCTION || !has_defer(ast->u.function.body))
        return;
    StringMap labels;
    map_init(&labels);
    defer_collect_labels(ast->u.function.body, &labels);
    Walk w = { NULL, 0, 0, NULL, -1, &labels, true };
    walk_stmt(&w, ast->u.function.body);
    xfree(w.stack);
    defer_free_labels(&labels);
}
