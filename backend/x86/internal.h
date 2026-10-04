//
// x86-64 code generator internals (System V psABI; see backend/x86/Plan.md).
//
// Registers: rdi, rsi, rdx, rcx, r8, r9 and xmm0-xmm7 carry arguments, rax/rdx and
// xmm0/xmm1 results; rax, r10 and r11 are the scratch registers of instruction
// selection, xmm14/xmm15 the SSE ones; rbx, rbp and r12-r15 are callee-saved, no xmm
// register is; rbp is the frame pointer, rsp the stack pointer.
//
// Every `%` name lives in a slot at a fixed offset from rbp, any other name at its
// symbol, addressed as sym(%rip).  An instruction loads its operands into the scratch
// registers, with one of them straight from memory or an immediate where x86 allows,
// and stores the result back.
//
// Frame (rbp = rsp after rbp is pushed, 16-byte aligned):
//   rbp + 16 ...     incoming stack arguments
//   rbp + 8          return address
//   rbp + 0          saved rbp
//   rbp - ...        slots
//   rsp + 0 ...      outgoing stack arguments
//
// A value in a general register is in canonical form: a type of 32 bits or fewer in
// the 32-bit view, extended to 32 bits by its own type, the upper half zero (as every
// 32-bit write leaves it); a 64-bit one in the 64-bit view.
//
#ifndef X86_INTERNAL_H
#define X86_INTERNAL_H

#include "string_map.h"
#include "tac.h"
#include "x86.h"

enum {
    T0 = X86_RAX, // operands and results
    T1 = X86_R10,
    T2 = X86_R11,
    F0 = X86_XMM0 + 14,
    F1 = X86_XMM0 + 15,
};

typedef struct {
    const Tac_Type *type;
    int offset; // from rbp
} Slot;

// A constant in .rodata: `size` bytes (4, 8 or 16) of lo:hi, aligned to its size.
typedef struct {
    uint64_t lo, hi;
    int size;
    int label; // .LC<label>
} FpConst;

typedef struct {
    X86_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    X86_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int locals_size;   // bytes of slots below the saved rbp
    int outgoing;      // bytes of the outgoing argument area
    int x87_tmp;       // the x87 scratch slot, or 0 (x87.c)
    FpConst *consts;   // the function's .rodata constants
    int nconsts, maxconsts;
} Gen;

//
// Types (frame.c)
//
int x86_size(const Tac_Type *t);
int x86_align(const Tac_Type *t);
bool x86_is_fp(const Tac_Type *t); // float or double
bool x86_is_double(const Tac_Type *t);
bool x86_is_ld(const Tac_Type *t); // long double
bool x86_is_unsigned(const Tac_Type *t);
bool x86_is_aggregate(const Tac_Type *t);
// The width of a scalar of `size` bytes.
X86_Width x86_width_of(int size);
// The width of an integer operation on type `t`: 32 bits up to 4 bytes, else 64.
X86_Width x86_op_width(const Tac_Type *t);

//
// Frame and value access (frame.c)
//
// A new translation unit: label numbering starts over.
void gen_unit_begin(void);
// A new local label `.Lx<n>`, unique in the translation unit, into `buf`.
void new_label(char buf[32]);
// The memory operand of a .rodata constant of `size` bytes, lo:hi.
X86_Operand const_mem(Gen *g, uint64_t lo, uint64_t hi, int size);
// Emit the function's .rodata constants.
void emit_consts(const Gen *g, FILE *out);
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
// The memory operand of named object `name`, `off` bytes in: off(%rbp) for a slot,
// name+off(%rip) for a static object.
X86_Operand name_mem(const Gen *g, const char *name, int64_t off);
X86_Instr *emit0(Gen *g, X86_Op op, X86_Width width);
X86_Instr *emit1(Gen *g, X86_Op op, X86_Width width, X86_Operand a);
X86_Instr *emit2(Gen *g, X86_Op op, X86_Width width, X86_Operand src, X86_Operand dst);
// reg = imm, at width (X86_L zero-extends to 64 bits).
void gen_li(Gen *g, int reg, X86_Width width, int64_t imm);
// The value of integer constant `c` as its type says, in 64 bits.
int64_t const_value(const Tac_Const *c);
// Integer constant `c` converted to integer type `t`, as the register form of `t`
// holds it: narrower types sign- or zero-extended to 32 bits.
int64_t const_as(const Tac_Const *c, const Tac_Type *t);
// Load a scalar of type `t` at memory operand `m` into register `reg`: a general one in
// its canonical form, an xmm one with movss/movsd; store one at its own width.
void load_mem(Gen *g, int reg, const Tac_Type *t, X86_Operand m);
void store_mem(Gen *g, int reg, const Tac_Type *t, X86_Operand m);
// Load scalar value `v` into `reg` (general or xmm), as its own type; store `reg` into
// variable `v`, at the width of the variable's type.
void load_val(Gen *g, int reg, const Tac_Val *v);
void store_val(Gen *g, int reg, const Tac_Val *v);
// Load integer value `v` into `reg` for an operation on type `t`: a variable as its own
// type, a constant converted to `t` (its own kind may differ).
void load_int_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *t);
// The source operand of integer value `v` in an operation on type `t`: an immediate,
// the variable in memory when it has the operation's width, or else `scratch` after
// loading it.
X86_Operand src_operand(Gen *g, const Tac_Val *v, const Tac_Type *t, int scratch);
// A marker for the frame teardown, then ret.
void gen_epilogue(Gen *g);
// Fill the prologue and the epilogues, once the frame is known.
void gen_prologue(Gen *g);

//
// Calls, parameters and returns (call.c)
//
// A slot for each parameter, stored from its argument register or placed over its
// stack slot.
void gen_params(Gen *g);
void gen_call(Gen *g, const Tac_Instruction *in);
void gen_return(Gen *g, const Tac_Val *v);

//
// Floating point, SSE (fp.c)
//
// The source operand of FP value `v`: the variable in memory, or a .rodata constant.
X86_Operand fp_operand(Gen *g, const Tac_Val *v);
void gen_fp_unary(Gen *g, const Tac_Instruction *in, const Tac_Type *t);
void gen_fp_binary(Gen *g, const Tac_Instruction *in, const Tac_Type *t);
void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);
// Branch to `label` when FP value `cond` is zero (or nonzero); a NaN is nonzero.
void gen_fp_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *label);
// dst = src, a float or double, through rax.
void gen_fp_copy(Gen *g, const Tac_Val *src, X86_Operand dst, const Tac_Type *t);

//
// long double, x87 (x87.c)
//
// dst = long double `src`, copied as two quadwords through rax.
void gen_ld_copy(Gen *g, const Tac_Val *src, X86_Operand dst);
// Push long double `v` onto the x87 stack.
void gen_ld_load(Gen *g, const Tac_Val *v);
void gen_ld_unary(Gen *g, const Tac_Instruction *in);
void gen_ld_binary(Gen *g, const Tac_Instruction *in);
void gen_ld_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);
// Branch to `label` when long double `cond` is zero (or nonzero); a NaN is nonzero.
void gen_ld_cond_jump(Gen *g, bool if_zero, const Tac_Val *cond, const char *label);

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

#endif // X86_INTERNAL_H
