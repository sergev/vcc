//
// RISC-V code generator internals.
//
// A scalar `%` name that is never in memory may get a register (regalloc.c): an
// argument register unless it is live across a call, else a callee-saved one; any other `%` name
// lives in a slot at a fixed offset from the frame pointer s0, any other name at its symbol.  An
// instruction works on registers directly, and goes through scratch registers for operands in
// memory.
//
// Frame (s0 = sp at entry, 16-byte aligned):
//   s0 + 0 ...       incoming stack arguments
//   s0 - 64 ...      a0-a7, in a variadic function only
//   s0 - H + 8       saved ra (H = 16, or 80 when variadic)
//   s0 - H           saved s0
//   s0 - H - ...     saved s1-s11/fs0-fs11 in use, then slots
//   sp + 0 ...       outgoing stack arguments
// alloca lowers sp (the frame then always from s0) by its size rounded to 16, and
// returns sp plus the outgoing area rounded to 16, which stays below it.
//
// A leaf function that never touches s0 or sp, and saves no register, has no frame.
// Otherwise, when the frame is small enough, every s0-relative operand is rewritten
// against sp and s0 is not set up (ra is saved only when there are calls); see
// gen_prologue.
//
// A long double never gets a register: it lives in a 16-byte slot, and its
// operations are calls to the runtime (libc/common/float128.c).
//
// Scratch registers: t0-t2 and ft0-ft2 hold operands, t3/t4 addresses of an
// aggregate copy, t5 the address of a global, t6 a large frame offset or the bits of
// an FP constant.
//
#ifndef RISCV_INTERNAL_H
#define RISCV_INTERNAL_H

#include "rv.h"
#include "string_map.h"
#include "tac.h"

typedef struct {
    const Tac_Type *type;
    int offset; // from s0
    int reg;    // allocated register, or 0 for the slot
    int reg_hi; // of a long long on rv32 in registers: the high word's
} Slot;

typedef struct {
    Rv_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
    Rv_Block *prologue;
    StringMap frame;   // name → Slot *
    StringMap globals; // name → const Tac_Type *
    int header;        // bytes above the slots: saved registers and a0-a7
    int locals_size;   // bytes of slots below the saved registers
    int max_align;     // of any slot
    int outgoing;      // bytes of the outgoing argument area
    bool moves_sp;     // calls a stack builtin (alloca): sp moves in the body
    StringMap regs;    // name → allocated register
    StringMap regs_hi; // name → register of a long long's high word
    StringMap dead;    // allocated parameters dead on entry (regalloc.c)
    int nsaved;        // callee-saved registers in use
    int saved_reg[32];
    int saved_off[32]; // their save slots
    int ret_ptr;       // slot of the hidden result pointer we pass, or 0 (call.c)
    int nconsts;       // double literals (rv32), emitted after the function
    int consts_cap;
    uint64_t *const_bits;
    int *const_label; // .LC<n>
} Gen;

//
// Types (frame.c)
//
int rv_size(const Tac_Type *t);
int rv_align(const Tac_Type *t);
bool rv_is_fp(const Tac_Type *t); // float or double
// A long double (binary128) lives in memory, and goes in integer register pairs.
bool rv_is_ld(const Tac_Type *t);
// A long long on rv32.
bool rv_is_ll(const Tac_Type *t);
// A scalar two registers wide, kept in memory and passed in a register pair: long
// double on rv64, long long on rv32.
bool rv_is_pair(const Tac_Type *t);
bool rv_is_unsigned(const Tac_Type *t);
bool rv_is_aggregate(const Tac_Type *t);
bool rv_is_double(const Tac_Type *t);
bool gen_variadic(const Gen *g);
// Load and store of a register-width integer: ld/sd, or lw/sw on rv32.
Rv_Op xlen_load(void);
Rv_Op xlen_store(void);

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
// Keep `name` in register `reg` (and a long long's high word in `hi`, else 0).
void place_reg(Gen *g, const char *name, const Tac_Type *type, int reg, int hi);
// The register allocated to name `name` (by gen_regalloc), or 0; and the high word's.
int assigned_reg(const Gen *g, const char *name);
int assigned_reg_hi(const Gen *g, const char *name);
// The register holding the high word of long long `v`, or 0.
int var_reg_hi(const Gen *g, const Tac_Val *v);
// The register holding variable `v`, or 0 when it is in memory or a constant.
int var_reg(const Gen *g, const Tac_Val *v);
const Tac_Type *val_type(const Gen *g, const Tac_Val *v);
const Tac_Type *name_type(const Gen *g, const char *name);
// A memory operand for base + off, using t6 when off is beyond 12 bits.
Rv_Operand mem(Gen *g, int base, int64_t off);
// Where named object `name` is: s0 + offset, or `scratch` + 0 after loading a global's
// address into `scratch`.
void name_addr(Gen *g, const char *name, int scratch, int *base, int64_t *off);
// reg = base + off.
void gen_addr(Gen *g, int reg, int base, int64_t off);
// Load/store a scalar of type `t` between `reg` (integer or FP) and base + off.
void load_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off);
void store_mem(Gen *g, int reg, const Tac_Type *t, int base, int64_t off);
// Load a scalar value into `reg`, or store `reg` into a variable.
void load_val(Gen *g, int reg, const Tac_Val *v);
void store_val(Gen *g, int reg, const Tac_Val *v);
// The register holding `v`: its own, or `scratch` after loading it.
int use_val(Gen *g, int scratch, const Tac_Val *v);
// The register to compute `v` into: its own, or `scratch` (then store_val it).
int def_reg(const Gen *g, int scratch, const Tac_Val *v);
// FP register `reg` = +0.0, a double when `dbl`.
void fp_zero(Gen *g, int reg, bool dbl);
// The label number of a double literal with `bits`, emitted after the function.
int riscv_const_label(Gen *g, uint64_t bits);
// dst = src, registers of one class; nothing when they are the same.
void move_reg(Gen *g, int dst, int src, const Tac_Type *t);
// dst = src in the register form of integer type `t`: extended from its width.
void gen_canon(Gen *g, int dst, int src, const Tac_Type *t);
// The value of integer constant `c` as its type says, in 64 bits.
int64_t const_value(const Gen *g, const Tac_Const *c);
// Load integer constant `c` converted to type `t`.
void load_const_as(Gen *g, int reg, const Tac_Const *c, const Tac_Type *t);
// Load the low (half 0) or high register of pair value `v` into `reg`.  A narrower
// integer is extended to a long long.  `reg` is not t5 or t6.
void pair_half(Gen *g, int reg, const Tac_Val *v, int half);
// Pair variable `dst` = lo, hi: into its registers, or its memory.  lo and hi are not
// t5 or t6.
void set_pair(Gen *g, const Tac_Val *dst, int lo, int hi);
// Store pair value `src` (or a long double on rv32) at base + off; base is not t0, t2,
// t3 or t5.
void copy_pair(Gen *g, const Tac_Val *src, int base, int64_t off);
// Copy `size` bytes; the bases are registers other than t2 and t6.
void gen_memcopy(Gen *g, int dst, int64_t dst_off, int src, int64_t src_off, int size, int align);
// Load `size` (1..8) bytes at base + off into `reg`, or store them, byte by byte when
// fewer than 8.  Uses t2 as a temporary.
void load_bytes(Gen *g, int reg, int base, int64_t off, int size);
void store_bytes(Gen *g, int reg, int base, int64_t off, int size);
Rv_Instr *emit2(Gen *g, Rv_Op op, Rv_Operand a, Rv_Operand b);
Rv_Instr *emit3(Gen *g, Rv_Op op, Rv_Operand a, Rv_Operand b, Rv_Operand c);
void gen_li(Gen *g, int reg, int64_t imm);
void gen_epilogue(Gen *g);
void gen_prologue(Gen *g);

//
// Register allocation (regalloc.c): fills g->regs and the callee-saved registers used.
//
void gen_regalloc(Gen *g);

//
// Calls and parameters (call.c)
//
void gen_params(Gen *g);
// The incoming register of each scalar parameter passed in one of its own class; of
// the high word of a long long pair in `hints_hi`.
void param_hints(const Gen *g, StringMap *hints, StringMap *hints_hi);
// Hints for a call: each scalar argument variable its argument register, the result
// a0/fa0; only where hint[var] is still 0 (indexed by flow variable).  The high word
// of a long long pair at hint[var + f->nvars], a1 for the result.
struct Flow;
void call_hints(const Gen *g, const struct Flow *f, const Tac_Instruction *in, int *hint);
void gen_call(Gen *g, const Tac_Instruction *in);
// Whether `in` calls __builtin_alloca, __builtin_stack_save or __builtin_stack_restore,
// which gen_call expands in place.
bool rv_stack_builtin(const Tac_Instruction *in);
// The outgoing area of every call in the body, ahead of it: what alloca's result is
// above.  After layout_frame, which gives every name its type.
void reserve_outgoing(Gen *g);
// A call to runtime routine `name` with arguments `args`, of `types` (or their own when
// NULL), by the calling convention; the result, of type `ret`, into `dst` (or left in
// a0/fa0).
void gen_runtime_call(Gen *g, const char *name, const Tac_Type *ret, const Tac_Val *const *args,
                      const Tac_Type *const *types, int nargs, const Tac_Val *dst);
void gen_return(Gen *g, const Tac_Val *v);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);
// Whether `in` calls a runtime routine (long double arithmetic and conversions, and
// on rv32 long long division and conversions); `type_of(arg, v)` gives the type of
// operand `v`.  Sets *dst to its result.
typedef const Tac_Type *TypeOf(const void *arg, const Tac_Val *v);
bool runtime_call(const Tac_Instruction *in, TypeOf *type_of, const void *arg, const Tac_Val **dst);

//
// 64-bit integers on rv32, in register pairs (llong.c)
//
void gen_ll_binary(Gen *g, const Tac_Instruction *in);
void gen_ll_unary(Gen *g, const Tac_Instruction *in);
// An integer conversion to or from long long.
void gen_ll_int_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);
// A conversion between long long and float or double: a call to the runtime.
void gen_ll_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind);
// Whether `in`, of a long long operand type `t` or result type `dt`, calls the runtime.
bool ll_runtime_call(const Tac_Instruction *in, const Tac_Type *t, const Tac_Type *dt);
// Shared with instr.c.
bool rv_unsigned_op(Tac_BinaryOperator op);
// Store integer result `d` into `dst`, brought to its type's form in a register.
void store_int_result(Gen *g, int d, const Tac_Val *dst);
bool rv_from_unsigned(Tac_InstructionKind kind);
void call_runtime(Gen *g, const char *name);
// Pair value `v` into registers reg, reg + 1; a pair result in a0/a1 into `dst`.
void pair_arg(Gen *g, int reg, const Tac_Val *v);
void pair_result(Gen *g, const Tac_Val *dst);

//
// Static data (data.c)
//
// `alignment` (from _Alignas) is used when stricter than the type's.
void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment);

#endif // RISCV_INTERNAL_H
