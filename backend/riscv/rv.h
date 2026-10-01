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
    RV_T4   = 29,
    RV_T5   = 30,
    RV_T6   = 31,
    RV_F0   = 32, // ft0
    RV_FA0  = 42,
    RV_VREG = 64, // first virtual register
};

typedef enum {
    RV_OPND_NONE,
    RV_OPND_REG, // reg
    RV_OPND_IMM, // imm
    RV_OPND_SYM, // sym + imm: a symbol, label or rounding mode
    RV_OPND_MEM, // imm(reg)
} Rv_OperandKind;

typedef struct {
    Rv_OperandKind kind;
    int reg;
    int64_t imm;
    char *sym; // owned
} Rv_Operand;

// Opcode and mnemonic.
#define RV_OPS(X)                                                                          \
    X(LI, "li") X(LA, "la") X(MV, "mv") X(ADD, "add") X(ADDI, "addi") X(ADDW, "addw")     \
    X(SUB, "sub") X(SUBW, "subw") X(MUL, "mul") X(MULW, "mulw") X(DIV, "div")             \
    X(DIVW, "divw") X(DIVU, "divu") X(DIVUW, "divuw") X(REM, "rem") X(REMW, "remw")       \
    X(REMU, "remu") X(REMUW, "remuw") X(AND, "and") X(ANDI, "andi") X(OR, "or")           \
    X(XOR, "xor") X(XORI, "xori") X(SLL, "sll") X(SLLW, "sllw") X(SLLI, "slli")           \
    X(SRL, "srl") X(SRLW, "srlw") X(SRLI, "srli") X(SRA, "sra") X(SRAW, "sraw")           \
    X(SRAI, "srai") X(SLT, "slt") X(SLTU, "sltu") X(SEQZ, "seqz") X(SNEZ, "snez")         \
    X(NEG, "neg") X(NEGW, "negw") X(NOT, "not") X(SEXTW, "sext.w")                        \
    X(LB, "lb") X(LBU, "lbu") X(LH, "lh") X(LHU, "lhu") X(LW, "lw") X(LD, "ld")           \
    X(SB, "sb") X(SH, "sh") X(SW, "sw") X(SD, "sd")                                       \
    X(FLW, "flw") X(FLD, "fld") X(FSW, "fsw") X(FSD, "fsd")                               \
    X(FADDS, "fadd.s") X(FADDD, "fadd.d") X(FSUBS, "fsub.s") X(FSUBD, "fsub.d")           \
    X(FMULS, "fmul.s") X(FMULD, "fmul.d") X(FDIVS, "fdiv.s") X(FDIVD, "fdiv.d")           \
    X(FNEGS, "fneg.s") X(FNEGD, "fneg.d") X(FEQS, "feq.s") X(FEQD, "feq.d")               \
    X(FLTS, "flt.s") X(FLTD, "flt.d") X(FLES, "fle.s") X(FLED, "fle.d")                   \
    X(FCVTSW, "fcvt.s.w") X(FCVTSWU, "fcvt.s.wu") X(FCVTSL, "fcvt.s.l")                   \
    X(FCVTSLU, "fcvt.s.lu") X(FCVTDW, "fcvt.d.w") X(FCVTDWU, "fcvt.d.wu")                 \
    X(FCVTDL, "fcvt.d.l") X(FCVTDLU, "fcvt.d.lu") X(FCVTWS, "fcvt.w.s")                   \
    X(FCVTWUS, "fcvt.wu.s") X(FCVTLS, "fcvt.l.s") X(FCVTLUS, "fcvt.lu.s")                 \
    X(FCVTWD, "fcvt.w.d") X(FCVTWUD, "fcvt.wu.d") X(FCVTLD, "fcvt.l.d")                   \
    X(FCVTLUD, "fcvt.lu.d") X(FCVTSD, "fcvt.s.d") X(FCVTDS, "fcvt.d.s")                   \
    X(FMVXW, "fmv.x.w") X(FMVWX, "fmv.w.x") X(FMVXD, "fmv.x.d") X(FMVDX, "fmv.d.x")       \
    X(J, "j") X(BEQZ, "beqz") X(BNEZ, "bnez") X(CALL, "call") X(JALR, "jalr")             \
    X(RET, "ret")

typedef enum {
#define RV_ENUM(op, mnem) RV_##op,
    RV_OPS(RV_ENUM)
#undef RV_ENUM
        RV_NUM_OPS
} Rv_Op;

typedef struct Rv_Instr {
    struct Rv_Instr *next;
    Rv_Op op;
    Rv_Operand opnd[3];
} Rv_Instr;

typedef struct Rv_Block {
    struct Rv_Block *next;
    char *label; // owned; NULL for none
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
// Append an instruction to block `b`, or to the current (last) block.
Rv_Instr *rv_append_to(Rv_Block *b, Rv_Op op);
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
