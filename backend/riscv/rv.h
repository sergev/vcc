//
// RISC-V backend IR: a function is a list of blocks, a block a list of instructions.
// Operands name physical or virtual registers; only what the backend emits is modelled
// (riscv.asdl is the full ISA reference).
//
#ifndef RISCV_RV_H
#define RISCV_RV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: x0..x31, then f0..f31, then virtual registers.
enum {
    RV_ZERO = 0,
    RV_RA   = 1,
    RV_SP   = 2,
    RV_GP   = 3,
    RV_TP   = 4,
    RV_T0   = 5,
    RV_T1   = 6,
    RV_T2   = 7,
    RV_S0   = 8,
    RV_S1   = 9,
    RV_A0   = 10,
    RV_A7   = 17,
    RV_S2   = 18,
    RV_S11  = 27,
    RV_T3   = 28,
    RV_T6   = 31,
    RV_F0   = 32,
    RV_FA0  = 42,
    RV_VREG = 64, // first virtual register
};

typedef enum {
    RV_OPND_NONE,
    RV_OPND_REG, // reg
    RV_OPND_IMM, // imm
    RV_OPND_SYM, // sym + imm: a symbol or label
    RV_OPND_MEM, // imm(reg)
} Rv_OperandKind;

typedef struct {
    Rv_OperandKind kind;
    int reg;
    int64_t imm;
    char *sym; // owned
} Rv_Operand;

typedef enum {
    RV_LI,
    RV_MV,
    RV_J,
    RV_RET,
    RV_NUM_OPS
} Rv_Op;

typedef struct Rv_Instr {
    struct Rv_Instr *next;
    Rv_Op op;
    Rv_Operand opnd[3];
} Rv_Instr;

typedef struct Rv_Block {
    struct Rv_Block *next;
    char *label; // owned; NULL for the entry block
    Rv_Instr *head, *tail;
} Rv_Block;

typedef struct {
    char *name; // owned
    bool global;
    Rv_Block *blocks, *tail;
    int num_vregs;
} Rv_Func;

extern const char *const rv_mnemonic[RV_NUM_OPS];

Rv_Func *rv_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
Rv_Block *rv_new_block(Rv_Func *fn, const char *label);
// Append an instruction to the current (last) block.
Rv_Instr *rv_append(Rv_Func *fn, Rv_Op op);
void rv_free_func(Rv_Func *fn);

Rv_Operand rv_reg(int reg);
Rv_Operand rv_imm(int64_t imm);
Rv_Operand rv_sym(const char *sym, int64_t offset);
Rv_Operand rv_mem(int base, int64_t offset);
const char *rv_reg_name(int reg);

// GNU assembler output.
void rv_emit_func(FILE *out, const Rv_Func *fn);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // RISCV_RV_H
