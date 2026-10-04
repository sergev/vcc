//
// AVR code generator internals (the avr-gcc ABI; see backend/avr/Plan.md).
//
// Registers: r0 is scratch (__tmp_reg__), r1 is zero at every call and return
// (__zero_reg__).  Arguments go from r25 down to r8, results in r24, r25:r24, r25:r22
// or r25:r18.  r18-r27 and r30-r31 are call-clobbered, r2-r17 and r28-r29 call-saved;
// Y (r29:r28) is the frame pointer, X (r27:r26) and Z (r31:r30) the pointer scratch of
// instruction selection.
//
// Every TAC variable lives in memory: a `%` name in a frame slot, any other name at its
// symbol.  An operation loads its operands into two register blocks laid out as the
// first two arguments of a call (block_a and block_b), computes in place, and stores the
// result; the helpers with special contracts (__divmodhi4, __mulsi3, ...) then need no
// moves.
//
// Frame (Y is set to SP after the slots are reserved; SP points below the last byte):
//   Y + frame + 5 ...   incoming stack arguments
//   Y + frame + 3       return address (2 bytes)
//   Y + frame + 1       saved Y
//   Y + 1 ...           slots: scalars first, then aggregates
//   below Y             the call-saved registers in use, pushed after Y is set up
// A slot is reached as Y+q while its last byte is within Y+63, else through a pointer
// register loaded with its address (access_bytes).
//
#ifndef AVR_INTERNAL_H
#define AVR_INTERNAL_H

#include "avr_ir.h"
#include "string_map.h"
#include "tac.h"

enum {
    Y_MAX = 63, // the largest displacement of ldd/std
};

typedef struct {
    const Tac_Type *type;
    int q; // displacement from Y of the first byte
} Slot;

typedef struct {
    AVR_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    AVR_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int frame_size;    // bytes of slots
    char exit[32];     // the label of the epilogue
} Gen;

//
// Types (frame.c)
//
int avr_type_size(const Tac_Type *t);
bool avr_is_unsigned(const Tac_Type *t); // unsigned integers and pointers
bool avr_is_fp(const Tac_Type *t);       // float, double, long double
bool avr_is_scalar(const Tac_Type *t);
// The first register of operand block A or B of an operation on `size` bytes: A is
// r24 or r(26-size)..r25, B is r22 or the `size` registers below A (an 8-byte B is
// r10-r17, call-saved).
int block_a(int size);
int block_b(int size);

//
// Frame and value access (frame.c)
//
// A new translation unit: label numbering starts over.
void gen_unit_begin(void);
// A new local label `.Lv<n>`, unique in the translation unit, into `buf`.
void new_label(char buf[32]);
void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl);
void gen_done(Gen *g);
const char *gen_name(const Gen *g);
// The slots of the parameters and locals, the stack parameters' placed first;
// ALLOCATE_LOCAL may ask for more room.
void layout_frame(Gen *g);
// Give parameter `name` a slot over its incoming stack argument, `off` bytes into the
// stack argument area.
void place_stack_param(Gen *g, const char *name, const Tac_Type *type, int off);
const Slot *find_slot(const Gen *g, const char *name);
const Tac_Type *name_type(const Gen *g, const char *name);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
// Whether global `name` is a function, whose address is a word address in flash.
bool is_function(const Gen *g, const char *name);
AVR_Instr *emit0(Gen *g, AVR_Op op);
AVR_Instr *emit1(Gen *g, AVR_Op op, AVR_Operand a);
AVR_Instr *emit2(Gen *g, AVR_Op op, AVR_Operand a, AVR_Operand b);
// reg = imm (one byte), into any register: ldi for r16-r31, r1 for zero, else through
// r26.
void gen_li(Gen *g, int reg, int imm);
// The bytes of integer or FP constant `c`, little-endian: an integer sign- or
// zero-extended by its kind, a floating-point one as binary32.
uint64_t const_bits(const Tac_Const *c);
// Load (or store) registers reg..reg+n-1 from (or to) bytes off..off+n-1 of named
// object `name`.
void access_bytes(Gen *g, bool store, const char *name, int off, int reg, int n);

// Load register `reg` from byte `off` of the incoming stack arguments.
void access_incoming(Gen *g, int off, int reg);
// Point pointer register `ptr` (X or Z) at byte `off` of named object `name`.
void address_of(Gen *g, int ptr, const char *name, int off);
// Copy `size` bytes from X to Z, both advanced: unrolled up to 16, else a loop counted
// in r25:r24.
void copy_bytes(Gen *g, int size);

typedef enum {
    EXT_TYPE, // by the value's own type: sign for a signed integer, else zero
    EXT_ZERO,
    EXT_SIGN,
} Ext;
// Load value `v` into reg..reg+n-1: its low n bytes, or all of it extended as `ext`
// says when it is narrower.
void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext);
// Store reg..reg+n-1 into variable `v`: its low bytes, or with zero high bytes when it
// is wider.
void store_val(Gen *g, const Tac_Val *v, int reg, int n);
// rd..rd+n-1 = rs..rs+n-1, by pairs where both are even; nothing when the same.
void move_regs(Gen *g, int rd, int rs, int n);
// Fill reg+from..reg+n-1 by extending reg+from-1: zeros, or copies of its sign.
void extend_regs(Gen *g, int reg, int from, int n, bool sign);
// Start a new block labelled `label`.
void gen_label_block(Gen *g, const char *label);
// Fill the prologue and the epilogue, once the body is done.
void gen_frame(Gen *g);

//
// Static data (data.c)
//
// `alignment` (from _Alignas) is used when stricter than 1.
void emit_static_variable(FILE *out, const Tac_TopLevel *program, const char *name, bool global,
                          const Tac_Type *type, const Tac_StaticInit *init, bool readonly,
                          int alignment);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in, bool last);
// r24 = 1 when branch `br` would be taken on the flags as they are, else 0.
void gen_set_on(Gen *g, AVR_Op br);

//
// Floating point, in software (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
void gen_fp_unary(Gen *g, const Tac_Instruction *in);
// The zero flag of FP value `v`: set when it is a zero of either sign (not a NaN).
void gen_fp_test(Gen *g, const Tac_Val *v);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Calls, parameters and returns (call.c)
//
// Slots over the incoming stack arguments, before layout_frame.
void place_params(Gen *g);
// Store the register parameters into their slots.
void store_params(Gen *g);
void gen_return(Gen *g, const Tac_Val *v, bool last);
// A call, direct or through a pointer; FUN_CALL_NORETURN too.
void gen_call(Gen *g, const Tac_Instruction *in);

#endif // AVR_INTERNAL_H
