//
// Control flow.  TAC jumps to labels; wasm only branches out of an enclosing block (to
// its end) or loop (to its start).  Stage 1, here, is a skeleton that takes any graph:
// the basic blocks in their order, block i's code just after the end of a wasm block
// B_i, with B_i enclosing every earlier block's code:
//
//     loop                        only with a backward jump
//       block B_n-1
//         ...
//           block B_0             only with a backward jump
//             local.get state
//             br_table {0, 1, ..., n-1, 0}
//           end_block
//           <code of block 0>
//         end_block               B_1
//         <code of block 1>
//       ...
//       end_block                 B_n-1
//       <code of block n-1>
//     end_loop
//
// A forward jump from block i to block j leaves B_j, which encloses i's code: br j-i-1.
// A backward one sets the state to j and restarts the loop, br n-1-i, whose br_table
// leaves B_j.  Falling through is running into the next block's code.
//
#include "flow.h"
#include "internal.h"
#include "xalloc.h"

// The block a jump to label `target` goes to.
static int target_block(const Gen *g, const char *target)
{
    intptr_t b;
    if (!map_get(&g->labels, target, &b))
        fatal_error("wasm: %s: no label %s", g->fn->name, target);
    return (int)b;
}

void gen_branch_setup(Gen *g, const char *target)
{
    int j = target_block(g, target);
    if (j <= g->cur) {
        wasm_append(g->fn, WASM_I32_CONST)->imm = j;
        wasm_append(g->fn, WASM_LOCAL_SET)->imm = g->state;
    }
}

void gen_branch(Gen *g, const char *target, bool conditional)
{
    int j     = target_block(g, target);
    int depth = j > g->cur ? j - g->cur - 1 : g->nblocks - 1 - g->cur;
    wasm_append(g->fn, conditional ? WASM_BR_IF : WASM_BR)->imm = depth;
}

void gen_body(Gen *g)
{
    Flow *f    = g->flow;
    g->nblocks = f->nblocks;
    map_init(&g->labels);
    for (int b = 0; b < f->nblocks; b++)
        for (int i = f->blocks[b].first; i <= f->blocks[b].last; i++)
            if (f->instrs[i]->kind == TAC_INSTRUCTION_LABEL)
                map_insert(&g->labels, f->instrs[i]->u.label.name, b, 0);

    // A backward jump needs the loop and the state.
    bool loop = false;
    for (int b = 0; b < f->nblocks; b++)
        for (int k = 0; k < f->blocks[b].nsucc; k++)
            if (f->blocks[b].succ[k] <= b)
                loop = true;
    Wasm_Func *fn = g->fn;
    if (loop) {
        g->state = wasm_add_local(fn, WASM_I32);
        wasm_append(fn, WASM_LOOP);
    }
    for (int b = f->nblocks - 1; b >= (loop ? 0 : 1); b--)
        wasm_append(fn, WASM_BLOCK);
    if (loop) {
        wasm_append(fn, WASM_LOCAL_GET)->imm = g->state;
        Wasm_Instr *in                       = wasm_append(fn, WASM_BR_TABLE);
        in->ntable                           = f->nblocks + 1;
        in->table = xalloc(in->ntable * sizeof(int), __func__, __FILE__, __LINE__);
        for (int b = 0; b < f->nblocks; b++)
            in->table[b] = b;
        in->table[f->nblocks] = 0;
    }
    for (int b = 0; b < f->nblocks; b++) {
        if (b > 0 || loop)
            wasm_append(fn, WASM_END_BLOCK);
        g->cur = b;
        for (int i = f->blocks[b].first; i <= f->blocks[b].last; i++)
            gen_instr(g, f->instrs[i]);
    }
    if (loop)
        wasm_append(fn, WASM_END_LOOP);
    map_destroy(&g->labels);
}
