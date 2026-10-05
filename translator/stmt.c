//
// Statement lowering: AST Stmt → TAC instructions.
//

#include <stdlib.h>
#include <string.h>

#include "c_escape.h"
#include "target.h"
#include "translate.h"
#include "typecheck.h"
#include "xalloc.h"

static void collect_cases(TacCtx *ctx, Stmt *stmt, CaseList *list)
{
    if (!stmt)
        return;
    switch (stmt->kind) {
    case STMT_CASE: {
        stmt->branch_target_label = new_temp(ctx);
        CaseEntry *e              = xalloc(sizeof *e, __func__, __FILE__, __LINE__);
        e->expr                   = stmt->u.case_stmt.expr;
        e->label                  = stmt->branch_target_label;
        e->next                   = NULL;
        *list->tail               = e;
        list->tail                = &e->next;
        collect_cases(ctx, stmt->u.case_stmt.stmt, list);
        break;
    }
    case STMT_DEFAULT:
        stmt->branch_target_label = new_temp(ctx);
        list->default_label       = stmt->branch_target_label;
        collect_cases(ctx, stmt->u.default_stmt, list);
        break;
    case STMT_COMPOUND:
        for (DeclOrStmt *ds = stmt->u.compound; ds; ds = ds->next)
            if (ds->kind == DECL_OR_STMT_STMT)
                collect_cases(ctx, ds->u.stmt, list);
        break;
    case STMT_IF:
        collect_cases(ctx, stmt->u.if_stmt.then_stmt, list);
        collect_cases(ctx, stmt->u.if_stmt.else_stmt, list);
        break;
    case STMT_WHILE:
        collect_cases(ctx, stmt->u.while_stmt.body, list);
        break;
    case STMT_DO_WHILE:
        collect_cases(ctx, stmt->u.do_while.body, list);
        break;
    case STMT_FOR:
        collect_cases(ctx, stmt->u.for_stmt.body, list);
        break;
    case STMT_LABELED:
        collect_cases(ctx, stmt->u.labeled.stmt, list);
        break;
    default: // STMT_SWITCH and leaf statements: do not recurse
        break;
    }
}

// Lower `char arr[N] = "…"` to a run of byte stores into the frame slot: one
// COPY_BYTE_TO_OFFSET per source byte at successive byte offsets, then zero-fill up to
// the array's size (C string-init semantics — the terminating null and any trailing
// zeros).  Used for a 1-D char array and for each inner string row of a multi-dimensional
// char array (whose contiguous rows the caller has already offset).  Char data keeps its
// source (ASCII) encoding, like every scalar char value; only the static-data path repacks
// to KOI-7 (which differs solely for lowercase Latin — see backend/besm6/KOI7_Encoding.md).
// With skip_zero the object was bulk-zeroed, so zero bytes are not stored.
static void gen_char_array_string_init(TacCtx *ctx, const char *var_name, int base_offset,
                                       const Expr *str_expr, int array_bytes, bool skip_zero)
{
    size_t len;
    char *decoded = c_decode_string_literal(str_expr->u.literal->u.string_val, &len);
    for (int i = 0; i < array_bytes; i++) {
        int byte                    = (size_t)i < len ? (unsigned char)decoded[i] : 0;
        if (skip_zero && byte == 0)
            continue;
        Tac_Instruction *in         = tac_new_instruction(TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET);
        in->u.copy_to_offset.src    = val_int(byte);
        in->u.copy_to_offset.dst    = xstrdup(var_name);
        in->u.copy_to_offset.offset = base_offset + i;
        tac_append(ctx, in);
    }
    xfree(decoded);
}

// True for a string literal initializing a char array (`char a[N] = "…"`), whether a
// top-level 1-D array or an inner row of a multi-dimensional char array.
static bool is_char_array_string_init(const Type *type, const Initializer *init)
{
    return init->kind == INITIALIZER_SINGLE && type && unalias(type)->kind == TYPE_ARRAY &&
           init->u.expr->kind == EXPR_LITERAL &&
           init->u.expr->u.literal->kind == LITERAL_STRING;
}

// Objects with at least this many zero stores (word or byte) are zeroed by a loop first.
#define ZERO_FILL_STORES 8

// Zero bytes a string stores into a char array of array_bytes.
static int string_zero_bytes(const Expr *str_expr, int array_bytes)
{
    size_t len;
    char *decoded = c_decode_string_literal(str_expr->u.literal->u.string_val, &len);
    int n         = 0;
    for (int i = 0; i < array_bytes; i++)
        n += (size_t)i >= len || decoded[i] == 0;
    xfree(decoded);
    return n;
}

// A leaf storing an integer, char or pointer zero, made redundant by bulk zeroing.  A
// floating zero is kept: an all-zero word is not +0.0 on every target.
static bool is_zero_leaf(const Initializer *init)
{
    return init->kind == INITIALIZER_SINGLE && init->type &&
           (is_integer(init->type) || is_pointer(init->type)) &&
           init->u.expr->kind == EXPR_LITERAL && is_zero_int(init->u.expr->u.literal);
}

// Zero stores an initializer makes: zero leaves, and the zero bytes of strings.
static int zero_stores(const Initializer *init)
{
    if (init->kind == INITIALIZER_SINGLE) {
        if (is_char_array_string_init(init->type, init))
            return string_zero_bytes(init->u.expr, (int)get_size(init->type));
        return is_zero_leaf(init);
    }
    int n = 0;
    for (const InitItem *item = init->u.items; item; item = item->next)
        n += zero_stores(item->init);
    return n;
}

// Zero the first `bytes` bytes of var_name: a word-store loop, then byte stores for a tail.
// A word-addressed target's slot is whole words, so there the loop covers the tail too.
static void gen_zero_fill(TacCtx *ctx, const char *var_name, int bytes)
{
    int w = target_word_bytes();
    if (target_word_addressed())
        bytes = (bytes + w - 1) / w * w;
    int nwords = bytes / w;

    char *ptr             = new_typed_temp(ctx, tac_type_ptr(tac_type_word()));
    Tac_Instruction *ga   = tac_new_instruction(TAC_INSTRUCTION_GET_ADDRESS);
    ga->u.get_address.src = val_var(var_name);
    ga->u.get_address.dst = val_var(ptr);
    tac_append(ctx, ga);

    char *count         = new_typed_temp(ctx, tac_new_type(TAC_TYPE_INT));
    Tac_Instruction *cp = tac_new_instruction(TAC_INSTRUCTION_COPY);
    cp->u.copy.src      = val_int(nwords);
    cp->u.copy.dst      = val_var(count);
    tac_append(ctx, cp);

    char *top = new_temp(ctx);
    emit_label(ctx, top);

    // A word-sized zero: on a byte-addressed target an int is narrower than a pointer.
    Tac_Instruction *st = tac_new_instruction(TAC_INSTRUCTION_STORE);
    st->u.store.src     = (int)target_config->int_size == w ? val_int(0) : val_long_long(0);
    st->u.store.dst_ptr = val_var(ptr);
    tac_append(ctx, st);

    Tac_Instruction *ap = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
    ap->u.add_ptr.ptr   = val_var(ptr);
    ap->u.add_ptr.index = val_int(1);
    ap->u.add_ptr.scale = w;
    ap->u.add_ptr.dst   = val_var(ptr);
    tac_append(ctx, ap);

    Tac_Instruction *sub = tac_new_instruction(TAC_INSTRUCTION_BINARY);
    sub->u.binary.op     = TAC_BINARY_SUBTRACT;
    sub->u.binary.src1   = val_var(count);
    sub->u.binary.src2   = val_int(1);
    sub->u.binary.dst    = val_var(count);
    tac_append(ctx, sub);

    Tac_Instruction *jnz              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
    jnz->u.jump_if_not_zero.condition = val_var(count);
    jnz->u.jump_if_not_zero.target    = top;
    tac_append(ctx, jnz);

    for (int i = nwords * w; i < bytes; i++) {
        Tac_Instruction *in         = tac_new_instruction(TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET);
        in->u.copy_to_offset.src    = val_int(0);
        in->u.copy_to_offset.dst    = xstrdup(var_name);
        in->u.copy_to_offset.offset = i;
        tac_append(ctx, in);
    }
    xfree(ptr);
    xfree(count);
}

static void gen_init(TacCtx *ctx, const char *var_name, int base_offset, const Initializer *init,
                     bool skip_zero)
{
    if (init->kind == INITIALIZER_SINGLE) {
        if (skip_zero && is_zero_leaf(init))
            return;
        // A string literal initializing a char array (e.g. an inner row of a multi-dim
        // char array): store its bytes individually rather than COPY a whole-word pointer.
        if (is_char_array_string_init(init->type, init)) {
            gen_char_array_string_init(ctx, var_name, base_offset, init->u.expr,
                                       (int)get_size(init->type), skip_zero);
            return;
        }
        Tac_Val *src    = gen_expr(ctx, init->u.expr);
        const Type *it0 = init->type ? unalias(init->type) : NULL;
        if (it0 && (it0->kind == TYPE_STRUCT || it0->kind == TYPE_UNION)) {
            // A whole struct/union value (not a scalar leaf) — copy it word by word.
            gen_struct_assign(ctx, var_name, base_offset, src->u.var_name, init->type);
            tac_free_val(src);
            return;
        }
        // A char/signed char/unsigned char leaf occupies a single packed byte;
        // its element offset is a byte offset, so it must use the byte-store kind
        // (the word-store kind rejects a sub-word offset).
        bool byte_leaf              = init->type && get_size(init->type) == 1;
        Tac_Instruction *in         = tac_new_instruction(
            byte_leaf ? TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET : TAC_INSTRUCTION_COPY_TO_OFFSET);
        in->u.copy_to_offset.src    = src;
        in->u.copy_to_offset.dst    = xstrdup(var_name);
        in->u.copy_to_offset.offset = base_offset;
        tac_append(ctx, in);
        return;
    }
    const Type *t = unalias(init->type);
    if (t->kind == TYPE_ARRAY) {
        int elem_size = (int)get_size(t->u.array.element);
        int i         = 0;
        for (const InitItem *item = init->u.items; item; item = item->next, i++)
            gen_init(ctx, var_name, base_offset + i * elem_size, item->init, skip_zero);
    } else if (t->kind == TYPE_STRUCT) {
        // Member byte offsets were resolved and cached on each InitItem by typecheck
        // (typecheck_init), while the struct tag was still live in structtab.  Consume
        // them here instead of re-querying structtab, which a block-local tag has left.
        for (const InitItem *item = init->u.items; item; item = item->next)
            gen_init(ctx, var_name, base_offset + item->offset, item->init, skip_zero);
    } else if (t->kind == TYPE_UNION) {
        // typecheck_init reduced the union initializer to its single first member,
        // which lives at offset 0 of the union — initialize it there.  No structtab
        // lookup is needed, so this works for block-scope unions too.
        if (init->u.items)
            gen_init(ctx, var_name, base_offset, init->u.items->init, skip_zero);
    } else {
        fatal_error("Compound initializer for unsupported type %d in TAC lowering", (int)t->kind);
    }
}

void gen_compound_init(TacCtx *ctx, const char *var_name, int base_offset, const Initializer *init)
{
    gen_init(ctx, var_name, base_offset, init, false);
}

// Initialize a whole aggregate object of `bytes` bytes.  With many zero leaves, zero it
// by a loop first and store only the rest.
void gen_aggregate_init(TacCtx *ctx, const char *var_name, const Initializer *init, int bytes)
{
    bool bulk = zero_stores(init) >= ZERO_FILL_STORES;
    if (bulk)
        gen_zero_fill(ctx, var_name, bytes);
    gen_init(ctx, var_name, 0, init, bulk);
}

// Initialize a char array of `bytes` bytes from a string, bulk-zeroing a long zero tail.
void gen_string_array_init(TacCtx *ctx, const char *var_name, const Expr *str_expr, int bytes)
{
    bool bulk = string_zero_bytes(str_expr, bytes) >= ZERO_FILL_STORES;
    if (bulk)
        gen_zero_fill(ctx, var_name, bytes);
    gen_char_array_string_init(ctx, var_name, 0, str_expr, bytes, bulk);
}

static void gen_local_decl(TacCtx *ctx, const Declaration *decl)
{
    if (decl->kind != DECL_VAR)
        return;
    // Variables with automatic storage are private to this function; record
    // their names so the optimizer does not mistake them for observable globals.
    // static/extern/typedef/thread-local declarators are not automatics: a
    // static or extern name denotes observable storage and must be left out.
    StorageClass storage =
        decl->u.var.specifiers ? decl->u.var.specifiers->storage : STORAGE_CLASS_NONE;
    bool is_automatic = storage == STORAGE_CLASS_NONE || storage == STORAGE_CLASS_AUTO ||
                        storage == STORAGE_CLASS_REGISTER;
    for (const InitDeclarator *id = decl->u.var.declarators; id; id = id->next) {
        if (!id->name)
            continue;
        const Type *idt = id->type ? unalias(id->type) : NULL;
        // A block-scope function declaration (int f(void);) is not an automatic:
        // it names an external-linkage function with no frame slot, so it must
        // not be percent-prefixed like a local.
        if (idt && idt->kind == TYPE_FUNCTION) {
            tac_record_extern(ctx, id->name, id->type);
            continue;
        }
        if (storage == STORAGE_CLASS_EXTERN)
            tac_record_extern(ctx, id->name, id->type);
        if (!is_automatic) {
            // A static/extern local array has no frame slot — its storage is the
            // module-local static datum, addressed by its label like a global — but it
            // still decays to its address when used as a value, so record it for the decay.
            if (idt && idt->kind == TYPE_ARRAY)
                tac_record_array_local(ctx, id->name);
            continue;
        }
        tac_record_local(ctx, id->name, id->type);
        // Aggregate locals (arrays, structs, unions) occupy contiguous frame slots;
        // emit an AllocateLocal so the backend reserves the full size instead of a
        // single slot. Scalars keep their implicit one-slot allocation. Size and
        // alignment are in target bytes (like every other offset in the TAC stream);
        // each backend converts to its own allocation unit (the besm6 backend divides
        // by the 6-byte machine word).
        // A local array decays to a pointer when used as a value; record it so gen_expr
        // can emit the array-decay GET_ADDRESS (symtab locals are gone by then).
        if (idt && idt->kind == TYPE_ARRAY)
            tac_record_array_local(ctx, id->name);
        // An over-aligned (_Alignas) scalar is allocated the same way, to carry its
        // alignment.
        int align   = (int)get_alignment(id->type);
        int alignas = alignas_bytes(decl->u.var.specifiers);
        if ((idt && (idt->kind == TYPE_ARRAY || idt->kind == TYPE_STRUCT ||
                     idt->kind == TYPE_UNION)) ||
            alignas > align) {
            int bytes = (int)get_size(id->type);
            if (bytes > 0) {
                Tac_Instruction *in       = tac_new_instruction(TAC_INSTRUCTION_ALLOCATE_LOCAL);
                in->u.allocate_local.name = xstrdup(id->name);
                in->u.allocate_local.size = bytes;
                in->u.allocate_local.alignment = alignas > align ? alignas : align;
                tac_append(ctx, in);
            }
        }
    }
    for (InitDeclarator *id = decl->u.var.declarators; id; id = id->next) {
        if (id->init && id->init->kind == INITIALIZER_SINGLE) {
            // `char a[N] = "…"`: copy the string's bytes into the frame slot (a real
            // per-byte copy, not an alias of the string-constant pointer).
            if (is_char_array_string_init(id->type, id->init)) {
                gen_string_array_init(ctx, id->name, id->init->u.expr, (int)get_size(id->type));
                continue;
            }
            const Type *idt2 = id->type ? unalias(id->type) : NULL;
            if (idt2 && (idt2->kind == TYPE_STRUCT || idt2->kind == TYPE_UNION)) {
                // Whole-aggregate initialization (e.g. struct r = other; struct r = f();
                // union u = *ptr;): copy every word from the source into the new local.
                // The source may be a named aggregate, a call/compound rvalue, or — the
                // case gen_struct_assign mishandled — an lvalue reached through a pointer.
                gen_aggregate_init_from_expr(ctx, id->name, 0, id->init->u.expr, id->type);
            } else {
                Tac_Val *src        = gen_expr(ctx, id->init->u.expr);
                Tac_Instruction *in = tac_new_instruction(TAC_INSTRUCTION_COPY);
                in->is_volatile     = type_is_volatile(id->type); // never dropped as dead
                in->u.copy.src      = src;
                in->u.copy.dst      = val_var(id->name);
                tac_append(ctx, in);
            }
        } else if (id->init && id->init->kind == INITIALIZER_COMPOUND) {
            gen_aggregate_init(ctx, id->name, id->init, (int)get_size(id->type));
        }
    }
}

// The test of a loop: jump to `target` when `cond` is true (`if_true`) or false. A
// rotated loop lowers its condition twice, once for each copy of the test.
static void emit_loop_test(TacCtx *ctx, Expr *cond, bool if_true, const char *target)
{
    Tac_Val *v = gen_cond_val(ctx, cond);
    Tac_Instruction *j;
    if (if_true) {
        j                              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
        j->u.jump_if_not_zero.condition = v;
        j->u.jump_if_not_zero.target    = xstrdup(target);
    } else {
        j                          = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_ZERO);
        j->u.jump_if_zero.condition = v;
        j->u.jump_if_zero.target    = xstrdup(target);
    }
    tac_append(ctx, j);
}

void gen_stmt(TacCtx *ctx, Stmt *stmt)
{
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case STMT_COMPOUND: {
        for (DeclOrStmt *ds = stmt->u.compound; ds; ds = ds->next) {
            if (ds->kind == DECL_OR_STMT_DECL) {
                gen_local_decl(ctx, ds->u.decl);
            } else {
                gen_stmt(ctx, ds->u.stmt);
            }
        }
        break;
    }
    case STMT_EXPR:
        if (stmt->u.expr) {
            tac_free_val(gen_expr(ctx, stmt->u.expr));
        }
        break;
    case STMT_RETURN: {
        if (ctx->sret_name && stmt->u.expr) {
            // A struct return through the hidden pointer (sret): copy the result into the
            // caller's slot through the
            // hidden return pointer, then return the pointer itself.
            Tac_Val *src = gen_expr(ctx, stmt->u.expr); // VAR naming the source aggregate
            AggPlace dst = { NULL, 0, ctx->sret_name };
            AggPlace from = { src->u.var_name, 0, NULL };
            gen_aggregate_copy(ctx, &dst, &from, stmt->u.expr->type);
            tac_free_val(src);
            Tac_Instruction *in = tac_new_instruction(TAC_INSTRUCTION_RETURN);
            in->u.return_.src   = val_var(ctx->sret_name);
            tac_append(ctx, in);
            break;
        }
        Tac_Instruction *in = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        in->u.return_.src   = stmt->u.expr ? gen_expr(ctx, stmt->u.expr) : NULL;
        tac_append(ctx, in);
        break;
    }
    case STMT_IF: {
        Tac_Val *cond = gen_cond_val(ctx, stmt->u.if_stmt.condition);
        char *else_l  = new_temp(ctx);
        char *end_l   = new_temp(ctx);

        Tac_Instruction *jz          = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_ZERO);
        jz->u.jump_if_zero.condition = cond;
        jz->u.jump_if_zero.target    = else_l; // instruction takes ownership
        tac_append(ctx, jz);
        gen_stmt(ctx, stmt->u.if_stmt.then_stmt);
        emit_jump(ctx, end_l);
        emit_label(ctx, else_l);
        if (stmt->u.if_stmt.else_stmt) {
            gen_stmt(ctx, stmt->u.if_stmt.else_stmt);
        }
        emit_label(ctx, end_l);
        xfree(end_l); // emit_jump and emit_label each xstrdup; free the original
        break;
    }
    case STMT_WHILE: {
        const char *cl = stmt->loop_continue_label;
        const char *bl = stmt->loop_end_label;
        if (!cl || !bl) {
            fatal_error("while: missing loop labels (label_loops not run?)");
        }
        if (translate_rotate_loops) {
            // Rotated: the test at the bottom, and a copy of it at the top as a guard.
            emit_loop_test(ctx, stmt->u.while_stmt.condition, false, bl);
            char *top = new_temp(ctx);
            emit_label(ctx, top);
            gen_stmt(ctx, stmt->u.while_stmt.body);
            emit_label(ctx, cl);
            emit_loop_test(ctx, stmt->u.while_stmt.condition, true, top);
            xfree(top);
            emit_label(ctx, bl);
            break;
        }
        emit_label(ctx, cl);
        emit_loop_test(ctx, stmt->u.while_stmt.condition, false, bl);
        gen_stmt(ctx, stmt->u.while_stmt.body);
        emit_jump(ctx, cl);
        emit_label(ctx, bl);
        break;
    }
    case STMT_DO_WHILE: {
        const char *cl = stmt->loop_continue_label;
        const char *bl = stmt->loop_end_label;
        if (!cl || !bl) {
            fatal_error("do-while: missing loop labels");
        }
        char *loop_top = new_temp(ctx);
        emit_label(ctx, loop_top);
        gen_stmt(ctx, stmt->u.do_while.body);
        emit_label(ctx, cl);
        Tac_Val *cond                     = gen_cond_val(ctx, stmt->u.do_while.condition);
        Tac_Instruction *jnz              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
        jnz->u.jump_if_not_zero.condition = cond;
        jnz->u.jump_if_not_zero.target    = loop_top;
        tac_append(ctx, jnz);
        emit_label(ctx, bl);
        break;
    }
    case STMT_FOR: {
        const char *cl = stmt->loop_continue_label;
        const char *bl = stmt->loop_end_label;
        if (!cl || !bl) {
            fatal_error("for: missing loop labels");
        }
        if (stmt->u.for_stmt.init) {
            if (stmt->u.for_stmt.init->kind == FOR_INIT_EXPR) {
                if (stmt->u.for_stmt.init->u.expr) {
                    tac_free_val(gen_expr(ctx, stmt->u.for_stmt.init->u.expr));
                }
            } else {
                gen_local_decl(ctx, stmt->u.for_stmt.init->u.decl);
            }
        }
        Expr *cond = stmt->u.for_stmt.condition;
        bool rotate = translate_rotate_loops && cond;
        if (rotate)
            emit_loop_test(ctx, cond, false, bl); // the guard
        char *top = new_temp(ctx);
        emit_label(ctx, top);
        if (cond && !rotate)
            emit_loop_test(ctx, cond, false, bl);
        gen_stmt(ctx, stmt->u.for_stmt.body);
        emit_label(ctx, cl);
        if (stmt->u.for_stmt.update) {
            tac_free_val(gen_expr(ctx, stmt->u.for_stmt.update));
        }
        if (rotate)
            emit_loop_test(ctx, cond, true, top);
        else
            emit_jump(ctx, top);
        xfree(top); // emit_label and emit_jump each xstrdup; free the original
        emit_label(ctx, bl);
        break;
    }
    case STMT_SWITCH: {
        if (!stmt->loop_end_label)
            fatal_error("switch: missing end label (label_loops not run?)");

        CaseList cases = { NULL, NULL, NULL };
        cases.tail     = &cases.head;
        collect_cases(ctx, stmt->u.switch_stmt.body, &cases);

        Tac_Val *ctrl_raw     = gen_expr(ctx, stmt->u.switch_stmt.expr);
        Tac_Val *ctrl_dst     = new_var_val(ctx, ast_type_to_tac_type(stmt->u.switch_stmt.expr->type));
        const char *ctrl_name = ctrl_dst->u.var_name; // save before ownership transfer
        Tac_Instruction *cp   = tac_new_instruction(TAC_INSTRUCTION_COPY);
        cp->u.copy.src        = ctrl_raw;
        cp->u.copy.dst        = ctrl_dst;
        tac_append(ctx, cp);

        const Type *ctrl_type = stmt->u.switch_stmt.expr->type;
        for (CaseEntry *e = cases.head; e; e = e->next) {
            // C11 §6.8.4.2p5: a case constant is converted to the promoted type of the
            // controlling expression.  Only a width change can alter the comparison.
            Tac_Val *cval = gen_expr(ctx, e->expr);
            if (get_size(e->expr->type) != get_size(ctrl_type))
                cval = emit_cast(ctx, cval, e->expr->type, ctrl_type);
            Tac_Val *cmp_dst     = new_var_val(ctx, tac_new_type(TAC_TYPE_INT));
            const char *cmp_name = cmp_dst->u.var_name;
            Tac_Instruction *bin = tac_new_instruction(TAC_INSTRUCTION_BINARY);
            bin->u.binary.op     = TAC_BINARY_EQUAL;
            bin->u.binary.src1   = val_var(ctrl_name);
            bin->u.binary.src2   = cval;
            bin->u.binary.dst    = cmp_dst;
            tac_append(ctx, bin);
            Tac_Instruction *jnz = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
            jnz->u.jump_if_not_zero.condition = val_var(cmp_name);
            jnz->u.jump_if_not_zero.target    = xstrdup(e->label);
            tac_append(ctx, jnz);
        }

        emit_jump(ctx, cases.default_label ? cases.default_label : stmt->loop_end_label);
        gen_stmt(ctx, stmt->u.switch_stmt.body);
        emit_label(ctx, stmt->loop_end_label);

        for (CaseEntry *e = cases.head; e;) {
            CaseEntry *nx = e->next;
            xfree(e);
            e = nx;
        }
        break;
    }
    case STMT_BREAK: {
        if (!stmt->branch_target_label) {
            fatal_error("break without target label");
        }
        emit_jump(ctx, stmt->branch_target_label);
        break;
    }
    case STMT_CONTINUE: {
        if (!stmt->branch_target_label) {
            fatal_error("continue without target label");
        }
        emit_jump(ctx, stmt->branch_target_label);
        break;
    }
    case STMT_GOTO:
        emit_jump(ctx, user_label_name(ctx, stmt->u.goto_label));
        break;
    case STMT_LABELED:
        emit_label(ctx, user_label_name(ctx, stmt->u.labeled.label));
        gen_stmt(ctx, stmt->u.labeled.stmt);
        break;
    case STMT_CASE:
        if (!stmt->branch_target_label)
            fatal_error("case: missing label (collect_cases not run?)");
        emit_label(ctx, stmt->branch_target_label);
        gen_stmt(ctx, stmt->u.case_stmt.stmt);
        break;
    case STMT_DEFAULT:
        if (!stmt->branch_target_label)
            fatal_error("default: missing label (collect_cases not run?)");
        emit_label(ctx, stmt->branch_target_label);
        gen_stmt(ctx, stmt->u.default_stmt);
        break;
    default:
        fatal_error("Unsupported statement kind %d in TAC lowering", (int)stmt->kind);
    }
}
