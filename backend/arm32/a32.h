//
// ARM32 backend IR: a function is a list of blocks, a block a list of instructions.
// Every instruction carries a condition (A32_AL by default) and a set-flags bit, as in
// the A32 encoding.  Operands name physical or virtual registers; only what the backend
// emits is modelled (arm32.asdl is the full ISA reference).
//
#ifndef ARM32_A32_H
#define ARM32_A32_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: r0..r15, then the VFP registers by single-precision index s0..s31
// (d<k> is s<2k> used at double width), then virtual registers.
enum {
    A32_R0   = 0,
    A32_R4   = 4,  // first callee-saved
    A32_R9   = 9,  // platform register: callee-saved on bare-metal EABI
    A32_R10  = 10,
    A32_FP   = 11, // frame pointer (ARM state)
    A32_IP   = 12, // scratch, may be clobbered by linker veneers
    A32_SP   = 13,
    A32_LR   = 14,
    A32_PC   = 15,
    A32_S0   = 16, // s0..s31 = d0..d15
    A32_VREG = 48, // first virtual register
};

typedef enum {
    A32_CORE, // a core register
    A32_S,    // a single-precision VFP register
    A32_D,    // a double-precision VFP register: the even single and the next
} A32_Width;

typedef enum {
    A32_OPND_NONE,
    A32_OPND_REG,     // reg at width
    A32_OPND_IMM,     // #imm
    A32_OPND_SYM,     // sym + imm, or #:lower16:/#:upper16: of it
    A32_OPND_MEM,     // [base, #±imm], [base, ±index{, shift #n}], pre/post-indexed
    A32_OPND_SHIFT,   // reg, shift #amount or reg, shift rs (operand2)
    A32_OPND_REGLIST, // {r4, r5, lr}: a mask of core registers
} A32_OperandKind;

typedef enum { A32_SHIFT_LSL, A32_SHIFT_LSR, A32_SHIFT_ASR, A32_SHIFT_ROR } A32_Shift;

typedef enum { A32_MEM_OFFSET, A32_MEM_PRE, A32_MEM_POST } A32_MemMode;

typedef enum { A32_RELOC_NONE, A32_RELOC_LOWER16, A32_RELOC_UPPER16 } A32_Reloc;

// A32_AL is zero, so an instruction is unconditional unless told otherwise.
typedef enum {
    A32_AL,
    A32_EQ,
    A32_NE,
    A32_HS,
    A32_LO,
    A32_MI,
    A32_PL,
    A32_VS,
    A32_VC,
    A32_HI,
    A32_LS,
    A32_GE,
    A32_LT,
    A32_GT,
    A32_LE,
} A32_Cond;

typedef struct {
    A32_OperandKind kind;
    int reg; // the register; the base of a memory operand
    A32_Width width;
    int64_t imm;   // immediate, offset, or shift amount; a register list's mask
    int sub;       // A32_Shift, A32_MemMode or A32_Reloc, by kind
    int reg2;      // a memory operand's index register, or a shift's amount register; -1
    bool negative; // a memory operand's index is subtracted
    char *sym;     // owned
} A32_Operand;

// Opcode and mnemonic.  A condition and the S suffix go before any `.type` suffix.
// EPILOGUE is a marker the frame pass replaces by the function's return sequence.
#define A32_OPS(X)                                                                         \
    X(MOV, "mov") X(MVN, "mvn") X(MOVW, "movw") X(MOVT, "movt") X(ADD, "add") X(SUB, "sub") \
    X(LDR, "ldr") X(LDRB, "ldrb") X(LDRSB, "ldrsb") X(LDRH, "ldrh") X(LDRSH, "ldrsh")       \
    X(STR, "str") X(STRB, "strb") X(STRH, "strh") X(PUSH, "push") X(POP, "pop")             \
    X(BX, "bx") X(VLDR, "vldr") X(VSTR, "vstr") X(VMOV, "vmov") X(VPUSH, "vpush")           \
    X(VPOP, "vpop") X(SXTB, "sxtb") X(SXTH, "sxth") X(UXTB, "uxtb") X(UXTH, "uxth")       \
    X(EPILOGUE, "<epilogue>")

typedef enum {
#define A32_ENUM(op, mnem) A32_##op,
    A32_OPS(A32_ENUM)
#undef A32_ENUM
        A32_NUM_OPS
} A32_Op;

#define A32_MAX_OPERANDS 4

typedef struct A32_Instr {
    struct A32_Instr *next;
    A32_Op op;
    A32_Cond cond;
    bool set_flags; // the S suffix
    A32_Operand opnd[A32_MAX_OPERANDS];
} A32_Instr;

typedef struct A32_Block {
    struct A32_Block *next;
    char *label; // owned; NULL for none
    A32_Instr *head, *tail;
} A32_Block;

typedef struct {
    char *name; // owned
    bool global;
    A32_Block *blocks, *tail;
} A32_Func;

extern const char *const a32_mnemonic[A32_NUM_OPS];

A32_Func *a32_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
A32_Block *a32_new_block(A32_Func *fn, const char *label);
// Append an instruction to the current (last) block.
A32_Instr *a32_append(A32_Func *fn, A32_Op op);
void a32_free_func(A32_Func *fn);

A32_Operand a32_reg(int reg);              // a core register
A32_Operand a32_sreg(int reg);             // reg at single precision
A32_Operand a32_dreg(int reg);             // reg (an even single) at double precision
A32_Operand a32_imm(int64_t imm);
A32_Operand a32_sym(const char *sym, int64_t offset);
A32_Operand a32_lower16(const char *sym, int64_t offset);
A32_Operand a32_upper16(const char *sym, int64_t offset);
A32_Operand a32_mem(int base, int64_t offset);
A32_Operand a32_mem_pre(int base, int64_t offset);
A32_Operand a32_mem_post(int base, int64_t offset);
// [base, ±index, lsl #shift]
A32_Operand a32_mem_index(int base, int index, bool negative, int shift);
A32_Operand a32_shift(int reg, A32_Shift shift, int amount);
A32_Operand a32_shift_reg(int reg, A32_Shift shift, int amount_reg);
A32_Operand a32_reglist(unsigned mask);  // bit n is rn
A32_Operand a32_dreglist(unsigned mask); // bit n is dn

bool a32_is_vfp(int reg); // s0-s31
// The assembler name of register `reg` at `width`, or NULL for a virtual register or
// one used at a width of the other file (or an odd single at double width).
const char *a32_reg_name(int reg, A32_Width width);
// Whether `v` is an ARM modified immediate: 8 bits rotated right by an even amount.
bool a32_operand2_imm(uint32_t v);

// GNU unified assembler output.  The header goes once at the top of a module.
void a32_emit_header(FILE *out);
void a32_emit_func(FILE *out, const A32_Func *fn);
// One instruction, as a line of a32_emit_func.
void a32_emit_instr(FILE *out, const A32_Instr *in);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // ARM32_A32_H
