// ============================================================================
// ivsr.c — induction-variable strength reduction of array addressing in loops.
//
// In
//
//     for (j = 0; j < n; j++)  ... v[j] ... v[j + 1] ...
//
// every iteration computes v + j*s for each subscript. This pass gives each
// such address its own pointer q, kept equal to v + j*s at every point of the
// loop: set ahead of the loop, stepped by c*s right after j steps by c. A
// subscript then reads q (v[j]) or q plus a constant (v[j + 1] = q + s), and
// the multiply and the add leave the loop. Copy propagation and dead-store
// elimination clean up after it.
//
//   - Loops are the natural loops of the CFG: a back edge b → h, h dominating
//     b, and the blocks that reach b without passing h. Their preheader is the
//     one predecessor of h outside the loop; with none or several, the loop is
//     left alone. The rotated loops the translator builds have one: the guard.
//   - A basic induction variable j is a private, not address-taken integer
//     with one definition in the loop: j = j ± c, or j = t with t = j ± c the
//     one definition of t, dominating the copy.
//   - A reduced address is ADD_PTR(v, x, s) with v private, not address-taken
//     and not defined in the loop, and x equal to j plus a constant where it is
//     used:
//         j                      → q
//         t,  t = j ± c          → q ± c*s
//         sign_extend(j or t)    → as above, for a signed j only: its
//                                  overflow is undefined, so the extension of
//                                  j + c is the extension of j, plus c
//     A step of j between the computation of x and its use is taken back from
//     the constant; whether j steps in between is decided by dominance and by
//     reachability within one iteration. An index of j's own width needs no
//     care about overflow: the address wraps with it.
//   - Linear-function test replacement: a loop test `x op bound`, bound
//     invariant, becomes a comparison of q + c*s with the end pointer
//     v + bound*s, formed ahead of the loop; then j goes when only its own step
//     reads it.
//
// The pass runs at the fixed point of the scalar passes, which then run again
// when it changed something: a loop bound is invariant only once CSE and copy
// propagation have found its one computation.
//
// The new pointers are typed temporaries, added to the function's locals.
// The pass is idempotent: a reduced ADD_PTR reads q, which the loop defines,
// so it is no candidate the next round.
//
// See docs/TAC_Optimization.md §"Induction variables".
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "alias.h"
#include "cfg.h"
#include "dataflow.h"
#include "optimize.h"
#include "string_map.h"
#include "tac.h"
#include "target.h"
#include "xalloc.h"

#define XALLOC(n, t) ((t *)xalloc((n) * sizeof(t), __func__, __FILE__, __LINE__))

// The variable an instruction defines, or NULL.
static const char *def_name(const Tac_Instruction *in)
{
    const Tac_Val *d;
    if (in->kind == TAC_INSTRUCTION_COPY)
        d = in->u.copy.dst;
    else if (in->kind == TAC_INSTRUCTION_FUN_CALL || in->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN)
        d = in->u.fun_call.dst;
    else
        d = opt_defining_dst(in);
    return d && d->kind == TAC_VAL_VAR ? d->u.var_name : NULL;
}

static bool is_var(const Tac_Val *v)
{
    return v && v->kind == TAC_VAL_VAR;
}

// The integer value of constant `v`, false when it is not an integer constant.
static bool int_const(const Tac_Val *v, long long *out)
{
    if (!v || v->kind != TAC_VAL_CONSTANT)
        return false;
    const Tac_Const *c = v->u.constant;
    switch (c->kind) {
    case TAC_CONST_INT:
        *out = c->u.int_val;
        return true;
    case TAC_CONST_LONG:
        *out = c->u.long_val;
        return true;
    case TAC_CONST_LONG_LONG:
        *out = c->u.long_long_val;
        return true;
    case TAC_CONST_UINT:
        *out = (long long)c->u.uint_val;
        return true;
    case TAC_CONST_ULONG:
        *out = (long long)c->u.ulong_val;
        return true;
    case TAC_CONST_ULONG_LONG:
        *out = (long long)c->u.ulong_long_val;
        return true;
    default:
        return false;
    }
}

// The constant kind of an integer type an index may have, or -1.
static int const_kind(const Tac_Type *t)
{
    if (!t)
        return -1;
    switch (t->kind) {
    case TAC_TYPE_INT:
        return TAC_CONST_INT;
    case TAC_TYPE_LONG:
        return TAC_CONST_LONG;
    case TAC_TYPE_LONG_LONG:
        return TAC_CONST_LONG_LONG;
    case TAC_TYPE_UINT:
        return TAC_CONST_UINT;
    case TAC_TYPE_ULONG:
        return TAC_CONST_ULONG;
    case TAC_TYPE_ULONG_LONG:
        return TAC_CONST_ULONG_LONG;
    default:
        return -1;
    }
}

static bool is_signed_kind(int k)
{
    return k == TAC_CONST_INT || k == TAC_CONST_LONG || k == TAC_CONST_LONG_LONG;
}

// An integer constant of `kind`, an unsigned one wrapped to the target's width.
static Tac_Val *new_const(int kind, long long value)
{
    size_t size = kind == TAC_CONST_UINT    ? target_config->int_size
                  : kind == TAC_CONST_ULONG ? target_config->long_size
                                            : target_config->llong_size;
    if (!is_signed_kind(kind))
        value = (long long)unsigned_narrow((uint64_t)value, (int)size * 8);
    Tac_Const *c = tac_new_const((Tac_ConstKind)kind);
    switch (kind) {
    case TAC_CONST_INT:
        c->u.int_val = value;
        break;
    case TAC_CONST_LONG:
        c->u.long_val = (long)value;
        break;
    case TAC_CONST_LONG_LONG:
        c->u.long_long_val = value;
        break;
    case TAC_CONST_UINT:
        c->u.uint_val = (uint64_t)value;
        break;
    case TAC_CONST_ULONG:
        c->u.ulong_val = (unsigned long)value;
        break;
    default:
        c->u.ulong_long_val = (unsigned long long)value;
        break;
    }
    Tac_Val *v    = tac_new_val(TAC_VAL_CONSTANT);
    v->u.constant = c;
    return v;
}

static Tac_Val *new_var(const char *name)
{
    Tac_Val *v    = tac_new_val(TAC_VAL_VAR);
    v->u.var_name = xstrdup(name);
    return v;
}

// ============================================================================
// Pass state
// ============================================================================

typedef struct {
    OptCfg *cfg;
    Tac_TopLevel *fn;
    OptPreds preds;
    int n;
    bool *reach;           // reachable from the entry
    unsigned char *dom;    // dom[b * n + d]: d dominates b
    StringMap types;       // private name → its Tac_Type
    StringMap address_taken;
    StringMap names;       // every name and label of the function, for fresh names
    int next_temp;
    int h;                 // the loop at work: its header,
    const bool *in_loop;   // and its blocks
    int *stack;            // scratch for reaches()
    bool *seen;
} Ivsr;

// The instruction before `in` in block `b`, NULL when `in` is first.
static Tac_Instruction *prev_of(const OptBlock *b, const Tac_Instruction *in)
{
    Tac_Instruction *p = NULL;
    for (Tac_Instruction *i = b->first; i && i != in; i = i->next)
        p = i;
    return p;
}

static void insert_after(OptBlock *b, Tac_Instruction *at, Tac_Instruction *in)
{
    in->next = at->next;
    at->next = in;
    if (b->last == at)
        b->last = in;
}

static void insert_before(OptBlock *b, Tac_Instruction *at, Tac_Instruction *in)
{
    Tac_Instruction *p = prev_of(b, at);
    in->next           = at;
    if (p)
        p->next = in;
    else
        b->first = in;
}

// Insert `in` at the end of block `b`: ahead of the jump it ends in, and ahead of the
// computation of a conditional jump's condition right before it, which the code
// generators fuse with the jump.
static void insert_at_end(OptBlock *b, Tac_Instruction *in)
{
    Tac_Instruction *last = b->last;
    if (!last) {
        b->first = b->last = in;
        in->next           = NULL;
        return;
    }
    const Tac_Val *cond = last->kind == TAC_INSTRUCTION_JUMP_IF_ZERO   ? last->u.jump_if_zero.condition
                          : last->kind == TAC_INSTRUCTION_JUMP_IF_NOT_ZERO ? last->u.jump_if_not_zero.condition
                                                                         : NULL;
    if (!cond && last->kind != TAC_INSTRUCTION_JUMP) {
        insert_after(b, last, in);
        return;
    }
    Tac_Instruction *at = last, *p = prev_of(b, last);
    const char *d       = p ? def_name(p) : NULL;
    if (is_var(cond) && d && !strcmp(d, cond->u.var_name) && p->kind == TAC_INSTRUCTION_BINARY)
        at = p;
    insert_before(b, at, in);
}

// A fresh temporary of type `type` in name[32], added to the function's locals.
static void fresh_temp(Ivsr *s, const Tac_Type *type, char *name)
{
    do
        snprintf(name, 32, "%%%d", s->next_temp++);
    while (map_get(&s->names, name, NULL));
    map_insert(&s->names, name, 1, 0);
    Tac_Param *p = tac_new_param();
    p->name      = xstrdup(name);
    p->type      = tac_clone_type(type);
    p->next      = s->fn->u.function.locals;
    s->fn->u.function.locals = p;
    map_insert(&s->types, name, (intptr_t)p->type, 0);
}

static const Tac_Type *type_of(const Ivsr *s, const char *name)
{
    intptr_t t = 0;
    return map_get(&s->types, name, &t) ? (const Tac_Type *)t : NULL;
}

// A private, not address-taken variable.
static bool is_private(const Ivsr *s, const char *name)
{
    return name[0] == '%' && type_of(s, name) && !map_get(&s->address_taken, name, NULL);
}

static void note_name(const char *name, void *arg)
{
    map_insert((StringMap *)arg, name, 1, 0);
}

// ============================================================================
// Dominators: the iterative dataflow, dom(b) = {b} ∪ ∩ dom(p) over the
// reachable predecessors p. Functions are small, so a byte matrix will do.
// ============================================================================

static void compute_dominators(Ivsr *s)
{
    int n    = s->n;
    s->reach = XALLOC(n, bool);
    s->dom   = XALLOC((size_t)n * n, unsigned char);
    for (int i = 0; i < n; i++)
        s->reach[i] = false;
    int *stack = XALLOC(n, int), sp = 0;
    stack[sp++]  = 0;
    s->reach[0]  = true;
    while (sp) {
        const OptBlock *b = s->cfg->blocks[stack[--sp]];
        for (int k = 0; k < b->nsucc; k++)
            if (!s->reach[b->succs[k]->id]) {
                s->reach[b->succs[k]->id] = true;
                stack[sp++]               = b->succs[k]->id;
            }
    }
    xfree(stack);

    for (int b = 0; b < n; b++)
        for (int d = 0; d < n; d++)
            s->dom[b * n + d] = b == 0 ? d == 0 : 1;
    unsigned char *tmp = XALLOC(n, unsigned char);
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = 1; b < n; b++) {
            if (!s->reach[b])
                continue;
            for (int d = 0; d < n; d++)
                tmp[d] = 1;
            for (int k = 0; k < s->preds.npreds[b]; k++) {
                int p = s->preds.preds[b][k];
                if (!s->reach[p])
                    continue;
                for (int d = 0; d < n; d++)
                    tmp[d] &= s->dom[p * n + d];
            }
            tmp[b] = 1;
            if (memcmp(tmp, &s->dom[b * n], n)) {
                memcpy(&s->dom[b * n], tmp, n);
                changed = true;
            }
        }
    }
    xfree(tmp);
}

// ============================================================================
// One loop
// ============================================================================

// Whether instruction `x` comes before `y` in block `b`.
static bool precedes(const OptBlock *b, const Tac_Instruction *x, const Tac_Instruction *y)
{
    for (const Tac_Instruction *i = b->first; i; i = i->next) {
        if (i == y)
            return false;
        if (i == x)
            return true;
    }
    return false;
}

static bool dominates(const Ivsr *s, int a, int b)
{
    return s->dom[b * s->n + a];
}

// One definition in the loop of each name, or NULL for a name defined more than once.
typedef struct {
    StringMap count; // name → number of definitions in the loop
    StringMap def;   // name → its defining instruction
    StringMap block; // name → the block of that instruction
} LoopDefs;

static void collect_defs(const Ivsr *s, const bool *in_loop, LoopDefs *d)
{
    map_init(&d->count);
    map_init(&d->def);
    map_init(&d->block);
    for (int b = 0; b < s->n; b++) {
        if (!in_loop[b])
            continue;
        for (const Tac_Instruction *in = s->cfg->blocks[b]->first; in; in = in->next) {
            const char *name = def_name(in);
            if (!name)
                continue;
            intptr_t c = 0;
            map_get(&d->count, name, &c);
            map_insert(&d->count, name, c + 1, 0);
            map_insert(&d->def, name, (intptr_t)in, 0);
            map_insert(&d->block, name, b, 0);
        }
    }
}

static void free_defs(LoopDefs *d)
{
    map_destroy(&d->count);
    map_destroy(&d->def);
    map_destroy(&d->block);
}

// The one definition of `name` in the loop, its block in *block; NULL if it has none
// or several.
static Tac_Instruction *single_def(const LoopDefs *d, const char *name, int *block)
{
    intptr_t c = 0, in = 0, b = 0;
    if (!map_get(&d->count, name, &c) || c != 1)
        return NULL;
    map_get(&d->def, name, &in);
    map_get(&d->block, name, &b);
    if (block)
        *block = (int)b;
    return (Tac_Instruction *)in;
}

static bool defined_in_loop(const LoopDefs *d, const char *name)
{
    return map_get(&d->count, name, NULL);
}

// `in` is t = j ± c (signed or not); the constant ±c in *step.
static bool is_step(const Tac_Instruction *in, const char *j, long long *step)
{
    if (in->kind != TAC_INSTRUCTION_BINARY || in->is_volatile)
        return false;
    Tac_BinaryOperator op = in->u.binary.op;
    bool add = op == TAC_BINARY_ADD || op == TAC_BINARY_ADD_UNSIGNED;
    bool sub = op == TAC_BINARY_SUBTRACT || op == TAC_BINARY_SUBTRACT_UNSIGNED;
    long long c;
    if (!(add || sub) || !is_var(in->u.binary.src1) || strcmp(in->u.binary.src1->u.var_name, j) ||
        !int_const(in->u.binary.src2, &c))
        return false;
    *step = add ? c : -c;
    return true;
}

// A basic induction variable: its one step in the loop, the instruction after which j
// holds its new value.
typedef struct {
    const char *name;
    Tac_Instruction *def; // j = j ± c, or j = t
    int block;
    long long step;
} BasicIv;

static bool basic_iv(const Ivsr *s, const LoopDefs *d, const char *j, BasicIv *iv)
{
    if (!is_private(s, j) || const_kind(type_of(s, j)) < 0)
        return false;
    int b;
    Tac_Instruction *in = single_def(d, j, &b);
    if (!in || in->is_volatile)
        return false;
    iv->name  = j;
    iv->def   = in;
    iv->block = b;
    if (is_step(in, j, &iv->step))
        return true;
    if (in->kind != TAC_INSTRUCTION_COPY || !is_var(in->u.copy.src))
        return false;
    int tb;
    Tac_Instruction *t = single_def(d, in->u.copy.src->u.var_name, &tb);
    if (!t || !is_step(t, j, &iv->step))
        return false;
    // t computed ahead of the copy in every iteration that copies it: j, whose one
    // definition is the copy, is then the same at both.
    return tb == b ? precedes(s->cfg->blocks[b], t, in) : dominates(s, tb, b);
}

// Whether block `a` reaches block `b` within one iteration of the loop: along a path
// of at least one edge that does not enter the header.
static bool reaches(const Ivsr *s, int a, int b)
{
    for (int i = 0; i < s->n; i++)
        s->seen[i] = false;
    int sp        = 0;
    s->stack[sp++] = a;
    while (sp) {
        const OptBlock *blk = s->cfg->blocks[s->stack[--sp]];
        for (int k = 0; k < blk->nsucc; k++) {
            int t = blk->succs[k]->id;
            if (t == b)
                return true;
            if (t != s->h && s->in_loop[t] && !s->seen[t]) {
                s->seen[t]     = true;
                s->stack[sp++] = t;
            }
        }
    }
    return false;
}

// The steps of j between instruction `from` in block `fb` and a later `to` in block
// `tb` of the same iteration, `from` dominating `to`: 0 or 1; -1 when `from` does not
// dominate `to`, or j steps on some paths between them and not on others.
static int steps_between(const Ivsr *s, const Tac_Instruction *from, int fb,
                         const Tac_Instruction *to, int tb, const BasicIv *iv)
{
    int db = iv->block;
    if (fb == tb) {
        if (!precedes(s->cfg->blocks[fb], from, to))
            return -1;
        // Straight-line code: j steps between them only at a step in between.
        return db == fb && precedes(s->cfg->blocks[fb], from, iv->def) &&
               precedes(s->cfg->blocks[fb], iv->def, to);
    }
    if (tb == s->h || !dominates(s, fb, tb))
        return -1;
    bool after_from = db == fb ? precedes(s->cfg->blocks[fb], from, iv->def) : reaches(s, fb, db);
    bool before_to  = db == tb ? precedes(s->cfg->blocks[tb], iv->def, to) : reaches(s, db, tb);
    if (!after_from || !before_to)
        return 0;
    bool always = (db == fb || dominates(s, fb, db)) && (db == tb || dominates(s, db, tb));
    return always ? 1 : -1;
}

// A value x read by `use` in block `b` as j plus a constant, at the point of `use`:
// the IV in *iv, the constant in *off, and whether x is j sign-extended in *ext.
static bool affine_index(const Ivsr *s, const LoopDefs *d, int b, const Tac_Instruction *use,
                         const Tac_Val *x, BasicIv *iv, long long *off, bool *ext)
{
    if (!is_var(x))
        return false;
    *ext             = false;
    *off             = 0;
    const char *name = x->u.var_name;
    int xb;
    Tac_Instruction *xd = single_def(d, name, &xb);
    const Tac_Instruction *sx = NULL; // the sign extension, in block sb
    int sb                    = -1;
    if (xd && xd->kind == TAC_INSTRUCTION_SIGN_EXTEND && !xd->is_volatile &&
        is_var(xd->u.sign_extend.src)) {
        sx   = xd;
        sb   = xb;
        *ext = true;
        name = xd->u.sign_extend.src->u.var_name;
        xd   = single_def(d, name, &xb);
    }
    // Where j is read: by the use, the extension, or t = j ± c.
    const Tac_Instruction *from = sx ? sx : use;
    int fb                      = sx ? sb : b;
    const char *j               = name;
    if (!basic_iv(s, d, name, iv)) {
        if (!xd || xd->kind != TAC_INSTRUCTION_BINARY || !is_var(xd->u.binary.src1))
            return false;
        j = xd->u.binary.src1->u.var_name;
        if (!basic_iv(s, d, j, iv) || !is_step(xd, j, off))
            return false;
        if (*ext && xd->u.binary.op != TAC_BINARY_ADD && xd->u.binary.op != TAC_BINARY_SUBTRACT)
            return false;
        // The extension reads t as computed: no step between them.
        if (sx && steps_between(s, xd, xb, sx, sb, iv) != 0)
            return false;
        from = xd;
        fb   = xb;
    }
    if (*ext && !is_signed_kind(const_kind(type_of(s, j))))
        return false;
    // q follows j: a step between the read and the use is taken back from the constant.
    if (from != use) {
        int k = steps_between(s, from, fb, use, b, iv);
        if (k < 0)
            return false;
        *off -= k * iv->step;
    }
    return true;
}

// A pointer kept at base + j*scale through the loop.
typedef struct Reduced {
    struct Reduced *next;
    char *base, *iv;
    int scale;
    bool ext;
    char *type; // the pointer's type, spelled
    char q[32];
    bool every; // one of its addresses is formed every iteration
} Reduced;

static void remove_instr(OptBlock *b, Tac_Instruction *in)
{
    Tac_Instruction *p = prev_of(b, in);
    if (p)
        p->next = in->next;
    else
        b->first = in->next;
    if (b->last == in)
        b->last = p;
    in->next = NULL;
    tac_free_instruction(in);
}

typedef struct {
    const char *name;
    int count;
} Mention;

static void count_name(const char *name, void *arg)
{
    Mention *m = arg;
    if (!strcmp(name, m->name))
        m->count++;
}

// Whether an instruction of the function other than `a` and `b` reads `name`. One
// that only defines it is a dead store once `a` and `b` are gone.
static bool read_elsewhere(const Ivsr *s, const char *name, const Tac_Instruction *a,
                           const Tac_Instruction *b)
{
    for (int k = 0; k < s->n; k++)
        for (const Tac_Instruction *in = s->cfg->blocks[k]->first; in; in = in->next) {
            if (in == a || in == b)
                continue;
            Mention m = { name, 0 };
            tac_visit_names(in, count_name, &m);
            const char *d = def_name(in);
            if (m.count > (d && !strcmp(d, name)))
                return true;
        }
    return false;
}

// The comparison of pointers that orders them as `op` orders a j stepping by `step`,
// or -1: a pointer grows with j, so only a test against the direction of the step
// carries over (and != and ==, which need no direction).
static int pointer_test(Tac_BinaryOperator op, bool is_signed, long long step)
{
    switch (op) {
    case TAC_BINARY_NOT_EQUAL:
    case TAC_BINARY_EQUAL:
        return op;
    case TAC_BINARY_LESS_THAN:
    case TAC_BINARY_LESS_OR_EQUAL:
        return is_signed && step > 0 ? (int)op : -1;
    case TAC_BINARY_GREATER_THAN:
    case TAC_BINARY_GREATER_OR_EQUAL:
        return is_signed && step < 0 ? (int)op : -1;
    case TAC_BINARY_LESS_THAN_UNSIGNED:
        return !is_signed && step > 0 ? TAC_BINARY_LESS_THAN : -1;
    case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
        return !is_signed && step > 0 ? TAC_BINARY_LESS_OR_EQUAL : -1;
    case TAC_BINARY_GREATER_THAN_UNSIGNED:
        return !is_signed && step < 0 ? TAC_BINARY_GREATER_THAN : -1;
    case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
        return !is_signed && step < 0 ? TAC_BINARY_GREATER_OR_EQUAL : -1;
    default:
        return -1;
    }
}

// Linear-function test replacement: a loop test `x op bound`, x = j + c, becomes
// `q + c*scale op base + bound*scale`, the end pointer formed ahead of the loop. The
// pointers are valid, so they do not wrap: one of q's addresses is formed every
// iteration (`every`), the end one past the last. Then j itself goes, when its step is
// all that is left of it. True when anything changed.
static bool replace_tests(Ivsr *s, const bool *in_loop, const LoopDefs *d, Reduced *reduced,
                          int pre)
{
    bool changed = false;
    for (int b = 0; b < s->n; b++) {
        if (!in_loop[b])
            continue;
        OptBlock *blk = s->cfg->blocks[b];
        Tac_Instruction *jmp = blk->last;
        if (!jmp || (jmp->kind != TAC_INSTRUCTION_JUMP_IF_ZERO &&
                     jmp->kind != TAC_INSTRUCTION_JUMP_IF_NOT_ZERO))
            continue;
        const Tac_Val *c = jmp->kind == TAC_INSTRUCTION_JUMP_IF_ZERO ? jmp->u.jump_if_zero.condition
                                                                      : jmp->u.jump_if_not_zero.condition;
        int cb;
        Tac_Instruction *cmp = is_var(c) ? single_def(d, c->u.var_name, &cb) : NULL;
        if (!cmp || cb != b || cmp->kind != TAC_INSTRUCTION_BINARY || cmp->is_volatile)
            continue;
        // The bound, through copies in the loop of an invariant (CSE leaves a copy of
        // the guard's bound for copy propagation, which forwards it a round later).
        const Tac_Val *bound = cmp->u.binary.src2;
        for (int k = 0; k < 4 && is_var(bound); k++) {
            Tac_Instruction *cp = single_def(d, bound->u.var_name, NULL);
            if (!cp || cp->kind != TAC_INSTRUCTION_COPY || cp->is_volatile)
                break;
            bound = cp->u.copy.src;
        }
        long long bval = 0;
        if (is_var(bound) ? !is_private(s, bound->u.var_name) || defined_in_loop(d, bound->u.var_name)
                          : !int_const(bound, &bval))
            continue;
        BasicIv iv;
        long long off;
        bool ext;
        if (!affine_index(s, d, b, cmp, cmp->u.binary.src1, &iv, &off, &ext) || ext)
            continue;
        int jkind = const_kind(type_of(s, iv.name));
        int op    = pointer_test(cmp->u.binary.op, is_signed_kind(jkind), iv.step);
        Reduced *r = reduced;
        while (r && !(r->every && !strcmp(r->iv, iv.name)))
            r = r->next;
        if (op < 0 || !r)
            continue;

        // Ahead of the loop: end = base + bound*scale, the bound extended as q's index.
        const Tac_Type *qt = type_of(s, r->q);
        int xkind          = jkind;
        Tac_Val *index;
        if (r->ext) {
            // q's index is the extended j: a long of the pointer's width.
            xkind = target_config->pointer_size == target_config->long_size ? TAC_CONST_LONG
                                                                            : TAC_CONST_LONG_LONG;
            if (is_var(bound)) {
                Tac_Type *lt = tac_new_type(xkind == TAC_CONST_LONG ? TAC_TYPE_LONG : TAC_TYPE_LONG_LONG);
                char e[32];
                fresh_temp(s, lt, e);
                tac_free_type(lt);
                Tac_Instruction *sx        = tac_new_instruction(TAC_INSTRUCTION_SIGN_EXTEND);
                sx->u.sign_extend.src      = new_var(bound->u.var_name);
                sx->u.sign_extend.dst      = new_var(e);
                sx->u.sign_extend.dst_kind = xkind;
                insert_at_end(s->cfg->blocks[pre], sx);
                index = new_var(e);
            } else {
                index = new_const(xkind, bval);
            }
        } else {
            index = is_var(bound) ? new_var(bound->u.var_name) : new_const(jkind, bval);
        }
        char end[32];
        fresh_temp(s, qt, end);
        Tac_Instruction *init = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
        init->u.add_ptr.ptr   = new_var(r->base);
        init->u.add_ptr.index = index;
        init->u.add_ptr.scale = r->scale;
        init->u.add_ptr.dst   = new_var(end);
        insert_at_end(s->cfg->blocks[pre], init);

        // The test: q, or q + c*scale, against the end.
        opt_trace_instr("[ivsr] test:", cmp);
        Tac_Val *lhs = new_var(r->q);
        if (off != 0) {
            char qx[32];
            fresh_temp(s, qt, qx);
            Tac_Instruction *a = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
            a->u.add_ptr.ptr   = lhs;
            a->u.add_ptr.index = new_const(xkind, off);
            a->u.add_ptr.scale = r->scale;
            a->u.add_ptr.dst   = new_var(qx);
            insert_before(blk, cmp, a);
            lhs = new_var(qx);
        }
        tac_free_val(cmp->u.binary.src1);
        tac_free_val(cmp->u.binary.src2);
        cmp->u.binary.src1 = lhs;
        cmp->u.binary.src2 = new_var(end);
        cmp->u.binary.op   = (Tac_BinaryOperator)op;
        opt_trace_instr("[ivsr]    →", cmp);
        changed = true;
    }
    return changed;
}

typedef struct {
    const Ivsr *s;
    const LoopDefs *d;
    Tac_Instruction *drop[16];
    int block[16];
    int n;
} DeadIvs;

static void find_dead_iv(const char *name, intptr_t value, const void *arg)
{
    (void)value;
    DeadIvs *dv = (DeadIvs *)arg;
    BasicIv iv;
    if (dv->n + 2 > 16 || !basic_iv(dv->s, dv->d, name, &iv))
        return;
    Tac_Instruction *tdef = NULL;
    int tb                = iv.block;
    if (iv.def->kind == TAC_INSTRUCTION_COPY)
        tdef = single_def(dv->d, iv.def->u.copy.src->u.var_name, &tb);
    if (read_elsewhere(dv->s, name, iv.def, tdef) ||
        (tdef && read_elsewhere(dv->s, iv.def->u.copy.src->u.var_name, iv.def, tdef)))
        return;
    dv->drop[dv->n]    = iv.def;
    dv->block[dv->n++] = iv.block;
    if (tdef) {
        dv->drop[dv->n]    = tdef;
        dv->block[dv->n++] = tb;
    }
}

// Drop each induction variable of the loop that only its own step reads: j = j ± c,
// or t = j ± c and j = t. Its other definitions are then dead stores. True when
// anything changed.
static bool drop_dead_ivs(Ivsr *s, LoopDefs *d)
{
    DeadIvs dv = { .s = s, .d = d, .n = 0 };
    map_iterate(&d->count, find_dead_iv, &dv);
    for (int i = 0; i < dv.n; i++) {
        opt_trace_instr("[ivsr] drop:", dv.drop[i]);
        remove_instr(s->cfg->blocks[dv.block[i]], dv.drop[i]);
    }
    return dv.n > 0;
}

// Reduce the addressing of the loop headed by `h` with blocks `in_loop`; true when
// anything changed.
static bool reduce_loop(Ivsr *s, int h, const bool *in_loop)
{
    // The preheader: the one predecessor of h outside the loop.
    int pre = -1;
    for (int k = 0; k < s->preds.npreds[h]; k++) {
        int p = s->preds.preds[h][k];
        if (in_loop[p] || !s->reach[p])
            continue;
        if (pre >= 0)
            return false;
        pre = p;
    }
    if (pre < 0)
        return false;

    // A block formed every iteration dominates every latch.
    bool *every = XALLOC(s->n, bool);
    for (int b = 0; b < s->n; b++) {
        every[b] = in_loop[b];
        for (int l = 0; l < s->preds.npreds[h] && every[b]; l++) {
            int p = s->preds.preds[h][l];
            if (in_loop[p] && !s->dom[p * s->n + b])
                every[b] = false;
        }
    }

    LoopDefs d;
    collect_defs(s, in_loop, &d);
    Reduced *reduced = NULL;
    bool changed     = false;
    for (int b = 0; b < s->n; b++) {
        if (!in_loop[b])
            continue;
        OptBlock *blk = s->cfg->blocks[b];
        for (Tac_Instruction *in = blk->first; in; in = in->next) {
            if (in->kind != TAC_INSTRUCTION_ADD_PTR || in->is_volatile ||
                !is_var(in->u.add_ptr.ptr) || !is_var(in->u.add_ptr.dst))
                continue;
            const char *base = in->u.add_ptr.ptr->u.var_name;
            if (!is_private(s, base) || defined_in_loop(&d, base))
                continue;
            BasicIv iv;
            long long off;
            bool ext;
            if (!affine_index(s, &d, b, in, in->u.add_ptr.index, &iv, &off, &ext))
                continue;
            int scale = in->u.add_ptr.scale;
            // The index's own constant kind: the extended one, or the IV's.
            const Tac_Type *xt = type_of(s, in->u.add_ptr.index->u.var_name);
            int kind           = const_kind(xt);
            if (kind < 0)
                continue;

            const Tac_Type *pt = type_of(s, in->u.add_ptr.dst->u.var_name);
            if (!pt)
                continue;
            char *ptype = tac_type_str(pt);
            Reduced *r  = reduced;
            while (r && !(!strcmp(r->base, base) && !strcmp(r->iv, iv.name) &&
                          r->scale == scale && r->ext == ext && !strcmp(r->type, ptype)))
                r = r->next;
            if (r) {
                xfree(ptype);
            } else {
                r        = XALLOC(1, Reduced);
                r->base  = xstrdup(base);
                r->iv    = xstrdup(iv.name);
                r->scale = scale;
                r->ext   = ext;
                r->type  = ptype;
                fresh_temp(s, pt, r->q);
                r->next  = reduced;
                reduced  = r;

                // Ahead of the loop: q = base + j*scale.
                Tac_Val *index = new_var(r->iv);
                if (ext) {
                    char e[32];
                    fresh_temp(s, xt, e);
                    Tac_Instruction *sx          = tac_new_instruction(TAC_INSTRUCTION_SIGN_EXTEND);
                    sx->u.sign_extend.src        = index;
                    sx->u.sign_extend.dst        = new_var(e);
                    sx->u.sign_extend.dst_kind   = kind;
                    insert_at_end(s->cfg->blocks[pre], sx);
                    index = new_var(e);
                }
                Tac_Instruction *init = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
                init->u.add_ptr.ptr   = new_var(r->base);
                init->u.add_ptr.index = index;
                init->u.add_ptr.scale = scale;
                init->u.add_ptr.dst   = new_var(r->q);
                insert_at_end(s->cfg->blocks[pre], init);

                // After j's step: q += step*scale.
                Tac_Instruction *step = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
                step->u.add_ptr.ptr   = new_var(r->q);
                step->u.add_ptr.index = new_const(kind, iv.step);
                step->u.add_ptr.scale = scale;
                step->u.add_ptr.dst   = new_var(r->q);
                insert_after(s->cfg->blocks[iv.block], iv.def, step);
            }

            r->every |= every[b];

            // The address itself: q, or q + off*scale.
            opt_trace_instr("[ivsr] reduce:", in);
            tac_free_val(in->u.add_ptr.ptr);
            tac_free_val(in->u.add_ptr.index);
            if (off == 0) {
                Tac_Val *dst      = in->u.add_ptr.dst;
                in->kind          = TAC_INSTRUCTION_COPY;
                in->u.copy.src    = new_var(r->q);
                in->u.copy.dst    = dst;
            } else {
                in->u.add_ptr.ptr   = new_var(r->q);
                in->u.add_ptr.index = new_const(kind, off);
            }
            opt_trace_instr("[ivsr]      →", in);
            changed = true;
        }
    }
    if (reduced && replace_tests(s, in_loop, &d, reduced, pre))
        changed = true;
    if (drop_dead_ivs(s, &d))
        changed = true;
    xfree(every);
    while (reduced) {
        Reduced *next = reduced->next;
        xfree(reduced->type);
        xfree(reduced->base);
        xfree(reduced->iv);
        xfree(reduced);
        reduced = next;
    }
    free_defs(&d);
    return changed;
}

// ============================================================================
// Entry point
// ============================================================================

// p + 0 → p, for a pointer p of the destination's type: a reduced pointer starts at
// base + j*scale, j often 0. (Not for an aggregate base, whose COPY would copy it.)
static bool fold_zero_offsets(Ivsr *s)
{
    bool changed = false;
    for (int b = 0; b < s->n; b++)
        for (Tac_Instruction *in = s->cfg->blocks[b]->first; in; in = in->next) {
            long long v;
            if (in->kind != TAC_INSTRUCTION_ADD_PTR || in->is_volatile ||
                !is_var(in->u.add_ptr.ptr) || !is_var(in->u.add_ptr.dst) ||
                !int_const(in->u.add_ptr.index, &v) || v != 0)
                continue;
            const Tac_Type *pt = type_of(s, in->u.add_ptr.ptr->u.var_name);
            const Tac_Type *dt = type_of(s, in->u.add_ptr.dst->u.var_name);
            if (!pt || !dt || pt->kind != TAC_TYPE_POINTER || dt->kind != TAC_TYPE_POINTER)
                continue;
            char *ps = tac_type_str(pt), *ds = tac_type_str(dt);
            bool same = !strcmp(ps, ds);
            xfree(ps);
            xfree(ds);
            if (!same)
                continue;
            opt_trace_instr("[ivsr] zero offset:", in);
            Tac_Val *ptr = in->u.add_ptr.ptr, *dst = in->u.add_ptr.dst;
            tac_free_val(in->u.add_ptr.index);
            in->kind       = TAC_INSTRUCTION_COPY;
            in->u.copy.src = ptr;
            in->u.copy.dst = dst;
            changed        = true;
        }
    return changed;
}

bool reduce_induction_variables(OptCfg *cfg, Tac_TopLevel *fn)
{
    if (cfg->nblocks == 0 || !fn || fn->kind != TAC_TOPLEVEL_FUNCTION)
        return false;
    bool changed = false;
    Ivsr s = { .cfg = cfg, .fn = fn, .n = cfg->nblocks };
    opt_preds_build(&s.preds, cfg);
    StringMap observable;
    collect_alias_sets(cfg, fn, &observable, &s.address_taken);
    map_destroy(&observable);
    map_init(&s.types);
    map_init(&s.names);
    for (const Tac_Param *p = fn->u.function.params; p; p = p->next)
        if (p->name && p->type)
            map_insert(&s.types, p->name, (intptr_t)p->type, 0);
    for (const Tac_Param *p = fn->u.function.locals; p; p = p->next)
        if (p->name && p->type)
            map_insert(&s.types, p->name, (intptr_t)p->type, 0);
    for (int b = 0; b < s.n; b++)
        for (const Tac_Instruction *in = cfg->blocks[b]->first; in; in = in->next) {
            tac_visit_names(in, note_name, &s.names);
            if (in->kind == TAC_INSTRUCTION_LABEL)
                map_insert(&s.names, in->u.label.name, 1, 0);
        }
    for (const Tac_Param *p = fn->u.function.params; p; p = p->next)
        if (p->name)
            map_insert(&s.names, p->name, 1, 0);
    for (const Tac_Param *p = fn->u.function.locals; p; p = p->next)
        if (p->name)
            map_insert(&s.names, p->name, 1, 0);
    s.next_temp = 1000;
    if (fold_zero_offsets(&s))
        changed = true;
    compute_dominators(&s);

    // Each header with its back edges' natural loop.
    bool *in_loop = XALLOC(s.n, bool);
    int *stack    = XALLOC(s.n, int);
    s.stack       = XALLOC(s.n, int);
    s.seen        = XALLOC(s.n, bool);
    for (int h = 0; h < s.n; h++) {
        if (!s.reach[h])
            continue;
        int sp = 0;
        for (int b = 0; b < s.n; b++)
            in_loop[b] = false;
        for (int k = 0; k < s.preds.npreds[h]; k++) {
            int b = s.preds.preds[h][k];
            if (s.reach[b] && s.dom[b * s.n + h] && !in_loop[b]) {
                in_loop[b]  = true;
                stack[sp++] = b;
            }
        }
        if (!sp)
            continue;
        in_loop[h] = true;
        while (sp) {
            int b = stack[--sp];
            if (b == h)
                continue;
            for (int k = 0; k < s.preds.npreds[b]; k++) {
                int p = s.preds.preds[b][k];
                if (s.reach[p] && !in_loop[p]) {
                    in_loop[p]  = true;
                    stack[sp++] = p;
                }
            }
        }
        s.h       = h;
        s.in_loop = in_loop;
        if (reduce_loop(&s, h, in_loop))
            changed = true;
    }
    xfree(stack);
    xfree(s.stack);
    xfree(s.seen);
    xfree(in_loop);
    xfree(s.dom);
    xfree(s.reach);
    map_destroy(&s.types);
    map_destroy(&s.names);
    map_destroy(&s.address_taken);
    opt_preds_free(&s.preds);
    return changed;
}
