//
// Control flow.  TAC jumps to labels; wasm only branches out of an enclosing block (to
// its end) or loop (to its start).  A reducible graph becomes blocks, loops and ifs by
// Ramsey's translation ("Beyond Relooper", ICFP 2022); any graph, an irreducible one
// included, the dispatch skeleton.
//
// Ramsey's translation walks the dominator tree, the blocks numbered in reverse
// postorder.  A block with a backward jump into it heads a loop around its subtree.  A
// merge node, a block with two forward jumps into it or more, follows a wasm block
// around the code of the earlier parts of its dominator's subtree, the latest one
// outermost, so a jump to it leaves that block.  Any other block has a single forward
// predecessor, which dominates it, and is placed right at that jump.  A conditional
// jump is an if whose arms are the two jumps.  A jump finds its target in the context,
// the stack of enclosing constructs: a backward one continues its loop, a forward one
// to a merge node leaves its block.
//
// The skeleton: the basic blocks in their order, block i's code just after the end of
// a wasm block B_i, with B_i enclosing every earlier block's code:
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
#include <stdlib.h>

#include "flow.h"
#include "internal.h"
#include "xalloc.h"

bool wasm_structure = true;

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

// The dispatch skeleton.
static void gen_dispatch(Gen *g)
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

//
// Ramsey's translation.
//
enum { CTX_IF, CTX_LOOP, CTX_BLOCK };

typedef struct {
    Gen *g;
    const Flow *f;
    int n;
    int *rpo;     // block → its number in reverse postorder, -1 when unreachable
    int *order;   // number → block
    int count;    // reachable blocks
    int *idom;    // block → its immediate dominator
    int *nfwd;    // block → forward jumps into it
    bool *header; // block → a backward jump into it: a loop header
    int *kids;    // scratch for merge children
    struct {
        int kind, block;
    } *ctx;
    int nctx;
} Structure;

static int intersect(const Structure *s, int a, int b)
{
    while (a != b) {
        while (s->rpo[a] > s->rpo[b])
            a = s->idom[a];
        while (s->rpo[b] > s->rpo[a])
            b = s->idom[b];
    }
    return a;
}

static bool dominates(const Structure *s, int d, int b)
{
    for (;;) {
        if (b == d)
            return true;
        if (b == 0)
            return false;
        b = s->idom[b];
    }
}

// Number the blocks, find dominators, loop headers and merge nodes; false when the
// graph is irreducible (a backward jump to a block that does not dominate its source).
static bool analyze(Structure *s)
{
    const Flow *f = s->f;
    int n         = s->n;
    for (int b = 0; b < n; b++)
        s->rpo[b] = s->idom[b] = -1;

    // Postorder by an explicit depth-first walk, then reversed.
    int *stack = xalloc((n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    int *next  = xalloc((n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    int *post  = xalloc((n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    bool *seen = xalloc((n + 1) * sizeof(bool), __func__, __FILE__, __LINE__);
    for (int b = 0; b < n; b++)
        seen[b] = false;
    int sp = 0, np = 0;
    stack[sp] = 0;
    next[sp++] = 0;
    seen[0]    = true;
    while (sp) {
        int b = stack[sp - 1];
        if (next[sp - 1] < f->blocks[b].nsucc) {
            int t = f->blocks[b].succ[next[sp - 1]++];
            if (!seen[t]) {
                seen[t]    = true;
                stack[sp]  = t;
                next[sp++] = 0;
            }
        } else {
            post[np++] = b;
            sp--;
        }
    }
    s->count = np;
    for (int i = 0; i < np; i++) {
        s->order[i]             = post[np - 1 - i];
        s->rpo[post[np - 1 - i]] = i;
    }
    xfree(stack);
    xfree(next);
    xfree(post);
    xfree(seen);

    // Dominators (Cooper, Harvey and Kennedy), over the predecessors.
    int *start = xalloc((n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    int *preds = xalloc((2 * n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    for (int b = 0; b <= n; b++)
        start[b] = 0;
    for (int i = 0; i < s->count; i++)
        for (int k = 0; k < f->blocks[s->order[i]].nsucc; k++)
            start[f->blocks[s->order[i]].succ[k] + 1]++;
    for (int b = 0; b < n; b++)
        start[b + 1] += start[b];
    int *fill = xalloc((n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    for (int b = 0; b < n; b++)
        fill[b] = start[b];
    for (int i = 0; i < s->count; i++) {
        int p = s->order[i];
        for (int k = 0; k < f->blocks[p].nsucc; k++)
            preds[fill[f->blocks[p].succ[k]]++] = p;
    }
    xfree(fill);
    s->idom[0]   = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i < s->count; i++) {
            int b = s->order[i], nd = -1;
            for (int j = start[b]; j < start[b + 1]; j++) {
                int p = preds[j];
                if (s->idom[p] >= 0)
                    nd = nd < 0 ? p : intersect(s, p, nd);
            }
            if (nd != s->idom[b]) {
                s->idom[b] = nd;
                changed    = true;
            }
        }
    }
    xfree(start);
    xfree(preds);

    // Loop headers and merge nodes.
    for (int b = 0; b < n; b++) {
        s->nfwd[b]   = 0;
        s->header[b] = false;
    }
    for (int i = 0; i < s->count; i++) {
        int p = s->order[i];
        for (int k = 0; k < f->blocks[p].nsucc; k++) {
            int t = f->blocks[p].succ[k];
            if (s->rpo[t] <= s->rpo[p]) {
                if (!dominates(s, t, p))
                    return false;
                s->header[t] = true;
            } else {
                s->nfwd[t]++;
            }
        }
    }
    return true;
}

static void push_ctx(Structure *s, int kind, int block)
{
    s->ctx[s->nctx].kind    = kind;
    s->ctx[s->nctx++].block = block;
}

// The depth of a br to the construct of `kind` for `block` in the context.
static int ctx_depth(const Structure *s, int kind, int block)
{
    for (int i = s->nctx - 1; i >= 0; i--)
        if (s->ctx[i].kind == kind && s->ctx[i].block == block)
            return s->nctx - 1 - i;
    fatal_error("wasm: %s: no enclosing construct for block %d", s->g->fn->name, block);
}

static void do_tree(Structure *s, int x);

static void do_branch(Structure *s, int from, int to)
{
    if (s->rpo[to] <= s->rpo[from])
        wasm_append(s->g->fn, WASM_BR)->imm = ctx_depth(s, CTX_LOOP, to);
    else if (s->nfwd[to] >= 2)
        wasm_append(s->g->fn, WASM_BR)->imm = ctx_depth(s, CTX_BLOCK, to);
    else
        do_tree(s, to);
}

// Whether going from block x to block `to` is a bare br: backward, or to a merge node.
static bool is_br(const Structure *s, int from, int to)
{
    return s->rpo[to] <= s->rpo[from] || s->nfwd[to] >= 2;
}

// Running on from the end of block x: into the next, or off the end of the function.
static void do_fall(Structure *s, int x)
{
    if (x + 1 < s->n)
        do_branch(s, x, x + 1);
    else if (s->g->fn->result == WASM_VOID)
        gen_return(s->g, NULL);
    else
        wasm_append(s->g->fn, WASM_UNREACHABLE);
}

static int target_of(const Gen *g, const char *label)
{
    intptr_t b;
    if (!map_get(&g->labels, label, &b))
        fatal_error("wasm: %s: no label %s", g->fn->name, label);
    return (int)b;
}

// Block x's code and its jumps, inside the blocks of merge nodes ys[0..k-1] (in
// reverse postorder, the last outermost).
static void node_within(Structure *s, int x, const int *ys, int k)
{
    Gen *g        = s->g;
    Wasm_Func *fn = g->fn;
    if (k > 0) {
        int y = ys[k - 1];
        wasm_append(fn, WASM_BLOCK);
        push_ctx(s, CTX_BLOCK, y);
        node_within(s, x, ys, k - 1);
        s->nctx--;
        wasm_append(fn, WASM_END_BLOCK);
        do_tree(s, y);
        return;
    }
    const Flow_Block *blk     = &s->f->blocks[x];
    const Tac_Instruction *in = s->f->instrs[blk->last];
    bool jump = in->kind == TAC_INSTRUCTION_JUMP || in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO ||
                in->kind == TAC_INSTRUCTION_JUMP_IF_NOT_ZERO;
    g->cur = x;
    for (int i = blk->first; i <= blk->last - (jump ? 1 : 0); i++)
        gen_instr(g, s->f->instrs[i]);
    switch (in->kind) {
    case TAC_INSTRUCTION_JUMP:
        do_branch(s, x, target_of(g, in->u.jump.target));
        return;
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO: {
        int t = target_of(g, in->u.jump_if_zero.target), e = x + 1;
        bool if_zero = in->kind == TAC_INSTRUCTION_JUMP_IF_ZERO;
        if (t == e) { // both ways to one place
            do_branch(s, x, t);
            return;
        }
        if (is_br(s, x, t) || (e < s->n && is_br(s, x, e))) {
            // One way is a bare br: a br_if, then the other way.
            bool to_t = is_br(s, x, t);
            push_condition(g, in->u.jump_if_zero.condition, to_t ? if_zero : !if_zero);
            int to = to_t ? t : e;
            wasm_append(fn, WASM_BR_IF)->imm =
                s->rpo[to] <= s->rpo[x] ? ctx_depth(s, CTX_LOOP, to) : ctx_depth(s, CTX_BLOCK, to);
            if (to_t)
                do_fall(s, x);
            else
                do_branch(s, x, t);
            return;
        }
        push_condition(g, in->u.jump_if_zero.condition, if_zero);
        wasm_append(fn, WASM_IF);
        push_ctx(s, CTX_IF, x);
        do_branch(s, x, t);
        wasm_append(fn, WASM_ELSE);
        do_fall(s, x);
        s->nctx--;
        wasm_append(fn, WASM_END_IF);
        return;
    }
    case TAC_INSTRUCTION_RETURN:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        return;
    default:
        do_fall(s, x);
        return;
    }
}

static const int *by_rpo_table;
static int by_rpo(const void *a, const void *b)
{
    return by_rpo_table[*(const int *)a] - by_rpo_table[*(const int *)b];
}

static void do_tree(Structure *s, int x)
{
    // The merge nodes x immediately dominates, in reverse postorder.
    int k   = 0;
    int *ys = xalloc((s->n + 1) * sizeof(int), __func__, __FILE__, __LINE__);
    for (int i = 0; i < s->count; i++) {
        int y = s->order[i];
        if (y != x && s->idom[y] == x && s->nfwd[y] >= 2)
            ys[k++] = y;
    }
    by_rpo_table = s->rpo;
    qsort(ys, k, sizeof(int), by_rpo);
    if (s->header[x]) {
        wasm_append(s->g->fn, WASM_LOOP);
        push_ctx(s, CTX_LOOP, x);
        node_within(s, x, ys, k);
        s->nctx--;
        wasm_append(s->g->fn, WASM_END_LOOP);
    } else {
        node_within(s, x, ys, k);
    }
    xfree(ys);
}

// Ramsey's translation of the body, or false (and nothing emitted) when its graph is
// irreducible.
static bool gen_structured(Gen *g)
{
    Structure s = { .g = g, .f = g->flow, .n = g->flow->nblocks };
    int n       = s.n + 1;
    s.rpo       = xalloc(n * sizeof(int), __func__, __FILE__, __LINE__);
    s.order     = xalloc(n * sizeof(int), __func__, __FILE__, __LINE__);
    s.idom      = xalloc(n * sizeof(int), __func__, __FILE__, __LINE__);
    s.nfwd      = xalloc(n * sizeof(int), __func__, __FILE__, __LINE__);
    s.header    = xalloc(n * sizeof(bool), __func__, __FILE__, __LINE__);
    s.ctx       = xalloc(3 * n * sizeof(*s.ctx), __func__, __FILE__, __LINE__);
    bool ok     = analyze(&s);
    if (ok) {
        map_init(&g->labels);
        for (int b = 0; b < s.n; b++)
            for (int i = s.f->blocks[b].first; i <= s.f->blocks[b].last; i++)
                if (s.f->instrs[i]->kind == TAC_INSTRUCTION_LABEL)
                    map_insert(&g->labels, s.f->instrs[i]->u.label.name, b, 0);
        do_tree(&s, 0);
        map_destroy(&g->labels);
    }
    xfree(s.rpo);
    xfree(s.order);
    xfree(s.idom);
    xfree(s.nfwd);
    xfree(s.header);
    xfree(s.ctx);
    return ok;
}

void gen_body(Gen *g)
{
    if (!wasm_structure || g->flow->nblocks == 0 || !gen_structured(g))
        gen_dispatch(g);
}
