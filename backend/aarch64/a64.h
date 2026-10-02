//
// AArch64 backend IR: a function is a list of blocks, a block a list of instructions.
// Operands name physical or virtual registers; only what the backend emits is modelled
// (aarch64.asdl is the full ISA reference).
//
#ifndef AARCH64_A64_H
#define AARCH64_A64_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: x0..x30, sp, the zero register, then v0..v31, then virtual
// registers.  Numbering starts at 1: 0 means "no register" (as in the shared register
// allocator).  An operand gives the width the register is used at.
#define A64_X(n) (1 + (n))
#define A64_V(n) (A64_V0 + (n))
enum {
    A64_X0   = A64_X(0),
    A64_X8   = A64_X(8),  // indirect result address
    A64_X16  = A64_X(16), // ip0
    A64_X17  = A64_X(17), // ip1
    A64_X18  = A64_X(18), // platform register: never used
    A64_X19  = A64_X(19), // first callee-saved
    A64_X28  = A64_X(28),
    A64_FP   = A64_X(29),
    A64_LR   = A64_X(30),
    A64_SP   = A64_X(31),
    A64_ZR   = A64_X(32),
    A64_V0   = A64_X(33),
    A64_VREG = A64_V0 + 32, // first virtual register
};

typedef enum {
    A64_W, // 32-bit general register
    A64_X, // 64-bit general register
    A64_S, // single
    A64_D, // double
    A64_Q, // 128 bits
} A64_Width;

typedef enum {
    A64_OPND_NONE,
    A64_OPND_REG,   // reg at width
    A64_OPND_IMM,   // #imm
    A64_OPND_SYM,   // sym + imm, or :lo12:sym + imm
    A64_OPND_MEM,   // [reg, #imm], [reg, #imm]!, or [reg], #imm
    A64_OPND_SHIFT, // reg at width, shifted: lsl/lsr/asr #amount
    A64_OPND_EXT,   // reg at width, extended: uxtw/sxtw/... #amount
    A64_OPND_LSL,   // a bare "lsl #imm" (movz, movk)
    A64_OPND_COND,  // a condition code
} A64_OperandKind;

typedef enum { A64_SHIFT_LSL, A64_SHIFT_LSR, A64_SHIFT_ASR } A64_Shift;

typedef enum {
    A64_EXT_UXTB,
    A64_EXT_UXTH,
    A64_EXT_UXTW,
    A64_EXT_UXTX,
    A64_EXT_SXTB,
    A64_EXT_SXTH,
    A64_EXT_SXTW,
    A64_EXT_SXTX,
} A64_Extend;

typedef enum { A64_MEM_OFFSET, A64_MEM_PRE, A64_MEM_POST } A64_MemMode;

typedef enum {
    A64_EQ,
    A64_NE,
    A64_HS,
    A64_LO,
    A64_MI,
    A64_PL,
    A64_VS,
    A64_VC,
    A64_HI,
    A64_LS,
    A64_GE,
    A64_LT,
    A64_GT,
    A64_LE,
} A64_Cond;

typedef struct {
    A64_OperandKind kind;
    int reg;
    A64_Width width;
    int64_t imm; // immediate, offset, or shift/extend amount
    int sub;     // A64_Shift, A64_Extend, A64_MemMode or A64_Cond, by kind
    bool lo12;   // a symbol's :lo12: relocation
    char *sym;   // owned
} A64_Operand;

// Opcode and mnemonic.
#define A64_OPS(X)                                                                                \
    X(MOV, "mov")                                                                                 \
    X(MOVZ, "movz")                                                                               \
    X(MOVN, "movn")                                                                               \
    X(MOVK, "movk")                                                                               \
    X(ADD, "add") X(SUB, "sub") X(NEG, "neg") X(MUL, "mul") X(SDIV, "sdiv") X(UDIV, "udiv")       \
        X(MSUB, "msub") X(MADD, "madd") X(AND, "and") X(ORR, "orr") X(EOR, "eor") X(MVN, "mvn")   \
            X(LSL, "lsl") X(LSR, "lsr") X(ASR, "asr") X(CMP, "cmp") X(CSET, "cset")               \
                X(SXTB, "sxtb") X(SXTH, "sxth") X(SXTW, "sxtw") X(UXTB, "uxtb") X(UXTH, "uxth")   \
                    X(LDR, "ldr") X(LDRB, "ldrb") X(LDRSB, "ldrsb") X(LDRH, "ldrh")               \
                        X(LDRSH, "ldrsh") X(LDRSW, "ldrsw") X(STR, "str") X(STRB, "strb")         \
                            X(STRH, "strh") X(LDP, "ldp") X(STP, "stp") X(ADRP, "adrp") X(B, "b") \
                                X(BCOND, "b.") X(CBZ, "cbz") X(CBNZ, "cbnz") X(BL, "bl")          \
                                    X(BLR, "blr") X(RET, "ret") X(FMOV, "fmov") X(FADD, "fadd")   \
                                        X(FSUB, "fsub") X(FMUL, "fmul") X(FDIV, "fdiv")           \
                                            X(FNEG, "fneg") X(FCMP, "fcmp") X(FCVT, "fcvt")       \
                                                X(SCVTF, "scvtf") X(UCVTF, "ucvtf")               \
                                                    X(FCVTZS, "fcvtzs") X(FCVTZU, "fcvtzu")       \
                                                        X(EPILOGUE, "#epilogue")

typedef enum {
#define A64_ENUM(op, mnem) A64_##op,
    A64_OPS(A64_ENUM)
#undef A64_ENUM
        A64_NUM_OPS
} A64_Op;

#define A64_MAX_OPERANDS 4

typedef struct A64_Instr {
    struct A64_Instr *next;
    A64_Op op;
    A64_Operand opnd[A64_MAX_OPERANDS];
} A64_Instr;

typedef struct A64_Block {
    struct A64_Block *next;
    char *label; // owned; NULL for none
    A64_Instr *head, *tail;
} A64_Block;

typedef struct {
    char *name; // owned
    bool global;
    A64_Block *blocks, *tail;
} A64_Func;

// A conditional branch is `b.<cond> label`: BCOND with the condition as its first
// operand, printed as part of the mnemonic.

extern const char *const a64_mnemonic[A64_NUM_OPS];

A64_Func *a64_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
A64_Block *a64_new_block(A64_Func *fn, const char *label);
// Append an instruction to block `b`, or to the current (last) block.
A64_Instr *a64_append_to(A64_Block *b, A64_Op op);
A64_Instr *a64_append(A64_Func *fn, A64_Op op);
void a64_free_func(A64_Func *fn);

A64_Operand a64_reg(int reg, A64_Width width);
A64_Operand a64_imm(int64_t imm);
A64_Operand a64_sym(const char *sym, int64_t offset);
A64_Operand a64_lo12(const char *sym, int64_t offset);
A64_Operand a64_mem(int base, int64_t offset);
A64_Operand a64_mem_pre(int base, int64_t offset);
A64_Operand a64_mem_post(int base, int64_t offset);
A64_Operand a64_shift(int reg, A64_Width width, A64_Shift shift, int amount);
A64_Operand a64_ext(int reg, A64_Width width, A64_Extend ext, int amount);
A64_Operand a64_lsl(int amount);
A64_Operand a64_cond(A64_Cond cond);
// The assembler name of register `reg` at `width`, or NULL for a virtual register.
const char *a64_reg_name(int reg, A64_Width width);
bool a64_is_fpreg(int reg); // v0-v31

// GNU assembler output.
void a64_emit_func(FILE *out, const A64_Func *fn);
// One instruction, as a line of a64_emit_func.
void a64_emit_instr(FILE *out, const A64_Instr *in);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // AARCH64_A64_H
