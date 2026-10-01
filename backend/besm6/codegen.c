#include "codegen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "frame.h"
#include "internal.h"
#include "string_map.h"
#include "tac.h"
#include "xalloc.h"

// Forward declaration.
static void codegen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out,
                             Besm_Dialect dialect);
static void bemsh_declare_call_targets(Besm_Func *func, const char *self_name);

void codegen_program(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out,
                     Besm_Dialect dialect)
{
    switch (tl->kind) {
    case TAC_TOPLEVEL_FUNCTION:
        codegen_function(program, tl, out, dialect);
        break;
    case TAC_TOPLEVEL_STATIC_VARIABLE:
        codegen_static_variable(program, tl, out, dialect);
        break;
    case TAC_TOPLEVEL_STATIC_CONSTANT:
        // String constants are no longer emitted as standalone global modules;
        // each is folded into the (single) module that references it.
        break;
    case TAC_TOPLEVEL_EXTERN:
        // A referenced name is declared where it is used (SUBP).
        break;
    }
}

// Declare `v` as an external (SUBP) if it is a module-level name that has no
// frame slot and has not been declared yet.  SUBP allocates no memory; it just
// tells the single-pass assembler the name is external, and must precede the
// first UTC that references it.
static void declare_global_name(Besm_Block *block, Besm_Instr **tail, const Frame *f,
                                StringMap *declared, const char *name)
{
    int sr, so;
    intptr_t dummy;
    if (frame_lookup(f, name, &sr, &so))
        return; // local / param
    if (map_get(declared, name, &dummy))
        return; // already declared
    Besm_Instr *ssubp = emit(block, tail, BESM_STMT_SUBP);
    ssubp->name       = xstrdup(name);
    map_insert(declared, name, 1, 0);
}

static void declare_global_operand(Besm_Block *block, Besm_Instr **tail, const Frame *f,
                                   StringMap *declared, const Tac_Val *v)
{
    if (!v || v->kind != TAC_VAL_VAR)
        return;
    declare_global_name(block, tail, f, declared, v->u.var_name);
}

//
// After the peephole pass, compute how many auto-variable words the frame still
// needs.  Peephole can delete the only store/reload of a temporary (collapsing it
// into pure A-register dataflow), leaving its slot unreferenced, so the prologue
// stack extension emitted up front may now reserve dead words.
//
// Only compiler temporaries ('%'+digit) are reclaimed, and only when nothing in the
// final stream references their slot.  Named locals and aggregates are always kept:
// their address can be taken (e.g. `&x`), and GET_ADDRESS of a slot-0 local emits
// `ita 7` — the register number rides in `addr`, not `reg` — so a direct reg==REG_AUTO
// scan would miss it.  Temporaries are never address-taken, so a reg==REG_AUTO load/
// store fully captures their use.  We therefore keep every non-temp slot plus every
// referenced temp slot, and shrink to the highest such offset + 1 (slots are absolute
// r7+off offsets and cannot be renumbered, so only trailing dead temps are reclaimed).
//
static int used_auto_words(const Besm_Func *func, const Frame *f)
{
    int orig = frame_num_autos(f);
    if (orig <= 0)
        return 0;

    bool *referenced = (bool *)xalloc(orig * sizeof(bool), __func__, __FILE__, __LINE__);
    for (int i = 0; i < orig; i++)
        referenced[i] = false;
    for (const Besm_Func *fn = func; fn; fn = fn->next)
        for (const Besm_Block *block = fn->blocks; block; block = block->next)
            for (const Besm_Instr *i = block->body; i; i = i->next)
                if (i->name == NULL && i->konst == NULL && (int)i->reg == REG_AUTO &&
                    i->addr >= 0 && i->addr < orig)
                    referenced[i->addr] = true;

    int used = 0;
    for (int off = 0; off < orig; off++)
        if (!frame_slot_is_temp(f, REG_AUTO, off) || referenced[off])
            used = off + 1; // this slot must stay reserved

    xfree(referenced);
    return used;
}

// Unlink `target` from the block's instruction list and free it.  `besm_free_instr`
// recurses on ->next, so unlink first (set target->next = NULL) to free only `target`.
static void remove_instr(Besm_Block *block, const Besm_Instr *target)
{
    Besm_Instr *prev = NULL;
    for (Besm_Instr *i = block->body; i; prev = i, i = i->next) {
        if (i != target)
            continue;
        if (prev)
            prev->next = i->next;
        else
            block->body = i->next;
        i->next = NULL;
        besm_free_instr(i);
        return;
    }
}

// Bemsh has no auto-declaring call macro — unlike Madlen's `,call,` and Unix b6as, which
// implicitly extern an undefined callee, Bemsh's `пв` (VJM) does not.  So every distinct
// call target must be declared external with a `внешн`.  After instruction selection, scan
// the final stream for BESM_BRANCH_CALL targets and splice one BESM_STMT_SUBP (rendered as
// `внешн .NAME` by emit_bemsh.c) right after the `,name,`/старт for each distinct one.
// Excludes the function's own label (a recursive call stays a local reference) and any name
// already declared external (b$ret and globals, from the pre-selection SUBP scan).  The new
// SUBPs are spliced in behind the forward scan cursor, so they are never re-visited and no
// bound on the number of callees is needed.  Bemsh-only; Madlen/Unix rely on their assembler.
static void bemsh_declare_call_targets(Besm_Func *func, const char *self_name)
{
    Besm_Block *first = func->blocks;
    if (!first || !first->body)
        return;
    Besm_Instr *name_node = first->body; // the ,name, (старт) — always the first instruction

    StringMap declared;
    map_init(&declared);
    intptr_t dummy;
    map_insert(&declared, self_name, 1, 0);
    for (const Besm_Block *b = func->blocks; b; b = b->next)
        for (const Besm_Instr *i = b->body; i; i = i->next)
            if (i->kind == BESM_STMT_SUBP && i->name)
                map_insert(&declared, i->name, 1, 0);

    Besm_Instr *ins = name_node; // insertion cursor: keeps declarations in call order
    for (const Besm_Block *b = func->blocks; b; b = b->next) {
        for (const Besm_Instr *i = b->body; i; i = i->next) {
            if (i->kind != BESM_BRANCH_CALL || !i->name)
                continue;
            if (map_get(&declared, i->name, &dummy))
                continue;
            map_insert(&declared, i->name, 1, 0);
            Besm_Instr *subp = besm_new_instr(BESM_STMT_SUBP);
            subp->name       = xstrdup(i->name);
            subp->next       = ins->next;
            ins->next        = subp;
            ins              = subp;
        }
    }
    map_destroy(&declared);
}

static void codegen_function(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out,
                             Besm_Dialect dialect)
{
    const char *name = tl->u.function.name;

    int num_params = 0;
    for (const Tac_Param *p = tl->u.function.params; p; p = p->next)
        num_params++;
    bool needs_param_setup = (num_params >= 2) || tl->u.function.variadic;
    bool is_empty          = (tl->u.function.body == NULL);

    Besm_Module *module = besm_new_module(name);
    Besm_Func *func     = besm_new_func(name, BESM_CC_BESM6_C);
    func->global        = tl->u.function.global;
    module->funcs       = func;

    Besm_Block *block = besm_new_block();
    func->blocks      = block;

    Besm_Instr *tail   = NULL;
    Frame *f           = NULL; // built for non-empty functions; passed to the peephole pass
    Besm_Instr *utm_sp = NULL; // prologue stack-extension; shrunk/dropped after peephole

    Besm_Instr *iname = emit(block, &tail, BESM_STMT_NAME);
    iname->name       = xstrdup(name);

    if (is_empty) {
        // Optimized prologue for empty functions: no b/save or b/ret.
        // The `program` entry is a Dubna-monitor convention (Madlen/Bemsh); the Unix
        // b6as path gets its entry point from crt0/the linker and must not emit it —
        // it would also collide with a user function named `program`.
        if (strcmp(name, "main") == 0 && dialect != BESM_UNIX) {
            Besm_Instr *entry_prog = emit(block, &tail, BESM_STMT_ENTRY);
            entry_prog->name       = xstrdup("program");
        }
        if (needs_param_setup) {
            // 14 ,utc, 1
            Besm_Instr *utc14 = emit(block, &tail, BESM_MOD_UTC);
            utc14->reg        = REG_CNT;
            utc14->addr       = 1;
            // 15 ,utm,
            Besm_Instr *utm15 = emit(block, &tail, BESM_REG_UTM);
            utm15->reg        = REG_SP;
        }
        // 13 ,uj,
        Besm_Instr *uj13 = emit(block, &tail, BESM_BRANCH_UJ);
        uj13->reg        = REG_RET;
        emit(block, &tail, BESM_STMT_END);
    } else {
        // Full prologue: push last argument + capture return address, call b/save,
        // then extend the stack by the number of auto-variable slots.
        //
        // Build the frame early so we can declare SUBP references for static
        // constants before the first instruction that uses them (single-pass assembler).
        f             = frame_build(tl, program);
        int num_autos = frame_num_autos(f);

        // A parameterless _Noreturn function never returns, so the b/save0 prologue's
        // register saving and the b/ret epilogue are pure waste: nothing is ever
        // restored.  Skip them and inline only the frame setup that still matters —
        // R = 7 always, plus r7 = stack top when there are auto/temp slots to address.
        bool noret_no_params = tl->u.function.noret && num_params == 0;

        if (!noret_no_params) {
            Besm_Instr *subp_cret = emit(block, &tail, BESM_STMT_SUBP);
            subp_cret->name       = xstrdup("b$ret");
        }

        // The `program` entry is a Dubna-monitor convention (Madlen/Bemsh); the Unix
        // b6as path gets its entry point from crt0/the linker and must not emit it —
        // it would also collide with a user function named `program`.
        if (strcmp(name, "main") == 0 && dialect != BESM_UNIX) {
            Besm_Instr *entry_prog = emit(block, &tail, BESM_STMT_ENTRY);
            entry_prog->name       = xstrdup("program");
        }

        // Declare each module-level name a function references as a SUBP word.  SUBP
        // allocates no memory; it just tells the assembler the name is external.  It
        // must appear before the first instruction that uses the name (single-pass
        // assembler), so scan every operand of every instruction up front.  A name with
        // a frame slot is a local/param and is skipped; a map avoids duplicates.
        StringMap declared;
        map_init(&declared);
        // A block-scope static local is defined inside this very module (its storage is
        // emitted before the `,end,`), so it must be referenced as a module-local label, not
        // declared external.  Pre-seeding the "declared" set suppresses its SUBP.
        for (const Tac_StaticLocal *sl = tl->u.function.static_locals; sl; sl = sl->next)
            map_insert(&declared, sl->name, 1, 0);
        for (const Tac_Instruction *instr = tl->u.function.body; instr; instr = instr->next) {
            switch (instr->kind) {
            case TAC_INSTRUCTION_RETURN:
                declare_global_operand(block, &tail, f, &declared, instr->u.return_.src);
                break;
            case TAC_INSTRUCTION_COPY:
                declare_global_operand(block, &tail, f, &declared, instr->u.copy.src);
                declare_global_operand(block, &tail, f, &declared, instr->u.copy.dst);
                break;
            case TAC_INSTRUCTION_GET_ADDRESS:
            case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
            case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
                declare_global_operand(block, &tail, f, &declared, instr->u.get_address.src);
                break;
            case TAC_INSTRUCTION_UNARY:
                declare_global_operand(block, &tail, f, &declared, instr->u.unary.src);
                declare_global_operand(block, &tail, f, &declared, instr->u.unary.dst);
                break;
            case TAC_INSTRUCTION_BINARY:
                declare_global_operand(block, &tail, f, &declared, instr->u.binary.src1);
                declare_global_operand(block, &tail, f, &declared, instr->u.binary.src2);
                declare_global_operand(block, &tail, f, &declared, instr->u.binary.dst);
                break;
            case TAC_INSTRUCTION_LOAD:
            case TAC_INSTRUCTION_LOAD_BYTE:
                declare_global_operand(block, &tail, f, &declared, instr->u.load.src_ptr);
                declare_global_operand(block, &tail, f, &declared, instr->u.load.dst);
                break;
            case TAC_INSTRUCTION_STORE:
            case TAC_INSTRUCTION_STORE_BYTE:
                declare_global_operand(block, &tail, f, &declared, instr->u.store.src);
                declare_global_operand(block, &tail, f, &declared, instr->u.store.dst_ptr);
                break;
            case TAC_INSTRUCTION_ADD_PTR:
                declare_global_operand(block, &tail, f, &declared, instr->u.add_ptr.ptr);
                declare_global_operand(block, &tail, f, &declared, instr->u.add_ptr.index);
                declare_global_operand(block, &tail, f, &declared, instr->u.add_ptr.dst);
                break;
            case TAC_INSTRUCTION_PTR_DIFF:
                declare_global_operand(block, &tail, f, &declared, instr->u.ptr_diff.ptr_a);
                declare_global_operand(block, &tail, f, &declared, instr->u.ptr_diff.ptr_b);
                declare_global_operand(block, &tail, f, &declared, instr->u.ptr_diff.dst);
                break;
            case TAC_INSTRUCTION_COPY_TO_OFFSET:
            case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
                declare_global_operand(block, &tail, f, &declared, instr->u.copy_to_offset.src);
                declare_global_name(block, &tail, f, &declared, instr->u.copy_to_offset.dst);
                break;
            case TAC_INSTRUCTION_COPY_FROM_OFFSET:
            case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
                declare_global_name(block, &tail, f, &declared, instr->u.copy_from_offset.src);
                declare_global_operand(block, &tail, f, &declared, instr->u.copy_from_offset.dst);
                break;
            case TAC_INSTRUCTION_JUMP_IF_ZERO:
                declare_global_operand(block, &tail, f, &declared, instr->u.jump_if_zero.condition);
                break;
            case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
                declare_global_operand(block, &tail, f, &declared,
                                       instr->u.jump_if_not_zero.condition);
                break;
            case TAC_INSTRUCTION_FUN_CALL:
            case TAC_INSTRUCTION_FUN_CALL_NORETURN:
                // After copy propagation, a global may appear directly as a FUN_CALL
                // argument (UTC/XTA sequence).
                for (const Tac_Val *a = instr->u.fun_call.args; a; a = a->next)
                    declare_global_operand(block, &tail, f, &declared, a);
                declare_global_operand(block, &tail, f, &declared, instr->u.fun_call.dst);
                // A _Noreturn tail call is emitted as ,uj, <callee>.  Unlike ,call, (which
                // the assembler resolves as an implicit external), a ,uj, to an undefined
                // name is a "undefined identifier" error, so the external callee must be
                // declared SUBP.  A frame-resident name (function pointer) is skipped by
                // declare_global_name.  An indirect call reads its callee's address out of
                // a variable — `,wtc, name` — so that name needs the same declaration.
                if (instr->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN ||
                    instr->u.fun_call.indirect)
                    declare_global_name(block, &tail, f, &declared, instr->u.fun_call.fun_name);
                break;
            // All width/int-FP/pointer-representation conversions share the {src, dst}
            // layout (a union common initial sequence), so one representative member
            // reads either operand.  Their src/dst can be a global (e.g. `(int)glob`
            // for a module-level double), which must be self-declared external too.
            case TAC_INSTRUCTION_SIGN_EXTEND:
            case TAC_INSTRUCTION_TRUNCATE:
            case TAC_INSTRUCTION_ZERO_EXTEND:
            case TAC_INSTRUCTION_DOUBLE_TO_INT:
            case TAC_INSTRUCTION_DOUBLE_TO_UINT:
            case TAC_INSTRUCTION_INT_TO_DOUBLE:
            case TAC_INSTRUCTION_UINT_TO_DOUBLE:
            case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
            case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
            case TAC_INSTRUCTION_INT_TO_FLOAT:
            case TAC_INSTRUCTION_UINT_TO_FLOAT:
            case TAC_INSTRUCTION_FLOAT_TO_INT:
            case TAC_INSTRUCTION_FLOAT_TO_UINT:
            case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
            case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
            case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
            case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
            case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
            case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
            case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
            case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
            case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
            case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
                declare_global_operand(block, &tail, f, &declared, instr->u.double_to_int.src);
                declare_global_operand(block, &tail, f, &declared, instr->u.double_to_int.dst);
                break;
            default:
                break;
            }
        }
        map_destroy(&declared);

        if (noret_no_params) {
            // No b/save0: just establish R = 7 (the mode b/save0 would have left), and
            // when the function has autos, point r7 at the incoming stack top before the
            // utm reserves the auto slots.  No register save area is pushed.
            Besm_Instr *ntr7 = emit(block, &tail, BESM_EXP_SETR);
            ntr7->addr       = 7;

            if (num_autos > 0) {
                Besm_Instr *mtj7 = emit(block, &tail, BESM_MEM_MTJ);
                mtj7->reg        = REG_SP;
                mtj7->addr       = REG_AUTO;
            }
        } else {
            Besm_Instr *its13 = emit(block, &tail, BESM_MEM_ITS);
            its13->addr       = REG_RET;

            Besm_Instr *call_csave = emit(block, &tail, BESM_BRANCH_CALL);
            call_csave->name       = xstrdup(num_params == 0 ? "b$save0" : "b$save");
        }

        if (num_autos > 0) {
            utm_sp       = emit(block, &tail, BESM_REG_UTM);
            utm_sp->reg  = REG_SP;
            utm_sp->addr = num_autos;
        }

        for (const Tac_Instruction *instr = tl->u.function.body; instr; instr = instr->next)
            codegen_instr(instr, f, block, &tail);

        // A _Noreturn function never reaches its epilogue, and with no b/save there is
        // nothing for b/ret to restore — omit the epilogue jump entirely.
        if (!noret_no_params) {
            Besm_Instr *uj_cret = emit(block, &tail, BESM_BRANCH_UJ);
            uj_cret->name       = xstrdup("b$ret");
        }
        emit(block, &tail, BESM_STMT_END);
    }

    // Final polish: peephole-optimize the selected instruction stream before emission.
    // The frame (NULL for empty functions) lets the pass classify temporary slots for
    // dead temp-store elimination; it is freed once the pass no longer needs it.
    besm_peephole(func, f);

    // Peephole may have eliminated every reference to one or more top auto slots, so
    // the prologue stack extension (sized before selection) can now reserve dead words.
    // Shrink it to the slots still in use, dropping the `utm` entirely when none remain.
    if (utm_sp != NULL) {
        int used = used_auto_words(func, f);
        if (used < utm_sp->addr) {
            if (used == 0)
                remove_instr(block, utm_sp);
            else
                utm_sp->addr = used;
        }
    }

    if (f)
        frame_free(f);

    // Bemsh needs an explicit `внешн` for each call target (its `пв` does not auto-declare).
    // Run after selection/peephole so only surviving calls are declared; Bemsh-only.
    if (dialect == BESM_BEMSH)
        bemsh_declare_call_targets(func, name);

    // Emit this function's block-scope static locals as module-local labeled data, spliced
    // in after the code (before `,end,`).  Done before folding string constants so that a
    // static-local initializer referencing a string literal gets that string folded in too.
    besm_emit_static_locals(module, tl, dialect);

    // Fold any string literals this function references into its module as local
    // labels, removing their external SUBP declarations.
    besm_fold_string_constants(module, program, dialect);

    besm_emit_module(out, module, dialect);
    besm_free_module(module);
}
