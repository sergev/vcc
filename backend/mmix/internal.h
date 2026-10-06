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
// Registers, as the allocator numbers them (regalloc.c), in GCC's fixed model: $0-$13
// hold values live across a call, $14 rJ, $15 the hole of every call (pushj $15), and
// $16-$31 the arguments and the values live across no call.  A scalar `%` name that is
// never in memory may get one of them (a value live across a call one of $0-$13);
// any other `%` name lives in a frame slot (k($254)), any other name at its symbol.
// The compaction then moves $14 up: P being one more than the highest of $0-$13 in use,
// rJ goes in $P, the hole in $(P+1) and the arguments from $(P+2), one uniform shift of
// $14-$31 (a leaf has no rJ: $15-$31 go from $P).  With nothing allocated, P is 0:
// every call is pushj $1, with rJ in $0.  A result goes back in $0, or in the hole when
// rJ is in $0.
//
// An instruction works on registers directly, and goes through the scratch registers
// $248-$250 for an operand in memory or a constant: globals that GCC treats as
// call-clobbered and crt0 reserves, so none is ever allocated and none holds a value
// across a call.  $255 holds an address or an offset that does not fit the instruction.
// A register value is always extended to 64 bits, as its type says; memory holds a
// narrow value in its own width, so a store truncates and a load extends.  A float is
// held in a register as its exact binary64 value: ldsf and stsf convert, and stsf
// rounds; a float result in a register is rounded through the slot %.fround.
//
// Frame (SP is constant in the body; every offset is from it):
//   frame + 8*i ...     incoming stack arguments, the 17th and later
//   ... frame - 1       a variadic function's save area: the argument registers after
//                       its named ones, so the variable arguments are one run of slots
//   copy ... frame - 1  a call's copies of its large structure arguments and its
//                       ignored structure result, for the call that needs most
//   out ... copy - 1    slots: each aligned to its type; frame is a multiple of 8
//   0 ... out - 1       outgoing stack arguments, for the call that needs most
//
// Structures: one of 8 bytes or less goes in a register, right-justified (the
// big-endian integer of its bytes); a larger one by reference.  GCC's caller passes the
// address of its own object and its callee copies the object before writing it, so ours
// copies every one into its slot on entry; our caller passes a copy as well, which keeps
// an argument apart from the result's destination.  A structure result of any size
// goes through the address the caller puts in $251; the callee saves it in the slot
// %.sret on entry.
#define SRET_SLOT "%.sret"
//
#ifndef MMIX_INTERNAL_H
#define MMIX_INTERNAL_H

#include "codegen.h"
#include "mmix_ir.h"
#include "string_map.h"
#include "tac.h"

enum {
    REG_A        = 248, // scratch
    REG_B        = 249,
    REG_C        = 250,
    V_RJ         = 14, // the allocator's numbering: rJ,
    V_HOLE       = 15, // the hole,
    V_ARG0       = 16, // the first argument,
    V_LAST       = 31, // the last local
    MAX_REG_ARGS = 16,
};
#define FROUND_SLOT "%.fround"

typedef struct {
    const Tac_Type *type;
    int off; // from SP
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
    int va_off;        // a variadic function: the first variable argument's slot
    bool leaf;         // makes no call: rJ stays where it is
    StringMap regs;    // name → its register in the allocator's numbering + 1
    StringMap dead;    // allocated parameters dead on entry
    int P;             // the compaction: $0..$(P-1) hold values live across a call
    struct Flow *flow; // with the peephole optimizations: the body's variables,
    int *uses;         // and how many times each is read
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
// The offset from SP of the incoming stack slot of parameter `i` (16 or more).
int stack_param_off(const Gen *g, int i);
// Whether a parameter of type `t` comes by reference: a structure over 8 bytes.
bool param_byref(const Tac_Type *t);
const Tac_Type *name_type(const Gen *g, const char *name);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
Mmix_Instr *emit0(Gen *g, Mmix_Op op);
Mmix_Instr *emit1(Gen *g, Mmix_Op op, Mmix_Operand a);
Mmix_Instr *emit2(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b);
Mmix_Instr *emit3(Gen *g, Mmix_Op op, Mmix_Operand a, Mmix_Operand b, Mmix_Operand c);
// The register a result goes back in: $0, or the hole when rJ is in $0, which the
// epilogue moves.
int ret_reg(const Gen *g);
// rJ's register in a function that makes a call, and the hole of its calls.
int rj_reg(const Gen *g);
int hole_reg(const Gen *g);
// The register of allocator register `v`, after the compaction.
int phys_reg(const Gen *g, int v);
// The register of variable `name`, or -1 when it is in memory.
int var_reg(const Gen *g, const char *name);
int val_reg(const Gen *g, const Tac_Val *v);

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
// The register holding `v` in type `t` (its own when NULL): its own when it has one and
// needs no other extension, else `scratch`, loaded.
int use_val(Gen *g, const Tac_Val *v, int scratch, const Tac_Type *t);
// The register a result for `dst` is computed in: its own, or `scratch`.
int def_reg(const Gen *g, const Tac_Val *dst, int scratch);
// The result for `dst` is in `reg` (from def_reg): stored when `dst` is in memory, else
// extended when not `canonical` (an integer result that may have overflowed its type),
// rounded when a float.
void def_done(Gen *g, int reg, const Tac_Val *dst, bool canonical);
// `dst` = `src` extended from its low `size` bytes as `sign` says (size 8: a move).
void extend_reg(Gen *g, int dst, int src, int size, bool sign);
// `dst` = `src`, unless the same.
void move_reg(Gen *g, int dst, int src);
// Round the binary64 value in `reg` to binary32, through the slot %.fround.
void round_float(Gen *g, int reg);
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
// Whether the function returns a scalar: then pop hands back $0.
bool returns_value(const Gen *g);

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
// `in` and the instruction after it as one, when the second alone reads the first's
// result: a comparison or a !x and a conditional jump on it, a pointer sum and a load or
// store through it.  Returns whether it did.
bool gen_fused(Gen *g, const Tac_Instruction *in);
// The outgoing stack bytes instruction `in` needs: a call's stack arguments.
int instr_out_size(const Gen *g, const Tac_Instruction *in);

//
// Register allocation (regalloc.c)
//
void gen_regalloc(Gen *g);

//
// Peephole optimization of the function's code (peephole.c)
//
void mmix_peephole(Mmix_Func *fn, int fround_off);

//
// Floating point, in hardware (fp.c)
//
void gen_fp_binary(Gen *g, const Tac_Instruction *in);
void gen_fp_unary(Gen *g, const Tac_Instruction *in);
// Whether FP value `v` is not a zero of either sign (NaN is true): nonzero then, in
// `scratch`, which is returned.
int gen_fp_test(Gen *g, const Tac_Val *v, int scratch);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);

//
// Calls, parameters and returns (call.c)
//
// Store the register parameters into their slots.
void store_params(Gen *g);
// Copy each structure parameter over 8 bytes from the caller's object into its slot: at
// the start of the body, since a long copy is a loop, which the prologue cannot hold.
void copy_byref_params(Gen *g);
void gen_return(Gen *g, const Tac_Val *v, bool last);
// A call, direct or through a pointer; FUN_CALL_NORETURN too.
void gen_call(Gen *g, const Tac_Instruction *in);
// The stack bytes of the arguments of call `in`.
int call_stack_size(const Gen *g, const Tac_Instruction *in);
// Whether the function makes a call (__va_start is none: it is expanded in place).
bool makes_call(const Tac_TopLevel *tl);
// The number of named parameters of the function.
int param_count(const Gen *g);

#endif // MMIX_INTERNAL_H
