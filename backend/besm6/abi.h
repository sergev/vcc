#ifndef BESM6_ABI_H
#define BESM6_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

// One BESM-6 machine word is 6 eight-bit bytes.  The TAC stream is sized in target
// bytes (CopyToOffset offsets, AddPtr scales, AllocateLocal/structure sizes); the
// backend converts to words by dividing by this, rounding up.
#define BESM6_WORD_BYTES 6

//
// INT-format encoding for integers.
// An integer value V is stored as a 48-bit floating-point word with:
//   bits 48-42 (exponent field) = BESM_INT_EXP = 0150 octal = 104 decimal
//   bit  41    (sign bit)       = 0
//   bits 40-1  (mantissa)       = 40-bit two's-complement representation of V
// Range: BESM_INT_MIN .. BESM_INT_MAX
//
#define BESM_INT_EXP 0150               // octal; exponent field for INT-format words
#define BESM_INT_MAX ((1LL << 40) - 1)  // +2^40 - 1
#define BESM_INT_MIN (-(1LL << 40) + 1) // -(2^40 - 1)

//
// Calling convention register assignments (Dubna C ABI).
// See backend/besm6/Besm6_Calling_Conventions.md for full protocol.
//
// After b/save:
//   r6 (REG_PAR)  points to the parameter block: r6+i = address of param i
//   r7 (REG_AUTO) points to the auto-variable block: r7+j = local variable j
//
// b/save and b/ret are external Madlen routines; the backend declares them
// with ",CALL, b/save" and ",CALL, b/ret" — it does not emit their bodies.
//
#define REG_AUTO 7  // auto-variable pointer (set by b/save)
#define REG_PAR  6  // parameter pointer (set by b/save)
#define REG_CNT  14 // negative argument count (set by caller)
#define REG_RET  13 // return address (set by caller via VJM)
#define REG_SP   15 // stack pointer (grows toward higher addresses)

// Scratch index register, free for a value live across a couple of instructions.  r14 is the
// argument count (REG_CNT), but only across a call: the caller loads it immediately before the
// `,call,`, and b/save consumes it at entry, so between calls nothing is live in it.  The
// backend already treats it as scratch — GET_ADDRESS materializes an address with a lone
// `14 ,vtm, name` — and an extracode sets M[016] = r14 from its effective address anyway.  Used
// by the privileged I/O intrinsics to hold a computed device address (intrinsics.c); needs no
// save/restore.  Keeping it out of r8-r12 also leaves those free for the register allocator
// (task 4) and clear of the runtime helpers, which use r12 as their own scratch and frame base
// (`,ati, 12` in b_tout.madlen, the r12 frame in b_umod.madlen).
#define REG_SCRATCH 14 // same register as REG_CNT — see above

#ifdef __cplusplus
}
#endif

#endif // BESM6_ABI_H
