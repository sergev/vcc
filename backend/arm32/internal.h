//
// ARM32 code generator internals (AAPCS-VFP; see backend/arm32/Plan.md).
//
// Registers: r0-r3/s0-s15 (d0-d7) carry arguments and results; r4-r11 and d8-d15 are
// callee-saved; r11 is the frame pointer, r13 sp, r14 lr, r15 pc.
//
// Every `%` name lives in a slot at a fixed offset from r11, any other name at its
// symbol.  An instruction loads its operands into scratch registers, computes, and
// stores the result back.
//
// Frame (r11 = sp after the frame record is pushed, 8-byte aligned):
//   r11 + 8 ...      incoming stack arguments
//   r11 + 4          saved lr
//   r11 + 0          saved r11
//   r11 - ...        slots
//   below them       the callee-saved scratch registers in use: d14/d15, then r10
//   sp + 0 ...       outgoing stack arguments
//
// A value in a register is in canonical form: an integer of 32 bits or fewer extended
// to 32 bits by its own type (AAPCS has the sender extend, and the receiver relies on
// it); float in an s register, double and long double (the same type here) in a d
// register.  A long long is two words, the low one first, and is operated on in a
// pair of registers.
//
// Scratch registers: r12 and lr hold operands; r10 is the third, for an address while
// both hold values (a long long stored to a global, a copy between two addresses) and
// the quotient of a remainder; d14 and d15 (s28 and s30 at single precision) hold FP
// operands.  r10, d14 and d15 are callee-saved, so a function that uses one saves it.
//
#ifndef ARM32_INTERNAL_H
#define ARM32_INTERNAL_H

#include "a32.h"
#include "string_map.h"
#include "tac.h"

enum {
    T0 = A32_IP,
    T1 = A32_LR,
    T2 = A32_R10,
    F0 = A32_S0 + 28, // d14
    F1 = A32_S0 + 30, // d15
};

// d<k> and s<k> as register numbers.
#define A32_DREG(k) (A32_S0 + 2 * (k))
#define A32_SREG(k) (A32_S0 + (k))

typedef struct {
    const Tac_Type *type;
    int offset; // from r11
} Slot;

typedef struct {
    A32_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    A32_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int locals_size;   // bytes of slots below the frame record
    int outgoing;      // bytes of the outgoing argument area
} Gen;

//
// Types (frame.c)
//
int a32_size(const Tac_Type *t);
int a32_align(const Tac_Type *t);
bool a32_is_fp(const Tac_Type *t);     // float, double or long double
bool a32_is_double(const Tac_Type *t); // double or long double
bool a32_is_pair(const Tac_Type *t);   // long long, signed or not
bool a32_is_unsigned(const Tac_Type *t);
bool a32_is_aggregate(const Tac_Type *t);

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
A32_Instr *emit0(Gen *g, A32_Op op);
A32_Instr *emit1(Gen *g, A32_Op op, A32_Operand a);
A32_Instr *emit2(Gen *g, A32_Op op, A32_Operand a, A32_Operand b);
A32_Instr *emit3(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c);
A32_Instr *emit4(Gen *g, A32_Op op, A32_Operand a, A32_Operand b, A32_Operand c, A32_Operand d);
// reg = imm (32 bits).
void gen_li(Gen *g, int reg, uint32_t imm);
// reg = base + off, by add or sub of up to four modified immediates.
void gen_addr(Gen *g, int reg, int base, int64_t off);
// A memory operand for base + off as instruction `op` takes it; through core register
// `scratch` when the offset is out of the instruction's reach.
A32_Operand mem(Gen *g, A32_Op op, int base, int64_t off, int scratch);
// Where named object `name` is: r11 + offset, or `scratch` + 0 after loading a global's
// address into `scratch`.
void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off);
// Load/store a scalar of type `t` (of at most 4 bytes in a core register) between
// `reg` and base + off; a large offset goes through `scratch` (a load into a core
// register uses that register).
void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off);
void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off, int scratch);
// The bits of integer or FP constant `c` converted to type `t`: an integer by C's
// conversion, a long double read as the double it is here.
uint64_t const_bits(const Tac_Const *c, const Tac_Type *t);
// FP register `reg` = constant `c` of type `t`, its bits through core registers `lo`
// (and `hi` for a double).
void load_fp_const(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t, int lo, int hi);
// Load scalar value `v` into `reg`: a core register for an integer of at most 4 bytes
// (or a float's bits), a VFP register at the width of its FP type.  `as` is the type a
// constant is converted to (NULL: its own).  r12 and lr may change.
void load_val(Gen *g, int reg, const Tac_Val *v);
void load_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as);
// Store `reg` into variable `v` in the width of its type.  The address of a global
// goes through r12, or lr when `reg` is r12.
void store_val(Gen *g, int reg, const Tac_Val *v);
// Word `half` (0 low, 1 high) of 8-byte value `v` (a long long, or a narrower integer
// converted to one; or a double's bits) into core register `reg`.
void load_word(Gen *g, int reg, const Tac_Val *v, const Tac_Type *as, int half);
// 8-byte variable `dst` = lo, hi (core registers).
void store_pair(Gen *g, const Tac_Val *dst, int lo, int hi);
// Copy `size` bytes from src + src_off to dst + dst_off; the bases are not r12, lr
// or r10.
void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align);
// The return sequence, filled in once the frame is known.
void gen_epilogue(Gen *g);
// Fill the prologue and the epilogues.
void gen_prologue(Gen *g);

//
// Calls, parameters and returns (call.c)
//
void gen_params(Gen *g);
void gen_return(Gen *g, const Tac_Val *v);

//
// 64-bit integers (llong.c)
//
void gen_ll_binary(Gen *g, const Tac_Instruction *in);
void gen_ll_unary(Gen *g, const Tac_Instruction *in);
// Whether conversion `kind` is from an unsigned integer.
bool from_unsigned(Tac_InstructionKind kind);
// A conversion between long long and float or double.
void gen_ll_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);
// The condition of comparison `op`, or -1 when it is not one.
int compare_cond(Tac_BinaryOperator op, bool is_unsigned);
// Whether operation `op` on type `t` is unsigned: by the operator, as the
// operands' types may differ once copy propagation has removed a cast, or a pointer.
bool unsigned_operation(const Tac_Type *t, Tac_BinaryOperator op);
// A branch to TAC label `tac` when condition `cond` holds (A32_AL: always).
void gen_branch(Gen *g, int cond, const char *tac);
// reg = 1 when condition `cond` holds, else 0; the flags are kept.
void set_cond(Gen *g, int reg, int cond);
// Operand 2 of `op` for integer value `v` of type `t`: a modified immediate when the
// constant is one, or when its negation (add, sub, cmp) or complement (and) is, with
// *op changed to the counterpart; else core register `scratch`, loaded.
A32_Operand operand2(Gen *g, A32_Op *op, const Tac_Val *v, const Tac_Type *t, int scratch);

#endif // ARM32_INTERNAL_H
