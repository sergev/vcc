//
// AVR backend IR: a function is a list of blocks, a block a list of instructions.  An
// instruction has up to two operands in assembler order (destination first, as AVR
// writes them).  Operands name physical or virtual 8-bit registers; only what the
// backend emits is modelled (avr.asdl is the full ISA reference).  Every instruction
// knows its size, 2 or 4 bytes, so the branch relaxation pass can compute offsets.
//
#ifndef AVR_IR_H
#define AVR_IR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: r0..r31, then virtual registers.
enum {
    AVR_TMP  = 0,  // __tmp_reg__
    AVR_ZERO = 1,  // __zero_reg__: 0 at every call and return
    AVR_X    = 26, // r27:r26
    AVR_Y    = 28, // r29:r28, the frame pointer
    AVR_Z    = 30, // r31:r30
    AVR_VREG = 32, // first virtual register
};

typedef enum {
    AVR_OPND_NONE,
    AVR_OPND_REG,   // rN; a register pair (movw, adiw) is named by its low register
    AVR_OPND_IMM,   // a number: an immediate, an I/O address, a bit number
    AVR_OPND_SYM,   // sym+off, possibly under a modifier: lo8(sym+off), pm(sym), ...
    AVR_OPND_PTR,   // X, Y or Z, possibly post-incremented (X+) or pre-decremented (-X)
    AVR_OPND_DISP,  // Y+q or Z+q, q in 0..63
    AVR_OPND_LABEL, // sym: a branch, jump or call target
} AVR_OperandKind;

// What part of a symbol's address an AVR_OPND_SYM operand takes.
typedef enum {
    AVR_MOD_NONE,   // the address itself: lds/sts, and .short data
    AVR_MOD_LO8,    // lo8(): bits 7..0 of a data address
    AVR_MOD_HI8,    // hi8(): bits 15..8
    AVR_MOD_HH8,    // hh8(): bits 23..16
    AVR_MOD_PM_LO8, // pm_lo8(): bits 7..0 of a code (word) address
    AVR_MOD_PM_HI8, // pm_hi8(): bits 15..8 of a code (word) address
    AVR_MOD_PM,     // pm(): a code (word) address, in data
} AVR_Modifier;

typedef enum {
    AVR_PTR_PLAIN,    // X
    AVR_PTR_POST_INC, // X+
    AVR_PTR_PRE_DEC,  // -X
} AVR_PtrMode;

typedef struct {
    AVR_OperandKind kind;
    int reg;          // a register, or the pointer of PTR/DISP (AVR_X, AVR_Y, AVR_Z)
    int64_t imm;      // an immediate, a symbol's offset, or a displacement
    AVR_Modifier mod; // of a symbol
    AVR_PtrMode mode; // of a pointer
    char *sym;        // owned
} AVR_Operand;

// Opcode, mnemonic, and size in bytes.
#define AVR_OPS(X)                                                                         \
    X(ADD, "add", 2)                                                                       \
    X(ADC, "adc", 2)                                                                       \
    X(ADIW, "adiw", 2)                                                                     \
    X(SUB, "sub", 2)                                                                       \
    X(SUBI, "subi", 2)                                                                     \
    X(SBC, "sbc", 2)                                                                       \
    X(SBCI, "sbci", 2)                                                                     \
    X(SBIW, "sbiw", 2)                                                                     \
    X(AND, "and", 2)                                                                       \
    X(ANDI, "andi", 2)                                                                     \
    X(OR, "or", 2)                                                                         \
    X(ORI, "ori", 2)                                                                       \
    X(EOR, "eor", 2)                                                                       \
    X(COM, "com", 2)                                                                       \
    X(NEG, "neg", 2)                                                                       \
    X(INC, "inc", 2)                                                                       \
    X(DEC, "dec", 2)                                                                       \
    X(MUL, "mul", 2)                                                                       \
    X(MULS, "muls", 2)                                                                     \
    X(MULSU, "mulsu", 2)                                                                   \
    X(CP, "cp", 2)                                                                         \
    X(CPC, "cpc", 2)                                                                       \
    X(CPI, "cpi", 2)                                                                       \
    X(CPSE, "cpse", 2)                                                                     \
    X(TST, "tst", 2)                                                                       \
    X(CLR, "clr", 2)                                                                       \
    X(SER, "ser", 2)                                                                       \
    X(MOV, "mov", 2)                                                                       \
    X(MOVW, "movw", 2)                                                                     \
    X(LDI, "ldi", 2)                                                                       \
    X(LD, "ld", 2)                                                                         \
    X(LDD, "ldd", 2)                                                                       \
    X(LDS, "lds", 4)                                                                       \
    X(ST, "st", 2)                                                                         \
    X(STD, "std", 2)                                                                       \
    X(STS, "sts", 4)                                                                       \
    X(PUSH, "push", 2)                                                                     \
    X(POP, "pop", 2)                                                                       \
    X(IN, "in", 2)                                                                         \
    X(OUT, "out", 2)                                                                       \
    X(LSL, "lsl", 2)                                                                       \
    X(LSR, "lsr", 2)                                                                       \
    X(ROL, "rol", 2)                                                                       \
    X(ROR, "ror", 2)                                                                       \
    X(ASR, "asr", 2)                                                                       \
    X(SWAP, "swap", 2)                                                                     \
    X(BST, "bst", 2)                                                                       \
    X(BLD, "bld", 2)                                                                       \
    X(SBRC, "sbrc", 2)                                                                     \
    X(SBRS, "sbrs", 2)                                                                     \
    X(BREQ, "breq", 2)                                                                     \
    X(BRNE, "brne", 2)                                                                     \
    X(BRLO, "brlo", 2)                                                                     \
    X(BRSH, "brsh", 2)                                                                     \
    X(BRLT, "brlt", 2)                                                                     \
    X(BRGE, "brge", 2)                                                                     \
    X(BRMI, "brmi", 2)                                                                     \
    X(BRPL, "brpl", 2)                                                                     \
    X(RJMP, "rjmp", 2)                                                                     \
    X(JMP, "jmp", 4)                                                                       \
    X(RCALL, "rcall", 2)                                                                   \
    X(CALL, "call", 4)                                                                     \
    X(ICALL, "icall", 2)                                                                   \
    X(IJMP, "ijmp", 2)                                                                     \
    X(RET, "ret", 2)                                                                       \
    X(CLI, "cli", 2)                                                                       \
    X(NOP, "nop", 2)

typedef enum {
#define AVR_ENUM(op, mnem, size) AVR_##op,
    AVR_OPS(AVR_ENUM)
#undef AVR_ENUM
        AVR_NUM_OPS
} AVR_Op;

#define AVR_MAX_OPERANDS 2

typedef struct AVR_Instr {
    struct AVR_Instr *next;
    AVR_Op op;
    AVR_Operand opnd[AVR_MAX_OPERANDS];
} AVR_Instr;

typedef struct AVR_Block {
    struct AVR_Block *next;
    char *label; // owned; NULL for none
    AVR_Instr *head, *tail;
} AVR_Block;

typedef struct {
    char *name; // owned
    bool global;
    AVR_Block *blocks, *tail;
} AVR_Func;

extern const char *const avr_mnemonic[AVR_NUM_OPS];
extern const int avr_size[AVR_NUM_OPS]; // bytes

AVR_Func *avr_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
AVR_Block *avr_new_block(AVR_Func *fn, const char *label);
// Append an instruction to the current (last) block.
AVR_Instr *avr_append(AVR_Func *fn, AVR_Op op);
// Append an instruction to block `b`.
AVR_Instr *avr_append_to(AVR_Block *b, AVR_Op op);
void avr_free_func(AVR_Func *fn);
// Move the instructions after `in` in block `b` into a new block labelled `label`
// (copied), right after `b`; returns it.
AVR_Block *avr_split_after(AVR_Func *fn, AVR_Block *b, AVR_Instr *in, const char *label);
// Insert an instruction after `in` in block `b`.
AVR_Instr *avr_insert_after(AVR_Block *b, AVR_Instr *in, AVR_Op op);

AVR_Operand avr_reg(int reg);
AVR_Operand avr_imm(int64_t imm);
AVR_Operand avr_sym(AVR_Modifier mod, const char *sym, int64_t off);
AVR_Operand avr_ptr(int ptr, AVR_PtrMode mode); // ptr is AVR_X, AVR_Y or AVR_Z
AVR_Operand avr_disp(int ptr, int q);           // ptr is AVR_Y or AVR_Z
AVR_Operand avr_label(const char *sym);

// Branch relaxation, the last pass: a conditional branch beyond +-64 words becomes the
// inverse branch over an rjmp (or jmp), an rjmp beyond +-2K words a jmp; repeated until
// every branch reaches, as sizes only grow.
void avr_relax(AVR_Func *fn);

// GNU avr-as syntax, as clang emits it.  The header goes once at the top of a module.
void avr_emit_header(FILE *out);
void avr_emit_func(FILE *out, const AVR_Func *fn);
// One instruction, as a line of avr_emit_func.
void avr_emit_instr(FILE *out, const AVR_Instr *in);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // AVR_IR_H
