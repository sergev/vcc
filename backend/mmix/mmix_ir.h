//
// MMIX backend IR: a function is a list of blocks, a block a list of instructions.  An
// instruction is `op X,Y,Z` with up to three operands in assembler order; each names a
// physical or virtual register, an immediate, a symbol plus offset, a label or a special
// register.  Only what the backend emits is modelled (mmix.asdl is the full ISA
// reference).  Every instruction is 4 bytes, and the assembler and linker (-x) expand
// what is out of range, so the IR keeps no sizes.
//
#ifndef MMIX_IR_H
#define MMIX_IR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: $0..$255, then virtual registers.
enum {
    MMIX_SP   = 254, // the stack pointer
    MMIX_FP   = 253, // the frame pointer, GCC's too
    MMIX_SRET = 251, // the address of a structure result
    MMIX_TMP  = 255, // scratch, never allocated
    MMIX_VREG = 256, // first virtual register
};

// The special registers, by their number in get and put.
#define MMIX_SPECIALS(X) \
    X(rB, 0)             \
    X(rD, 1)             \
    X(rE, 2)             \
    X(rH, 3)             \
    X(rJ, 4)             \
    X(rM, 5)             \
    X(rR, 6)             \
    X(rBB, 7)            \
    X(rC, 8)             \
    X(rN, 9)             \
    X(rO, 10)            \
    X(rS, 11)            \
    X(rI, 12)            \
    X(rT, 13)            \
    X(rTT, 14)           \
    X(rK, 15)            \
    X(rQ, 16)            \
    X(rU, 17)            \
    X(rV, 18)            \
    X(rG, 19)            \
    X(rL, 20)            \
    X(rA, 21)            \
    X(rF, 22)            \
    X(rP, 23)            \
    X(rW, 24)            \
    X(rX, 25)            \
    X(rY, 26)            \
    X(rZ, 27)            \
    X(rWW, 28)           \
    X(rXX, 29)           \
    X(rYY, 30)           \
    X(rZZ, 31)

typedef enum {
#define MMIX_SPECIAL_ENUM(name, num) MMIX_##name = num,
    MMIX_SPECIALS(MMIX_SPECIAL_ENUM)
#undef MMIX_SPECIAL_ENUM
        MMIX_NUM_SPECIALS
} Mmix_Special;

typedef enum {
    MMIX_OPND_NONE,
    MMIX_OPND_REG,     // $n
    MMIX_OPND_IMM,     // an unsigned immediate, decimal: 8 bits, or 16 in pop's YZ
    MMIX_OPND_WYDE,    // a 16-bit wyde, #hex (seth...andnl)
    MMIX_OPND_SYM,     // sym+off: a base-plus-offset address, geta, pushj, jmp
    MMIX_OPND_LABEL,   // a branch target: L:n, or a name
    MMIX_OPND_SPECIAL, // rJ, rR...
} Mmix_OperandKind;

typedef struct {
    Mmix_OperandKind kind;
    int reg;     // a register, or a special register's number
    int64_t imm; // an immediate, a wyde, or a symbol's offset
    char *sym;   // owned; NULL for none
} Mmix_Operand;

// The operands an opcode takes, which the emitter checks.
typedef enum {
    MMIX_FORM_XYZ,    // $X, $Y, $Z or Z: arithmetic, compare, conditional set, go
    MMIX_FORM_FP,     // $X, $Y, $Z: floating point takes no immediate
    MMIX_FORM_NEG,    // $X, Y, $Z or Z: Y is the minuend, an immediate
    MMIX_FORM_ROUND,  // $X, Y, $Z: Y is the rounding mode (fix, fixu, fsqrt)
    MMIX_FORM_XZ,     // $X, $Z or Z: flot and the like
    MMIX_FORM_XY,     // $X, $Y: set, a register copy
    MMIX_FORM_MEM,    // $X, $Y, $Z or Z; or $X, sym+off (the assembler finds a base)
    MMIX_FORM_WYDE,   // $X, wyde
    MMIX_FORM_BRANCH, // $X, label
    MMIX_FORM_ADDR,   // $X, sym+off or label: lda, geta
    MMIX_FORM_JUMP,   // label or sym+off: jmp
    MMIX_FORM_PUSHJ,  // $X, sym+off or label
    MMIX_FORM_POP,    // X, YZ
    MMIX_FORM_GET,    // $X, special
    MMIX_FORM_PUT,    // special, $Z or Z
    MMIX_FORM_TRAP,   // X, Y, Z
    MMIX_FORM_NONE,   // no operand: swym
} Mmix_Form;

// Opcode, mnemonic, form.  The mnemonics are lowercase, as everywhere in this project.
#define MMIX_OPS(X)           \
    X(ADD, "add", XYZ)        \
    X(ADDU, "addu", XYZ)      \
    X(SUB, "sub", XYZ)        \
    X(SUBU, "subu", XYZ)      \
    X(MUL, "mul", XYZ)        \
    X(MULU, "mulu", XYZ)      \
    X(DIV, "div", XYZ)        \
    X(DIVU, "divu", XYZ)      \
    X(NEG, "neg", NEG)        \
    X(NEGU, "negu", NEG)      \
    X(ADDU2, "2addu", XYZ)    \
    X(ADDU4, "4addu", XYZ)    \
    X(ADDU8, "8addu", XYZ)    \
    X(ADDU16, "16addu", XYZ)  \
    X(CMP, "cmp", XYZ)        \
    X(CMPU, "cmpu", XYZ)      \
    X(SL, "sl", XYZ)          \
    X(SLU, "slu", XYZ)        \
    X(SR, "sr", XYZ)          \
    X(SRU, "sru", XYZ)        \
    X(AND, "and", XYZ)        \
    X(OR, "or", XYZ)          \
    X(XOR, "xor", XYZ)        \
    X(ANDN, "andn", XYZ)      \
    X(ORN, "orn", XYZ)        \
    X(NAND, "nand", XYZ)      \
    X(NOR, "nor", XYZ)        \
    X(NXOR, "nxor", XYZ)      \
    X(FADD, "fadd", FP)       \
    X(FSUB, "fsub", FP)       \
    X(FMUL, "fmul", FP)       \
    X(FDIV, "fdiv", FP)       \
    X(FCMP, "fcmp", FP)       \
    X(FEQL, "feql", FP)       \
    X(FUN, "fun", FP)         \
    X(FSQRT, "fsqrt", ROUND)  \
    X(FIX, "fix", ROUND)      \
    X(FIXU, "fixu", ROUND)    \
    X(FLOT, "flot", XZ)       \
    X(FLOTU, "flotu", XZ)     \
    X(SFLOT, "sflot", XZ)     \
    X(SFLOTU, "sflotu", XZ)   \
    X(ZSN, "zsn", XYZ)        \
    X(ZSZ, "zsz", XYZ)        \
    X(ZSP, "zsp", XYZ)        \
    X(ZSOD, "zsod", XYZ)      \
    X(ZSNN, "zsnn", XYZ)      \
    X(ZSNZ, "zsnz", XYZ)      \
    X(ZSNP, "zsnp", XYZ)      \
    X(ZSEV, "zsev", XYZ)      \
    X(CSN, "csn", XYZ)        \
    X(CSZ, "csz", XYZ)        \
    X(CSP, "csp", XYZ)        \
    X(CSOD, "csod", XYZ)      \
    X(CSNN, "csnn", XYZ)      \
    X(CSNZ, "csnz", XYZ)      \
    X(CSNP, "csnp", XYZ)      \
    X(CSEV, "csev", XYZ)      \
    X(LDB, "ldb", MEM)        \
    X(LDBU, "ldbu", MEM)      \
    X(LDW, "ldw", MEM)        \
    X(LDWU, "ldwu", MEM)      \
    X(LDT, "ldt", MEM)        \
    X(LDTU, "ldtu", MEM)      \
    X(LDO, "ldo", MEM)        \
    X(LDSF, "ldsf", MEM)      \
    X(STB, "stb", MEM)        \
    X(STBU, "stbu", MEM)      \
    X(STW, "stw", MEM)        \
    X(STWU, "stwu", MEM)      \
    X(STT, "stt", MEM)        \
    X(STTU, "sttu", MEM)      \
    X(STO, "sto", MEM)        \
    X(STSF, "stsf", MEM)      \
    X(SET, "set", XY)         \
    X(SETH, "seth", WYDE)     \
    X(SETMH, "setmh", WYDE)   \
    X(SETML, "setml", WYDE)   \
    X(SETL, "setl", WYDE)     \
    X(INCH, "inch", WYDE)     \
    X(INCMH, "incmh", WYDE)   \
    X(INCML, "incml", WYDE)   \
    X(INCL, "incl", WYDE)     \
    X(ORH, "orh", WYDE)       \
    X(ORMH, "ormh", WYDE)     \
    X(ORML, "orml", WYDE)     \
    X(ORL, "orl", WYDE)       \
    X(ANDNH, "andnh", WYDE)   \
    X(ANDNMH, "andnmh", WYDE) \
    X(ANDNML, "andnml", WYDE) \
    X(ANDNL, "andnl", WYDE)   \
    X(LDA, "lda", ADDR)       \
    X(GETA, "geta", ADDR)     \
    X(BN, "bn", BRANCH)       \
    X(BZ, "bz", BRANCH)       \
    X(BP, "bp", BRANCH)       \
    X(BOD, "bod", BRANCH)     \
    X(BNN, "bnn", BRANCH)     \
    X(BNZ, "bnz", BRANCH)     \
    X(BNP, "bnp", BRANCH)     \
    X(BEV, "bev", BRANCH)     \
    X(PBN, "pbn", BRANCH)     \
    X(PBZ, "pbz", BRANCH)     \
    X(PBP, "pbp", BRANCH)     \
    X(PBOD, "pbod", BRANCH)   \
    X(PBNN, "pbnn", BRANCH)   \
    X(PBNZ, "pbnz", BRANCH)   \
    X(PBNP, "pbnp", BRANCH)   \
    X(PBEV, "pbev", BRANCH)   \
    X(JMP, "jmp", JUMP)       \
    X(GO, "go", XYZ)          \
    X(PUSHJ, "pushj", PUSHJ)  \
    X(PUSHGO, "pushgo", XYZ)  \
    X(POP, "pop", POP)        \
    X(GET, "get", GET)        \
    X(PUT, "put", PUT)        \
    X(TRAP, "trap", TRAP)     \
    X(SWYM, "swym", NONE)

typedef enum {
#define MMIX_ENUM(op, mnem, form) MMIX_##op,
    MMIX_OPS(MMIX_ENUM)
#undef MMIX_ENUM
        MMIX_NUM_OPS
} Mmix_Op;

#define MMIX_MAX_OPERANDS 3

typedef struct Mmix_Instr {
    struct Mmix_Instr *next;
    Mmix_Op op;
    Mmix_Operand opnd[MMIX_MAX_OPERANDS];
} Mmix_Instr;

typedef struct Mmix_Block {
    struct Mmix_Block *next;
    char *label; // owned; NULL for none
    Mmix_Instr *head, *tail;
} Mmix_Block;

typedef struct {
    char *name; // owned
    bool global;
    Mmix_Block *blocks, *tail;
} Mmix_Func;

extern const char *const mmix_mnemonic[MMIX_NUM_OPS];
extern const Mmix_Form mmix_form[MMIX_NUM_OPS];
extern const char *const mmix_special_name[MMIX_NUM_SPECIALS];

Mmix_Func *mmix_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
Mmix_Block *mmix_new_block(Mmix_Func *fn, const char *label);
// Append an instruction to the current (last) block.
Mmix_Instr *mmix_append(Mmix_Func *fn, Mmix_Op op);
// Append an instruction to block `b`.
Mmix_Instr *mmix_append_to(Mmix_Block *b, Mmix_Op op);
void mmix_free_func(Mmix_Func *fn);

Mmix_Operand mmix_reg(int reg);
Mmix_Operand mmix_imm(int64_t imm);
Mmix_Operand mmix_wyde(unsigned wyde);
Mmix_Operand mmix_sym(const char *sym, int64_t off);
Mmix_Operand mmix_label(const char *label);
Mmix_Operand mmix_special(Mmix_Special sr);

// GNU mmix-knuth-mmixware-as syntax, for `as -x -no-predefined-syms`.  A form's operands
// are checked, an immediate against its field (8 bits, or 16 in pop's YZ and a wyde):
// a violation is a fatal error, not a wrong instruction.
void mmix_emit_func(FILE *out, const Mmix_Func *fn);
// One instruction, as a line of mmix_emit_func.
void mmix_emit_instr(FILE *out, const Mmix_Instr *in);

#ifndef __cplusplus
#include "srcloc.h"

_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // MMIX_IR_H
