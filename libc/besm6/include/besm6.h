/*
 * <besm6.h> — compiler intrinsics for the BESM-6 machine instructions.
 *
 * The BESM-6 has no I/O address space, no memory-mapped device registers and no
 * channel programs: every peripheral is reached by one of two supervisor
 * instructions, and the machine's bit-manipulation instructions have no C
 * equivalent at all.  The intrinsics below expose them directly, so that a
 * kernel or a driver can be written in C instead of assembly.  They are
 * specified in backend/besm6/Besm6_Intrinsics.md.
 *
 * Status: all twelve are lowered — Tier 1 (ext, mod, the halt, and the three
 * mode-word intrinsics), all five Tier-2 bit manipulations, and Tier 3 (the
 * extracode).  Each becomes a single inline machine instruction, never a call.
 *
 * This header declares those twelve and nothing else.  Readable wrappers for the
 * registers a given program cares about — a popcount, an spl(), the ГРП bit
 * names — are the caller's own business; they are one #define each, and what
 * they should be named depends on the program.
 *
 * Every intrinsic that carries a machine word takes and returns `unsigned`,
 * never `int`.  A BESM-6 word is 48 bits, but a signed int on this target holds
 * only 41 of them; a device control word or a ГРП value (whose bit 48 is live)
 * would not survive the trip.  See backend/besm6/Besm6_Data_Representation.md.
 *
 * The three PSW intrinsics are the deliberate exception: they are typed `int`.
 * What they carry is not a machine word but a 15-bit address-field value — PSW
 * is read and written through the 15-bit paths `ita`/`ati`/`vtm`, and every bit
 * of it fits a signed int with room to spare.
 *
 * Absolute machine addresses need no intrinsic: the BESM-6 is word-addressed,
 * so a C pointer is a word index and a `volatile unsigned *` reaches low memory
 * directly.  There is also no atomic instruction in the ISA — mutual exclusion
 * on this machine is interrupt masking.
 *
 * Addresses and opcodes here are OCTAL, as in every BESM-6 document, and bits
 * are numbered right-to-left from 1 (bit 1 = LSB, bit 48 = MSB).
 */
#ifndef _BESM6_H
#define _BESM6_H

/* ---- Tier 1: privileged — reaching the hardware ---- */
/*
 * These two mirror the hardware exactly: the accumulator is both the input and
 * the output, and the direction of the transfer lives in the address, not in
 * the instruction (one bit of the address means "read" — 04000 for ext, 0200
 * for mod).  ADDR is the verbatim address from the peripherals map, read bit
 * included; on a read address the incoming accumulator is ignored, so pass 0.
 *
 * Both are side-effecting and never eliminable, even when the result is unused:
 * they are the machine's only I/O.
 */

/*
 * 033 ext (увв) — the peripherals: drums, disks, tape, printer, punches, card
 * equipment, terminals.  A := acc; ext addr; result := A.
 */
unsigned __besm6_ext(unsigned addr, unsigned acc);

/*
 * 002 mod (рег) — the CPU-internal registers: the cache БРЗ, the page registers
 * РП, the protection register РЗ, the interrupt register ГРП and its mask МГРП,
 * the mode bits РУУ.  A := acc; mod addr; result := A.
 *
 * Two surprises worth restating, both about ГРП, the interrupt register.  002
 * 037 clears it by writing a mask in which a ZERO bit clears (GRP &= ACC |
 * GRP_WIRED_BITS), so to dismiss one interrupt you write the COMPLEMENT of its
 * bit.  And the wired bits — the "device free" and "exchange done" bits of the
 * mass-storage channels — are live wires, not flip-flops; they cannot be
 * cleared that way at all, and go down only when the device is itself given a
 * new command.
 */
unsigned __besm6_mod(unsigned addr, unsigned acc);

/*
 * 033 format 2, stop (стоп) — halt the processor.  Legal in user mode, and NOT
 * _Noreturn: the halt is resumable.  A human operator presses the console's
 * continue button and execution carries on at the next instruction, so this is
 * an ordinary void call and the code after it is reachable.
 *
 * CODE is a halt reason, encoded in the instruction's own 15-bit address field
 * — it must therefore be a compile-time constant in 0..077777.  It identifies
 * the halt site on the operator's console and in a trace or dump; note that our
 * two simulators (dubna, b6sim) ignore it and simply end the simulation, so
 * nothing after a stop runs under them.
 */
void __besm6_stop(unsigned code);

/*
 * The mode word PSW — machine register 021.  The register file that holds the
 * index registers continues past M[017] into the machine's own control
 * registers, and PSW is the one a kernel needs from C: bit БлПр (02000) is the
 * global interrupt-enable flag, and hence the kernel's interrupt priority
 * level, while БлП (01) and БлЗ (02) override the address mapping and the
 * protection register.
 *
 * Unlike РП and РЗ, which are write-only, PSW CAN be read back — which is why
 * a getter exists at all.
 */

/*
 * 042 ita (счи) — A := M[021], zero-extended.  The only way to see the
 * interrupt level from C.
 */
int __besm6_getpsw(void);

/*
 * 040 ati (уи) — M[021] := A[15:1].  The general mode-word write, for the bits
 * __besm6_maskpsw cannot reach; use it with __besm6_getpsw for a
 * read-modify-write.
 */
void __besm6_setpsw(int psw);

/*
 * 024 vtm (уиа) with REGISTER FIELD 0 — the one-instruction mode write.  M[0]
 * always reads 0, so the register half of the instruction is a no-op and in
 * supervisor mode the hardware spends it on PSW instead: БлП (01), БлЗ (02) and
 * БлПр (02000) are taken straight from MASK and written ALL THREE AT ONCE.  It
 * is a *masked* write — ПоП, ПоК and the write-watch bit are not in the mask and
 * do not move — and it disturbs neither the accumulator nor ω.  In user mode it
 * has no effect at all.
 *
 * This is the whole of cli/sti: __besm6_maskpsw(02003) blocks interrupts,
 * __besm6_maskpsw(3) delivers them, both re-asserting the unmapped kernel
 * invariant БлП = БлЗ = 1 on the way past.
 *
 * MASK is an immediate field of the instruction word, so it must be a
 * compile-time constant in 0..077777.
 */
void __besm6_maskpsw(int mask);

/* ---- Tier 2: bit manipulation ---- */
/*
 * Three of these add their result to X with END-AROUND CARRY: a 48-bit unsigned
 * add in which a carry out of bit 48 comes back into bit 1.  That is the
 * machine's own integer add, and it is not C's `+` on unsigned, which wraps
 * mod 2^48.  Pass x = 0 to get the plain value.
 */

/*
 * 020 apx (сбр) — gather.  Collect the bits of A selected by MASK, in source
 * order, ALIGNED TO THE MSB: popcount(mask) bits occupy result bits 48 down.
 * This is the opposite alignment from x86's PEXT; to right-align, follow with
 * >> (48 - popcount(mask)).
 */
unsigned __besm6_apx(unsigned a, unsigned mask);

/*
 * 021 aux (рзб) — scatter, the exact inverse of apx.  Each 1-bit of MASK,
 * scanned from bit 48 down, consumes one bit of A taken from A's MSB downward
 * and deposits it at that position; 0-bits of MASK yield 0.
 */
unsigned __besm6_aux(unsigned a, unsigned mask);

/*
 * 022 acx (чед) — population count: popcount(a) added to X, end-around carry.
 */
unsigned __besm6_acx(unsigned a, unsigned x);

/*
 * 023 anx (нед) — highest set bit: the POSITION of A's highest set bit numbered
 * FROM THE MSB (bit 48 -> 1, bit 1 -> 48), added to X, end-around carry.  If A
 * is zero the result is just X — there is no distinguished "not found" value,
 * so the caller must test for zero first.
 */
unsigned __besm6_anx(unsigned a, unsigned x);

/*
 * 013 arx (слц) — cyclic add: a added to X, end-around carry.  Useful for
 * checksums.
 */
unsigned __besm6_arx(unsigned a, unsigned x);

/* ---- Tier 3: extracodes ---- */
/*
 * 050-077 — invoke extracode OP: M[016] := ea; A := acc; result := A.
 * Extracodes execute in user mode; they are how a program asks the operating
 * system for a privileged operation, and the Unix v7 syscall trap $77 N rides
 * on exactly this mechanism.
 *
 * OP is the opcode — it becomes an immediate field of the instruction word, so
 * it must be a compile-time constant in 050..077; the compiler evaluates it at
 * typecheck and diagnoses anything else.  EA may be constant or computed.
 *
 * Note the ABI consequence: an extracode sets M[016] — that is, r14 — from the
 * effective address, so code around this intrinsic must treat r14 as clobbered.
 * It is caller-saved, so this is legal.
 */
unsigned __besm6_extracode(int op, unsigned ea, unsigned acc);

#endif /* _BESM6_H */
