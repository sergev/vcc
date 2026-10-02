//
// AArch64 code generator internals (AAPCS64; see backend/aarch64/Plan.md).
//
// Registers: x0-x7/v0-v7 carry arguments and results, x8 the indirect result address;
// x9-x15, x16/x17 and v16-v31 are scratch; x19-x28 and the low halves of v8-v15 are
// callee-saved; x18 is never used; x29 is the frame pointer, x30 the link register.
//
// Every `%` name lives in a slot at a fixed offset from x29, any other name at its
// symbol.  An instruction loads its operands into scratch registers, computes, and
// stores the result.
//
// Frame (x29 = sp after the frame record is pushed, 16-byte aligned):
//   x29 + 16 ...     incoming stack arguments
//   x29 + 8          saved x30
//   x29 + 0          saved x29
//   x29 - ...        slots
//   sp + 0 ...       outgoing stack arguments
//
// A value in a register: a type of 32 bits or fewer in the W view, extended to 32 bits
// by its own type (the upper half zero, as every W write leaves it); a 64-bit one in
// the X view; float and double in S and D.  AAPCS64 leaves the upper bits of a narrow
// argument or result unspecified, so whoever receives one extends it.
//
// Scratch registers: x9-x11 and v16-v18 hold operands, x12/x13 the addresses of an
// aggregate copy, x14 the address of a global, x16 a large offset or address, x17 the
// bits of an FP constant.
//
#ifndef AARCH64_INTERNAL_H
#define AARCH64_INTERNAL_H

#include "a64.h"
#include "string_map.h"
#include "tac.h"

enum {
    T0  = A64_X(9),
    T1  = A64_X(10),
    T2  = A64_X(11),
    T3  = A64_X(12),
    T4  = A64_X(13),
    T5  = A64_X(14),
    IP0 = A64_X16,
    IP1 = A64_X17,
    F0  = A64_V(16),
    F1  = A64_V(17),
    F2  = A64_V(18),
};

typedef struct {
    const Tac_Type *type;
    int offset; // from x29
} Slot;

typedef struct {
    A64_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    A64_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int locals_size;   // bytes of slots below the frame record
    int max_align;     // of any slot
    int outgoing;      // bytes of the outgoing argument area
} Gen;

//
// Types (frame.c)
//
int a64_size(const Tac_Type *t);
int a64_align(const Tac_Type *t);
bool a64_is_fp(const Tac_Type *t); // float or double
bool a64_is_double(const Tac_Type *t);
bool a64_is_ld(const Tac_Type *t); // long double
bool a64_is_unsigned(const Tac_Type *t);
bool a64_is_aggregate(const Tac_Type *t);
// The register view of a scalar of type `t`: W, X, S or D.
A64_Width a64_width(const Tac_Type *t);

//
// Frame and value access (frame.c)
//
void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl);
void gen_done(Gen *g);
const char *gen_name(const Gen *g);
// A new slot of `size` bytes for `name` (or anonymous when NULL); returns its offset.
int alloc_slot(Gen *g, const char *name, const Tac_Type *type, int size, int align);
// Give `name` a slot at a fixed offset (an incoming stack argument).
void place_slot(Gen *g, const char *name, const Tac_Type *type, int offset);
const Slot *find_slot(const Gen *g, const char *name);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
const Tac_Type *name_type(const Gen *g, const char *name);
// A memory operand for base + off, accessing `size` bytes; through ip0 when the offset
// fits neither an ldr/str nor an ldur/stur.
A64_Operand mem(Gen *g, int base, int64_t off, int size);
// reg = base + off.
void gen_addr(Gen *g, int reg, int base, int64_t off);
// Where named object `name` is: x29 + offset, or `scratch` + 0 after loading a global's
// address into `scratch`.
void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off);
// Load/store a scalar of type `t` between `reg` (general or FP) and base + off.
void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off);
void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off);
// Load scalar value `v` into `reg`, in the view of its own type; store `reg` into a
// variable, in the view of the variable's type.
void load_val(Gen *g, int reg, const Tac_Val *v);
void store_val(Gen *g, int reg, const Tac_Val *v);
// Load integer value `v` into `reg` for an operation on type `t`: a variable as its own
// type, a constant converted to `t` (its own kind may differ).
void load_int_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *t);
// The value of integer constant `c` as its type says, in 64 bits.
int64_t const_value(const Tac_Const *c);
// Load integer constant `c` converted to integer type `t`, in its register form.
void load_const_as(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t);
// Copy `size` bytes; the bases are registers other than x11 and ip0.
void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align);
A64_Instr *emit1(Gen *g, A64_Op op, A64_Operand a);
A64_Instr *emit2(Gen *g, A64_Op op, A64_Operand a, A64_Operand b);
A64_Instr *emit3(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c);
A64_Instr *emit4(Gen *g, A64_Op op, A64_Operand a, A64_Operand b, A64_Operand c, A64_Operand d);
// reg = imm, at `width` (the value truncated to it).
void gen_li(Gen *g, int reg, A64_Width width, int64_t imm);
// A marker for the frame teardown, then ret.
void gen_epilogue(Gen *g);
// Fill the prologue and the epilogues, once the frame is known.
void gen_prologue(Gen *g);

//
// Calls, parameters and returns (call.c)
//
// A slot for each parameter, stored from its register or placed over its stack slot.
void gen_params(Gen *g);
void gen_call(Gen *g, const Tac_Instruction *in);
void gen_return(Gen *g, const Tac_Val *v);

//
// Static data (data.c)
//
// `alignment` (from _Alignas) is used when stricter than the type's.
void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);

#endif // AARCH64_INTERNAL_H
