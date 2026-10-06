//
// MSP430 code generator internals (the MSP430 EABI as GCC implements it; see
// docs/Msp430_Backend.md).
//
// Registers: r0 is PC, r1 SP, r2 SR (and a constant generator), r3 the other constant
// generator.  Arguments go in r12-r15, results in r12, r13:r12 or r15:r12.  r11-r15 are
// call-clobbered, r4-r10 call-saved.
//
// A scalar variable not in memory may get registers (regalloc.c): an int, pointer or
// char one, a long or float two, from r12-r14 and r11 when it is not live across a call
// or a helper, else from r10-r4, which the prologue then pushes.  The rest live in
// memory: a `%` name in a frame slot, reached as x(r1); any other name at its symbol,
// reached as &sym.
//
// Every operand of the ISA may be a register or memory, on both sides, so an operation
// works where its values are: a copy is a move, `d = a + b` is `mov a, d; add b, d`
// whether d is a register or a slot, a compare is a `cmp` of the operands in place.  r15
// is the one scratch register: it is never allocated, and it serves as a pointer, a
// count or an intermediate.  A helper is the one GCC's code calls, so that our objects
// link with libgcc as well as with our runtime: the __mspabi_* names with their operands
// in r12-r15, a 64-bit first operand in r8-r11 (r8-r10 then hold no variable, and the
// prologue saves them); the libgcc predicates for an FP comparison, a second double on
// the stack.  The operands go into place all at once (parallel_moves), so that none is
// overwritten before it is read; an instruction with a helper counts as a call for the
// allocator.
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

#include "flow.h"
#include "msp_ir.h"
#include "string_map.h"
#include "tac.h"

enum {
    MSP_SCRATCH = 15, // the selection's scratch register, never allocated
};

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
    StringMap regs;    // register variable → low register | high register << 8
    StringMap dead;    // parameters dead on entry: left where they arrive
    StringMap byref;   // structure parameters read through their pointer, uncopied
    bool no_r8;        // a helper takes r8-r11: no variable there
    int out_size;      // bytes of outgoing stack arguments
    int frame_size;    // bytes of the outgoing area and the slots, even
    int sp_bias;       // bytes pushed for the moment: added to every x(r1)
    bool vol;          // the TAC instruction being selected is a volatile access
    const Flow *flow;  // with the peephole pass: for compare-and-branch fusion
    int *uses;         // the reads of each flow variable
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

// The register of word `word` of variable `name`, or 0 when it lives in memory.
int var_reg(const Gen *g, const char *name, int word);
// Byte `off` of named object `name`: its register (word off/2) for a register
// variable, x(r1) for a slot, &name+off for a global.
Msp_Operand mem_at(const Gen *g, const char *name, int off);
// Byte `off` of named object `name` in its slot or at its symbol, a structure parameter
// read through its pointer included (whose slot holds the pointer).
Msp_Operand slot_at(const Gen *g, const char *name, int off);
// Byte `off` of the incoming stack arguments.
Msp_Operand incoming_at(int off);
// Word `i` of scalar `v` (its byte, for a char): an immediate for a constant.
Msp_Operand val_word(const Gen *g, const Tac_Val *v, int i);
// Whether two operands are the same register or the same memory word.
bool same_opnd(const Msp_Operand *a, const Msp_Operand *b);
// Whether operand `o` reads or writes register `reg`, as itself or as a base.
bool opnd_uses_reg(const Msp_Operand *o, int reg);
// The high byte of word operand `o`, a register's excepted.
Msp_Operand high_byte(const Msp_Operand *o);
// dst = the address of named object `name` + off.
void address_of(Gen *g, Msp_Operand dst, const char *name, int off);
// Copy `size` bytes between the memory at register `ptr` and named object `name` + off:
// into the object (`load`), or out of it.  By words when `align` is 2 (and the size
// even), else by bytes; unrolled up to 16 moves, else a loop through r15 and r13-r14,
// which it pushes.  Only r15 is changed.
void copy_ptr(Gen *g, bool load, int ptr, const char *name, int off, int size, int align);
// Copy `size` bytes of named objects: dst+doff = src+soff.
void copy_named(Gen *g, const char *dst, int doff, const char *src, int soff, int size,
                int align);

// The moves dst = src at once, in an order that reads every source before it is
// overwritten; a cycle is broken by swapping two locations with three `xor`s.
typedef struct {
    Msp_Operand dst, src;
    bool byte;
} Move;
void parallel_moves(Gen *g, Move *m, int n);

typedef enum {
    EXT_TYPE, // by the value's own type: sign for a signed integer, else zero
    EXT_ZERO,
    EXT_SIGN,
} Ext;
// Value `v` into registers reg..reg+n-1: its low n words, or all of it extended as `ext`
// says when it is narrower.  A char is one word.
typedef struct {
    const Tac_Val *v;
    int reg, n;
    Ext ext;
} Load;
// Several loads at once: none overwrites a register another still reads.
void load_vals(Gen *g, const Load *l, int n);
void load_val(Gen *g, const Tac_Val *v, int reg, int n, Ext ext);
// Store reg..reg+n-1 into variable `v`: its low words (a char's low byte), or with zero
// high words when it is wider.
void store_val(Gen *g, const Tac_Val *v, int reg, int n);
// Fill w[from..n-1] by extending w[from-1]: zeros, or copies of its sign.
void extend_words(Gen *g, const Msp_Operand *w, int from, int n, bool sign);
// Whether a value of type `t` extended as `ext` says gets copies of its sign.
bool ext_sign(const Tac_Type *t, Ext ext);
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
// A comparison (or a logical not) `in` whose result only `next`, a conditional jump,
// reads: the compare and the jump, no 0 or 1 in between; false when they are not such a
// pair.
bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next);
// d = 1 when `cond` (a conditional jump) holds on the flags as they are, else 0.
void gen_set_on(Gen *g, Msp_Op cond, const Tac_Val *d);
// Call runtime helper `name`.
void call_helper(Gen *g, const char *name);
// The outgoing stack bytes instruction `in` needs: a call's stack arguments, or a
// 64-bit helper's second operand.
int instr_out_size(const Gen *g, const Tac_Instruction *in);
// Whether `in`, not a call, calls a helper (the allocator then keeps the values live
// across it out of r11-r14); in *r8 whether the helper takes r8-r11.
bool uses_helper(const Gen *g, const Tac_Instruction *in, bool *r8);

//
// Floating point, in software (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
// An FP comparison up to the flags; returns the jump taken when it holds, or
// MSP_NUM_OPS when `in` is no comparison (nothing emitted).
Msp_Op gen_fp_compare(Gen *g, const Tac_Instruction *in);
// The outgoing stack bytes FP operator `op` on `size`-byte operands needs: 8 for a
// binary64 comparison's second operand.
int fp_out_size(Tac_BinaryOperator op, int size);
void gen_fp_unary(Gen *g, const Tac_Instruction *in);
// The zero flag of FP value `v`: set when it is a zero of either sign (not a NaN).
void gen_fp_test(Gen *g, const Tac_Val *v);
// Whether FP binary operator `op` on `size`-byte operands calls an arithmetic helper
// (else a comparison's).
bool fp_arith(Tac_BinaryOperator op, int size);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Calls, parameters and returns (call.c)
//
// Where the parameters arrive: slots over the stack ones, before layout_frame.
void place_params(Gen *g);
// Store the register parameters (and the stack part of a split long) into their slots.
void store_params(Gen *g);
// Find the structure parameters the function reads through the incoming pointer, with
// no copy: it only reads them, by member or whole, and makes no call, no store through
// a pointer and no write to a global, so the caller's object cannot change meanwhile.
void find_byref_params(Gen *g);
// Whether `name` is such a parameter; its members are then x(r15), r15 loaded by
// load_byref.
bool is_byref(const Gen *g, const char *name);
// r15 = the address of structure parameter `name`, read through its pointer.
void load_byref(Gen *g, const char *name);
// The variable `in` writes, or NULL (a call's is its own).
const Tac_Val *instr_dst(const Tac_Instruction *in);
void gen_return(Gen *g, const Tac_Val *v, bool last);
// A call, direct or through a pointer; FUN_CALL_NORETURN too.
void gen_call(Gen *g, const Tac_Instruction *in);
// The stack bytes of the arguments of call `in`.
int call_stack_size(const Gen *g, const Tac_Instruction *in);
// Register allocation hints: the registers the parameters arrive in, and those of a
// call's arguments and result.
void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi);
void call_hints(const Gen *g, const Flow *f, const Tac_Instruction *in, int *hint);

//
// Register allocation (regalloc.c)
//
void gen_regalloc(Gen *g);

//
// Peephole (peephole.c)
//
// The body, before the frame: jumps, known register contents, liveness.
// `out` is the size of the outgoing argument area at the bottom of the frame.
void msp_peephole_pass(Msp_Func *fn, unsigned result, int out);
// Whether the body still addresses the frame: an x(r1) slot, or r1 as a value.
bool msp_frame_referenced(const Msp_Func *fn);
// After the frame: the jumps again, and tail calls of a frameless function.
void msp_peephole_frame(Msp_Func *fn, unsigned result);

//
// Branch relaxation (relax.c)
//
void msp_relax(Msp_Func *fn);

#endif // MSP_INTERNAL_H
