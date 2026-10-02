#ifndef BESM_H
#define BESM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// A scalar constant operand is carried structurally (see below); the frontend type is
// forward-declared here so the backend IR need not include tac.h.
struct Tac_Const;

// Forward declarations for self-referential linked-list nodes
typedef struct Besm_Instr Besm_Instr;
typedef struct Besm_Block Besm_Block;
typedef struct Besm_Func Besm_Func;
typedef struct Besm_DataItem Besm_DataItem;
typedef struct Besm_DataSection Besm_DataSection;

// Defined in frame.h; the peephole pass uses it to classify temp slots.
typedef struct Frame Frame;

typedef enum {
    // Load/store and index-register transfer
    BESM_MEM_XTA, // XTA(mem_addr addr) -- load A from memory
    BESM_MEM_ATX, // ATX(mem_addr addr) -- store A to memory
    BESM_MEM_STX, // STX(mem_addr addr) -- store A; pop stack
    BESM_MEM_XTS, // XTS(mem_addr addr) -- push A; load from memory
    BESM_MEM_ITA, // ITA(int ireg) -- A = M[ireg]
    BESM_MEM_ATI, // ATI(int ireg) -- M[ireg] = A[15:1]
    BESM_MEM_ITS, // ITS(int ireg) -- push A; A = M[ireg]
    BESM_MEM_STI, // STI(int ireg) -- M[ireg] = A[15:1]; pop
    BESM_MEM_MTJ, // MTJ(mreg src, int dst_j) -- M[dst_j] = M[src]

    // Floating-point arithmetic
    BESM_ARITH_ADD,    // ADD(mem_addr addr)    -- A + X
    BESM_ARITH_SUB,    // SUB(mem_addr addr)    -- A - X
    BESM_ARITH_RSUB,   // RSUB(mem_addr addr)   -- X - A
    BESM_ARITH_ABSSUB, // ABSSUB(mem_addr addr) -- |A| - |X|
    BESM_ARITH_MUL,    // MUL(mem_addr addr)    -- A * X
    BESM_ARITH_DIV,    // DIV(mem_addr addr)    -- A / X
    BESM_ARITH_CNEG,   // CNEG(mem_addr addr)   -- cond. negate A

    // Logical and bit-manipulation
    BESM_LOG_AAX, // AAX(mem_addr addr) -- A & X
    BESM_LOG_AOX, // AOX(mem_addr addr) -- A | X
    BESM_LOG_AEX, // AEX(mem_addr addr) -- Y=A; A ^= X
    BESM_LOG_ARX, // ARX(mem_addr addr) -- A + X end-around
    BESM_LOG_APX, // APX(mem_addr addr) -- pack bits
    BESM_LOG_AUX, // AUX(mem_addr addr) -- unpack bits
    BESM_LOG_ACX, // ACX(mem_addr addr) -- popcount(A) + X
    BESM_LOG_ANX, // ANX(mem_addr addr) -- highest bit pos + X

    // Exponent, shift, and mode-register
    BESM_EXP_EADDX,   // EADDX(mem_addr addr)   -- exponent += X_exp - 64
    BESM_EXP_ESUBX,   // ESUBX(mem_addr addr)   -- exponent -= X_exp - 64
    BESM_EXP_SHIFTX,  // SHIFTX(mem_addr addr)  -- shift by X_exp - 64
    BESM_EXP_SETRMEM, // SETRMEM(mem_addr addr) -- R = X[47:42]
    BESM_EXP_GETR,    // GETR(int mask)         -- A = (R & mask) << 41
    BESM_EXP_YTA,     // YTA(int yoffset)       -- get younger bits
    BESM_EXP_EADDN,   // EADDN(int n_imm)       -- exponent += n_imm - 64
    BESM_EXP_ESUBN,   // ESUBN(int n_imm)       -- exponent -= n_imm - 64
    BESM_EXP_SHIFTN,  // SHIFTN(int n_imm)      -- shift by n_imm - 64
    BESM_EXP_SETR,    // SETR(int value)        -- R = value (0..63)

    // Index-register manipulation
    BESM_REG_VTM,   // VTM(mreg dst, int value)   -- M[dst] = value
    BESM_REG_UTM,   // UTM(mreg dst, int value)   -- M[dst] += value
    BESM_REG_JADDM, // JADDM(mreg src, int dst_j) -- M[dst_j] += M[src]

    // Address modification (C register)
    BESM_MOD_UTC, // UTC(mem_addr addr) -- C = EA (set C immediately)
    BESM_MOD_WTC, // WTC(mem_addr addr) -- C = mem[EA][15:1]

    // Control flow
    BESM_BRANCH_UZA,  // UZA(mem_addr addr)         -- branch if ω = 0
    BESM_BRANCH_U1A,  // U1A(mem_addr addr)         -- branch if ω ≠ 0
    BESM_BRANCH_UJ,   // UJ(mem_addr addr)          -- unconditional jump
    BESM_BRANCH_VJM,  // VJM(mreg link, target tgt) -- call subroutine
    BESM_BRANCH_VZM,  // VZM(mreg test, target tgt) -- branch if M[test] = 0
    BESM_BRANCH_V1M,  // V1M(mreg test, target tgt) -- branch if M[test] ≠ 0
    BESM_BRANCH_VLM,  // VLM(mreg cnt, target tgt)  -- loop (inc. + branch)
    BESM_BRANCH_CALL, // ,call, name -- declare + call external
    BESM_BRANCH_STOP, // STOP                       -- halt processor

    // Privileged I/O (supervisor).  The machine's only I/O: the effective address names a
    // device or a CPU register, the accumulator carries the data in both directions, and
    // one bit of the address selects the direction.  Distinct from the BESM_MOD_* group
    // above, which is the C-register address modification (UTC/WTC).
    BESM_IO_EXT, // EXT(mem_addr addr) -- 033: peripherals
    BESM_IO_MOD, // MOD(mem_addr addr) -- 002: CPU-internal registers

    // Extracode (user mode).  The trap into the operating system: M[016] = EA, the
    // accumulator carries the data in both directions, and the opcode (050-077) selects
    // the service.  Every dialect spells it differently — Madlen `,*71,`, Bemsh `э71`,
    // b6as `$77` — so it is BESM_SHAPE_SPECIAL and its opcode rides in `opcode`.
    BESM_IO_EXTRACODE, // EXTRACODE(int opcode, mem_addr addr) -- 050..077

    // Assembly directives
    BESM_STMT_LABEL, // name: ,bss,        — label definition point
    BESM_STMT_NAME,  // name: ,name,       — subprogram name
    BESM_STMT_BASE,  //   reg ,base, name  — relocatable basing
    BESM_STMT_SUBP,  // name: ,subp,       — declare external subprogram
    BESM_STMT_ENTRY, // name: ,entry,      — secondary entry point
    BESM_STMT_END,   //       ,end,        — end of subprogram

    // Data section directives
    BESM_DATA_INT,    // addr      → ,int, value
    BESM_DATA_REAL,   // real_val  → ,real, value
    BESM_DATA_LOG,    // log_val   → ,log, value (logical constant)
    BESM_DATA_BSS,    // addr      → ,bss, count  (addr=0: bare ,bss,)
    BESM_DATA_EQU,    // addr      → ,equ, value
    BESM_DATA_REF,    // name      → ,oct, name
    BESM_DATA_STRING, // name      → ,int, c0 / ... / ,int, 0
    BESM_DATA_Z00,    // reg=disp, name=sym → [reg] ,z00, [name]
} Besm_InstrKind;

struct Besm_Instr {
    struct Besm_Instr *next;
    Besm_InstrKind kind;
    unsigned reg;            // index-register 0..15, or displacement (BESM_DATA_Z00)
    int addr;                // offset, or integer data value (BESM_DATA_INT/BSS/EQU)
    char *name;              // symbolic name, optional (heap-owned)
    char *label;             // Madlen label for a data word whose `name` is already an
                             // operand (currently only BESM_DATA_Z00), optional (heap-owned)
    struct Tac_Const *konst; // scalar nonzero constant operand (=N / #N / =Ю'…'), optional
                             // (heap-owned); when set, the operand is this literal, not
                             // (name, addr).  The per-dialect emitter formats it.  A zero
                             // constant is never attached: the instruction is left with no
                             // operand and reads memory word 0 (see emit.c attach_const).
    uint64_t log_val;        // BESM_DATA_LOG: 48-bit logical constant
    double real_val;         // BESM_DATA_REAL
    int opcode;              // BESM_IO_EXTRACODE: the extracode opcode, 050..077 (the
                             // mnemonic is a function of it, and differs per dialect)
    // TODO: star plus offset
    // TODO: literal address with value (int, uns, real)
};

//
// block = Block(string? name, instr* body)
//
struct Besm_Block {
    struct Besm_Block *next;
    char *name; // nullable (heap-owned)
    Besm_Instr *body;
};

//
// call_conv
//
typedef enum {
    BESM_CC_BESM6_C,  // BESM6_C: standard Dubna C ABI
    BESM_CC_INTERNAL, // INTERNAL: compiler-internal, no ABI discipline
} Besm_CallConv;

//
// func = Func(string name, call_conv cc, block* blocks)
//
struct Besm_Func {
    struct Besm_Func *next;
    char *name; // heap-owned
    bool global; // external linkage; false for a `static` function
    Besm_CallConv cc;
    Besm_Block *blocks;
};

//
// section_kind
//
typedef enum {
    BESM_SK_CODE, // SK_Code: executable subprogram text
    BESM_SK_DATA, // SK_Data: initialised data
    BESM_SK_BSS,  // SK_Bss: zero-initialised storage
} Besm_SectionKind;

//
// data_section = DataSection(section_kind kind, string? name, instr* items)
//
struct Besm_DataSection {
    struct Besm_DataSection *next;
    Besm_SectionKind kind;
    char *name;     // nullable (heap-owned)
    bool global;    // external linkage
    bool tentative; // a C tentative definition (init==NULL) — a global BSS section is a
                    // `.comm` common; a zero-*initialized* def (init all-zero) is a strong
                    // .bss symbol instead
    Besm_Instr *items;
};

//
// module = Module(string name, func* funcs, data_section* sections)
//
typedef struct {
    char *name;    // heap-owned
    char *comment; // heap-owned, nullable — emitted on the c-line separator
    Besm_Func *funcs;
    Besm_DataSection *sections;
} Besm_Module;

//
// Target assembler dialect.  The backend builds one dialect-agnostic Besm_Module
// and a per-dialect emitter renders it: Madlen (,mnem, comma-framed, runs on dubna),
// the Unix b6as assembler (AT&T-style, .text/.data segments), or Bemsh (Cyrillic
// autocode, runs on dubna).  Selected on the genbesm command line.
//
typedef enum {
    BESM_MADLEN, // Madlen (Dubna monitor) — the historical default
    BESM_UNIX,   // Unix b6as (AT&T syntax, b.out objects)
    BESM_BEMSH,  // Bemsh autocode (Cyrillic mnemonics, Dubna monitor)
} Besm_Dialect;

//
// Allocate
//
Besm_Instr *besm_new_instr(Besm_InstrKind kind);
Besm_Block *besm_new_block(void);
Besm_Func *besm_new_func(const char *name, Besm_CallConv cc);
Besm_DataSection *besm_new_data_section(Besm_SectionKind kind);
Besm_Module *besm_new_module(const char *name);

//
// Deallocate
//
void besm_free_instr(Besm_Instr *instr);
void besm_free_block(Besm_Block *block);
void besm_free_func(Besm_Func *func);
void besm_free_data_section(Besm_DataSection *section);
void besm_free_module(Besm_Module *module);

//
// Generate a unique internal label: writes "prefix.N" into buf (N auto-increments).
//
void mad_fresh_label(char *buf, size_t n, const char *prefix);

// Format a double as a Madlen REAL constant's decimal-number field (mandatory '.').
void mad_format_real(char *buf, size_t n, double val);

// Peephole-optimize a function's instruction stream in place.
//
// Slides a small window over each block's `Besm_Instr` linked list, tracks the
// implicit machine state (accumulator A, mode register R, ω group) across
// straight-line code, resets that state at every basic-block boundary (label or
// branch), matches a table of local rewrite rules, and rewrites to a fixpoint
// (repeats until a full sweep changes nothing).  Removed nodes are spliced out of
// the list and freed; the one rule that inserts a node is the ω fixup ahead of a
// conditional branch (#33).  Observable behavior is unchanged; only the instruction
// sequence is made cheaper.  See backend/besm6/Peephole_Rewrites.md.
//
// `frame` classifies which auto slots are compiler temporaries (for dead temp-store
// elimination, rule #28); it may be NULL (e.g. for an empty function), in which case
// that rule is skipped.
void besm_peephole(Besm_Func *func, const Frame *frame);

//
// Emit assembly for the selected dialect.  Dispatches to the per-dialect module
// emitter (emit_madlen_module / emit_unix_module / emit_bemsh_module).
//
void besm_emit_module(FILE *out, const Besm_Module *module, Besm_Dialect dialect);

//
// Emit Madlen assembly
//
void emit_madlen_instr(FILE *out, const Besm_Instr *instr);
void emit_madlen_block(FILE *out, const Besm_Block *block);
void emit_madlen_func(FILE *out, const Besm_Func *func);
void emit_madlen_data_section(FILE *out, const Besm_DataSection *section);
void emit_madlen_module(FILE *out, const Besm_Module *module);

//
// Emit Unix (b6as) assembly
//
void emit_unix_module(FILE *out, const Besm_Module *module);

//
// Emit Bemsh (Cyrillic autocode) assembly
//
void emit_bemsh_module(FILE *out, const Besm_Module *module);

#ifdef __cplusplus
}
#endif

#endif // BESM_H
