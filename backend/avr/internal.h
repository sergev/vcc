//
// AVR code generator internals (the avr-gcc ABI; see docs/Avr_Backend.md).
//
// Registers: r0 is scratch (__tmp_reg__), r1 is zero at every call and return
// (__zero_reg__).  Arguments go from r25 down to r8, results in r24, r25:r24, r25:r22
// or r25:r18.  r18-r27 and r30-r31 are call-clobbered, r2-r17 and r28-r29 call-saved;
// Y (r29:r28) is the frame pointer, X (r27:r26) and Z (r31:r30) the pointer scratch of
// instruction selection.
//
// A scalar variable not in memory may get registers (regalloc.c): an int, pointer or
// char a pair, a long or float two pairs, from r24-r18 when it is not live across a
// call or a helper, else from r16-r2.  The rest live in memory: a `%` name in a frame
// slot, any other name at its symbol.
//
// Instruction selection has two forms.  The naive one loads the operands into two
// register blocks laid out as the first two arguments of a call (block_a and block_b),
// computes in place and stores the result; the helpers with special contracts
// (__divmodhi4, __mulsi3, ...) then need no moves.  It is all there is without register
// allocation.  With it, an instruction that needs no helper and no more than four
// bytes computes in its destination's registers, or in X and Z, with r0 for an operand
// byte from memory (uses_scratch tells the two apart, for the allocator too).
//
// Frame (Y is set to SP after the slots are reserved, with `rcall .` for up to 6 bytes;
// SP points below the last byte), unless the function has neither slots nor stack
// arguments: it then saves no Y and leaves SP alone, and Y may hold variables:
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
#include "flow.h"
#include "string_map.h"
#include "tac.h"

enum {
    Y_MAX = 63, // the largest displacement of ldd/std
};

typedef struct {
    const Tac_Type *type;
    int q; // displacement from Y of the first byte
} Slot;

// The registers of a value, byte by byte (a structure piece takes up to 18).
typedef struct {
    int n;
    int r[32];
} Regs;

typedef struct {
    AVR_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    AVR_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    StringMap locals;  // parameter or local → const Tac_Type *
    bool alloc;        // registers allocated: the scratch-free selection
    StringMap regs;    // register variable → low pair | high pair << 8
    StringMap dead;    // parameters dead on entry: left where they arrive
    uint32_t var_regs; // the registers holding variables, as a bit mask
    bool y_free;       // Y may hold variables: the function is to have no frame
    bool stack_args;   // some parameter (or part of one) comes on the stack
    bool frameless;    // no slots and no stack arguments: Y is not set up
    bool vol;          // the TAC instruction being selected is a volatile access
    const Flow *flow;  // with the peephole pass: for compare-and-branch fusion
    int *uses;         // the reads of each flow variable
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
void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl, bool alloc);
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
// The same in memory, register by register: a slot past Y+63 through Z, or through X
// when Z is among `regs` or in `busy` (a register mask).
void access_mem(Gen *g, bool store, const char *name, int off, const int *regs, int n,
                uint32_t busy);
// Whether a slot's bytes off..off+n-1 are reached as Y+q (or the name is a global).
bool near_bytes(const Gen *g, const char *name, int off, int n);

// Registers reg..reg+n-1.
Regs regs_range(int reg, int n);
// Whether variable `name` (or value `v`) lives in registers; its bytes in *out.
bool var_regs(const Gen *g, const char *name, Regs *out);
bool val_regs(const Gen *g, const Tac_Val *v, Regs *out);
// The byte moves dst[i] = src[i] at once, in an order that reads every source before
// it is overwritten; a cycle is broken through X, Z or r0 when no move touches it
// (none holds a value across a parallel move otherwise), else through the stack.
void parallel_move(Gen *g, const int *dst, const int *src, int n);
// Push (or pop, in reverse) the registers lo..hi holding variables; returns the mask.
uint32_t save_var_regs(Gen *g, int lo, int hi);
void restore_var_regs(Gen *g, uint32_t mask);

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
// Value `v` into registers `to`: its low to.n bytes, or all of it extended as `ext`
// says when it is narrower.
typedef struct {
    const Tac_Val *v;
    Regs to;
    Ext ext;
} Load;
// Whether a value of type `t` extended as `ext` says gets copies of its sign.
bool ext_sign(const Tac_Type *t, Ext ext);
// The bits of constant `c` of `size` bytes as `n` bytes, extended as `ext` says.
uint64_t const_extended(const Tac_Const *c, int size, int n, Ext ext);
// Several loads at once: no load overwrites a register another still reads.
void load_vals(Gen *g, const Load *l, int n);
void load_regs(Gen *g, const Tac_Val *v, const Regs *to, Ext ext);
// Value `v` into reg..reg+n-1.
void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext);
// Two values at once.
void load_two(Gen *g, const Tac_Val *v1, int reg1, int n1, const Tac_Val *v2, int reg2, int n2);
// Store registers `regs` (n of them) into variable `v`: its low bytes, or with zero
// high bytes when it is wider.
void store_regs(Gen *g, const Tac_Val *v, const int *regs, int n);
void store_val(Gen *g, const Tac_Val *v, int reg, int n);
// Fill r[from..n-1] by extending r[from-1]: zeros, or copies of its sign.
void extend_regs(Gen *g, const int *r, int from, int n, bool sign);
// Start a new block labelled `label`.
void gen_label_block(Gen *g, const char *label);
// Fill the prologue and the epilogue, once the body is done.
void gen_frame(Gen *g);
// Whether every slot of up to 4 bytes is reached as Y+q, as the scratch-free
// selection needs.
bool frame_is_near(const Gen *g);

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
// The type of an operand, for uses_scratch.
typedef const Tac_Type *(*TypeOf)(const void *arg, const Tac_Val *v);
// Whether `in`, not a call, needs the naive selection's registers r18-r25 (or calls a
// helper): the allocator then keeps the values live across it out of them.
bool uses_scratch(const Tac_Instruction *in, TypeOf type_of, const void *arg);
// The variable `in` writes, or NULL (a call's is its own).
const Tac_Val *instr_dst(const Tac_Instruction *in);
void gen_instr(Gen *g, const Tac_Instruction *in, bool last);
// A comparison `in` whose result only `next`, a conditional jump, reads: the compare
// and the branch, no 0 or 1 in between; false when they are not such a pair.
bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next);
// r24 = 1 when branch `br` would be taken on the flags as they are, else 0.
void gen_set_on(Gen *g, AVR_Op br);

//
// Floating point, in software (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
// An FP comparison up to the flags; returns the branch taken when it holds, or
// AVR_NUM_OPS when `in` is no comparison.
AVR_Op gen_fp_compare(Gen *g, const Tac_Instruction *in);
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
// Register allocation hints: the registers the parameters arrive in, and those of a
// call's arguments and result.
void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi);
void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint);
// The type of `v` in function `f` (before the frame is laid out).
const Tac_Type *flow_val_type(const Gen *g, const Flow *f, const Tac_Val *v);

//
// Register allocation (regalloc.c)
//
void gen_regalloc(Gen *g);

#endif // AVR_INTERNAL_H
