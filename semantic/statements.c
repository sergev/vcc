//
// Type-checking for statements.
//
#include <stdio.h>

#include "semantic.h"
#include "string_map.h"
#include "typecheck.h"

typedef struct SwitchCtx {
    StringMap seen_cases; /* key = "%ld" formatted case value */
    const Type *type;     /* the promoted controlling type: case values convert to it */
    bool seen_default;
    struct SwitchCtx *outer;
} SwitchCtx;

static SwitchCtx *current_switch = NULL;

bool has_storage(const DeclSpec *spec)
{
    return spec && (spec->storage != STORAGE_CLASS_NONE);
}

// Type-check a block of declarations and statements.
DeclOrStmt *typecheck_block(const Type *ret_type, DeclOrStmt *block)
{
    if (semantic_debug) {
        printf("--- %s()\n", __func__);
    }
    for (DeclOrStmt *item = block; item; item = item->next) {
        if (item->kind == DECL_OR_STMT_STMT) {
            item->u.stmt = typecheck_statement(ret_type, item->u.stmt);
        } else {
            typecheck_local_decl(item->u.decl);
        }
    }
    return block;
}

// Type-check a statement.
Stmt *typecheck_statement(const Type *ret_type, Stmt *s);

static Stmt *typecheck_statement_at(const Type *ret_type, Stmt *s)
{
    if (semantic_debug) {
        printf("--- %s()\n", __func__);
    }
    if (!s)
        return NULL;
    switch (s->kind) {
    case STMT_RETURN:
        if (s->u.expr) {
            if (unalias(ret_type)->kind == TYPE_VOID) {
                fatal_error("Void function cannot return a value");
            }
            s->u.expr = coerce_for_assignment(typecheck_and_decay(s->u.expr), ret_type);
            coro_lint_bind(NULL, -1, s->u.expr); // a frame returned outlives its storage
        } else if (unalias(ret_type)->kind != TYPE_VOID) {
            fatal_error("Non-void function must return a value");
        }
        return s;
    case STMT_EXPR: {
        s->u.expr = typecheck_and_decay(s->u.expr);
        coro_lint_statement(s->u.expr);
        return s;
    }
    case STMT_IF: {
        s->u.if_stmt.condition = typecheck_scalar(s->u.if_stmt.condition);
        s->u.if_stmt.then_stmt = typecheck_statement(ret_type, s->u.if_stmt.then_stmt);
        if (s->u.if_stmt.else_stmt) {
            s->u.if_stmt.else_stmt = typecheck_statement(ret_type, s->u.if_stmt.else_stmt);
        }
        return s;
    }
    case STMT_COMPOUND: {
        scope_increment();
        s->u.compound = typecheck_block(ret_type, s->u.compound);
        scope_decrement();
        return s;
    }
    case STMT_WHILE: {
        coro_loop_head_depth++;
        s->u.while_stmt.condition = typecheck_scalar(s->u.while_stmt.condition);
        coro_loop_head_depth--;
        s->u.while_stmt.body      = typecheck_statement(ret_type, s->u.while_stmt.body);
        return s;
    }
    case STMT_DO_WHILE: {
        s->u.do_while.body      = typecheck_statement(ret_type, s->u.do_while.body);
        coro_loop_head_depth++;
        s->u.do_while.condition = typecheck_scalar(s->u.do_while.condition);
        coro_loop_head_depth--;
        return s;
    }
    case STMT_FOR: {
        scope_increment();
        coro_loop_head_depth++;
        if (s->u.for_stmt.init->kind == FOR_INIT_DECL) {
            const Declaration *init_decl = s->u.for_stmt.init->u.decl;
            if (has_storage(init_decl->u.var.specifiers)) {
                fatal_error("Storage class not permitted in for loop header");
            }
            for (const InitDeclarator *id =
                     init_decl->kind == DECL_VAR ? init_decl->u.var.declarators : NULL;
                 id; id = id->next) {
                if (id->type && unalias(id->type)->kind == TYPE_FUNCTION) {
                    fatal_error("Function declaration not permitted in for loop header");
                }
            }
            typecheck_local_decl(s->u.for_stmt.init->u.decl);
        } else {
            s->u.for_stmt.init->u.expr =
                s->u.for_stmt.init->u.expr ? typecheck_and_decay(s->u.for_stmt.init->u.expr) : NULL;
        }
        s->u.for_stmt.condition =
            s->u.for_stmt.condition ? typecheck_scalar(s->u.for_stmt.condition) : NULL;
        s->u.for_stmt.update =
            s->u.for_stmt.update ? typecheck_and_decay(s->u.for_stmt.update) : NULL;
        coro_loop_head_depth--;
        s->u.for_stmt.body = typecheck_statement(ret_type, s->u.for_stmt.body);
        scope_decrement();
        return s;
    }
    case STMT_BREAK:
    case STMT_CONTINUE:
    case STMT_GOTO:
        return s;
    case STMT_SWITCH: {
        /* C11 §6.8.4.2 p1: controlling expression must be integer type. */
        Expr *ctrl = typecheck_and_decay(s->u.switch_stmt.expr);
        if (!is_integer(ctrl->type)) {
            fatal_error("Switch controlling expression must be of integer type");
        }
        /* Integer promotion: types narrower than int → int (or unsigned int). */
        if (is_promotable_narrow(ctrl->type)) {
            ctrl = convert_to_kind(ctrl, promoted_kind(ctrl->type));
        }
        s->u.switch_stmt.expr = ctrl;
        /* Push a fresh context for case/default validation. */
        SwitchCtx ctx = { .type = ctrl->type, .seen_default = false, .outer = current_switch };
        map_init(&ctx.seen_cases);
        current_switch        = &ctx;
        s->u.switch_stmt.body = typecheck_statement(ret_type, s->u.switch_stmt.body);
        current_switch        = ctx.outer;
        map_destroy(&ctx.seen_cases);
        return s;
    }
    case STMT_CASE: {
        if (!current_switch) {
            fatal_error("Case label outside switch statement");
        }
        /* C11 §6.8.4.2 p3: case expression must be integer constant. */
        Expr *ce = typecheck_and_decay(s->u.case_stmt.expr);
        if (!is_integer(ce->type)) {
            fatal_error("Case expression must be of integer type");
        }
        long val;
        if (!try_eval_const_int(ce, &val)) {
            fatal_error("Case expression is not a constant integer");
        }
        /* C11 §6.8.4.2p5: compared after conversion to the promoted controlling type,
           so 0 and 65536 are duplicates where int has 16 bits. */
        val = narrow_const_int(val, current_switch->type);
        char key[32];
        snprintf(key, sizeof(key), "%ld", val);
        if (map_get(&current_switch->seen_cases, key, NULL)) {
            fatal_error("Duplicate case value %ld in switch", val);
        }
        map_insert(&current_switch->seen_cases, key, 0, 0);
        s->u.case_stmt.expr = ce;
        s->u.case_stmt.stmt = typecheck_statement(ret_type, s->u.case_stmt.stmt);
        return s;
    }
    case STMT_DEFAULT: {
        if (!current_switch) {
            fatal_error("Default label outside switch statement");
        }
        if (current_switch->seen_default) {
            fatal_error("Multiple default labels in one switch");
        }
        current_switch->seen_default = true;
        s->u.default_stmt            = typecheck_statement(ret_type, s->u.default_stmt);
        return s;
    }
    case STMT_LABELED: {
        s->u.labeled.stmt = typecheck_statement(ret_type, s->u.labeled.stmt);
        return s;
    }
    case STMT_DEFER:
        // What a deferred statement may not do (leave it, or a jump past it) is
        // checked by check_defers() once the function's labels are known.  No yield
        // or await may suspend inside it.
        coro_defer_depth++;
        s->u.defer_stmt = typecheck_statement(ret_type, s->u.defer_stmt);
        coro_defer_depth--;
        return s;
    default:
        internal_error("Unsupported statement kind %d", s->kind);
    }
}

// typecheck_statement with diag_loc at the node, for the errors found in it.
Stmt *typecheck_statement(const Type *ret_type, Stmt *s)
{
    SrcLoc saved = diag_enter(s ? s->loc : diag_loc);
    Stmt *result = typecheck_statement_at(ret_type, s);
    diag_loc = saved;
    return result;
}
