//
// Types, the frame, and access to values in memory.
//
#include <stdio.h>
#include <string.h>

#include "float128.h"
#include "internal.h"
#include "xalloc.h"

int avr_type_size(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_POINTER:
    case TAC_TYPE_FUN_TYPE:
        return 2;
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_FLOAT:
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
        return 4;
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
        return 8;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * avr_type_size(t->u.array.elem_type);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    }
    return 2;
}

bool avr_is_unsigned(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_POINTER:
        return true;
    default:
        return false;
    }
}

bool avr_is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

bool avr_is_scalar(const Tac_Type *t)
{
    return t->kind != TAC_TYPE_ARRAY && t->kind != TAC_TYPE_STRUCTURE;
}

int block_a(int size)
{
    return size == 1 ? 24 : 26 - (size + 1) / 2 * 2;
}

int block_b(int size)
{
    return size == 1 ? 22 : 26 - 2 * ((size + 1) / 2 * 2);
}

static int unit_labels;

void gen_unit_begin(void)
{
    unit_labels = 0;
}

void new_label(char buf[32])
{
    snprintf(buf, 32, ".Lv%d", unit_labels++);
}

static void add_global(Gen *g, const char *name, const Tac_Type *type)
{
    if (type)
        map_insert(&g->globals, name, (intptr_t)type, 0);
}

void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl, bool alloc)
{
    memset(g, 0, sizeof(*g));
    g->program  = program;
    g->tl       = tl;
    g->alloc    = alloc;
    g->fn       = avr_new_func(tl->u.function.name, tl->u.function.global);
    g->prologue = g->fn->tail;
    avr_new_block(g->fn, NULL); // the body
    new_label(g->exit);
    map_init(&g->frame);
    map_init(&g->globals);
    map_init(&g->locals);
    map_init(&g->regs);
    map_init(&g->dead);
    for (const Tac_Param *p = tl->u.function.params; p; p = p->next)
        if (p->type)
            map_insert(&g->locals, p->name, (intptr_t)p->type, 0);
    for (const Tac_Param *p = tl->u.function.locals; p; p = p->next)
        if (p->type)
            map_insert(&g->locals, p->name, (intptr_t)p->type, 0);
    for (const Tac_TopLevel *t = program; t; t = t->next) {
        switch (t->kind) {
        case TAC_TOPLEVEL_FUNCTION:
            add_global(g, t->u.function.name, t->u.function.type);
            for (const Tac_StaticLocal *s = t->u.function.static_locals; s; s = s->next)
                add_global(g, s->name, s->type);
            break;
        case TAC_TOPLEVEL_STATIC_VARIABLE:
            add_global(g, t->u.static_variable.name, t->u.static_variable.type);
            break;
        case TAC_TOPLEVEL_STATIC_CONSTANT:
            add_global(g, t->u.static_constant.name, t->u.static_constant.type);
            break;
        case TAC_TOPLEVEL_EXTERN:
            add_global(g, t->u.extern_.name, t->u.extern_.type);
            break;
        }
    }
}

static void free_slot(intptr_t p)
{
    xfree((void *)p);
}

void gen_done(Gen *g)
{
    map_destroy_free(&g->frame, free_slot);
    map_destroy(&g->globals);
    map_destroy(&g->locals);
    map_destroy(&g->regs);
    map_destroy(&g->dead);
    avr_free_func(g->fn);
}

const char *gen_name(const Gen *g)
{
    return g->tl->u.function.name;
}

static void insert_slot(Gen *g, const char *name, const Tac_Type *type, int q)
{
    Slot *s = xalloc(sizeof(Slot), __func__, __FILE__, __LINE__);
    s->type = type;
    s->q    = q;
    map_insert_free(&g->frame, name, (intptr_t)s, 0, free_slot);
}

// The stack arguments start above the slots, the saved Y and the return address; the
// slots' size is not known yet, so the displacement is completed by layout_frame.
enum { STACK_PARAM = 1 << 20 };

void place_stack_param(Gen *g, const char *name, const Tac_Type *type, int off)
{
    insert_slot(g, name, type, STACK_PARAM + off);
}

// The size of the slot of local `p`: its type's, or more for an ALLOCATE_LOCAL.
static int local_size(const Tac_Param *p, const StringMap *allocs)
{
    int size = avr_type_size(p->type);
    intptr_t v;
    if (map_get(allocs, p->name, &v)) {
        const Tac_Instruction *in = (const Tac_Instruction *)v;
        if (in->u.allocate_local.size > size)
            size = in->u.allocate_local.size;
    }
    return size;
}

static bool is_aggregate_local(const Tac_Param *p, const StringMap *allocs)
{
    intptr_t v;
    return !avr_is_scalar(p->type) || map_get(allocs, p->name, &v);
}

static void free_nothing(intptr_t p)
{
    (void)p;
}

// The order of slots: scalars of up to 4 bytes first, so that they stay within Y+63
// (the scratch-free selection reaches them only so), then long long, then aggregates.
static int slot_class(const Tac_Type *t, bool aggregate)
{
    return aggregate ? 2 : avr_type_size(t) > 4 ? 1 : 0;
}

void layout_frame(Gen *g)
{
    StringMap allocs;
    map_init(&allocs);
    for (const Tac_Instruction *in = g->tl->u.function.body; in; in = in->next)
        if (in->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            map_insert(&allocs, in->u.allocate_local.name, (intptr_t)in, 0);

    // The register parameters among the others (the stack ones have their slots); a
    // variable in registers needs none.
    intptr_t v;
    for (int cls = 0; cls < 3; cls++) {
        for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
            if (!p->type)
                fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
            if (find_slot(g, p->name) || map_get(&g->regs, p->name, &v) ||
                slot_class(p->type, !avr_is_scalar(p->type)) != cls)
                continue;
            insert_slot(g, p->name, p->type, 1 + g->frame_size);
            g->frame_size += avr_type_size(p->type);
        }
        for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next) {
            if (!p->type)
                fatal_error("avr: %s: no type for %s", gen_name(g), p->name);
            if (find_slot(g, p->name) || map_get(&g->regs, p->name, &v) ||
                slot_class(p->type, is_aggregate_local(p, &allocs)) != cls)
                continue;
            insert_slot(g, p->name, p->type, 1 + g->frame_size);
            g->frame_size += local_size(p, &allocs);
        }
    }
    map_destroy_free(&allocs, free_nothing);
    if (g->frame_size > 0 && g->frame_size <= 6)
        g->frame_size = (g->frame_size + 1) & ~1; // reserved by rcall, 2 bytes each

    // The stack parameters, now that the slots' size is known.
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next) {
        if (map_get(&g->frame, p->name, &v) && ((Slot *)v)->q >= STACK_PARAM)
            ((Slot *)v)->q += g->frame_size + 5 - STACK_PARAM;
    }
}

static bool name_is_near(const Gen *g, const char *name)
{
    const Slot *s = find_slot(g, name);
    intptr_t v;
    if (!s || map_get(&g->regs, name, &v) || !avr_is_scalar(s->type))
        return true;
    int size = avr_type_size(s->type);
    return size > 4 || s->q + size - 1 <= Y_MAX;
}

bool frame_is_near(const Gen *g)
{
    for (const Tac_Param *p = g->tl->u.function.params; p; p = p->next)
        if (!name_is_near(g, p->name))
            return false;
    for (const Tac_Param *p = g->tl->u.function.locals; p; p = p->next)
        if (!name_is_near(g, p->name))
            return false;
    return true;
}

const Slot *find_slot(const Gen *g, const char *name)
{
    intptr_t v;
    return map_get(&g->frame, name, &v) ? (const Slot *)v : NULL;
}

const Tac_Type *name_type(const Gen *g, const char *name)
{
    intptr_t v;
    if (map_get(&g->locals, name, &v))
        return (const Tac_Type *)v;
    const Slot *s = find_slot(g, name);
    if (s)
        return s->type;
    if (map_get(&g->globals, name, &v))
        return (const Tac_Type *)v;
    fatal_error("avr: %s: no type for %s", gen_name(g), name);
}

const Tac_Type *val_type(const Gen *g, const Tac_Val *v)
{
    static const Tac_Type types[] = {
        [TAC_CONST_INT]         = { .kind = TAC_TYPE_INT },
        [TAC_CONST_LONG]        = { .kind = TAC_TYPE_LONG },
        [TAC_CONST_LONG_LONG]   = { .kind = TAC_TYPE_LONG_LONG },
        [TAC_CONST_UINT]        = { .kind = TAC_TYPE_UINT },
        [TAC_CONST_ULONG]       = { .kind = TAC_TYPE_ULONG },
        [TAC_CONST_ULONG_LONG]  = { .kind = TAC_TYPE_ULONG_LONG },
        [TAC_CONST_FLOAT]       = { .kind = TAC_TYPE_FLOAT },
        [TAC_CONST_DOUBLE]      = { .kind = TAC_TYPE_DOUBLE },
        [TAC_CONST_LONG_DOUBLE] = { .kind = TAC_TYPE_LONG_DOUBLE },
        [TAC_CONST_SCHAR]       = { .kind = TAC_TYPE_SCHAR },
        [TAC_CONST_UCHAR]       = { .kind = TAC_TYPE_UCHAR },
    };
    if (v->kind == TAC_VAL_CONSTANT)
        return &types[v->u.constant->kind];
    return name_type(g, v->u.var_name);
}

bool is_function(const Gen *g, const char *name)
{
    intptr_t v;
    if (find_slot(g, name) || map_get(&g->locals, name, &v))
        return false;
    return map_get(&g->globals, name, &v) && ((const Tac_Type *)v)->kind == TAC_TYPE_FUN_TYPE;
}

AVR_Instr *emit0(Gen *g, AVR_Op op)
{
    AVR_Instr *in = avr_append(g->fn, op);
    in->vol       = g->vol;
    return in;
}

AVR_Instr *emit1(Gen *g, AVR_Op op, AVR_Operand a)
{
    AVR_Instr *in = emit0(g, op);
    in->opnd[0]   = a;
    return in;
}

AVR_Instr *emit2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b)
{
    AVR_Instr *in = emit1(g, op, a);
    in->opnd[1]   = b;
    return in;
}

void gen_li(Gen *g, int reg, int imm)
{
    imm &= 0xff;
    if (reg >= 16) {
        emit2(g, AVR_LDI, avr_reg(reg), avr_imm(imm));
    } else if (imm == 0) {
        emit2(g, AVR_MOV, avr_reg(reg), avr_reg(AVR_ZERO));
    } else {
        emit2(g, AVR_LDI, avr_reg(AVR_X), avr_imm(imm));
        emit2(g, AVR_MOV, avr_reg(reg), avr_reg(AVR_X));
    }
}

uint64_t const_bits(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_SCHAR:
        return (uint64_t)(int64_t)(signed char)c->u.char_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    case TAC_CONST_INT:
        return (uint64_t)(int64_t)(int16_t)c->u.int_val;
    case TAC_CONST_UINT:
        return (uint16_t)c->u.uint_val;
    case TAC_CONST_LONG:
        return (uint64_t)(int64_t)(int32_t)c->u.long_val;
    case TAC_CONST_ULONG:
        return (uint32_t)c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        return (uint64_t)c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        return c->u.ulong_long_val;
    case TAC_CONST_FLOAT:
    case TAC_CONST_DOUBLE: {
        float f = (float)(c->kind == TAC_CONST_FLOAT ? c->u.float_val : c->u.double_val);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    case TAC_CONST_LONG_DOUBLE: {
        float f = f128_to_float(c->u.long_double_val);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return bits;
    }
    }
    return 0;
}

// Point pointer register `ptr` (X or Z) at Y+q.
static void address_slot(Gen *g, int ptr, int q)
{
    emit2(g, AVR_MOVW, avr_reg(ptr), avr_reg(AVR_Y));
    if (q <= Y_MAX) {
        emit2(g, AVR_ADIW, avr_reg(ptr), avr_imm(q));
    } else {
        emit2(g, AVR_SUBI, avr_reg(ptr), avr_imm(-q & 0xff));
        emit2(g, AVR_SBCI, avr_reg(ptr + 1), avr_imm((-q >> 8) & 0xff));
    }
}

void address_of(Gen *g, int ptr, const char *name, int off)
{
    const Slot *s = find_slot(g, name);
    if (s) {
        address_slot(g, ptr, s->q + off);
        return;
    }
    if (name[0] == '%')
        fatal_error("avr: %s: no slot for %s", gen_name(g), name);
    emit2(g, AVR_LDI, avr_reg(ptr), avr_sym(AVR_MOD_LO8, name, off));
    emit2(g, AVR_LDI, avr_reg(ptr + 1), avr_sym(AVR_MOD_HI8, name, off));
}

void copy_bytes(Gen *g, int size)
{
    if (size <= 16) {
        for (int i = 0; i < size; i++) {
            emit2(g, AVR_LD, avr_reg(AVR_TMP), avr_ptr(AVR_X, AVR_PTR_POST_INC));
            emit2(g, AVR_ST, avr_ptr(AVR_Z, AVR_PTR_POST_INC), avr_reg(AVR_TMP));
        }
        return;
    }
    char loop[32];
    new_label(loop);
    emit2(g, AVR_LDI, avr_reg(24), avr_imm(size & 0xff));
    emit2(g, AVR_LDI, avr_reg(25), avr_imm((size >> 8) & 0xff));
    gen_label_block(g, loop);
    emit2(g, AVR_LD, avr_reg(AVR_TMP), avr_ptr(AVR_X, AVR_PTR_POST_INC));
    emit2(g, AVR_ST, avr_ptr(AVR_Z, AVR_PTR_POST_INC), avr_reg(AVR_TMP));
    emit2(g, AVR_SBIW, avr_reg(24), avr_imm(1));
    emit1(g, AVR_BRNE, avr_label(loop));
}

Regs regs_range(int reg, int n)
{
    Regs r = { .n = n };
    for (int i = 0; i < n; i++)
        r.r[i] = reg + i;
    return r;
}

bool var_regs(const Gen *g, const char *name, Regs *out)
{
    intptr_t v;
    if (!map_get(&g->regs, name, &v))
        return false;
    int lo = (int)(v & 0xff), hi = (int)(v >> 8);
    out->n = avr_type_size(name_type(g, name));
    for (int i = 0; i < out->n; i++)
        out->r[i] = i < 2 ? lo + i : hi + i - 2;
    return true;
}

bool val_regs(const Gen *g, const Tac_Val *v, Regs *out)
{
    return v->kind == TAC_VAL_VAR && var_regs(g, v->u.var_name, out);
}

bool near_bytes(const Gen *g, const char *name, int off, int n)
{
    const Slot *s = find_slot(g, name);
    return !s || s->q + off + n - 1 <= Y_MAX;
}

static bool among(const int *regs, int n, int r)
{
    for (int i = 0; i < n; i++)
        if (regs[i] == r)
            return true;
    return false;
}

void access_mem(Gen *g, bool store, const char *name, int off, const int *regs, int n,
                uint32_t busy)
{
    const Slot *s = find_slot(g, name);
    if (!s) {
        if (name[0] == '%')
            fatal_error("avr: %s: no slot for %s", gen_name(g), name);
        for (int i = 0; i < n; i++) {
            if (store)
                emit2(g, AVR_STS, avr_sym(AVR_MOD_NONE, name, off + i), avr_reg(regs[i]));
            else
                emit2(g, AVR_LDS, avr_reg(regs[i]), avr_sym(AVR_MOD_NONE, name, off + i));
        }
        return;
    }
    int q = s->q + off;
    if (q + n - 1 <= Y_MAX) {
        for (int i = 0; i < n; i++) {
            if (store)
                emit2(g, AVR_STD, avr_disp(AVR_Y, q + i), avr_reg(regs[i]));
            else
                emit2(g, AVR_LDD, avr_reg(regs[i]), avr_disp(AVR_Y, q + i));
        }
        return;
    }
    // Past Y+63: through Z, or X when Z is taken.
    bool z_taken = among(regs, n, AVR_Z) || among(regs, n, AVR_Z + 1) || (busy >> AVR_Z & 3);
    int ptr      = z_taken ? AVR_X : AVR_Z;
    if (ptr == AVR_X && (among(regs, n, AVR_X) || among(regs, n, AVR_X + 1) || (busy >> AVR_X & 3)))
        fatal_error("avr: %s: no pointer register for %s", gen_name(g), name);
    address_slot(g, ptr, q);
    for (int i = 0; i < n; i++) {
        if (store)
            emit2(g, AVR_ST, avr_ptr(ptr, AVR_PTR_POST_INC), avr_reg(regs[i]));
        else
            emit2(g, AVR_LD, avr_reg(regs[i]), avr_ptr(ptr, AVR_PTR_POST_INC));
    }
}

void access_bytes(Gen *g, bool store, const char *name, int off, int reg, int n)
{
    Regs vr, r = regs_range(reg, n);
    if (var_regs(g, name, &vr)) {
        if (store)
            parallel_move(g, vr.r + off, r.r, n);
        else
            parallel_move(g, r.r, vr.r + off, n);
        return;
    }
    access_mem(g, store, name, off, r.r, n, 0);
}

void access_incoming(Gen *g, int off, int reg)
{
    int q = g->frame_size + 5 + off;
    if (q <= Y_MAX) {
        emit2(g, AVR_LDD, avr_reg(reg), avr_disp(AVR_Y, q));
        return;
    }
    address_slot(g, AVR_Z, q);
    emit2(g, AVR_LD, avr_reg(reg), avr_ptr(AVR_Z, AVR_PTR_PLAIN));
}

static bool is_src(const int *s, int n, int skip, int r)
{
    for (int i = 0; i < n; i++)
        if (i != skip && s[i] == r)
            return true;
    return false;
}

// Whether a pending move reads or writes register r.
static bool touched(const int *d, const int *s, int m, int r)
{
    for (int i = 0; i < m; i++)
        if (d[i] == r || s[i] == r)
            return true;
    return false;
}

void parallel_move(Gen *g, const int *dst, const int *src, int n)
{
    int d[64], s[64], m = 0;
    for (int i = 0; i < n; i++) {
        if (dst[i] == src[i])
            continue;
        if (m == 64)
            fatal_error("avr: %s: too many moves", gen_name(g));
        d[m]   = dst[i];
        s[m++] = src[i];
    }
    int pushed[64], np = 0;
    while (m > 0) {
        int pick = -1;
        for (int i = 0; i < m && pick < 0; i++)
            if (!is_src(s, m, i, d[i]))
                pick = i;
        if (pick < 0) {
            // A cycle: a source copied into a register no move touches (a pair of X
            // or Z with movw, else r0), or kept on the stack and popped last.
            int mate = -1;
            for (int j = 0; j < m && mate < 0; j++)
                if (d[0] % 2 == 0 && s[0] % 2 == 0 && d[j] == d[0] + 1 && s[j] == s[0] + 1)
                    mate = j;
            int tmp = -1;
            if (mate >= 0)
                for (int t = AVR_Z; t >= AVR_X && tmp < 0; t -= 4)
                    if (!touched(d, s, m, t) && !touched(d, s, m, t + 1))
                        tmp = t;
            if (tmp >= 0) {
                emit2(g, AVR_MOVW, avr_reg(tmp), avr_reg(s[0]));
                s[0]    = tmp;
                s[mate] = tmp + 1;
            } else if (!touched(d, s, m, AVR_TMP)) {
                emit2(g, AVR_MOV, avr_reg(AVR_TMP), avr_reg(s[0]));
                s[0] = AVR_TMP;
            } else {
                emit1(g, AVR_PUSH, avr_reg(s[0]));
                pushed[np++] = d[0];
                d[0]         = d[--m];
                s[0]         = s[m];
            }
            continue;
        }
        // movw when the next byte goes along with it.
        int mate = -1;
        if (d[pick] % 2 == 0 && s[pick] % 2 == 0)
            for (int j = 0; j < m && mate < 0; j++)
                if (d[j] == d[pick] + 1 && s[j] == s[pick] + 1 && !is_src(s, m, j, d[j]))
                    mate = j;
        if (mate >= 0) {
            emit2(g, AVR_MOVW, avr_reg(d[pick]), avr_reg(s[pick]));
            int hi = pick > mate ? pick : mate, lo = pick > mate ? mate : pick;
            d[hi] = d[--m], s[hi] = s[m];
            d[lo] = d[--m], s[lo] = s[m];
        } else {
            emit2(g, AVR_MOV, avr_reg(d[pick]), avr_reg(s[pick]));
            d[pick] = d[--m];
            s[pick] = s[m];
        }
    }
    while (np > 0)
        emit1(g, AVR_POP, avr_reg(pushed[--np]));
}

uint32_t save_var_regs(Gen *g, int lo, int hi)
{
    uint32_t mask = 0;
    for (int r = lo; r <= hi; r++)
        if (g->var_regs >> r & 1) {
            emit1(g, AVR_PUSH, avr_reg(r));
            mask |= 1u << r;
        }
    return mask;
}

void restore_var_regs(Gen *g, uint32_t mask)
{
    for (int r = 31; r >= 0; r--)
        if (mask >> r & 1)
            emit1(g, AVR_POP, avr_reg(r));
}

void extend_regs(Gen *g, const int *r, int from, int n, bool sign)
{
    if (from >= n)
        return;
    if (!sign) {
        for (int i = from; i < n; i++)
            emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(AVR_ZERO));
        return;
    }
    // The sign of the top byte into C, then 0 - C: 0x00 or 0xff.
    int top = r[from];
    emit2(g, AVR_MOV, avr_reg(top), avr_reg(r[from - 1]));
    emit1(g, AVR_LSL, avr_reg(top));
    emit2(g, AVR_SBC, avr_reg(top), avr_reg(top));
    for (int i = from + 1; i < n; i++)
        emit2(g, AVR_MOV, avr_reg(r[i]), avr_reg(top));
}

bool ext_sign(const Tac_Type *t, Ext ext)
{
    return ext == EXT_SIGN || (ext == EXT_TYPE && !avr_is_unsigned(t) && !avr_is_fp(t));
}

uint64_t const_extended(const Tac_Const *c, int size, int n, Ext ext)
{
    uint64_t bits = const_bits(c);
    if (size < 8 && n > size && ext != EXT_TYPE) {
        uint64_t mask = (1ull << (8 * size)) - 1;
        bool neg      = ext == EXT_SIGN && (bits >> (8 * size - 1) & 1);
        bits          = neg ? bits | ~mask : bits & mask;
    }
    return bits;
}

void load_vals(Gen *g, const Load *l, int n)
{
    // Every destination, for the choice of a pointer or an immediate register.
    uint32_t dsts = 0;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < l[k].to.n; i++)
            dsts |= 1u << l[k].to.r[i];

    // Register to register, all at once.
    int d[64], s[64], m = 0;
    for (int k = 0; k < n; k++) {
        Regs vr;
        if (!val_regs(g, l[k].v, &vr))
            continue;
        for (int i = 0; i < l[k].to.n && i < vr.n; i++) {
            d[m]   = l[k].to.r[i];
            s[m++] = vr.r[i];
        }
    }
    parallel_move(g, d, s, m);

    // From memory, then constants.
    for (int k = 0; k < n; k++) {
        const Tac_Val *v = l[k].v;
        Regs vr;
        if (v->kind != TAC_VAL_VAR || val_regs(g, v, &vr))
            continue;
        int size = avr_type_size(val_type(g, v));
        access_mem(g, false, v->u.var_name, 0, l[k].to.r, size < l[k].to.n ? size : l[k].to.n,
                   dsts);
    }
    for (int k = 0; k < n; k++) {
        const Tac_Val *v = l[k].v;
        if (v->kind != TAC_VAL_CONSTANT)
            continue;
        int size      = avr_type_size(val_type(g, v));
        uint64_t bits = const_extended(v->u.constant, size, l[k].to.n, l[k].ext);
        int tmp       = -1;
        for (int r = 26; r <= 31 && tmp < 0; r++)
            if ((r < 28 || r >= 30) && !(dsts >> r & 1))
                tmp = r;
        for (int i = 0; i < l[k].to.n; i++) {
            int reg = l[k].to.r[i], b = (int)(i < 8 ? bits >> (8 * i) & 0xff : 0);
            if (reg >= 16) {
                emit2(g, AVR_LDI, avr_reg(reg), avr_imm(b));
            } else if (b == 0) {
                emit2(g, AVR_MOV, avr_reg(reg), avr_reg(AVR_ZERO));
            } else {
                if (tmp < 0)
                    fatal_error("avr: %s: no register for a constant", gen_name(g));
                emit2(g, AVR_LDI, avr_reg(tmp), avr_imm(b));
                emit2(g, AVR_MOV, avr_reg(reg), avr_reg(tmp));
            }
        }
    }

    // The bytes past a narrower variable.
    for (int k = 0; k < n; k++) {
        const Tac_Val *v = l[k].v;
        if (v->kind != TAC_VAL_VAR)
            continue;
        const Tac_Type *t = val_type(g, v);
        extend_regs(g, l[k].to.r, avr_type_size(t), l[k].to.n, ext_sign(t, l[k].ext));
    }
}

void load_regs(Gen *g, const Tac_Val *v, const Regs *to, Ext ext)
{
    Load l = { v, *to, ext };
    load_vals(g, &l, 1);
}

void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext)
{
    Load l = { v, regs_range(reg, n), ext };
    load_vals(g, &l, 1);
}

void load_two(Gen *g, const Tac_Val *v1, int reg1, int n1, const Tac_Val *v2, int reg2, int n2)
{
    Load l[2] = { { v1, regs_range(reg1, n1), EXT_TYPE }, { v2, regs_range(reg2, n2), EXT_TYPE } };
    load_vals(g, l, 2);
}

void store_regs(Gen *g, const Tac_Val *v, const int *regs, int n)
{
    if (v->kind != TAC_VAL_VAR)
        fatal_error("avr: %s: store to a constant", gen_name(g));
    int size = avr_type_size(val_type(g, v));
    int m    = size < n ? size : n;
    Regs vr;
    if (var_regs(g, v->u.var_name, &vr)) {
        int src[8];
        for (int i = 0; i < size; i++)
            src[i] = i < m ? regs[i] : AVR_ZERO;
        parallel_move(g, vr.r, src, size);
        return;
    }
    uint32_t busy = 0;
    for (int i = 0; i < m; i++)
        busy |= 1u << regs[i];
    access_mem(g, true, v->u.var_name, 0, regs, m, busy);
    for (int i = m; i < size; i++) {
        int zero = AVR_ZERO;
        access_mem(g, true, v->u.var_name, i, &zero, 1, busy);
    }
}

void store_val(Gen *g, const Tac_Val *v, int reg, int n)
{
    Regs r = regs_range(reg, n);
    store_regs(g, v, r.r, n);
}

void gen_label_block(Gen *g, const char *label)
{
    avr_new_block(g->fn, label);
}

static void pro1(Gen *g, AVR_Op op, AVR_Operand a)
{
    avr_append_to(g->prologue, op)->opnd[0] = a;
}

static AVR_Instr *pro2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b)
{
    AVR_Instr *in = avr_append_to(g->prologue, op);
    in->opnd[0]   = a;
    in->opnd[1]   = b;
    return in;
}

// Y += delta, on the block being emitted (`pro` for the prologue).
static void adjust_y(Gen *g, bool pro, int delta)
{
    AVR_Instr *(*e)(Gen *, AVR_Op, AVR_Operand, AVR_Operand) = pro ? pro2 : emit2;
    if (delta >= -Y_MAX && delta < 0) {
        e(g, AVR_SBIW, avr_reg(AVR_Y), avr_imm(-delta));
    } else if (delta > 0 && delta <= Y_MAX) {
        e(g, AVR_ADIW, avr_reg(AVR_Y), avr_imm(delta));
    } else {
        e(g, AVR_SUBI, avr_reg(AVR_Y), avr_imm(-delta & 0xff));
        e(g, AVR_SBCI, avr_reg(AVR_Y + 1), avr_imm((-delta >> 8) & 0xff));
    }
}

// SP = Y, with interrupts held off between the halves (the I flag is restored before
// the low half, which the next instruction still completes).
static void write_sp(Gen *g, bool pro)
{
    AVR_Instr *(*e)(Gen *, AVR_Op, AVR_Operand, AVR_Operand) = pro ? pro2 : emit2;
    e(g, AVR_IN, avr_reg(AVR_TMP), avr_sym(AVR_MOD_NONE, "__SREG__", 0));
    if (pro)
        avr_append_to(g->prologue, AVR_CLI);
    else
        emit0(g, AVR_CLI);
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_H__", 0), avr_reg(AVR_Y + 1));
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SREG__", 0), avr_reg(AVR_TMP));
    e(g, AVR_OUT, avr_sym(AVR_MOD_NONE, "__SP_L__", 0), avr_reg(AVR_Y));
}

// The call-saved registers r2-r17 the body uses (and Y without a frame), as a mask.
static uint32_t saved_regs(const Gen *g)
{
    uint32_t mask = 0;
    for (const AVR_Block *b = g->fn->blocks; b; b = b->next)
        for (const AVR_Instr *in = b->head; in; in = in->next)
            for (int i = 0; i < AVR_MAX_OPERANDS; i++) {
                const AVR_Operand *o = &in->opnd[i];
                if (o->kind != AVR_OPND_REG || o->reg < 2 ||
                    (o->reg > 17 && !(g->frameless && o->reg >= AVR_Y && o->reg <= AVR_Y + 1)))
                    continue;
                mask |= 1u << o->reg;
                if (in->op == AVR_MOVW)
                    mask |= 1u << (o->reg + 1);
            }
    return mask;
}

void gen_frame(Gen *g)
{
    uint32_t saved = saved_regs(g);
    bool small     = g->frame_size <= 6; // reserved by rcall, released by pop

    // Prologue: save Y, reserve the slots and point Y at them, then save the
    // registers.
    if (!g->frameless) {
        pro1(g, AVR_PUSH, avr_reg(AVR_Y));
        pro1(g, AVR_PUSH, avr_reg(AVR_Y + 1));
        if (small)
            for (int i = 0; i < g->frame_size; i += 2)
                pro1(g, AVR_RCALL, avr_label("."));
        pro2(g, AVR_IN, avr_reg(AVR_Y), avr_sym(AVR_MOD_NONE, "__SP_L__", 0));
        pro2(g, AVR_IN, avr_reg(AVR_Y + 1), avr_sym(AVR_MOD_NONE, "__SP_H__", 0));
        if (!small) {
            adjust_y(g, true, -g->frame_size);
            write_sp(g, true);
        }
    }
    for (int r = 2; r <= 31; r++)
        if (saved & (1u << r))
            pro1(g, AVR_PUSH, avr_reg(r));

    // Epilogue, the reverse.
    avr_new_block(g->fn, g->exit);
    for (int r = 31; r >= 2; r--)
        if (saved & (1u << r))
            emit1(g, AVR_POP, avr_reg(r));
    if (!g->frameless) {
        if (small) {
            for (int i = 0; i < g->frame_size; i++)
                emit1(g, AVR_POP, avr_reg(AVR_TMP));
        } else {
            adjust_y(g, false, g->frame_size);
            write_sp(g, false);
        }
        emit1(g, AVR_POP, avr_reg(AVR_Y + 1));
        emit1(g, AVR_POP, avr_reg(AVR_Y));
    }
    emit0(g, AVR_RET);
}
