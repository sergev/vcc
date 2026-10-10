#include <stdio.h>
#include <string.h>

#include "besm.h"
#include "frame.h"
#include "internal.h"
#include "tac.h"
#include "xalloc.h"

bool besm_volatile_access;

// Append a new instruction to a block, maintaining *tail.
Besm_Instr *emit(Besm_Block *block, Besm_Instr **tail, Besm_InstrKind kind)
{
    Besm_Instr *i  = besm_new_instr(kind);
    i->is_volatile = besm_volatile_access;
    if (!block->body)
        block->body = i;
    else
        (*tail)->next = i;
    *tail = i;
    return i;
}

// Emit XTA: A = mem[reg + off].
void emit_xta(Besm_Block *b, Besm_Instr **t, int reg, int off)
{
    Besm_Instr *i = emit(b, t, BESM_MEM_XTA);
    i->reg        = reg;
    i->addr       = off;
}

// Emit ATX: mem[reg + off] = A.
void emit_atx(Besm_Block *b, Besm_Instr **t, int reg, int off)
{
    Besm_Instr *i = emit(b, t, BESM_MEM_ATX);
    i->reg        = reg;
    i->addr       = off;
}

// Emit an arithmetic instruction: A op= mem[reg + off].
static void emit_arith(Besm_Block *b, Besm_Instr **t, Besm_InstrKind kind, int reg, int off)
{
    Besm_Instr *i = emit(b, t, kind);
    i->reg        = reg;
    i->addr       = off;
}

// Frame lookup with fatal_error on miss.
void lookup(const Frame *f, const char *name, int *reg, int *off)
{
    if (!frame_lookup(f, name, reg, off))
        internal_error("besm6: variable '%s' is not in the frame", name);
}

// Store the accumulator A into a variable, whether a frame slot or a module-level
// global (UTC sets C to the global's address, then a bare ATX writes mem[C]).
void emit_store_a(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name)
{
    int reg, off;
    if (frame_lookup(f, name, &reg, &off)) {
        emit_atx(b, t, reg, off);
    } else {
        Besm_Instr *utc = emit(b, t, BESM_MOD_UTC);
        utc->name       = xstrdup(name); // C = &global
        emit(b, t, BESM_MEM_ATX);        // reg=0, addr=0 → mem[C] = A
    }
}

// Set the C address-modifier register to the value of a pointer variable, so a
// following bare XTA/ATX dereferences it (EA = C), or a bare VJM calls through it.
// One instruction either way: WTC (023) loads C from the low 15 bits of mem[EA],
// and it is a Format 2 instruction, so its address field is 15 bits wide and reaches
// any global directly — `wtc name` needs no UTC escape.  (The Format 1 accessors are
// the ones that do: see emit_asx_ptr.)  WTC touches neither A nor an index register,
// so this is safe between a source load and a store, and after the argument setup of
// a call.
void emit_wtc_ptr(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name)
{
    int reg, off;
    Besm_Instr *wtc = emit(b, t, BESM_MOD_WTC);
    if (frame_lookup(f, name, &reg, &off)) {
        wtc->reg  = reg;
        wtc->addr = off; // C = mem[slot] = pointer word (bits 15:1)
    } else {
        wtc->name = xstrdup(name); // C = mem[global] = pointer word
    }
}

// Emit ASX addressing a pointer variable's word.  A byte load through a fat pointer shifts
// the accumulator right by the pointer's byte offset, which lives in the exponent field of
// the pointer word itself — so the ASX operand is the pointer word.  For a frame-resident
// pointer this is a single ASX reg=slot.  For a module-level (global) pointer the pointer
// word is not in the frame: UTC sets C to the global's address, then a bare ASX (EA = C)
// uses mem[C] as its operand.  UTC does not touch A, so the shifted value is preserved.
// The escape is needed here and not in emit_wtc_ptr because ASX is a Format 1 instruction:
// its address field is only 12 bits and cannot hold a global's address.
void emit_asx_ptr(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name)
{
    int reg, off;
    if (frame_lookup(f, name, &reg, &off)) {
        Besm_Instr *asx = emit(b, t, BESM_EXP_SHIFTX);
        asx->reg        = reg;
        asx->addr       = off;
    } else {
        Besm_Instr *utc = emit(b, t, BESM_MOD_UTC);
        utc->name       = xstrdup(name); // C = &global
        emit(b, t, BESM_EXP_SHIFTX);     // reg=0, addr=0 → operand mem[C] = pointer word
    }
}

// Attach a TAC constant to an instruction as a structural operand.  The scalar is
// copied (Tac_Const is a flat POD) so the instruction owns it independently of the
// TAC being freed; each dialect's emitter later formats it into its own literal syntax
// (Madlen =octal/=rX, Unix #const pool, Bemsh =Ю'…').  Kept off `->name` so a name
// always means a symbol.
static Tac_Const *dup_const(const Tac_Const *c)
{
    Tac_Const *d = tac_new_const(c->kind);
    d->u         = c->u;
    return d;
}

// Give `i` the constant `c` as its operand -- unless `c` is zero, which needs no operand at
// all: the instruction is left with an empty address field, and EA = 0 reads memory word 0,
// which always holds zero.  The literal is never reserved, in the pool or anywhere else.
//
// The resulting bare instruction is the same shape as a C-group consumer (`utc g` + `xta`),
// and that is fine on both counts.  On the machine, C survives exactly one instruction and
// every C-setter here emits its consumer immediately, so a constant operand never lands in
// that slot.  In the peephole pass, a group is analysed as a unit and its consumer never
// reaches `plain_loc`, so a bare `xta` seen there is always this zero load.
static void attach_const(Besm_Instr *i, const Tac_Const *c)
{
    if (!besm_const_is_zero(c))
        i->konst = dup_const(c);
}

// Emit XTA for a TAC value: variable from frame, UTC+XTA for a global, or a constant.
void emit_xta_val(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *v)
{
    if (v->kind == TAC_VAL_VAR) {
        int reg, off;
        if (frame_lookup(f, v->u.var_name, &reg, &off)) {
            emit_xta(b, t, reg, off);
        } else {
            Besm_Instr *utc = emit(b, t, BESM_MOD_UTC);
            utc->name       = xstrdup(v->u.var_name);
            emit_xta(b, t, 0, 0);
        }
    } else {
        attach_const(emit(b, t, BESM_MEM_XTA), v->u.constant);
    }
}

// Emit XTS for a TAC value (push A to stack, load v into A) — used for args 1..N-1.
// For a global variable: UTC sets C to the global's address, then XTS loads mem[C+0].
void emit_xts_val(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *v)
{
    if (v->kind == TAC_VAL_VAR) {
        int reg, off;
        if (frame_lookup(f, v->u.var_name, &reg, &off)) {
            Besm_Instr *i = emit(b, t, BESM_MEM_XTS);
            i->reg        = reg;
            i->addr       = off;
        } else {
            Besm_Instr *utc = emit(b, t, BESM_MOD_UTC);
            utc->name       = xstrdup(v->u.var_name);
            Besm_Instr *i   = emit(b, t, BESM_MEM_XTS);
            (void)i; // reg=0, addr=0 → XTS mem[C+0]
        }
    } else {
        attach_const(emit(b, t, BESM_MEM_XTS), v->u.constant);
    }
}

// Emit an arithmetic instruction for a TAC value: local, global (via UTC), or constant literal.
void emit_arith_val(Besm_Block *b, Besm_Instr **t, Besm_InstrKind kind, const Frame *f,
                    const Tac_Val *v)
{
    if (v->kind == TAC_VAL_VAR) {
        int reg, off;
        if (frame_lookup(f, v->u.var_name, &reg, &off)) {
            emit_arith(b, t, kind, reg, off);
        } else {
            Besm_Instr *utc = emit(b, t, BESM_MOD_UTC);
            utc->name       = xstrdup(v->u.var_name);
            emit(b, t, kind); // reg=0, addr=0 → op mem[C+0]
        }
    } else {
        attach_const(emit(b, t, kind), v->u.constant);
    }
}
