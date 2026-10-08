//
// Internal types for translator.
//
#ifndef TRANSLATE_H
#define TRANSLATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"
#include "defer.h"
#include "optimize.h"
#include "semantic.h"
#include "string_map.h"
#include "symtab.h"
#include "tac.h"

//
// What runs when control leaves a block (docs/Coroutines_in_C.md, section 1): a
// deferred statement.  The coroutines' co_alloca will add its release here.
//
typedef enum { EXIT_DEFER } ExitKind;

typedef struct {
    ExitKind kind;
    Stmt *stmt; // EXIT_DEFER: the deferred statement
} ExitAction;

// A block being lowered, and the exit actions registered in it so far.
typedef struct {
    const Stmt *key; // as in DeferScope (semantic/defer.h)
    ExitAction *actions;
    int count, cap;
} TacScope;

// A loop or switch being lowered: where its break and continue go, and how many
// blocks were open outside it (those a break or continue does not leave).
typedef struct {
    const char *break_label;
    const char *cont_label; // NULL for a switch
    int depth;
} TacBreak;

//
// TAC generation context — one per function being lowered.
//
typedef struct {
    Tac_Instruction *head;
    Tac_Instruction *tail;
    int temp_id;
    Tac_TopLevel *static_constants; // strings accumulated during body lowering
    Tac_TopLevel *externs;          // block-scope extern declarations, as EXTERN toplevels
    Tac_Param *locals;              // automatic locals and temporaries, with their types
    Tac_Param *locals_tail;         // tail of `locals` for O(1) append
    Tac_Param *array_locals;        // names of local arrays (block-scope symbols are
                                    // purged before lowering, so value decay needs this)
    const char *sret_name;          // hidden return-pointer param name when the current
                                    // function returns a multi-word struct by value; else NULL
    StringMap user_labels;          // source label name -> unique %L<n> TAC name, per function
    // defer: the blocks open at the point being lowered, innermost last, and the loops
    // and switches around it.  While a deferred statement is lowered, they are its own.
    TacScope *scopes;
    int nscopes, scopes_cap;
    TacBreak *breaks;
    int nbreaks, breaks_cap;
    StringMap label_pos;  // label -> DeferPos *, for a goto that leaves defers behind
    bool label_pos_ready; // label_pos collected (only once a goto needs it)
    Stmt *body;           // the function body, where label_pos comes from
    int defer_depth;      // deferred statements being lowered, one inside another
} TacCtx;

//
// Switch case tracking — built by collect_cases(), consumed by gen_stmt STMT_SWITCH.
//
typedef struct CaseEntry {
    Expr *expr;        // case constant expression
    const char *label; // non-owning ptr to stmt->branch_target_label
    struct CaseEntry *next;
} CaseEntry;

typedef struct {
    CaseEntry *head;
    CaseEntry **tail;
    const char *default_label; // non-owning ptr, or NULL
} CaseList;

// Enable debug output
extern int translator_debug;
// Nonzero: check every translated function with tac_verify_function and stop on a
// problem.  Always on in a build without NDEBUG.
extern int translate_verify;
extern int translate_rotate_loops; // set by translate() from OptFlags.loop_rotate
extern int import_debug;
extern int export_debug;
extern int wio_debug;
extern int xalloc_debug;

//
// Low-level TAC-building helpers (translate.c)
//
void tac_append(TacCtx *ctx, Tac_Instruction *instr);
// A fresh label name.
char *new_temp(TacCtx *ctx);
// A fresh temporary holding a value of `type`, recorded in the function's symbol list
// (takes ownership of `type`).
char *new_typed_temp(TacCtx *ctx, Tac_Type *type);
void tac_record_local(TacCtx *ctx, const char *name, const Type *type);
void tac_record_array_local(TacCtx *ctx, const char *name);
bool tac_is_array_local(const TacCtx *ctx, const char *name);
// Record a block-scope declaration of an external object or function.  Its symbol is
// purged before lowering, so it travels as an EXTERN toplevel ahead of the function.
void tac_record_extern(TacCtx *ctx, const char *name, const Type *type);
Tac_Val *val_int(int64_t v);
Tac_Val *val_long(long v);
Tac_Val *val_long_long(long long v);
Tac_Val *val_uint(uint64_t v);
Tac_Val *val_ulong(unsigned long v);
Tac_Val *val_ulong_long(unsigned long long v);
Tac_Val *val_size(uint64_t v);
Tac_Val *val_float(float v);
Tac_Val *val_double(double v);
Tac_Val *val_long_double(Float128 v);
Tac_Val *val_var(const char *name);
// A Var naming a fresh temporary of `type` (takes ownership of `type`).
Tac_Val *new_var_val(TacCtx *ctx, Tac_Type *type);
Tac_Val *dup_val(const Tac_Val *v);
// A zero constant of scalar type `t`: floating-point for a floating type.
Tac_Val *val_zero(const Type *t);
Tac_Val *emit_cast(TacCtx *ctx, Tac_Val *src, const Type *from, const Type *to);
// "src != 0" — the C11 §6.3.1.2 conversion of a scalar to _Bool.  Used by emit_cast for
// every ordinary conversion, and directly by ++/--, which never builds a cast.  Returns
// the instruction-owned destination Val (see the definition).
Tac_Val *emit_bool_normalize(TacCtx *ctx, Tac_Val *src, const Type *from, const Type *to);
void emit_jump(TacCtx *ctx, const char *target);
void emit_label(TacCtx *ctx, const char *name);
const char *user_label_name(TacCtx *ctx, const char *src);
void free_user_labels(StringMap *labels);

//
// Struct-by-value support (translate.c)
//
// One target machine word, in bytes (the unit a scalar return value occupies).
int target_word_bytes(void);
// True when `t` is a struct/union too large to return by value on this target
// (Target.struct_return_max), so it uses the hidden-pointer (sret) calling convention.
bool type_is_byval_sret(const Type *t);
// True when `t` is a struct/union argument this target passes as consecutive words
// (Target.struct_args_split).
bool type_is_split_arg(const Type *t);
// True when a `?:` of type `t` merges its arms in a frame slot by aggregate copies
// rather than by a COPY: a struct/union wider than a word, or any struct/union on a
// byte-addressed target.
bool type_needs_slot(const Type *t);
// One side of an aggregate copy: a named frame/global aggregate `name` at byte `offset`
// (reached by COPY_*_OFFSET), or else the address held in variable `ptr` (ADD_PTR +
// LOAD/STORE).
typedef struct {
    const char *name;
    int offset;
    const char *ptr;
} AggPlace;
// The unit of an aggregate copy: the type's alignment, at most one machine word.  A size
// is a multiple of the alignment, so the chunks cover the object exactly.
int aggregate_chunk(const Type *t);
// Copy a whole object of `type` from `src` to `dst`, chunk by chunk.
void gen_aggregate_copy(TacCtx *ctx, const AggPlace *dst, const AggPlace *src, const Type *type);
// Copy a whole struct/union value from named aggregate `src_name` into `dst_name` at
// byte offset `dst_off`.
void gen_struct_assign(TacCtx *ctx, const char *dst_name, int dst_off, const char *src_name,
                       const Type *type);
// Initialize a whole aggregate (named destination base `dst_name`+`dst_off`) from a value
// expression, which may also be reached through a pointer (unlike gen_struct_assign,
// which assumes the source is a named aggregate base).  Lives in expr.c.
void gen_aggregate_init_from_expr(TacCtx *ctx, const char *dst_name, int dst_off, Expr *value,
                                  const Type *type);

//
// Type conversion (translate.c)
//
// ast_type_to_tac_type is semantic's (semantic.h).
// Pointer to `target` (takes ownership).
Tac_Type *tac_type_ptr(Tac_Type *target);
// Pointer to the TAC form of AST type `t`.
Tac_Type *tac_type_ptr_to(const Type *t);
// Byte sizes of the active target, for tac_verify.
void tac_layout_of_target(Tac_Layout *layout);
// Plain char, with the target's signedness.
Tac_Type *tac_type_char(void);
// The unsigned integer of one machine word (pointer size): the unit of word copies.
Tac_Type *tac_type_word(void);

// ptrdiff_t: the type of a pointer difference.
Tac_Type *tac_type_ptrdiff(void);

//
// Expression and statement lowering (translate_expr.c, translate_stmt.c)
//
Tac_Val *gen_expr(TacCtx *ctx, Expr *e);
// Initialize bit-field `bf` of type `type`, in the storage unit at `offset` of named
// aggregate `var`, to owned value `v`.
void gen_bitfield_init(TacCtx *ctx, const char *var, int offset, const BitField *bf,
                       const Type *type, Tac_Val *v);
Tac_Val *gen_cond_val(TacCtx *ctx, Expr *cond);
void gen_stmt(TacCtx *ctx, Stmt *stmt);
void gen_compound_init(TacCtx *ctx, const char *var_name, int base_offset, const Initializer *init);
void gen_aggregate_init(TacCtx *ctx, const char *var_name, const Initializer *init, int bytes);
void gen_string_array_init(TacCtx *ctx, const char *var_name, const Expr *str_expr, int bytes);

//
// Convert one external declaration to TAC and optimize each function it yields.
// Each function self-describes its params and automatic locals, so the optimizer
// classifies locals vs. globals per function — no whole-program context needed.
//
// Lower one external declaration to TAC.  `label_seq` is a caller-owned counter
// that must persist across every call within one translation unit and be reset to
// 0 at its start: compiler temporaries and branch labels (`%N`, from new_temp) are
// numbered from it, so a per-function reset would make different functions reuse
// `%0`, `%1`, …  That is harmless on the Madlen backend (each function is its own
// `,name,`/`,end,` module, so labels are module-scoped) but corrupts the Unix
// (b6as) and Bemsh backends, which place the whole unit in one file with
// file-scoped labels — a duplicate label silently binds a branch to the wrong
// function.  Threading one counter across the unit keeps every `%N` unique.
Tac_TopLevel *translate(const ExternalDecl *ast, OptFlags flags, int *label_seq);

// Bracket a translation unit.  Between the two calls translate() notes every name the
// unit defines or references; translate_unit_end() returns an EXTERN toplevel for each
// name referenced but not defined (sorted by name), and must run while the file-scope
// symbol table is still alive.
void translate_unit_begin(void);
Tac_TopLevel *translate_unit_end(void);

#ifdef __cplusplus
}
#endif

#endif /* TRANSLATE_H */
