#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "frame.h"
#include "internal.h"
#include "tac.h"
#include "xalloc.h"

// Lowering of the <besm6.h> compiler intrinsics (backend/besm6/Besm6_Intrinsics.md).
//
// An intrinsic *is* a call in the IR: it is declared as an ordinary prototype, so the
// front end checks its arity and coerces its arguments like any other call, and it reaches
// the back end as a TAC_INSTRUCTION_FUN_CALL whose fun_name begins with `__besm6_`.  Only
// instruction selection knows better: codegen_intrinsic intercepts the call and emits the
// machine instruction inline instead of a `,call,`.
//
// Every `__besm6_` name must be intercepted here.  All of them collide under Madlen's
// 8-character truncation (`__besm6_apx` and `__besm6_arx` both sanitize to `**BESM6*`), so
// one left to fall through would not fail to link — it would silently alias whichever
// intrinsic the assembler saw first.  Hence the fatal_error at the bottom rather than a
// `return false`.

// PSW, the mode word — machine register 021.  The register file the index registers live in
// continues past M[017] into the machine's own control registers, and PSW is the one a kernel
// needs from C: it carries the interrupt priority (БлПр 02000) and the mapping and protection
// overrides (БлП 01, БлЗ 02).  `ita`/`ati` name it through their address field just as they
// name an index register.
#define BESM_PSW_MREG 021

// The Tier-2 bit-manipulation intrinsics: gather, scatter, population count, highest set
// bit, and the machine's own end-around-carry add.  Each is a single A-op-X instruction —
// the operand comes from memory, the accumulator is both the other input and the result —
// so each lowers to exactly the inline shape of a C binary operator.
// `arx` is the one of the five that leaves *multiplicative* ω rather than logical; it needs
// no correction here, because the peephole's rule #33 inserts one exactly where ω matters —
// ahead of a conditional branch that would otherwise inherit the wrong group.
static const struct {
    const char *name;
    Besm_InstrKind kind;
} bit_intrinsics[] = {
    { "__besm6_apx", BESM_LOG_APX }, // 020 сбр — gather the bits selected by a mask
    { "__besm6_aux", BESM_LOG_AUX }, // 021 рзб — scatter into a mask's positions
    { "__besm6_acx", BESM_LOG_ACX }, // 022 чед — popcount(a) ⊞ x
    { "__besm6_anx", BESM_LOG_ANX }, // 023 нед — highest-set-bit position ⊞ x
    { "__besm6_arx", BESM_LOG_ARX }, // 013 слц — a ⊞ x (end-around carry)
};

// The Tier-1 privileged intrinsics: the machine's only I/O.  The BESM-6 has no I/O address
// space and no memory-mapped device registers — every peripheral is reached by `ext` and
// every CPU-internal register (page registers, ГРП and its mask, the mode bits) by `mod`.
// Both name their register through the *effective address* and pass the data through the
// accumulator in both directions; one bit of the address selects the direction.
static const struct {
    const char *name;
    Besm_InstrKind kind;
} io_intrinsics[] = {
    { "__besm6_ext", BESM_IO_EXT }, // 033 увв — the peripherals
    { "__besm6_mod", BESM_IO_MOD }, // 002 рег — the CPU-internal registers
};

//
// How the effective address of an `ext` / `mod` / extracode is delivered.  All three name
// their device register or trap argument through the EA, and `EA = addr + M[reg] + C`, so
// there are exactly three ways to get a value there.
//
typedef enum {
    IO_ADDR_IMM,  // small constant: the instruction's own 12-bit offset field
    IO_ADDR_UTC,  // larger constant: `utc N` (Format 2, 15 bits) puts it in C
    IO_ADDR_STACK // anything computed: push A, pop it back into C with a stack-mode `wtc`
} IoAddrMode;

//
// Classify an address operand and, for IO_ADDR_IMM, hand back the field value.
//
// A constant is an immediate when it fits the short field, and rides a `utc` when it fits
// the long one.  Everything else — a variable, a global, a computed temporary, an
// out-of-range or floating constant — goes through the accumulator and the stack.
//
static IoAddrMode io_addr_classify(const Tac_Val *addr, int *imm)
{
    if (addr->kind != TAC_VAL_CONSTANT)
        return IO_ADDR_STACK;

    Besm_ConstWord w = besm_const_word(addr->u.constant);
    if (w.is_real)
        return IO_ADDR_STACK;
    if (w.word <= BESM_SHORT_ADDR_MAX) {
        *imm = (int)w.word;
        return IO_ADDR_IMM;
    }
    if (w.word <= BESM_LONG_ADDR_MAX) {
        *imm = (int)w.word;
        return IO_ADDR_UTC;
    }
    return IO_ADDR_STACK;
}

//
// Emit the address setup and the accumulator load shared by `ext`, `mod` and the extracode,
// then the instruction itself, and return it so the caller can stamp in an opcode.
//
// The C register is added to the next instruction's EA and to that one only, so a C setter
// has to be the instruction immediately before the I/O op.  That fixes the order: whatever
// is needed for the address comes first, the accumulator second, the C setter last.
//
//   IO_ADDR_IMM     `xta acc`                       `,ext, N`     EA = N
//   IO_ADDR_UTC     `xta acc` `utc N`               `,ext,`       EA = C = N
//   IO_ADDR_STACK   `xta addr` `xts acc` `15 wtc`   `,ext,`       EA = C = the pushed word
//
// The stack round-trip is what keeps a computed address off an index register: XTS (003)
// writes A to mem[M[15]] and bumps the pointer, then loads the accumulator operand; the
// `wtc` that follows is in stack mode (V = 0 and M = 017), so it decrements the pointer and
// loads C from the word just pushed.  Push and pop are adjacent and balanced, no index
// register is disturbed, and both instructions leave ω logical — the mode compiled code
// already runs in.  It costs exactly what materializing the address into a register costs,
// and unlike a register it is a shape the peephole can fold: rules #32(a) and #32(b) turn
// `xta x` + `a+x =N` + this trailer into a lone `wtc x` plus the displacement N in the
// instruction's own address field.  See backend/besm6/Peephole_Rewrites.md §5.10.
//
static Besm_Instr *emit_io_op(Besm_Block *block, Besm_Instr **tail, const Frame *f,
                              Besm_InstrKind kind, const Tac_Val *addr, const Tac_Val *acc)
{
    int imm         = 0;
    IoAddrMode mode = io_addr_classify(addr, &imm);

    if (mode == IO_ADDR_STACK) {
        emit_xta_val(block, tail, f, addr); // A = the effective address
        emit_xts_val(block, tail, f, acc);  // push it; A = the accumulator operand
        Besm_Instr *wtc = emit(block, tail, BESM_MOD_WTC);
        wtc->reg        = REG_SP; // stack mode: pop the address back into C
    } else {
        emit_xta_val(block, tail, f, acc);
        if (mode == IO_ADDR_UTC) {
            Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
            utc->addr       = imm; // C = N; UTC touches neither A nor a register
        }
    }

    Besm_Instr *io = emit(block, tail, kind);
    if (mode == IO_ADDR_IMM)
        io->addr = imm;
    return io;
}

// The value of an integer constant `c` as an unsigned count, or -1 when it is not one.
static long long const_count(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_INT:
        return c->u.int_val;
    case TAC_CONST_UINT:
        return c->u.uint_val;
    case TAC_CONST_LONG:
        return c->u.long_val;
    case TAC_CONST_ULONG:
        return (long long)c->u.ulong_val;
    case TAC_CONST_LONG_LONG:
        return c->u.long_long_val;
    case TAC_CONST_ULONG_LONG:
        return (long long)c->u.ulong_long_val;
    default:
        return -1;
    }
}

// alloca(n), a call of __builtin_alloca: the memory is the top of the stack, which grows
// upward.  dst = r15 as a void * (a fat pointer to byte #0 of its word, as an array
// decays to), then r15 += the words of n bytes.  b/ret sets r15 back from r7, so the
// function's return gives the memory back.  A constant n takes exactly ceil(n/6) words;
// a computed one n/6 + 1, through b/udiv, the count then pushed and popped into C so that
// `15 ,utm,` adds it.
static void gen_alloca(const Tac_Instruction *instr, const Frame *f, Besm_Block *block,
                       Besm_Instr **tail)
{
    const Tac_Val *n = instr->u.fun_call.args;
    if (!n || n->next)
        fatal_error("__builtin_alloca takes exactly one argument");
    const Tac_Val *dst = instr->u.fun_call.dst;
    if (dst && dst->kind == TAC_VAL_VAR) {
        Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
        ita->addr       = REG_SP;
        Besm_Instr *aox = emit(block, tail, BESM_LOG_AOX);
        aox->name       = xstrdup("=:64"); // bit 48 (marker) + offset_enc 5 (MSB)
        emit_store_a(block, tail, f, dst->u.var_name);
    }
    if (n->kind == TAC_VAL_CONSTANT) {
        long long bytes = const_count(n->u.constant);
        if (bytes < 0)
            fatal_error("__builtin_alloca: a size that is not an integer");
        long long words = (bytes + BESM6_WORD_BYTES - 1) / BESM6_WORD_BYTES;
        if (words > 077777) // the 15-bit address field of utm, the whole memory
            fatal_error("__builtin_alloca: %lld bytes is more than the stack", bytes);
        if (words > 0) {
            Besm_Instr *utm = emit(block, tail, BESM_REG_UTM);
            utm->reg        = REG_SP;
            utm->addr       = (int)words;
        }
        return;
    }
    Tac_Const six = { .kind = TAC_CONST_ULONG, .u.ulong_val = BESM6_WORD_BYTES };
    emit_xta_val(block, tail, f, n);
    emit_xts_val(block, tail, f, &(Tac_Val){ .kind = TAC_VAL_CONSTANT, .u.constant = &six });
    Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
    call->name       = xstrdup("b$udiv");
    emit(block, tail, BESM_MEM_XTS);          // push the count; A = 0
    Besm_Instr *wtc = emit(block, tail, BESM_MOD_WTC);
    wtc->reg        = REG_SP;                 // stack mode: pop it into C
    Besm_Instr *utm = emit(block, tail, BESM_REG_UTM);
    utm->reg        = REG_SP;
    utm->addr       = 1;                      // r15 += C + 1
}

//
// Lower one intrinsic call, or return false if `instr` is an ordinary call.
//
// Called from the top of instr.c's FUN_CALL case, before any argument setup — and so also
// above the _Noreturn branch, which a `_Noreturn` intrinsic would otherwise take.
//
bool codegen_intrinsic(const Tac_Instruction *instr, const Frame *f, Besm_Block *block,
                       Besm_Instr **tail)
{
    const char *name = instr->u.fun_call.fun_name;
    if (strcmp(name, "__builtin_alloca") == 0) {
        gen_alloca(instr, f, block, tail);
        return true;
    }
    if (strncmp(name, "__besm6_", 8) != 0)
        return false;

    for (size_t i = 0; i < sizeof(bit_intrinsics) / sizeof(bit_intrinsics[0]); i++) {
        if (strcmp(name, bit_intrinsics[i].name) != 0)
            continue;

        const Tac_Val *a = instr->u.fun_call.args;
        const Tac_Val *x = a ? a->next : NULL;
        if (!x || x->next)
            fatal_error("intrinsic %s takes exactly two arguments", name);

        // The inline binop shape: A = a; A op= x; dst = A.  A zero constant operand needs
        // no literal at all — the instruction is left with an empty address field and reads
        // memory word 0, which always reads as zero (a plain popcount, acx(a, 0), is exactly
        // that).
        emit_xta_val(block, tail, f, a);
        emit_arith_val(block, tail, bit_intrinsics[i].kind, f, x);

        const Tac_Val *dst = instr->u.fun_call.dst;
        if (dst && dst->kind == TAC_VAL_VAR)
            emit_store_a(block, tail, f, dst->u.var_name);
        return true;
    }

    for (size_t i = 0; i < sizeof(io_intrinsics) / sizeof(io_intrinsics[0]); i++) {
        if (strcmp(name, io_intrinsics[i].name) != 0)
            continue;

        const Tac_Val *addr = instr->u.fun_call.args;
        const Tac_Val *acc  = addr ? addr->next : NULL;
        if (!acc || acc->next)
            fatal_error("intrinsic %s takes exactly two arguments: an address and a word",
                        name);

        // A constant address becomes the instruction's own 12-bit offset field
        // (`,ext, 2073`); a computed one arrives in C.  The hardware genuinely needs the
        // computed case — `002 0100`-`0137` (the РУУ mode bits) encodes its data *in* the
        // address, and tape-transport control selects the unit as addr - 0100.
        emit_io_op(block, tail, f, io_intrinsics[i].kind, addr, acc);

        // The result is the accumulator the instruction leaves behind.  Discarding it (a
        // write address returns the unchanged word) drops only the store: the instruction
        // itself is never eliminable — it is the machine's only I/O.  No ω correction is
        // needed: a read address leaves the AU in logical mode, which is the mode compiled
        // code already runs in.
        const Tac_Val *dst = instr->u.fun_call.dst;
        if (dst && dst->kind == TAC_VAL_VAR)
            emit_store_a(block, tail, f, dst->u.var_name);
        return true;
    }

    // __besm6_getpsw — read the mode word back.  `ita` loads M[021] into A zero-extended, so
    // the whole 15-bit word arrives; it leaves ω logical, the mode compiled code runs in, and
    // so needs no correction.  PSW is the only machine register that can be read back at all —
    // РП and РЗ are write-only — which is what makes this intrinsic worth having.
    if (strcmp(name, "__besm6_getpsw") == 0) {
        if (instr->u.fun_call.args)
            fatal_error("intrinsic %s takes no arguments", name);

        Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
        ita->addr       = BESM_PSW_MREG; // A = M[021]

        const Tac_Val *dst = instr->u.fun_call.dst;
        if (dst && dst->kind == TAC_VAL_VAR)
            emit_store_a(block, tail, f, dst->u.var_name);
        return true;
    }

    // __besm6_setpsw — the general mode-word write: `ati` takes A[15:1], so the argument goes
    // through the accumulator like any other value.  This is the read-modify-write path, for
    // the bits __besm6_maskpsw cannot reach; ω is kept, so again nothing to correct.
    if (strcmp(name, "__besm6_setpsw") == 0) {
        const Tac_Val *psw = instr->u.fun_call.args;
        if (!psw || psw->next)
            fatal_error("intrinsic %s takes exactly one argument: the mode word", name);

        emit_xta_val(block, tail, f, psw); // A = psw
        Besm_Instr *ati = emit(block, tail, BESM_MEM_ATI);
        ati->addr       = BESM_PSW_MREG; // M[021] = A[15:1]
        return true;                     // void: there is no result to store
    }

    // __besm6_maskpsw — the one-instruction mode write.  `024 vtm` with REGISTER FIELD 0 is not
    // an index-register load: M[0] always reads 0, so in supervisor mode the hardware spends the
    // instruction on PSW instead and writes БлП (01), БлЗ (02) and БлПр (02000) straight out of
    // the address field, all three at once.  It is a *masked* write — ПоП, ПоК and the
    // write-watch bit are not in the mask and keep their values — and it disturbs neither the
    // accumulator nor ω, unlike the `ita`/`ati` read-modify-write above.  In user mode it has no
    // effect at all.
    //
    // The mask therefore rides in the instruction's own 15-bit address field, exactly like the
    // halt code below, and must likewise be a compile-time constant.  The front end
    // (semantic/expressions.c, fold_immediate_arg0) evaluates and range-checks it and folds the
    // argument into a literal, so it arrives here as a TAC constant however deeply nested the
    // source expression was and whatever the optimizer flags say.  The test below is the
    // backstop, not the mechanism: relying on TAC-level folding instead used to reject a mask
    // written as a three-term OR of named bits.
    if (strcmp(name, "__besm6_maskpsw") == 0) {
        const Tac_Val *mask = instr->u.fun_call.args;
        if (mask && !mask->next && mask->kind == TAC_VAL_CONSTANT) {
            Besm_ConstWord w = besm_const_word(mask->u.constant);
            if (w.is_real || w.word > 077777)
                fatal_error("intrinsic %s: mask %llo does not fit the 15-bit address field",
                            name, (unsigned long long)w.word);

            Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
            vtm->reg        = 0; // the register field being 0 *is* the mode write
            vtm->addr       = (int)w.word;
            return true;
        }
        fatal_error("intrinsic %s takes one argument: a constant mask in 0..077777", name);
    }

    // __besm6_stop — the halt (033, Format 2).  It is *resumable*: the machine stops, the
    // operator reads the halt reason off the console and presses continue, and execution
    // carries on at the next instruction.  So it is an ordinary call that returns — no `,uj,`,
    // and the code after it is reachable (peephole rule #31(b) must not treat it as the start
    // of an unreachable run).
    //
    // The halt code rides in the instruction's own 15-bit address field (BESM_SHAPE_IMM0), so
    // it has to be a compile-time constant — folded and range-checked in the front end, like
    // the mask above.  Nothing is lost: it is a diagnostic literal, and
    // neither dubna nor b6sim even reads it — both simply end the run.  (IMM0 zeroes the
    // register field; should a register-modified code ever be wanted, IMMR renders identically
    // when reg == 0.)
    if (strcmp(name, "__besm6_stop") == 0) {
        const Tac_Val *code = instr->u.fun_call.args;
        if (code && !code->next && code->kind == TAC_VAL_CONSTANT) {
            Besm_ConstWord w = besm_const_word(code->u.constant);
            if (w.is_real || w.word > 077777)
                fatal_error("intrinsic %s: halt code %llo does not fit the 15-bit address field",
                            name, (unsigned long long)w.word);

            Besm_Instr *stop = emit(block, tail, BESM_BRANCH_STOP);
            stop->addr       = (int)w.word;
            return true;
        }
        fatal_error("intrinsic %s takes one argument: a constant halt code in 0..077777", name);
    }

    // __besm6_extracode(op, ea, acc) — the user-mode trap into the operating system:
    // M[016] := EA, A := acc, invoke extracode `op`, result := A.  The v7 syscall trap
    // `$77 N` rides on exactly this mechanism (N is the effective address).
    //
    // `op` *is* the opcode, so it must be a compile-time constant — the front end
    // (semantic/expressions.c) evaluates and range-checks it and folds the argument into a
    // literal, so it always arrives here as a TAC constant, whatever the optimizer flags.
    //
    // The effective address lowers exactly like ext/mod's device address (emit_io_op): a
    // constant that fits the Format-1 offset field becomes the instruction's own address,
    // anything else arrives in the C register.
    if (strcmp(name, "__besm6_extracode") == 0) {
        const Tac_Val *op  = instr->u.fun_call.args;
        const Tac_Val *ea  = op ? op->next : NULL;
        const Tac_Val *acc = ea ? ea->next : NULL;
        if (!acc || acc->next)
            fatal_error("intrinsic %s takes exactly three arguments: an opcode, an effective "
                        "address and a word",
                        name);
        if (op->kind != TAC_VAL_CONSTANT)
            fatal_error("intrinsic %s: the opcode must be a compile-time constant", name);

        Besm_ConstWord w = besm_const_word(op->u.constant);
        if (w.is_real || w.word < 050 || w.word > 077)
            fatal_error("intrinsic %s: opcode %llo is not an extracode (050..077)", name,
                        (unsigned long long)w.word);
        int opcode = (int)w.word;

        Besm_Instr *xc = emit_io_op(block, tail, f, BESM_IO_EXTRACODE, ea, acc);
        xc->opcode     = opcode;

        // The result is the accumulator the extracode leaves behind.  Discarding it drops
        // only the store: the trap itself is never eliminable.  Note the ABI consequence —
        // an extracode sets M[016] (r14) from the effective address, so r14 is clobbered
        // here.  That is harmless: nothing can be live in r14 across this point, since it
        // holds the argument count, which the caller loads immediately before a `,call,`.
        const Tac_Val *dst = instr->u.fun_call.dst;
        if (dst && dst->kind == TAC_VAL_VAR)
            emit_store_a(block, tail, f, dst->u.var_name);
        return true;
    }

    fatal_error("%s is not a <besm6.h> intrinsic", name);
}
