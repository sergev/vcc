// ============================================================================
// optimize.c — the machine-independent TAC optimization pipeline.
//
// No single pass is sufficient on its own; the four passes form a virtuous
// cycle and amplify one another:
//
//   - Constant folding produces constants that copy propagation can substitute
//     into expressions, which constant folding can then evaluate again.
//   - Constant folding turns conditional jumps into unconditional ones, creating
//     unreachable blocks that unreachable-code elimination can remove.
//   - Copy propagation eliminates the variable in a copy's destination, turning
//     the copy into a dead store that dead-store elimination can remove.
//   - Dead-store elimination removes instructions, which may make previously
//     reachable blocks empty, which unreachable-code elimination can clean up.
//
// Because the passes feed each other, the optimizer runs them in a loop until
// the instruction list stops changing (a fixed point). Within one iteration the
// pass order is fixed: constant folding runs first — it is the only pass that
// works on the flat instruction list and needs no CFG — and the remaining three
// run on the CFG in the order unreachable → copy-prop → dead-store, so each can
// exploit what the previous one produced in the same iteration.
//
// See docs/TAC_Optimization.md §"The optimization pipeline".
// ============================================================================

#include "optimize.h"

#include "cfg.h"
#include "string_map.h"

// Pass entry points, implemented in the sibling translation units.
Tac_Instruction *constant_fold(Tac_Instruction *body);
void eliminate_unreachable(OptCfg *cfg);
void propagate_copies(OptCfg *cfg, const Tac_TopLevel *fn);
void eliminate_dead_stores(const OptCfg *cfg, const Tac_TopLevel *fn);

// Process-global trace switch (see optimize.h). Default off.
int optimize_debug;

// Print one instruction under `prefix`, gated by optimize_debug. tac_print_instruction
// emits its own trailing newline, so the line reads "<prefix> <instruction>".
void opt_trace_instr(const char *prefix, const Tac_Instruction *ins)
{
    if (!optimize_debug)
        return;
    printf("%s ", prefix);
    if (ins)
        tac_print_instruction(stdout, ins, 0);
    else
        printf("(null)\n");
}

// Default flags: every CLI-toggleable pass is enabled, tracing off. Constant
// folding has no flag — it always runs, to keep the downstream code generators
// simpler.
OptFlags opt_flags_default(void)
{
    return (OptFlags){
        .unreachable_elim = true, .copy_propagation = true, .dead_store_elim = true, .debug = false
    };
}

// Run the pipeline on one function body to a fixed point and return the
// optimized list. The body is transformed in place across iterations; the
// caller owns the returned list. Each TAC_TOPLEVEL_FUNCTION is optimized
// independently (intraprocedural); `fn` is the function's own toplevel, supplying
// its params + locals so the CFG passes can tell private locals from globals.
Tac_Instruction *optimize_function(Tac_Instruction *body, OptFlags flags, const Tac_TopLevel *fn)
{
    if (!body)
        return NULL;

    optimize_debug = flags.debug ? 1 : 0;

    int iter = 0;
    for (;;) {
        iter++;
        OPT_TRACE("[optimize] iteration %d\n", iter);

        // Constant folding first, on the flat list (no CFG required).
        OPT_TRACE("[optimize] running pass: const-fold\n");
        body = constant_fold(body);

        // Split into basic blocks for the three CFG-based passes.
        OptCfg *cfg = cfg_build(body);
        OPT_TRACE("[optimize] cfg built: %d blocks\n", cfg->nblocks);

        if (flags.unreachable_elim) {
            OPT_TRACE("[optimize] running pass: unreachable-elim\n");
            eliminate_unreachable(cfg);
        } else {
            OPT_TRACE("[optimize] pass unreachable-elim: skipped (disabled)\n");
        }
        if (flags.copy_propagation) {
            OPT_TRACE("[optimize] running pass: copy-prop\n");
            propagate_copies(cfg, fn);
        } else {
            OPT_TRACE("[optimize] pass copy-prop: skipped (disabled)\n");
        }
        if (flags.dead_store_elim) {
            OPT_TRACE("[optimize] running pass: dead-store-elim\n");
            eliminate_dead_stores(cfg, fn);
        } else {
            OPT_TRACE("[optimize] pass dead-store-elim: skipped (disabled)\n");
        }

        // A pass (e.g. dead_store_elim) may remove the entry block's first
        // instruction, freeing it and leaving `body` dangling. When that
        // happens the entry block's `first` no longer equals `body`. We must
        // detect this *before* cfg_flatten, because afterward we may neither
        // dereference `body` (use-after-free in tac_compare_instruction) nor
        // free it again (double free).
        bool body_freed = (cfg->blocks[0]->first != body);

        // Rejoin the (possibly modified) blocks into a flat list.
        Tac_Instruction *new_body = cfg_flatten(cfg);
        cfg_free(cfg);

        // An empty result is also a terminal condition: nothing left to iterate.
        if (!new_body) {
            OPT_TRACE("[optimize] converged (empty body) after %d iteration(s)\n", iter);
            return new_body;
        }

        // Fixed point: when this iteration produced a structurally identical
        // list, the loop has converged and we return. Otherwise free the now-
        // superseded old body and iterate again on the new one. (When the entry
        // instruction was freed by a pass, `body` is already gone, so we skip
        // both the comparison and the free.)
        if (!body_freed && tac_compare_instruction(new_body, body)) {
            OPT_TRACE("[optimize] fixed point reached after %d iteration(s)\n", iter);
            return new_body;
        }
        if (body_freed) {
            OPT_TRACE("[optimize] entry instruction freed by a pass; iterating\n");
        } else {
            OPT_TRACE("[optimize] body changed; iterating\n");
            tac_free_instruction(body);
        }
        body = new_body;
    }
}

static void note_used(const char *name, void *arg)
{
    map_insert((StringMap *)arg, name, 1, 0);
}

void optimize_prune_locals(Tac_TopLevel *fn)
{
    StringMap used;
    map_init(&used);
    for (const Tac_Instruction *in = fn->u.function.body; in; in = in->next)
        tac_visit_names(in, note_used, &used);

    Tac_Param **pp = &fn->u.function.locals;
    while (*pp) {
        Tac_Param *p = *pp;
        if (map_get(&used, p->name, NULL)) {
            pp = &p->next;
            continue;
        }
        *pp     = p->next;
        p->next = NULL;
        tac_free_param(p);
    }
    map_destroy(&used);
}
