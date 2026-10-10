#ifndef BESM6_INTERNAL_H
#define BESM6_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "besm.h"
#include "frame.h"
#include "tac.h"

// Shared helpers used across the BESM-6 codegen translation units
// (codegen.c, emit.c, static.c, instr.c).  Helpers used in only one
// file stay `static` in that file and are not declared here.

#include "srcloc.h"

_Noreturn void fatal_error(const char *fmt, ...);

// Dialect-independent value of a scalar constant operand.  Integer constants become a
// 48-bit bit pattern (signed values masked to the low 41 bits with a zero exponent
// field; unsigned masked to all 48 bits); floating-point constants become a double.
// Each per-dialect emitter renders this into its own literal syntax (Madlen =octal/=rX,
// Unix #const pool, Bemsh =Ю'…'/=Е'…').  Defined in besm_const.c.
typedef struct {
    bool is_real;    // true → real_val holds an FP constant; false → word
    double real_val; // valid when is_real
    uint64_t word;   // valid when !is_real: the masked 48-bit integer pattern
} Besm_ConstWord;

Besm_ConstWord besm_const_word(const Tac_Const *c);

// The widest address each instruction format can carry in its own field.  A Format-1 offset
// is 12 bits — every address in the peripherals map fits (033 reaches 04177, 002 reaches
// 0237), so an I/O intrinsic's constant address is always an immediate and the Format-1 S
// bit is never needed.  A Format-2 offset is 15 bits, the whole address space, and so is the
// C register a `utc` sets from it.  Shared by intrinsics.c (lowering) and peephole.c
// (rule #32, which folds a displacement back into the short field).
#define BESM_SHORT_ADDR_MAX 07777
#define BESM_LONG_ADDR_MAX  077777

// True when the constant encodes the all-zero 48-bit word.  Such an operand needs no literal
// at all, so instruction selection attaches none and leaves the instruction with an empty
// address field: EA = 0, and memory word 0 always reads as zero.  Defined in besm_const.c.
bool besm_const_is_zero(const Tac_Const *c);

// How an instruction's operand and modifier-register field are formed.  The regular
// machine-instruction shapes are rendered by a table-driven loop shared in spirit by
// every emitter; BESM_SHAPE_SPECIAL kinds (directives, data, UTM/CALL/BASE) get explicit
// per-dialect handling.  Defined in besm_mnem.c.
typedef enum {
    BESM_SHAPE_MEM,     // operand via the dialect operand formatter; mreg = instr->reg
    BESM_SHAPE_IMM0,    // decimal immediate operand; mreg = 0
    BESM_SHAPE_IMMR,    // decimal immediate, or a symbolic address when instr->name is set
                        // (`vtm g`, a Format-2 15-bit field); mreg = instr->reg
    BESM_SHAPE_SPECIAL, // dialect-specific (directives, data, UTM/CALL/BASE)
} Besm_OperandShape;

Besm_OperandShape besm_operand_shape(Besm_InstrKind kind);

// Latin machine-instruction mnemonics indexed by Besm_InstrKind (shared by the Madlen
// and Unix emitters).  NULL for BESM_SHAPE_SPECIAL kinds.  Defined in besm_mnem.c.
extern const char *const besm_latin_mnem[];

// Append a new instruction to a block, maintaining *tail.  It is marked volatile while
// besm_volatile_access is set: the TAC instruction being selected is a volatile access.
extern bool besm_volatile_access;
Besm_Instr *emit(Besm_Block *block, Besm_Instr **tail, Besm_InstrKind kind);

// Emit XTA: A = mem[reg + off].
void emit_xta(Besm_Block *b, Besm_Instr **t, int reg, int off);

// Emit ATX: mem[reg + off] = A.
void emit_atx(Besm_Block *b, Besm_Instr **t, int reg, int off);

// Frame lookup with fatal_error on miss.
void lookup(const Frame *f, const char *name, int *reg, int *off);

// Emit XTA for a TAC value: variable from frame, UTC+XTA for a global, or =N/=rX for a constant.
void emit_xta_val(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *v);

// Emit XTS for a TAC value (push A to stack, load v into A) — used for args 1..N-1.
void emit_xts_val(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *v);

// Set the C modifier register from a pointer variable (frame slot or global) so a
// following bare XTA/ATX dereferences it.
void emit_wtc_ptr(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name);

// Emit ASX addressing a pointer variable's word (frame slot or global via UTC) — the
// operand's exponent field is the fat-pointer byte offset used as the shift count.
void emit_asx_ptr(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name);

// Store A into a variable, whether a frame slot or a module-level global.
void emit_store_a(Besm_Block *b, Besm_Instr **t, const Frame *f, const char *name);

// Emit an arithmetic instruction for a TAC value: local, global (via UTC), or constant literal.
void emit_arith_val(Besm_Block *b, Besm_Instr **t, Besm_InstrKind kind, const Frame *f,
                    const Tac_Val *v);

// Lower one TAC instruction (defined in instr.c).
void codegen_instr(const Tac_Instruction *instr, const Frame *f, Besm_Block *block,
                   Besm_Instr **tail);

// Lower a call to a <besm6.h> compiler intrinsic into inline machine instructions, or
// return false when `instr` is an ordinary call (defined in intrinsics.c).  Every
// `__besm6_` name is handled here: they all collide under Madlen's 8-character truncation,
// so one left to fall through would silently alias another instead of failing to link.
bool codegen_intrinsic(const Tac_Instruction *instr, const Frame *f, Besm_Block *block,
                       Besm_Instr **tail);

// Emit a module-level static variable (defined in static.c).  `program` is the full
// toplevel chain, used to fold referenced string constants into this module.
void codegen_static_variable(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out,
                             Besm_Dialect dialect);

// Pack a string static-init into a BESM_DATA_LOG chain; the first word is labeled
// `label` when non-NULL (defined in static.c).  `dialect` selects text encoding:
// KOI-7 for Madlen/Bemsh, raw source bytes for Unix (b6as).
Besm_Instr *besm_string_log_items(const Tac_StaticInit *init, const char *label,
                                  Besm_Dialect dialect);

// Fold every string constant `module` references into the module as a local label,
// dropping the constant's external SUBP (defined in static.c).
void besm_fold_string_constants(Besm_Module *module, const Tac_TopLevel *program,
                                Besm_Dialect dialect);

// Emit each block-scope static local of function `fn` as a module-local labeled datum,
// spliced into the function's module just before its `,end,` (defined in static.c).
void besm_emit_static_locals(Besm_Module *module, const Tac_TopLevel *fn, Besm_Dialect dialect);

// Mangle a name into a valid Bemsh label: ≤6 chars, letter-first, letters/digits/`_` only,
// with runtime helpers (`b$…`) mapped to their `libbem.bin` exports (`_…`).  A pure
// deterministic function of the name (defined in emit_bemsh.c); exposed for unit testing.
void bemsh_mangle(char *dst, size_t n, const char *src);

#endif // BESM6_INTERNAL_H
