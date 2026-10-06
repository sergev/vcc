//
// MMIX code generator internals (the MMIXware ABI as GCC implements it; see
// backend/mmix/Plan.md).
//
// Registers: a call is pushj $X,f, which renames the register file: the callee's $0 is
// the caller's $(X+1), the arguments arrive in $0..., and the caller's $0..$(X-1) come
// back intact after pop, with the result in $X.  $32..$255 are global: $254 is the stack
// pointer, $251 the structure-result address, $255 scratch, and the linker allocates
// base registers below $247.
//
// Naive selection: every TAC variable lives in memory, a `%` name in a frame slot
// (k($254)), any other name at its symbol.  An operation loads its operands into the
// scratch registers $1-$3, operates, and stores the result.  A non-leaf function keeps
// rJ in $0, so every call is pushj $1: its arguments go in $2..$17, its result comes back
// in $1, and nothing else lives in a register across it.  A leaf keeps nothing, and
// returns its result straight in $0.  $255 holds an address or an offset that does not
// fit the instruction.  A register value is always extended to 64 bits, as its type
// says; memory holds a narrow value in its own width, so a store truncates and a load
// extends.  A float is held in a register as its exact binary64 value: ldsf and stsf
// convert, and stsf rounds.
//
// Frame (SP is constant in the body; every offset is from it):
//   frame + 8*i ...     incoming stack arguments, the 17th and later
//   copy ... frame - 1  a call's copies of its large structure arguments and its
//                       ignored structure result, for the call that needs most
//   out ... copy - 1    slots: each aligned to its type; frame is a multiple of 8
//   0 ... out - 1       outgoing stack arguments, for the call that needs most
//
// Structures: one of 8 bytes or less goes in a register, right-justified (the
// big-endian integer of its bytes); a larger one by reference, to a copy the caller
// makes, and the callee reads and writes it through that address (its slot holds it:
// Slot.byref).  A structure result of any size goes through the address the caller
// puts in $251; the callee saves it in the slot %.sret on entry.
#define SRET_SLOT "%.sret"
//
#ifndef MMIX_INTERNAL_H
#define MMIX_INTERNAL_H

#include "mmix_ir.h"
#include "string_map.h"
#include "tac.h"

enum {
    REG_RJ   = 0, // rJ in a non-leaf function
    REG_A    = 1, // scratch, and a call's hole
    REG_B    = 2,
    REG_C    = 3,
    REG_ARG0 = 2, // the first argument of a call: pushj $1
    MAX_REG_ARGS = 16,
};

typedef struct {
    const Tac_Type *type;
    int off;    // from SP
    bool byref; // a structure parameter over 8 bytes: the slot holds its address
} Slot;

typedef struct {
    Mmix_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    Mmix_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    StringMap consts;  // the names of read-only objects in .rodata, reached by geta
    int out_size;      // bytes of outgoing stack arguments
    int copy_off;      // the scratch area of a call: copies of its structure arguments
    int copy_size;     // over 8 bytes, and its structure result when it has no destination
    int frame_size;    // bytes of the outgoing area and the slots, a multiple of 8
    bool leaf;         // makes no call: rJ stays where it is
    char exit[32];     // the label of the epilogue
} Gen;

//
// Types (frame.c)
//
int mmix_type_size(const Tac_Type *t);
int mmix_type_align(const Tac_Type *t);
bool mmix_is_unsigned(const Tac_Type *t); // unsigned integers and pointers
bool mmix_is_fp(const Tac_Type *t);       // float, double, long double
bool mmix_is_float(const Tac_Type *t);    // float alone: binary32 in memory
bool mmix_is_scalar(const Tac_Type *t);

//
// Constants (frame.c)
//
// The instructions that load a 64-bit constant, at most four; returns their count.  An
// instruction is an opcode and its operand: an immediate for negu (the subtrahend of
// 0), a wyde for the others.
typedef struct {
    Mmix_Op op;
    unsigned arg;
} ConstStep;
int mmix_const_steps(uint64_t value, ConstStep steps[4]);

//
// Frame and value access (frame.c)
//
// A new translation unit: label numbering starts over.
void gen_unit_begin(void);
// A new local label `L:x<n>`, unique in the translation unit, into `buf`.
void new_label(char buf[32]);
// The assembler label of TAC label `tac`: L:<name>, its % dropped.  Free the result.
char *label_name(const char *tac);
void gen_init(Gen *g, const Tac_TopLevel *program, const Tac_TopLevel *tl);
void gen_done(Gen *g);
const char *gen_name(const Gen *g);
// The outgoing area, the slots of the parameters and locals, then the incoming stack
// parameters above the frame.
void layout_frame(Gen *g);
const Slot *find_slot(const Gen *g, const char *name);
const Tac_Type *name_type(const Gen *g, const char *name);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
Mmix_Instr *emit0(Gen *g, Mmix_Op op);
Mmix_Instr *emit1(Gen *g, Mmix_Op op, Mmix_Operand a);
Mmix_Instr *emit2(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b);
Mmix_Instr *emit3(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b, Mmix_Operand c);
// The register a result goes back in: $0 in a leaf, else $1, which the epilogue moves.
int ret_reg(const Gen *g);

// The 64-bit value of constant `c` as a register holds it: an integer extended as its
// kind says, a float or double as binary64 (a float's exact value), a long double
// rounded to binary64.
uint64_t const_bits(const Tac_Const *c);
// The bits of constant `c` in memory: a float as binary32.
uint64_t const_mem_bits(const Tac_Const *c);
// `reg` = `value`, by the shortest sequence.
void gen_const(Gen *g, int reg, uint64_t value);
// `reg` += `off`, in place, a wyde at a time: no register besides.
void add_in_place(Gen *g, int reg, int64_t off);
// `reg` = `base` + `off`, through $255 when the offset is not a byte.
void add_offset(Gen *g, int reg, int base, int64_t off);
// A load or store of `reg` at byte `off` of named object `name`: k($254) for a slot,
// sym+off for a global, through geta for a read-only object.
void mem_op(Gen *g, Mmix_Op op, int reg, const char *name, int64_t off);
// A load or store of `reg` at `base` + `off`.
void mem_op_at(Gen *g, Mmix_Op op, int reg, int base, int64_t off);
// The load of a value of type `t`, extended by its signedness (or as `sign` says when
// `force`), and its store.
Mmix_Op load_op(const Tac_Type *t);
Mmix_Op load_op_ext(int size, bool sign);
Mmix_Op store_op(const Tac_Type *t);
// `reg` = the address of named object `name` + off.
void address_of(Gen *g, int reg, const char *name, int64_t off);
// Load value `v` into `reg`, extended to 64 bits by its type.
void load_val(Gen *g, const Tac_Val *v, int reg);
// The same in the type `t` of the operation that uses it: a constant's kind may differ
// in signedness (a cast between int and unsigned emits no TAC, and leaves the kind), so
// a constant takes the width and signedness of `t`.  A variable has its own type.
void load_val_as(Gen *g, const Tac_Val *v, int reg, const Tac_Type *t);
uint64_t const_as(const Tac_Const *c, const Tac_Type *t);
// The Z operand of an operation in type `t`: a constant byte as an immediate, else `v`
// loaded into `reg`.
Mmix_Operand val_operand(Gen *g, const Tac_Val *v, int reg, const Tac_Type *t);
// Store `reg` into variable `v`, in its own width.
void store_val(Gen *g, int reg, const Tac_Val *v);
// Whether a structure goes in a register (8 bytes or less) rather than by reference.
bool struct_in_reg(const Tac_Type *t);
// Whether the function returns a structure, through $251.
bool returns_struct(const Gen *g);
// Load structure `name` (`size` bytes, aligned to `align`) into `reg` right-justified,
// the big-endian integer of its bytes, with `tmp` for the pieces; and the reverse.
void load_small_struct(Gen *g, const char *name, const Tac_Type *t, int reg, int tmp);
void store_small_struct(Gen *g, int reg, const char *name, const Tac_Type *t);
// Copy `size` bytes from the address in $2 to the address in $3, `align` bytes at a
// time where both allow; both registers are changed, and $1 and $255.
void copy_bytes(Gen *g, int size, int align);
// Copy `size` bytes of named objects: dst+doff = src+soff.
void copy_named(Gen *g, const char *dst, int64_t doff, const char *src, int64_t soff, int size,
                int align);
// Start a new block labelled `label`.
void gen_label_block(Gen *g, const char *label);
// Fill the prologue and the epilogue, once the body is done.
void gen_frame(Gen *g);

//
// Static data (data.c)
//
// `alignment` (from _Alignas) is used when stricter than the type's.
void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in, bool last);
// The outgoing stack bytes instruction `in` needs: a call's stack arguments.
int instr_out_size(const Gen *g, const Tac_Instruction *in);

//
// Floating point, in hardware (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
void gen_fp_unary(Gen *g, const Tac_Instruction *in);
// `reg` = whether FP value `v` is not a zero of either sign (NaN is true): nonzero then.
void gen_fp_test(Gen *g, const Tac_Val *v, int reg);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Calls, parameters and returns (call.c)
//
// Store the register parameters into their slots.
void store_params(Gen *g);
void gen_return(Gen *g, const Tac_Val *v, bool last);
// A call, direct or through a pointer; FUN_CALL_NORETURN too.
void gen_call(Gen *g, const Tac_Instruction *in);
// The stack bytes of the arguments of call `in`.
int call_stack_size(const Gen *g, const Tac_Instruction *in);
// Whether the function makes a call.
bool makes_call(const Tac_TopLevel *tl);

#endif // MMIX_INTERNAL_H
