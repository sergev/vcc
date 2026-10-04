//
// Internal header: cross-file declarations for the typecheck implementation.
// Included by typecheck.c, expressions.c, initializers.c, statements.c, declarations.c.
// Not part of the public API — use semantic.h for external callers.
//
#ifndef TYPECHECK_H
#define TYPECHECK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"
#include "tac.h"

// Scope management — typecheck.c
void scope_increment(void);
void scope_decrement(void);

// Type system utilities — typecheck.c
void validate_type(const Type *t);

// Reject a struct/union tag reference whose keyword disagrees with an existing tag of the same
// name (e.g. using `union x` when `struct x` is in scope) — declarations.c.
void check_tag_kind(const Type *t);

// Register struct/union definitions embedded in a type tree — declarations.c.
void register_inline_struct_defs(const Type *t);

// Resolve and validate a type name, registering any struct/union it defines — expressions.c.
Type *check_type_name(Type *t);
size_t get_array_size(const Type *t);
void set_array_size(Type *t, size_t size);

// Type conversion — typecheck.c
Expr *convert_to_type(Expr *e, const Type *target_type);
Expr *convert_to_kind(Expr *e, TypeKind kind);
const Type *get_common_type(const Type *t1, const Type *t2);
bool is_zero_int(const Literal *c);
bool is_null_pointer_constant(const Expr *e);
Type *common_pointer_type(const Expr *e1, const Expr *e2);
bool compatible_type(const Type *target, const Type *src);
Expr *coerce_for_assignment(Expr *e, const Type *target_type);

// Const evaluation — typecheck.c
bool try_eval_const_int(const Expr *e, long *out);

// An integer constant converted to the integer type `t` of the active target: wrapped
// to its value width, as C11 §6.3.1.3 converts it.
long narrow_const_int(long val, const Type *t);
bool try_eval_const_real(const Expr *e, double *out);
bool try_eval_const_ld(const Expr *e, Float128 *out);

// Expression type-checking — expressions.c
Expr *typecheck_string(Expr *e);
Expr *typecheck_and_decay(Expr *e);
Expr *typecheck_scalar(Expr *e);

// Initializer type-checking — initializers.c
Tac_StaticInit *build_static_init(Type *var_type, Initializer **init);
Initializer *typecheck_init(Type *target_type, Initializer *init);

// Initializer normalization — init_normalize.c
typedef enum {
    INIT_AUTOMATIC, // automatic storage / compound literal: typecheck the leaves
    INIT_STATIC,    // static storage: leave the leaves raw
} InitMode;
Initializer *normalize_init(Type *type, Initializer *init, InitMode mode);

// Statement type-checking — statements.c
DeclOrStmt *typecheck_block(const Type *ret_type, DeclOrStmt *block);
Stmt *typecheck_statement(const Type *ret_type, Stmt *s);

// Declaration spec helpers — declarations.c
bool has_storage(const DeclSpec *spec);

// Declaration type-checking — declarations.c
void typecheck_local_decl(Declaration *d);
void typecheck_global_decl(ExternalDecl *d);

#ifdef __cplusplus
}
#endif

#endif /* TYPECHECK_H */
