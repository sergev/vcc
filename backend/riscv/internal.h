//
// RISC-V code generator internals.
//
// Every TAC name lives in memory: a frame-resident `%` name in a slot at a fixed
// offset from the frame pointer s0, any other name at its symbol.  Each instruction
// loads its operands into scratch registers, computes, and stores the result.
//
// Frame (s0 = sp at entry, 16-byte aligned):
//   s0 + 0 ...       incoming stack arguments
//   s0 - 64 ...      a0-a7, in a variadic function only
//   s0 - H + 8       saved ra (H = 16, or 80 when variadic)
//   s0 - H           saved s0
//   s0 - H - ...     slots
//   sp + 0 ...       outgoing stack arguments
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
    int outgoing;      // bytes of the outgoing argument area
} Gen;

//
// Types (frame.c)
//
int rv_size(const Tac_Type *t);
int rv_align(const Tac_Type *t);
bool rv_is_fp(const Tac_Type *t); // float or double; long double is fatal
bool rv_is_unsigned(const Tac_Type *t);
bool rv_is_aggregate(const Tac_Type *t);
bool rv_is_double(const Tac_Type *t);
bool gen_variadic(const Gen *g);

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
// Calls and parameters (call.c)
//
void gen_params(Gen *g);
void gen_call(Gen *g, const Tac_Instruction *in);
void gen_return(Gen *g, const Tac_Val *v);

//
// Instruction selection (instr.c)
//
void gen_instr(Gen *g, const Tac_Instruction *in);

//
// Static data (data.c)
//
void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly);

#endif // RISCV_INTERNAL_H
