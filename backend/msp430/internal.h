//
// MSP430 code generator internals (the MSP430 EABI as GCC implements it; see
// backend/msp430/Plan.md).
//
// Registers: r0 is PC, r1 SP, r2 SR (and a constant generator), r3 the other constant
// generator.  Arguments go in r12-r15, results in r12, r13:r12 or r15:r12.  r11-r15 are
// call-clobbered, r4-r10 call-saved.
//
// Every TAC variable lives in memory: a `%` name in a frame slot, reached as x(r1); any
// other name at its symbol, reached as &sym.  The ISA takes memory operands on both
// sides, so a copy, a store or a load is a memory-to-memory move; an operation loads its
// first operand into r12-r15 (block A, the first argument registers), takes the second
// straight from memory or as an immediate, and stores the result.  A helper is the
// one GCC's code calls, so that our objects link with libgcc as well as with our
// runtime: the __mspabi_* names with their operands in r12-r15, a 64-bit first operand
// in r8-r11 (the prologue then saves r8-r10); the libgcc predicates for an FP
// comparison, a second double on the stack.  r11 holds a 0/1 result, a shift count, a
// loop count or a call target; r14 and r15 hold pointers.
//
// Frame (SP is constant in the body; every offset is from it):
//   frame + 2*saved + 2 ...  incoming stack arguments
//   frame + 2*saved          return address
//   frame ...                the call-saved registers in use, pushed by the prologue
//   out ... frame - 1        slots: each aligned to its type
//   0 ... out - 1            outgoing stack arguments, for the call that needs most
// The saved registers are known only once the body is selected, so an operand into the
// incoming arguments is marked and completed by gen_frame.
//
#ifndef MSP_INTERNAL_H
#define MSP_INTERNAL_H

#include "msp_ir.h"
#include "string_map.h"
#include "tac.h"

typedef struct {
    const Tac_Type *type;
    int off;       // from SP, or from the incoming arguments' start
    bool incoming; // a parameter that lives where it came in, on the stack
} Slot;

typedef struct {
    Msp_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    Msp_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int out_size;      // bytes of outgoing stack arguments
    int frame_size;    // bytes of the outgoing area and the slots, even
    char exit[32];     // the label of the epilogue
} Gen;

//
// Types (frame.c)
//
int msp_type_size(const Tac_Type *t);
int msp_type_align(const Tac_Type *t);
bool msp_is_unsigned(const Tac_Type *t); // unsigned integers and pointers
bool msp_is_fp(const Tac_Type *t);       // float, double, long double
bool msp_is_scalar(const Tac_Type *t);
// The registers (16-bit words) a scalar of type `t` takes: 1 for a char.
int msp_words(const Tac_Type *t);

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
// The outgoing area (out_size), then the slots of the parameters and locals; the stack
// parameters already have theirs (place_stack_param).
void layout_frame(Gen *g);
// Parameter `name` lives `off` bytes into the incoming stack arguments.
void place_stack_param(Gen *g, const char *name, const Tac_Type *type, int off);
const Slot *find_slot(const Gen *g, const char *name);
const Tac_Type *name_type(const Gen *g, const char *name);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
Msp_Instr *emit0(Gen *g, Msp_Op op);
Msp_Instr *emit1(Gen *g, Msp_Op op, Msp_Operand a);
Msp_Instr *emit2(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b);
// The same in the .b form.
Msp_Instr *emit1b(Gen *g, Msp_Op op, Msp_Operand a);
Msp_Instr *emit2b(Gen *g, Msp_Op op, Msp_Operand a, Msp_Operand b);
// The bits of integer or FP constant `c`, little-endian: an integer sign- or
// zero-extended by its kind, a float as binary32, a double or long double as binary64.
uint64_t const_bits(const Tac_Const *c);

// Byte `off` of named object `name`: x(r1) for a slot, &name+off for a global.
Msp_Operand mem_at(const Gen *g, const char *name, int off);
// Byte `off` of the incoming stack arguments.
Msp_Operand incoming_at(int off);
// Word `i` of scalar `v` (its byte, for a char): an immediate for a constant.
Msp_Operand val_word(const Gen *g, const Tac_Val *v, int i);
// `reg` = the address of named object `name` + off.
void address_of(Gen *g, int reg, const char *name, int off);
// Copy `size` bytes from the address in r14 to the address in r15, by words when
// `align` is 2 (and the size even), else by bytes; both registers are changed.
void copy_bytes(Gen *g, int size, int align);
// Copy `size` bytes of named objects: dst+doff = src+soff.
void copy_named(Gen *g, const char *dst, int doff, const char *src, int soff, int size,
                int align);

typedef enum {
    EXT_TYPE, // by the value's own type: sign for a signed integer, else zero
    EXT_ZERO,
    EXT_SIGN,
} Ext;
// Load value `v` into registers reg..reg+n-1: its low n words, or all of it extended
// as `ext` says when it is narrower.  A char is one word.
void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext);
// Store reg..reg+n-1 into variable `v`: its low words (a char's low byte), or with zero
// high words when it is wider.
void store_val(Gen *g, const Tac_Val *v, int reg, int n);
// Fill reg+from..reg+n-1 by extending reg+from-1: zeros, or copies of its sign.
void extend_regs(Gen *g, int reg, int from, int n, bool sign);
// Start a new block labelled `label`.
void gen_label_block(Gen *g, const char *label);
// Fill the prologue and the epilogue, once the body is done, and complete the offsets
// into the incoming arguments.
void gen_frame(Gen *g);

//
// Static data (data.c)
//
// `alignment` (from _Alignas) is used when stricter than the type's.
void emit_static_variable(FILE *out, const Tac_TopLevel *program, const char *name, bool global,
                          const Tac_Type *type, const Tac_StaticInit *init, bool readonly,
                          int alignment);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in, bool last);
// r11 = 1 when `cond` (a conditional jump) holds on the flags as they are, else 0.
void gen_set_on(Gen *g, Msp_Op cond);
// Call runtime helper `name`.
void call_helper(Gen *g, const char *name);
// The outgoing stack bytes instruction `in` needs: a call's stack arguments, or a
// 64-bit helper's second operand.
int instr_out_size(const Gen *g, const Tac_Instruction *in);

//
// Floating point, in software (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
// The outgoing stack bytes FP operator `op` on `size`-byte operands needs: 8 for a
// binary64 comparison's second operand.
int fp_out_size(Tac_BinaryOperator op, int size);
void gen_fp_unary(Gen *g, const Tac_Instruction *in);
// The zero flag of FP value `v`: set when it is a zero of either sign (not a NaN).
void gen_fp_test(Gen *g, const Tac_Val *v);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Calls, parameters and returns (call.c)
//
// Where the parameters arrive: slots over the stack ones, before layout_frame.
void place_params(Gen *g);
// Store the register parameters (and the stack part of a split long) into their slots.
void store_params(Gen *g);
void gen_return(Gen *g, const Tac_Val *v, bool last);
// A call, direct or through a pointer; FUN_CALL_NORETURN too.
void gen_call(Gen *g, const Tac_Instruction *in);
// The stack bytes of the arguments of call `in`.
int call_stack_size(const Gen *g, const Tac_Instruction *in);

//
// Branch relaxation (relax.c)
//
void msp_relax(Msp_Func *fn);

#endif // MSP_INTERNAL_H
