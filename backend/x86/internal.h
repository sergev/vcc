//
// x86-64 code generator internals (System V psABI; see backend/x86/Plan.md).
//
// Registers: rdi, rsi, rdx, rcx, r8, r9 and xmm0-xmm7 carry arguments, rax/rdx and
// xmm0/xmm1 results; rax, r10 and r11 are the scratch registers of instruction
// selection, xmm14/xmm15 the SSE ones; rbx, rbp and r12-r15 are callee-saved, no xmm
// register is; rbp is the frame pointer, rsp the stack pointer.
//
// A scalar `%` name that is never in memory may get a register (regalloc.c): an
// argument register (xmm0-xmm13 for float and double) unless it is live across a
// call, else rbx, r12-r15, or rbp without a frame pointer; an FP value live across a
// call stays in its slot, as no xmm register is callee-saved.  Any other `%` name
// lives in a slot at a fixed offset from the frame base, any other name at its symbol,
// addressed as sym(%rip).  An instruction
// works on registers directly where x86 allows, and loads other operands into the
// scratch registers, with one of them straight from memory or an immediate.
//
// Frame (the base, X86_FRAME, 8 bytes below the return address, 16-byte aligned):
//   base + 16 ...    incoming stack arguments
//   base + 8         return address
//   base + 0         saved rbp, or (no frame pointer) the first callee-saved register
//   base - ...       the other callee-saved registers in use, pushed, then slots
//   rsp + 0 ...      outgoing stack arguments
// gen_prologue resolves the base to rbp with --frame-pointer, else to rsp plus the
// frame's size, or in a leaf whose slots fit the red zone to rsp - 8, with no frame.
//
// A value in a general register is in canonical form: a type of 32 bits or fewer in
// the 32-bit view, extended to 32 bits by its own type, the upper half zero (as every
// 32-bit write leaves it); a 64-bit one in the 64-bit view.  Register 0, rax, is never
// allocated, so 0 stands for no register.
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
    int reg;    // allocated register, or 0 for the slot
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
    int ret_ptr;       // the slot of the result address that came in rdi, or 0
    struct {
        int save;      // the slot of the register save area, or 0 (not variadic)
        int gp, fp;    // va_list's initial gp_offset and fp_offset
        int overflow;  // the first variadic stack argument, from rbp
    } va;
    FpConst *consts;   // the function's .rodata constants
    int nconsts, maxconsts;
    StringMap regs;    // name → allocated register (regalloc.c)
    StringMap dead;    // allocated parameters dead on entry (regalloc.c)
    struct Flow *flow; // with the peephole pass: the body's variables,
    int *uses;         // and how many times each is read
    int nsaved;        // callee-saved registers in use, pushed in this order
    int saved_reg[6];
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
// Keep `name` in register `reg`.
void place_reg(Gen *g, const char *name, const Tac_Type *type, int reg);
const Slot *find_slot(const Gen *g, const char *name);
// The register allocated to `name` by gen_regalloc, or 0.
int assigned_reg(const Gen *g, const char *name);
// The register holding variable `v`, or 0 when it is in memory or a constant.
int var_reg(const Gen *g, const Tac_Val *v);
// The register holding scalar `v`: its own, or `scratch` after loading it.
int use_val(Gen *g, int scratch, const Tac_Val *v);
// The register to compute `v` into: its own, or `scratch` (then store it).
int def_reg(const Gen *g, int scratch, const Tac_Val *v);
// dst = src, registers of one file, at the view of type `t`; nothing when the same.
void move_reg(Gen *g, int dst, int src, const Tac_Type *t);
// dst = general register src in the canonical form of integer type `t` (in place
// too: a narrow type is extended again).
void gen_canon(Gen *g, int dst, int src, const Tac_Type *t);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
const Tac_Type *name_type(const Gen *g, const char *name);
// The memory operand of named object `name`, `off` bytes in: off(%rbp) for a slot,
// name+off(%rip) for a static object.  Not for a variable in a register.
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
// variable `v`, at the width of the variable's type (into a register: brought to the
// canonical form of its type, even when it is that register).
void load_val(Gen *g, int reg, const Tac_Val *v);
void store_val(Gen *g, int reg, const Tac_Val *v);
// Load integer value `v` into `reg` for an operation on type `t`: a variable as its own
// type, a constant converted to `t` (its own kind may differ).
void load_int_as(Gen *g, int reg, const Tac_Val *v, const Tac_Type *t);
// The source operand of integer value `v` in an operation on type `t`: an immediate,
// the variable's register or memory when it has the operation's width, or else
// `scratch` after loading it.
X86_Operand src_operand(Gen *g, const Tac_Val *v, const Tac_Type *t, int scratch);
// Memory operand `m`, `off` bytes further (its symbol copied).
X86_Operand mem_at(X86_Operand m, int64_t off);
// Copy `size` bytes from memory operand `src` to `dst` (both consumed): moves of up to
// `align` bytes through r11, past 64 bytes in a loop of 16 bytes through xmm15, the
// addresses in rax and r10 and the count in r11.  Neither base register may be rax or
// r11, nor the destination's r10.
void gen_memcopy(Gen *g, X86_Operand dst, X86_Operand src, int size, int align);
// A marker for the frame teardown, then ret.
void gen_epilogue(Gen *g);
// Fill the prologue and the epilogues, once the frame is known.
void gen_prologue(Gen *g);

//
// Register allocation (regalloc.c): fills g->regs and the callee-saved registers used.
//
void gen_regalloc(Gen *g);
// Whether `in`, not a call, clobbers a register of the allocator's pools: rdx in a
// divide or remainder, rcx in a shift by a variable.  Sets *dst to its result.
typedef const Tac_Type *TypeOf(const void *arg, const Tac_Val *v);
bool clobbers_regs(const Tac_Instruction *in, TypeOf *type_of, const void *arg,
                   const Tac_Val **dst);

//
// Calls, parameters and returns (call.c)
//
// A register or a slot for each parameter: moved from its argument register, stored
// from it, or placed over its stack slot; in a variadic function, the register save
// area first.
void gen_params(Gen *g);
// The incoming register of each scalar parameter passed in a register of its own.
void param_hints(const Gen *g, StringMap *hints);
// Hints for a call: each scalar argument variable its argument register, an FP result
// xmm0; only where hint[var] is still 0 (indexed by flow variable).
struct Flow;
void call_hints(const Gen *g, const struct Flow *f, const Tac_Instruction *in, int *hint);
void gen_call(Gen *g, const Tac_Instruction *in);
void gen_return(Gen *g, const Tac_Val *v);
// Bit r for each register r that carries the function's result at ret; in *wide,
// for each general register all 64 bits of which do.
uint32_t result_regs(const Gen *g, uint32_t *wide);

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
// FP comparison `in` and a branch to `label` when its result is zero (or nonzero);
// false, emitting nothing, when `in` is not a comparison.
bool gen_fp_compare_branch(Gen *g, const Tac_Instruction *in, bool if_zero, const char *label);
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
// A comparison `in` whose only use is conditional jump `next`: cmp (or ucomis) and a
// jcc in place of both; false, emitting nothing, when it is not one.
bool gen_compare_branch(Gen *g, const Tac_Instruction *in, const Tac_Instruction *next);

//
// Peephole pass (peephole.c), on the finished function
//
void x86_peephole_func(X86_Func *fn);

#endif // X86_INTERNAL_H
