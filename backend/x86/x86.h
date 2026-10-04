//
// x86-64 backend IR: a function is a list of blocks, a block a list of instructions.
// An instruction has up to three operands, in AT&T order (sources, then the
// destination: `imul $k, src, dst`), and a width that a suffixed opcode spells as
// b/w/l/q.  Operands name physical or virtual
// registers, each at a width of its own; only what the backend emits is modelled
// (x86_64.asdl is the full ISA reference).
//
#ifndef X86_X86_H
#define X86_X86_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register numbers: the general registers in their encoding order, then xmm0..xmm15,
// then the x87 stack st(0)..st(7), then virtual registers.
enum {
    X86_RAX   = 0,
    X86_RCX   = 1,
    X86_RDX   = 2,
    X86_RBX   = 3,
    X86_RSP   = 4,
    X86_RBP   = 5,
    X86_RSI   = 6,
    X86_RDI   = 7,
    X86_R8    = 8,
    X86_R9    = 9,
    X86_R10   = 10,
    X86_R11   = 11,
    X86_R12   = 12,
    X86_R13   = 13,
    X86_R14   = 14,
    X86_R15   = 15,
    X86_XMM0  = 16, // xmm0..xmm15
    X86_ST0   = 32, // st(0)..st(7)
    X86_VREG  = 48, // first virtual register
};

// An operand's or an instruction's width.  A general register is named by it
// (%al, %ax, %eax, %rax); an xmm or x87 register is not.
typedef enum {
    X86_B, // 8 bits
    X86_W, // 16
    X86_L, // 32
    X86_Q, // 64
} X86_Width;

typedef enum {
    X86_OPND_NONE,
    X86_OPND_REG,   // %reg at width
    X86_OPND_IMM,   // $imm
    X86_OPND_MEM,   // disp(base, index, scale); base or index may be absent (-1)
    X86_OPND_RIP,   // sym+disp(%rip): a static datum, RIP-relative
    X86_OPND_LABEL, // sym: a jump or call target
    X86_OPND_INDIRECT, // *%reg: an indirect call target
} X86_OperandKind;

typedef struct {
    X86_OperandKind kind;
    int reg; // the register; the base of a memory operand, or -1
    X86_Width width;
    int64_t imm;  // immediate, or a memory operand's displacement
    int index;    // a memory operand's index register, or -1
    int scale;    // 1, 2, 4 or 8, with an index
    char *sym;    // owned
} X86_Operand;

// Condition codes in their encoding order, so that `c ^ 1` is the inverse of `c`.
typedef enum {
    X86_CC_O,
    X86_CC_NO,
    X86_CC_B, // unsigned <
    X86_CC_AE,
    X86_CC_E,
    X86_CC_NE,
    X86_CC_BE,
    X86_CC_A,
    X86_CC_S,
    X86_CC_NS,
    X86_CC_P, // unordered, after ucomis
    X86_CC_NP,
    X86_CC_L, // signed <
    X86_CC_GE,
    X86_CC_LE,
    X86_CC_G,
} X86_Cond;

// How an opcode's mnemonic is spelled: in full, with the instruction's width as a
// suffix (`mov` + l = `movl`), or with its condition (`set` + e = `sete`).
enum { X86_PLAIN, X86_SUFFIX, X86_CONDITION };

// Opcode, mnemonic, spelling.  The sign and zero extensions name the source width in
// the mnemonic and take the destination's as the suffix: `movsb` + l = `movsbl`.
#define X86_OPS(X)                                                                         \
    X(MOV, "mov", X86_SUFFIX)                                                              \
    X(MOVABS, "movabs", X86_SUFFIX)                                                        \
    X(MOVSB, "movsb", X86_SUFFIX)                                                          \
    X(MOVSW, "movsw", X86_SUFFIX)                                                          \
    X(MOVSL, "movsl", X86_SUFFIX)                                                          \
    X(MOVZB, "movzb", X86_SUFFIX)                                                          \
    X(MOVZW, "movzw", X86_SUFFIX)                                                          \
    X(LEA, "lea", X86_SUFFIX)                                                              \
    X(ADD, "add", X86_SUFFIX)                                                              \
    X(SUB, "sub", X86_SUFFIX)                                                              \
    X(IMUL, "imul", X86_SUFFIX)                                                            \
    X(IDIV, "idiv", X86_SUFFIX)                                                            \
    X(DIV, "div", X86_SUFFIX)                                                              \
    X(AND, "and", X86_SUFFIX)                                                              \
    X(OR, "or", X86_SUFFIX)                                                                \
    X(XOR, "xor", X86_SUFFIX)                                                              \
    X(NEG, "neg", X86_SUFFIX)                                                              \
    X(NOT, "not", X86_SUFFIX)                                                              \
    X(SHL, "shl", X86_SUFFIX)                                                              \
    X(SAR, "sar", X86_SUFFIX)                                                              \
    X(SHR, "shr", X86_SUFFIX)                                                              \
    X(CMP, "cmp", X86_SUFFIX)                                                              \
    X(TEST, "test", X86_SUFFIX)                                                            \
    X(PUSH, "push", X86_SUFFIX)                                                            \
    X(POP, "pop", X86_SUFFIX)                                                              \
    X(CLTD, "cltd", X86_PLAIN)                                                             \
    X(CQTO, "cqto", X86_PLAIN)                                                             \
    X(SET, "set", X86_CONDITION)                                                           \
    X(J, "j", X86_CONDITION)                                                               \
    X(JMP, "jmp", X86_PLAIN)                                                               \
    X(CALL, "call", X86_PLAIN)                                                             \
    X(LEAVE, "leave", X86_PLAIN)                                                           \
    X(RET, "ret", X86_PLAIN)                                                               \
    X(MOVD, "movd", X86_PLAIN)                                                             \
    X(MOVSS, "movss", X86_PLAIN)                                                           \
    X(MOVSD, "movsd", X86_PLAIN)                                                           \
    X(MOVAPS, "movaps", X86_PLAIN)                                                         \
    X(MOVUPS, "movups", X86_PLAIN)                                                         \
    X(ADDSS, "addss", X86_PLAIN)                                                           \
    X(ADDSD, "addsd", X86_PLAIN)                                                           \
    X(SUBSS, "subss", X86_PLAIN)                                                           \
    X(SUBSD, "subsd", X86_PLAIN)                                                           \
    X(MULSS, "mulss", X86_PLAIN)                                                           \
    X(MULSD, "mulsd", X86_PLAIN)                                                           \
    X(DIVSS, "divss", X86_PLAIN)                                                           \
    X(DIVSD, "divsd", X86_PLAIN)                                                           \
    X(XORPS, "xorps", X86_PLAIN)                                                           \
    X(UCOMISS, "ucomiss", X86_PLAIN)                                                       \
    X(UCOMISD, "ucomisd", X86_PLAIN)                                                       \
    X(CVTSS2SD, "cvtss2sd", X86_PLAIN)                                                     \
    X(CVTSD2SS, "cvtsd2ss", X86_PLAIN)                                                     \
    X(CVTSI2SS, "cvtsi2ss", X86_SUFFIX)                                                    \
    X(CVTSI2SD, "cvtsi2sd", X86_SUFFIX)                                                    \
    X(CVTTSS2SI, "cvttss2si", X86_PLAIN)                                                   \
    X(CVTTSD2SI, "cvttsd2si", X86_PLAIN)                                                   \
    X(FLDT, "fldt", X86_PLAIN)                                                             \
    X(FSTPT, "fstpt", X86_PLAIN)                                                           \
    X(FLDS, "flds", X86_PLAIN)                                                             \
    X(FLDL, "fldl", X86_PLAIN)                                                             \
    X(FSTPS, "fstps", X86_PLAIN)                                                           \
    X(FSTPL, "fstpl", X86_PLAIN)                                                           \
    X(FILDQ, "fildq", X86_PLAIN)                                                           \
    X(FISTPQ, "fistpq", X86_PLAIN)                                                         \
    X(FLDZ, "fldz", X86_PLAIN)                                                             \
    X(FLD1, "fld1", X86_PLAIN)                                                             \
    X(FADDP, "faddp", X86_PLAIN)                                                           \
    X(FSUBRP, "fsubrp", X86_PLAIN)                                                         \
    X(FMULP, "fmulp", X86_PLAIN)                                                           \
    X(FDIVRP, "fdivrp", X86_PLAIN)                                                         \
    X(FADDS, "fadds", X86_PLAIN)                                                           \
    X(FSUBS, "fsubs", X86_PLAIN)                                                           \
    X(FCHS, "fchs", X86_PLAIN)                                                             \
    X(FUCOMIP, "fucomip", X86_PLAIN)                                                       \
    X(FSTP, "fstp", X86_PLAIN)                                                             \
    X(FNSTCW, "fnstcw", X86_PLAIN)                                                         \
    X(FLDCW, "fldcw", X86_PLAIN)                                                           \
    X(EPILOGUE, "#epilogue", X86_PLAIN)

typedef enum {
#define X86_ENUM(op, mnem, form) X86_##op,
    X86_OPS(X86_ENUM)
#undef X86_ENUM
        X86_NUM_OPS
} X86_Op;

#define X86_MAX_OPERANDS 3

typedef struct X86_Instr {
    struct X86_Instr *next;
    X86_Op op;
    X86_Width width; // the suffix, for a suffixed opcode
    X86_Cond cond;   // the condition, for a conditional one
    X86_Operand opnd[X86_MAX_OPERANDS];
    bool is_volatile; // selected for a volatile access: the peephole keeps it as it is
} X86_Instr;

typedef struct X86_Block {
    struct X86_Block *next;
    char *label; // owned; NULL for none
    X86_Instr *head, *tail;
} X86_Block;

typedef struct {
    char *name; // owned
    bool global;
    X86_Block *blocks, *tail;
    bool volatile_access; // the TAC instruction being selected is a volatile access
} X86_Func;

extern const char *const x86_mnemonic[X86_NUM_OPS];
extern const int x86_form[X86_NUM_OPS];
extern const char *const x86_cond_name[16];

X86_Func *x86_new_func(const char *name, bool global);
// Append a block, labelled `label` (copied; NULL for none), and make it current.
X86_Block *x86_new_block(X86_Func *fn, const char *label);
// Append an instruction to the current (last) block.
X86_Instr *x86_append(X86_Func *fn, X86_Op op, X86_Width width);
void x86_free_func(X86_Func *fn);

X86_Operand x86_reg(int reg, X86_Width width); // a general register at width
X86_Operand x86_xmm(int reg);                  // xmm0..xmm15: X86_XMM0 + n
X86_Operand x86_st(int n);                     // st(n)
X86_Operand x86_imm(int64_t imm);
X86_Operand x86_mem(int base, int64_t disp);   // disp(base)
// disp(base, index, scale); base -1 for none
X86_Operand x86_mem_index(int base, int index, int scale, int64_t disp);
X86_Operand x86_rip(const char *sym, int64_t disp); // sym+disp(%rip)
X86_Operand x86_label(const char *sym);
X86_Operand x86_indirect(int reg); // *%reg

bool x86_is_xmm(int reg);
bool x86_is_st(int reg);
// The assembler name of register `reg` at `width`, without the `%`: "al", "r8d",
// "xmm3", "st(1)".  NULL for a virtual register.
const char *x86_reg_name(int reg, X86_Width width);
// Whether `v` fits a sign-extended 32-bit immediate.
bool x86_imm32(int64_t v);

// GNU/LLVM AT&T assembler output.  The header goes once at the top of a module.
void x86_emit_header(FILE *out);
void x86_emit_func(FILE *out, const X86_Func *fn);
// One instruction, as a line of x86_emit_func.
void x86_emit_instr(FILE *out, const X86_Instr *in);

#ifndef __cplusplus
_Noreturn void fatal_error(const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // X86_X86_H
