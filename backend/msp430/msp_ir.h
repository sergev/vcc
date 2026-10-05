//
// MSP430 backend IR: a function is a list of blocks, a block a list of instructions.  An
// instruction has up to two operands in assembler order (source first, as MSP430 writes
// them), and a byte flag for the .b forms.  Operands name physical or virtual 16-bit
// registers; only what the backend emits is modelled (msp430.asdl is the full ISA
// reference, MSP430X included).  Every instruction knows its size, 2, 4 or 6 bytes, so
// the branch relaxation pass can compute offsets.
//
#ifndef MSP_IR_H
#define MSP_IR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: r0..r15, then virtual registers.
enum {
    MSP_PC   = 0,
    MSP_SP   = 1,
    MSP_SR   = 2, // the status register, and constant generator 1
    MSP_CG   = 3, // constant generator 2
    MSP_VREG = 16, // first virtual register
};

typedef enum {
    MSP_OPND_NONE,
    MSP_OPND_REG,     // rN
    MSP_OPND_INDEXED, // x(rN), or sym+x(rN)
    MSP_OPND_ABS,     // &sym+x, or &x
    MSP_OPND_IND,     // @rN
    MSP_OPND_POSTINC, // @rN+: as a source, and only with a register destination (clang)
    MSP_OPND_IMM,     // #x, or #sym+x
    MSP_OPND_LABEL,   // sym: a jump target
} Msp_OperandKind;

typedef struct {
    Msp_OperandKind kind;
    int reg;       // a register, or the base of INDEXED/IND/POSTINC
    int64_t imm;   // an immediate, an offset, or an address
    char *sym;     // owned; NULL for none
    bool incoming; // x(r1) into the incoming stack arguments: x counts from their start,
                   // until the frame is laid out and the offset completed
} Msp_Operand;

// How an instruction is encoded, which decides its size and its operands.
typedef enum {
    MSP_FORM_DOUBLE, // Format I: source, destination
    MSP_FORM_SINGLE, // Format II: one operand, encoded as a source
    MSP_FORM_JUMP,   // Format III: a label within +-512 words
    MSP_FORM_DST,    // emulated, a constant-generator source: the destination alone
    MSP_FORM_TWICE,  // emulated as `op dst, dst` (rla, rlc): the destination twice
    MSP_FORM_SRC,    // emulated as `mov src, pc` (br): the source alone
    MSP_FORM_NONE,   // no operand: one word
} Msp_Form;

// Opcode, mnemonic, form.
#define MSP_OPS(X)                                                                         \
    X(MOV, "mov", DOUBLE)                                                                  \
    X(ADD, "add", DOUBLE)                                                                  \
    X(ADDC, "addc", DOUBLE)                                                                \
    X(SUB, "sub", DOUBLE)                                                                  \
    X(SUBC, "subc", DOUBLE)                                                                \
    X(CMP, "cmp", DOUBLE)                                                                  \
    X(BIT, "bit", DOUBLE)                                                                  \
    X(BIC, "bic", DOUBLE)                                                                  \
    X(BIS, "bis", DOUBLE)                                                                  \
    X(XOR, "xor", DOUBLE)                                                                  \
    X(AND, "and", DOUBLE)                                                                  \
    X(RRC, "rrc", SINGLE)                                                                  \
    X(SWPB, "swpb", SINGLE)                                                                \
    X(RRA, "rra", SINGLE)                                                                  \
    X(SXT, "sxt", SINGLE)                                                                  \
    X(PUSH, "push", SINGLE)                                                                \
    X(CALL, "call", SINGLE)                                                                \
    X(JNE, "jne", JUMP)                                                                    \
    X(JEQ, "jeq", JUMP)                                                                    \
    X(JLO, "jlo", JUMP)                                                                    \
    X(JHS, "jhs", JUMP)                                                                    \
    X(JN, "jn", JUMP)                                                                      \
    X(JGE, "jge", JUMP)                                                                    \
    X(JL, "jl", JUMP)                                                                      \
    X(JMP, "jmp", JUMP)                                                                    \
    X(CLR, "clr", DST)                                                                     \
    X(INC, "inc", DST)                                                                     \
    X(INCD, "incd", DST)                                                                   \
    X(DEC, "dec", DST)                                                                     \
    X(DECD, "decd", DST)                                                                   \
    X(TST, "tst", DST)                                                                     \
    X(INV, "inv", DST)                                                                     \
    X(ADC, "adc", DST)                                                                     \
    X(POP, "pop", DST)                                                                     \
    X(RLA, "rla", TWICE)                                                                   \
    X(RLC, "rlc", TWICE)                                                                   \
    X(BR, "br", SRC)                                                                       \
    X(RET, "ret", NONE)                                                                    \
    X(NOP, "nop", NONE)                                                                    \
    X(CLRC, "clrc", NONE)                                                                  \
    X(SETC, "setc", NONE)

typedef enum {
#define MSP_ENUM(op, mnem, form) MSP_##op,
    MSP_OPS(MSP_ENUM)
#undef MSP_ENUM
        MSP_NUM_OPS
} Msp_Op;

#define MSP_MAX_OPERANDS 2

typedef struct Msp_Instr {
    struct Msp_Instr *next;
    Msp_Op op;
    bool byte; // the .b form
    bool vol;  // part of a volatile access: the peephole pass leaves it alone
    // A call: the registers r12-r15 its arguments are in, as bits 12-15, with bit 0 set
    // once they are known; 0 for a call that may read all four.
    unsigned args;
    Msp_Operand opnd[MSP_MAX_OPERANDS];
} Msp_Instr;

typedef struct Msp_Block {
    struct Msp_Block *next;
    char *label; // owned; NULL for none
    Msp_Instr *head, *tail;
} Msp_Block;

typedef struct {
    char *name; // owned
    bool global;
    Msp_Block *blocks, *tail;
} Msp_Func;

extern const char *const msp_mnemonic[MSP_NUM_OPS];
extern const Msp_Form msp_form[MSP_NUM_OPS];

Msp_Func *msp_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
Msp_Block *msp_new_block(Msp_Func *fn, const char *label);
// Append an instruction to the current (last) block, or to block `b`.
Msp_Instr *msp_append(Msp_Func *fn, Msp_Op op);
Msp_Instr *msp_append_to(Msp_Block *b, Msp_Op op);
// Insert an instruction after `in` of block `b`.
Msp_Instr *msp_insert_after(Msp_Block *b, Msp_Instr *in, Msp_Op op);
// Free one instruction (its operands' symbols too).
void msp_free_instr(Msp_Instr *in);
// The conditional jump taken when `op` is not, or MSP_NUM_OPS when it has none (jn) or is
// no conditional jump.
Msp_Op msp_inverse(Msp_Op op);
// Move the instructions after `in` into a new block labelled `label`, after `b`.
Msp_Block *msp_split_after(Msp_Func *fn, Msp_Block *b, Msp_Instr *in, const char *label);
void msp_free_func(Msp_Func *fn);

Msp_Operand msp_reg(int reg);
Msp_Operand msp_indexed(int reg, const char *sym, int64_t off); // sym may be NULL
Msp_Operand msp_abs(const char *sym, int64_t off);              // sym may be NULL
Msp_Operand msp_ind(int reg);
Msp_Operand msp_postinc(int reg);
Msp_Operand msp_imm(int64_t imm);
Msp_Operand msp_imm_sym(const char *sym, int64_t off);
Msp_Operand msp_label(const char *sym);
// A copy of `o`, with its own symbol.
Msp_Operand msp_copy(const Msp_Operand *o);

// An immediate as the assembler reads it: sign-normalized to the operation's width, so
// that all ones is -1, which the constant generator supplies.
int64_t msp_imm_value(int64_t imm, bool byte);
// The instruction's size in bytes: a word, plus one per extension word.  Indexed and
// absolute operands take one (but a source 0(rN), which is @rN), and so does an
// immediate other than 0, 1, 2, 4, 8 and -1 (the constant generators), or one with a
// symbol; `push #4` and `push #8` take one too, as GNU as encodes them.
// x(rN) with x = 0 and no symbol, which a source field takes as @rN: GNU as encodes it
// so, one word shorter, and the emitter prints it so for clang to agree.
bool msp_zero_indexed(const Msp_Operand *o);

int msp_instr_size(const Msp_Instr *in);

// GNU msp430-as syntax, which GNU as and clang's assembler both accept and encode alike.
void msp_emit_func(FILE *out, const Msp_Func *fn);
// One instruction, as a line of msp_emit_func.
void msp_emit_instr(FILE *out, const Msp_Instr *in);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // MSP_IR_H
