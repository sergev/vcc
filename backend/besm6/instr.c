#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "frame.h"
#include "internal.h"
#include "tac.h"
#include "xalloc.h"

// Convert a struct-member byte offset to a word offset.  BESM-6 word-sized members
// (int/long/pointer/float/double) are 6 bytes, so a word-aligned member's byte offset
// is a multiple of 6.  A sub-word offset means a packed char member (task #22).
static int member_word_offset(int byte_offset)
{
    if (byte_offset % BESM6_WORD_BYTES != 0)
        internal_error("besm6: a copy at sub-word offset %d of a non-byte member", byte_offset);
    return byte_offset / BESM6_WORD_BYTES;
}

// Octal "=marker | offset_enc" fat-pointer constant for a packed char member at byte
// position byte_num within its word (0 = MSB / byte#0, 5 = LSB / byte#5).  offset_enc =
// 5 - byte_num occupies bits 47-45; the marker is bit 48.  ORing this onto a word address
// yields a char* fat pointer addressing that byte (same encoding as PTR_TO_CHAR_PTR/b/stb).
static const char *fat_marker_const(int byte_num)
{
    static const char *const tab[BESM6_WORD_BYTES] = {
        "=:64", // byte#0 -> offset_enc 5 (MSB)
        "=:60", // byte#1 -> offset_enc 4
        "=:54", // byte#2 -> offset_enc 3
        "=:50", // byte#3 -> offset_enc 2
        "=:44", // byte#4 -> offset_enc 1
        "=:40", // byte#5 -> offset_enc 0 (LSB)
    };
    return tab[byte_num];
}

// Emit code leaving in A a char* fat pointer to the packed char member at byte `offset`
// of `base` (a local frame slot or a module-level global): the member's word address with
// the marker and offset_enc (5 - offset%6) folded in.  Reused by the byte read and write
// paths of COPY_FROM_OFFSET / COPY_TO_OFFSET.
static void emit_member_fatptr(Besm_Block *block, Besm_Instr **tail, const Frame *f,
                               const char *base, int offset)
{
    int woff     = offset / BESM6_WORD_BYTES;
    int byte_num = offset % BESM6_WORD_BYTES;
    int br, bo;
    if (frame_lookup(f, base, &br, &bo)) {
        // A frame slot's index register can only ride in a UTC: VTM's register field is
        // its destination, so C must carry M[br] + bo + woff into the VTM.
        Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
        utc->reg        = br;
        utc->addr       = bo + woff;
        Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
        vtm->reg        = 14;
    } else {
        // A global needs no index register, so VTM names it directly: M[14] = &base + woff.
        Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
        vtm->reg        = 14;
        vtm->name       = xstrdup(base);
        vtm->addr       = woff;
    }
    Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
    ita->addr       = 14; // A = member word address
    Besm_Instr *aox = emit(block, tail, BESM_LOG_AOX);
    aox->name       = xstrdup(fat_marker_const(byte_num)); // marker + offset_enc
}

// Emit a binary op that lowers to a runtime helper:  dst = helper(src1, src2).
//
// Used by the integer comparisons (b/eq, b/ne, b/lt, b/le, b/gt, b/ge and the unsigned
// orderings) and by unsigned add (b/uadd).  These follow the lightweight helper
// convention (NOT the C ABI):
//   - first operand `a` sits on the stack top, second operand `b` is in A;
//   - the helper reads `a` through the stack register (M17 pre-decrement), which both
//     consumes `a` and pops it (r15 -= 1), so the caller emits no stack adjustment;
//   - the result is left in A; the helper returns via 13 ,uj,.
//   - ,call, self-declares the external (like printf), so no ,subp, is needed.
//
// Sequence (a = src1, b = src2):
//   XTA src1        — A = a
//   XTS src2        — push a (r15 += 1); A = b
//   ,call, helper   — helper pops a (r15 -= 1); result in A
//   reg ,ATX, off   — store result into dst's frame slot
//
static void emit_binop_helper(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *src1,
                              const Tac_Val *src2, const char *helper, int dr, int doff)
{
    emit_xta_val(b, t, f, src1);
    emit_xts_val(b, t, f, src2);
    Besm_Instr *call = emit(b, t, BESM_BRANCH_CALL);
    call->name       = xstrdup(helper);
    emit_atx(b, t, dr, doff);
}

// Emit a unary op that lowers to a runtime helper:  dst = helper(src).
//
// Lighter than emit_binop_helper: the single operand is passed in A (no stack push) and the
// result is returned in A.  Used by the int↔FP conversion helpers b/dtoi / b/dtou / b/utod,
// the same convention as b/uneg / b/not.
//
//   XTA src         — A = src
//   ,call, helper   — result in A
//   reg ,ATX, off   — store result into dst's frame slot
//
static void emit_unary_helper(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *src,
                              const char *helper, int dr, int doff)
{
    emit_xta_val(b, t, f, src);
    Besm_Instr *call = emit(b, t, BESM_BRANCH_CALL);
    call->name       = xstrdup(helper);
    emit_atx(b, t, dr, doff);
}

// Extract the integer value of an integer constant (used for constant shift counts).
static long tac_const_int(const Tac_Const *c)
{
    switch (c->kind) {
    case TAC_CONST_INT:
        return c->u.int_val;
    case TAC_CONST_LONG:
        return (long)c->u.long_val;
    case TAC_CONST_LONG_LONG:
        return (long)c->u.long_long_val;
    case TAC_CONST_SCHAR:
        return c->u.char_val;
    case TAC_CONST_UINT:
        return (long)c->u.uint_val;
    case TAC_CONST_ULONG:
        return (long)c->u.ulong_val;
    case TAC_CONST_ULONG_LONG:
        return (long)c->u.ulong_long_val;
    case TAC_CONST_UCHAR:
        return c->u.uchar_val;
    default:
        internal_error("besm6: a constant of kind %d as a shift count", (int)c->kind);
    }
}

// If v is a positive integer constant equal to 2^k with k >= 1, return k; else -1.
// Used for multiply/divide/remainder strength reduction.  Negative or out-of-range
// unsigned constants (cast to a negative long) fail the val < 2 test and fall back to
// the runtime helper, which is always safe.
static int tac_const_log2(const Tac_Val *v)
{
    if (v->kind != TAC_VAL_CONSTANT)
        return -1;
    long val = tac_const_int(v->u.constant);
    if (val < 2 || (val & (val - 1)) != 0)
        return -1;
    int k = 0;
    while ((1L << k) < val)
        k++;
    return k;
}

// Emit a shift:  dst = src1 << src2  (left) or  dst = src1 >> src2  (right).
//
// Shifts are logical for both int and unsigned, and right-shift does no sign extension.
// BESM-6 ASN/ASX shift by (exponent_field - 64): a field > 64 shifts right, < 64 shifts
// left.  So left by k uses field 64-k, right by k uses field 64+k.
//
//   - Constant count k: load the value, then a single ASN with the computed field.
//   - Variable count: runtime-helper convention (value on stack top, count in A) — call
//     b/lsh / b/rsh, which leave the result in A and pop the value.
static void emit_shift(Besm_Block *b, Besm_Instr **t, const Frame *f, const Tac_Val *src1,
                       const Tac_Val *src2, bool left, int dr, int doff)
{
    emit_xta_val(b, t, f, src1);
    if (src2->kind == TAC_VAL_CONSTANT) {
        long k          = tac_const_int(src2->u.constant);
        Besm_Instr *asn = emit(b, t, BESM_EXP_SHIFTN);
        asn->addr       = (int)(left ? 64 - k : 64 + k);
    } else {
        emit_xts_val(b, t, f, src2);
        Besm_Instr *call = emit(b, t, BESM_BRANCH_CALL);
        call->name       = xstrdup(left ? "b$lsh" : "b$rsh");
    }
    emit_atx(b, t, dr, doff);
}

void codegen_instr(const Tac_Instruction *instr, const Frame *f, Besm_Block *block,
                   Besm_Instr **tail)
{
    switch (instr->kind) {
    // COPY  dst = src
    //
    // In C:  b = a;
    // TAC:   copy src → dst
    //
    // Four sub-cases depending on whether each operand is a local (frame slot) or a
    // module-level global (addressed via UTC/XTA or UTC/ATX through C):
    //
    //   local  → local : reg_src ,XTA, off_src  /  reg_dst ,ATX, off_dst
    //   global → local : ,UTC, src_name  /  ,XTA,  /  reg_dst ,ATX, off_dst
    //   local  → global: reg_src ,XTA, off_src  /  ,UTC, dst_name  /  ,ATX,
    //   global → global: ,UTC, src_name  /  ,XTA,  /  ,UTC, dst_name  /  ,ATX,
    //
    // Bare ,XTA, / ,ATX, (reg=0, addr=0) load/store the word whose address is in C.
    // UTC sets C = address of the named global (using mem[0]=0 architecturally as base).
    // UTC does not disturb A, so the local→global order (XTA before UTC) is safe.
    //
    case TAC_INSTRUCTION_COPY: {
        const Tac_Val *src = instr->u.copy.src;
        const Tac_Val *dst = instr->u.copy.dst;
        emit_xta_val(block, tail, f, src);
        emit_store_a(block, tail, f, dst->u.var_name);
        break;
    }
    // GET_ADDRESS  dst = &src
    //
    // In C:  p = &a;
    // TAC:   get_address src → dst   (src is the variable being addressed;
    //                                 dst receives its runtime word address)
    //
    // BESM-6 sequence (r14 is a scratch index register).  For a local at a nonzero frame
    // offset the address needs an index register, which only UTC has room for (VTM's
    // register field names its destination):
    //   reg_src ,UTC, off_src — C = M[reg_src] + off_src: copy
    //                           the word address of src into C
    //   14 ,VTM, 0            — M[14] = 0 + C, so r14 now holds the word address of src
    //   ,ITA, 14              — A = M[14]: load that address into the accumulator
    //   reg_dst ,ATX, off_dst — store A (the address) into dst's frame slot
    //
    // A global needs no index register, and VTM's 15-bit address field takes a relocatable
    // name just like UTC's, so the UTC folds away:
    //   14 ,VTM, src          — M[14] = &src
    //   ,ITA, 14
    //   reg_dst ,ATX, off_dst
    //
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY: {
        const char *src_name = instr->u.get_address.src->u.var_name;
        int sr, so;
        if (frame_lookup(f, src_name, &sr, &so)) {
            // Local variable: compute address from its frame slot.
            if (so == 0) {
                Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
                ita->addr       = sr;
            } else {
                Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
                utc->reg        = sr;
                utc->addr       = so;
                Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
                vtm->reg        = 14;
                Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
                ita->addr       = 14;
            }
        } else {
            // Module-level static: VTM's address field holds the label directly.
            // The SUBP declaration for src_name is emitted before the prologue (single-pass).
            Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
            vtm->reg        = 14;
            vtm->name       = xstrdup(src_name);
            Besm_Instr *ita = emit(block, tail, BESM_MEM_ITA);
            ita->addr       = 14;
        }
        // &c where c is a char object yields a char* fat pointer: set the fat marker
        // (bit 48) with byte offset 0.  The marker makes the pointer's exponent field
        // 64, so the byte-load ASX shifts by 0 — addressing byte #5 (the low byte where
        // a standalone char lives).
        if (instr->kind == TAC_INSTRUCTION_GET_ADDRESS_BYTE) {
            Besm_Instr *aox = emit(block, tail, BESM_LOG_AOX);
            aox->name       = xstrdup("=:40"); // bit 48: fat marker, offset 0
        }
        // A char/void array (or string) decaying to a char* points at its first byte,
        // which is packed in the MSB (byte #0): set the marker with offset_enc 5.
        if (instr->kind == TAC_INSTRUCTION_GET_ADDRESS_DECAY) {
            Besm_Instr *aox = emit(block, tail, BESM_LOG_AOX);
            aox->name       = xstrdup("=:64"); // bit 48 + offset_enc 5 (MSB)
        }
        emit_store_a(block, tail, f, instr->u.get_address.dst->u.var_name);
        break;
    }
    // LOAD  dst = *src_ptr
    //
    // In C:  b = *p;
    // TAC:   load *src_ptr → dst   (src_ptr is a pointer variable in the frame;
    //                               dst receives the dereferenced value)
    //
    // BESM-6 sequence (WTC sets the C address-modifier register from the pointer word):
    //   reg_ptr ,WTC, off_ptr   — C = mem[reg_ptr+off_ptr][15:1]: the pointed-to word addr
    //   ,XTA,                   — A = mem[C]: dereference (the bare XTA's EA is 0+M[0]+C = C)
    //   reg_dst ,ATX, off_dst   — store the loaded value into dst's frame slot
    //
    // All BESM-6 pointers are word addresses; the bare XTA reads the base of the pointed-to
    // object.  WTC takes the same bits 15:1 the old ATI form took, and never touches r1.
    // C resets after the very next instruction, so the XTA must immediately follow WTC.
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE: {
        int dr, doff;
        lookup(f, instr->u.load.dst->u.var_name, &dr, &doff);
        if (instr->kind == TAC_INSTRUCTION_LOAD_BYTE) {
            const char *pname = instr->u.load.src_ptr->u.var_name;
            // Byte load through a char*/void* fat pointer.  The pointer word holds the
            // word address in bits 15-1 and the byte offset in its exponent field
            // (= 64 + offset*8).  WTC loads the word address into C; the bare XTA then
            // reads the containing word (C supplies the base); ASX shifts the target
            // byte down by offset*8 using the pointer's own exponent; AAX masks to 8 bits.
            // The pointer may be frame-local or a module-level global, so both the WTC
            // and the ASX go through the global-safe emit helpers.
            emit_wtc_ptr(block, tail, f, pname); // C = word address
            emit_xta(block, tail, 0, 0);         // A = mem[C]: the containing word
            emit_asx_ptr(block, tail, f, pname); // A >>= pointer's byte offset
            Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
            aax->name       = xstrdup("=377"); // mask low 8 bits
            emit_atx(block, tail, dr, doff);
            break;
        }
        // C = pointer (frame slot or global), then bare XTA reads mem[C].
        emit_wtc_ptr(block, tail, f, instr->u.load.src_ptr->u.var_name);
        emit_xta(block, tail, 0, 0); // A = mem[C]: the dereferenced word
        emit_atx(block, tail, dr, doff);
        break;
    }
    // STORE  *dst_ptr = src
    //
    // In C:  *p = a;
    // TAC:   store src → *dst_ptr   (dst_ptr is a pointer variable in the frame;
    //                                src is the value to write through it)
    //
    // BESM-6 sequence (WTC sets the C address-modifier register from the pointer word):
    //   reg_src ,XTA, off_src   — load the source value into A (this load resets C)
    //   reg_ptr ,WTC, off_ptr   — C = mem[reg_ptr+off_ptr][15:1]; A is unchanged
    //   ,ATX,                   — mem[C] = A: write A through the pointer (EA = C)
    //
    // The source is loaded BEFORE the pointer: C resets after the very next instruction,
    // so WTC must sit immediately before the ATX, and WTC (unlike the old ATI) does not
    // disturb A.  The source may be a frame var, a global, or a constant (e.g. arr[i] = 5
    // after the optimizer folds the value), so it is loaded via emit_xta_val.
    case TAC_INSTRUCTION_STORE:
    case TAC_INSTRUCTION_STORE_BYTE: {
        if (instr->kind == TAC_INSTRUCTION_STORE_BYTE) {
            // Byte store through a char*/void* fat pointer.  Read-modify-write is too
            // long to inline, so call the b/stb runtime helper with the lightweight
            // convention: the fat pointer `a` on the stack top, the byte value `b` in A.
            // The helper masks the byte, clears the target byte of the containing word,
            // ORs the new byte into place, and writes the word back; r15 is unchanged.
            // The pointer may be frame-local or a module-level global, so load it with
            // emit_xta_val (global-safe: UTC name + XTA for a global, a plain frame XTA
            // otherwise).
            emit_xta_val(block, tail, f, instr->u.store.dst_ptr); // A = fat pointer (a)
            emit_xts_val(block, tail, f, instr->u.store.src);     // push a; A = value (b)
            Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
            call->name       = xstrdup("b$stb");
            break;
        }
        emit_xta_val(block, tail, f, instr->u.store.src); // A = src (this load resets C)
        // C = pointer (frame slot or global); A unchanged, then bare ATX writes mem[C].
        emit_wtc_ptr(block, tail, f, instr->u.store.dst_ptr->u.var_name);
        emit_atx(block, tail, 0, 0); // mem[C] = A
        break;
    }
    // UNARY  dst = op src
    //
    // Negate, complement, and logical not are implemented (task #6).
    //
    // Logical not (!) lowers to the b/not runtime helper for every operand type: the
    // helper tests A against zero, leaving 1 if the operand is zero and 0 otherwise.  It
    // is a unary helper like b/uneg: operand in A, result in A, returns via 13 ,uj,.
    //
    // Negate needs three representation-specific sequences:
    //   - signed int:  X-A 0   (0 - A; mem[0]=0 architecturally).  R=7 after b/save
    //                  suppresses normalization/rounding, so this works directly.
    //   - unsigned:    the b/uneg runtime helper (48-bit modular negate).  It is a
    //                  unary helper: operand in A, result in A, returns via 13 ,uj,.
    //   - double:      enable normalization+rounding (NTR 0) around X-A 0, then
    //                  restore (NTR 7).  The surrounding NTRs are the temporary form;
    //                  a later pass will trim the ones that prove unnecessary.
    //
    // Complement is uniform for int and unsigned: AEX against an all-ones word flips
    // all 48 bits.  For unsigned this is the exact 48-bit complement; for signed int
    // it also flips the exponent field, yielding a non-canonical word (accepted UB).
    case TAC_INSTRUCTION_UNARY: {
        const Tac_Val *src = instr->u.unary.src;
        const Tac_Val *dst = instr->u.unary.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);

        switch (instr->u.unary.op) {
        case TAC_UNARY_NEGATE:
            emit_xta_val(block, tail, f, src);
            emit(block, tail, BESM_ARITH_RSUB);
            emit_atx(block, tail, rd, od);
            break;
        case TAC_UNARY_COMPLEMENT: {
            // Signed complement: flip only the 41 value bits (sign + 40-bit mantissa),
            // leaving the INT-format exponent field intact, so ~x stays a canonical
            // signed integer (~1 == -2).  Flipping the exponent too would scramble the
            // encoding into a non-integer.
            emit_xta_val(block, tail, f, src);
            Besm_Instr *aex = emit(block, tail, BESM_LOG_AEX);
            aex->name       = xstrdup("=37777777777777"); // 41 one-bits (value field), octal
            emit_atx(block, tail, rd, od);
            break;
        }
        case TAC_UNARY_COMPLEMENT_UNSIGNED: {
            // Unsigned complement: the operand is a plain 48-bit integer word, so flip
            // all 48 bits for the exact modular complement.
            emit_xta_val(block, tail, f, src);
            Besm_Instr *aex = emit(block, tail, BESM_LOG_AEX);
            aex->name       = xstrdup("=7777777777777777"); // 48 one-bits, octal
            emit_atx(block, tail, rd, od);
            break;
        }
        case TAC_UNARY_NEGATE_UNSIGNED: {
            emit_xta_val(block, tail, f, src);
            Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
            call->name       = xstrdup("b$uneg");
            emit_atx(block, tail, rd, od);
            break;
        }
        case TAC_UNARY_NOT: {
            // Logical NOT for any operand type: b/not returns 1 if A == 0, else 0.
            emit_xta_val(block, tail, f, src);
            Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
            call->name       = xstrdup("b$not");
            emit_atx(block, tail, rd, od);
            break;
        }
        case TAC_UNARY_NEGATE_DOUBLE: {
            emit_xta_val(block, tail, f, src);
            Besm_Instr *ntr_on = emit(block, tail, BESM_EXP_SETR);
            ntr_on->addr       = 0;
            emit(block, tail, BESM_ARITH_RSUB);
            Besm_Instr *ntr_off = emit(block, tail, BESM_EXP_SETR);
            ntr_off->addr       = 7;
            emit_atx(block, tail, rd, od);
            break;
        }
        default:
            internal_error("besm6: unary operator %d is not implemented", (int)instr->u.unary.op);
        }
        break;
    }
    // BINARY  dst = src1 op src2
    //
    // For integer add/subtract and the bitwise ops (and/or/xor), R=7 after b/save
    // suppresses normalization and rounding, so the arithmetic and logical
    // instructions work directly on raw words.  No NTR is needed.
    //
    // BESM-6 sequence:
    //   reg_src1 ,XTA, off_src1   — load src1 from its frame slot into A
    //   reg_src2 ,A+X, off_src2   — A = A op src2  (A-X / AAX / AOX / AEX)
    //   reg_dst  ,ATX, off_dst    — store A into dst's frame slot
    //
    case TAC_INSTRUCTION_BINARY: {
        const Tac_Val *src1 = instr->u.binary.src1;
        const Tac_Val *src2 = instr->u.binary.src2;
        const Tac_Val *dst  = instr->u.binary.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);

        // Comparisons lower to a runtime relational helper.  The unsigned ordering ops
        // temporarily reuse the signed helpers (correct within the 41-bit signed range)
        // until the b/ult/b/ule/b/ugt/b/uge library lands (TODO: task #20).  b/eq/b/ne
        // are signedness-independent.
        const char *cmp_helper = NULL;
        switch (instr->u.binary.op) {
        case TAC_BINARY_EQUAL:
            cmp_helper = "b$eq";
            break;
        case TAC_BINARY_NOT_EQUAL:
            cmp_helper = "b$ne";
            break;
        case TAC_BINARY_LESS_THAN:
            cmp_helper = "b$lt";
            break;
        case TAC_BINARY_LESS_OR_EQUAL:
            cmp_helper = "b$le";
            break;
        case TAC_BINARY_GREATER_THAN:
            cmp_helper = "b$gt";
            break;
        case TAC_BINARY_GREATER_OR_EQUAL:
            cmp_helper = "b$ge";
            break;
        case TAC_BINARY_LESS_THAN_UNSIGNED:
            cmp_helper = "b$ult";
            break;
        case TAC_BINARY_LESS_OR_EQUAL_UNSIGNED:
            cmp_helper = "b$ule";
            break;
        case TAC_BINARY_GREATER_THAN_UNSIGNED:
            cmp_helper = "b$ugt";
            break;
        case TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED:
            cmp_helper = "b$uge";
            break;
        // Floating-point orderings.  The FP helpers mirror the integer b/lt..b/ge but
        // bracket the subtract with NTR so the additive sign reflects the FP difference
        // (equal operands normalize to an exact zero).  FP ==/!= are pure bit equality,
        // so they keep using the type-independent b/eq/b/ne above.
        case TAC_BINARY_LESS_THAN_DOUBLE:
            cmp_helper = "b$flt";
            break;
        case TAC_BINARY_LESS_OR_EQUAL_DOUBLE:
            cmp_helper = "b$fle";
            break;
        case TAC_BINARY_GREATER_THAN_DOUBLE:
            cmp_helper = "b$fgt";
            break;
        case TAC_BINARY_GREATER_OR_EQUAL_DOUBLE:
            cmp_helper = "b$fge";
            break;
        default:
            break;
        }
        if (cmp_helper) {
            emit_binop_helper(block, tail, f, src1, src2, cmp_helper, rd, od);
            break;
        }

        // Unsigned add cannot use the inline additive unit: full 48-bit unsigned values
        // carry data in the exponent field (bits 48-42), which A+X misreads.  The b/uadd
        // helper does true 48-bit modular add via 24-bit half-words with explicit carry.
        // Signed ADD stays inline as A+X below.
        if (instr->u.binary.op == TAC_BINARY_ADD_UNSIGNED) {
            emit_binop_helper(block, tail, f, src1, src2, "b$uadd", rd, od);
            break;
        }

        // Unsigned subtract has the same exponent-field hazard as unsigned add: A-X
        // misreads the data in bits 48-42.  The b/usub helper does true 48-bit modular
        // subtract.  Signed SUBTRACT stays inline as A-X below.
        if (instr->u.binary.op == TAC_BINARY_SUBTRACT_UNSIGNED) {
            emit_binop_helper(block, tail, f, src1, src2, "b$usub", rd, od);
            break;
        }

        // Strength reduction: multiply by a constant power of two 2^k is a logical left
        // shift by k.  Multiply is commutative, so the constant may be either operand;
        // shift the other one.  The product is taken modulo the type width: unsigned keeps
        // the full 48-bit shift result, but a signed left shift spills significant bits
        // (including the sign) into the exponent field (bits 42-48), so the signed case
        // masks back to the 41-bit two's-complement representation that integers require.
        if (instr->u.binary.op == TAC_BINARY_MULTIPLY ||
            instr->u.binary.op == TAC_BINARY_MULTIPLY_UNSIGNED) {
            const Tac_Val *var = src1;
            int k              = tac_const_log2(src2);
            if (k < 0) {
                k   = tac_const_log2(src1);
                var = src2;
            }
            if (k >= 0) {
                emit_xta_val(block, tail, f, var);
                Besm_Instr *asn = emit(block, tail, BESM_EXP_SHIFTN);
                asn->addr       = 64 - k; // logical left shift by k bits
                if (instr->u.binary.op == TAC_BINARY_MULTIPLY) {
                    Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
                    aax->name       = xstrdup("=37777777777777"); // mask to 41 bits
                }
                emit_atx(block, tail, rd, od);
                break;
            }
        }

        // Multiply uses the b/mul runtime helper (the inline A*X needs FP normalization and
        // INT-format bridging, which the helper encapsulates).  Correct for signed operands.
        if (instr->u.binary.op == TAC_BINARY_MULTIPLY) {
            emit_binop_helper(block, tail, f, src1, src2, "b$mul", rd, od);
            break;
        }

        // Unsigned multiply uses b/umul, which forms the full 48-bit (low) product over the
        // entire unsigned range via operand splitting, without the signed 41-bit truncation
        // b/mul applies.
        if (instr->u.binary.op == TAC_BINARY_MULTIPLY_UNSIGNED) {
            emit_binop_helper(block, tail, f, src1, src2, "b$umul", rd, od);
            break;
        }

        // Signed divide and remainder use the b/div / b/mod runtime helpers, which bridge
        // raw operands to INT-format, FP-divide the absolute values, correct the exponent
        // and reapply the sign (b/mod = a - (a/b)*b).  Correct for signed operands and for
        // unsigned within the 41-bit range; full 48-bit unsigned divide/remainder use
        // b/udiv / b/umod below.
        if (instr->u.binary.op == TAC_BINARY_DIVIDE) {
            emit_binop_helper(block, tail, f, src1, src2, "b$div", rd, od);
            break;
        }
        if (instr->u.binary.op == TAC_BINARY_REMAINDER) {
            emit_binop_helper(block, tail, f, src1, src2, "b$mod", rd, od);
            break;
        }

        // Unsigned divide uses b/udiv.  The signed b/div borrows the hardware FP unit
        // (40-bit mantissa, bit 48 read as a sign), so it is wrong for unsigned operands
        // >= 2^40 or with bit 48 set.  b/udiv instead does an integer long division over
        // the full 48-bit word (divisor-shift / subtract loop).  Strength reduction:
        // unsigned divide by a constant 2^k is an exact logical right shift by k (unsigned
        // is full-48-bit logical, so no sign extension).
        if (instr->u.binary.op == TAC_BINARY_DIVIDE_UNSIGNED) {
            int k = tac_const_log2(src2);
            if (k >= 0) {
                emit_xta_val(block, tail, f, src1);
                Besm_Instr *asn = emit(block, tail, BESM_EXP_SHIFTN);
                asn->addr       = 64 + k; // logical right shift by k bits
                emit_atx(block, tail, rd, od);
                break;
            }
            emit_binop_helper(block, tail, f, src1, src2, "b$udiv", rd, od);
            break;
        }

        // Unsigned remainder uses b/umod.  The signed b/mod shares b/div's FP bridge and so
        // is wrong for the same out-of-range unsigned operands.  b/umod computes the full
        // 48-bit residue as a - (a/b)*b, reusing b/udiv / b/umul / b/usub.  Strength
        // reduction: unsigned remainder by a constant 2^k is masking the low k bits.
        if (instr->u.binary.op == TAC_BINARY_REMAINDER_UNSIGNED) {
            int k = tac_const_log2(src2);
            if (k >= 0) {
                emit_xta_val(block, tail, f, src1);
                Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
                char buf[32];
                snprintf(buf, sizeof(buf), "=%lo", (1UL << k) - 1); // mask low k bits
                aax->name = xstrdup(buf);
                emit_atx(block, tail, rd, od);
                break;
            }
            emit_binop_helper(block, tail, f, src1, src2, "b$umod", rd, od);
            break;
        }

        // Shifts are logical for int and unsigned alike (right-shift does no sign
        // extension), so all three shift ops reduce to "left" or "right".  Constant
        // counts inline an ASN; variable counts call b/lsh / b/rsh.
        if (instr->u.binary.op == TAC_BINARY_LEFT_SHIFT) {
            emit_shift(block, tail, f, src1, src2, /*left=*/true, rd, od);
            break;
        }
        if (instr->u.binary.op == TAC_BINARY_RIGHT_SHIFT ||
            instr->u.binary.op == TAC_BINARY_RIGHT_SHIFT_LOGICAL) {
            emit_shift(block, tail, f, src1, src2, /*left=*/false, rd, od);
            break;
        }

        // Floating-point add/sub/mul/div.  The operands are native 48-bit FP words, so
        // the result needs normalization and rounding — temporarily clear R's suppress
        // bits (NTR 0) around the hardware A+X/A-X/A*X/A/X, then restore the integer mode
        // (NTR 7) that b/save left in place.  Unlike signed integer multiply/divide, no
        // INT-format bridging is needed: the operands are already FP.
        {
            Besm_InstrKind fp_kind;
            bool is_fp = true;
            switch (instr->u.binary.op) {
            case TAC_BINARY_ADD_DOUBLE:
                fp_kind = BESM_ARITH_ADD;
                break;
            case TAC_BINARY_SUBTRACT_DOUBLE:
                fp_kind = BESM_ARITH_SUB;
                break;
            case TAC_BINARY_MULTIPLY_DOUBLE:
                fp_kind = BESM_ARITH_MUL;
                break;
            case TAC_BINARY_DIVIDE_DOUBLE:
                fp_kind = BESM_ARITH_DIV;
                break;
            default:
                is_fp   = false;
                fp_kind = BESM_ARITH_ADD;
                break;
            }
            if (is_fp) {
                emit_xta_val(block, tail, f, src1);
                Besm_Instr *ntr_on = emit(block, tail, BESM_EXP_SETR);
                ntr_on->addr       = 0;
                emit_arith_val(block, tail, fp_kind, f, src2);
                Besm_Instr *ntr_off = emit(block, tail, BESM_EXP_SETR);
                ntr_off->addr       = 7;
                emit_atx(block, tail, rd, od);
                break;
            }
        }

        // Bitwise and/or/xor map directly to the logical instructions AAX/AOX/AEX,
        // which act on raw 48-bit words with no normalization (same shape as ADD/SUB).
        // The result is correct for both signed (exponent field = 0) and unsigned
        // (full 48-bit) operands, so no signedness distinction is needed.
        Besm_InstrKind op_kind;
        switch (instr->u.binary.op) {
        case TAC_BINARY_ADD:
            op_kind = BESM_ARITH_ADD;
            break;
        case TAC_BINARY_SUBTRACT:
            op_kind = BESM_ARITH_SUB;
            break;
        case TAC_BINARY_BITWISE_AND:
            op_kind = BESM_LOG_AAX;
            break;
        case TAC_BINARY_BITWISE_OR:
            op_kind = BESM_LOG_AOX;
            break;
        case TAC_BINARY_BITWISE_XOR:
            op_kind = BESM_LOG_AEX;
            break;
        default:
            internal_error("besm6: binary operator %d is not implemented", (int)instr->u.binary.op);
        }
        emit_xta_val(block, tail, f, src1);
        emit_arith_val(block, tail, op_kind, f, src2);
        emit_atx(block, tail, rd, od);
        break;
    }
    // FUN_CALL  [dst =] fun(args...)
    //
    // BESM-6 sequence (N args):
    //   ,XTA, arg0         — load first arg into A (=N for integer constants)
    //   ,XTS, arg1 ... argN-1  — push each subsequent arg (XTS: stack←A, A←argI)
    //   14 ,VTM, -N        — set r14 = -N (negative arg count); omitted if N=0
    //   ,CALL, fun_name    — call; r13 ← return address
    //   reg ,ATX, off      — store result (A) into dst frame slot, if dst present
    //
    // Indirect call (`indirect`: fun_name names a function *pointer*, not the callee): the
    // callee address is not a label but the pointer's contents.  VJM's target is offset + C
    // (the C address-modifier register; M[reg] is not added — see the ISA reference), so the
    // arg setup is followed by a WTC that loads C from the pointer word (it survives exactly
    // one instruction) and a bare VJM to offset 0 — emit_wtc_ptr reaches the pointer wherever
    // it lives, a frame slot or a module-level variable:
    //   7 ,WTC, off        — C = mem[slot][15:1]  = target function address
    //     ,WTC, name       — C = mem[name][15:1]  = target function address (global pointer)
    //  13 ,VJM, 0          — M[13] ← return address; jump to 0 + C = the target
    // Nothing may sit between the WTC and the VJM (same C-survival rule as LOAD/STORE).
    //
    // Whether the name is a function or an object holding a function's address cannot be
    // recovered from the name: a module-level `f` may be either, and an extern pointer
    // defined in another translation unit appears nowhere in this program.  Only the flag
    // the translator set from the symbol's type may decide it — deducing "indirect" from
    // frame residency would compile a call through a file-scope pointer into a direct call
    // to the pointer's own storage in the data segment.
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN: {
        // A <besm6.h> intrinsic is a call in the IR but never one in the machine code: it
        // lowers to inline instructions.  Intercepted before any argument setup, and so
        // also above the _Noreturn branch below (__besm6_stop is _Noreturn).
        if (codegen_intrinsic(instr, f, block, tail))
            break;

        const char *fun_name = instr->u.fun_call.fun_name;
        const Tac_Val *args  = instr->u.fun_call.args;
        const Tac_Val *dst   = instr->u.fun_call.dst;

        int nargs = 0;
        for (const Tac_Val *a = args; a; a = a->next)
            nargs++;

        if (nargs > 0) {
            emit_xta_val(block, tail, f, args);
            for (const Tac_Val *a = args->next; a; a = a->next)
                emit_xts_val(block, tail, f, a);
            Besm_Instr *vtm = emit(block, tail, BESM_REG_VTM);
            vtm->reg        = REG_CNT;
            vtm->addr       = -nargs;
        }

        if (instr->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN) {
            // A _Noreturn callee never returns, so tail-jump to it (,uj,) instead of ,call,
            // (= 13 ,vjm,): no return-address linkage is needed.  The UJ also marks the
            // fall-through unreachable, so peephole rule #31(b) deletes the dead post-call
            // path and the function's epilogue.  Always a direct call, and void: no dst.
            Besm_Instr *uj = emit(block, tail, BESM_BRANCH_UJ);
            uj->name       = xstrdup(fun_name);
            break;
        }

        if (instr->u.fun_call.indirect) {
            // Indirect call: C = the pointer's contents, then jump to it.
            emit_wtc_ptr(block, tail, f, fun_name);
            Besm_Instr *vjm = emit(block, tail, BESM_BRANCH_VJM);
            vjm->reg        = REG_RET;
            vjm->addr       = 0;
        } else {
            // Direct call to a module-level function by name.
            Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
            call->name       = xstrdup(fun_name);
        }

        if (dst && dst->kind == TAC_VAL_VAR) {
            int dr, doff;
            lookup(f, dst->u.var_name, &dr, &doff);
            emit_atx(block, tail, dr, doff);
        }
        break;
    }
    // RETURN  [src]
    //
    // Load return value into A (if any), then jump to the return label.
    // Void functions do not emit this instruction; non-void functions emit it
    // before the unconditional epilogue jump (the duplicate UJ is dead code).
    case TAC_INSTRUCTION_RETURN: {
        const Tac_Val *src = instr->u.return_.src;
        if (src)
            emit_xta_val(block, tail, f, src);
        Besm_Instr *uj = emit(block, tail, BESM_BRANCH_UJ);
        uj->name       = xstrdup("b$ret");
        break;
    }
    // LABEL  name:
    //
    // Emit an inline label definition point.  No storage is allocated.
    case TAC_INSTRUCTION_LABEL: {
        Besm_Instr *lbl = emit(block, tail, BESM_STMT_LABEL);
        lbl->name       = xstrdup(instr->u.label.name);
        break;
    }
    // JUMP  target
    //
    // Unconditional branch: ,UJ, target.
    case TAC_INSTRUCTION_JUMP: {
        Besm_Instr *uj = emit(block, tail, BESM_BRANCH_UJ);
        uj->name       = xstrdup(instr->u.jump.target);
        break;
    }
    // JUMP_IF_ZERO  condition → target
    //
    // XTA sets logical ω from the loaded value (ω=0 iff A=0).
    // UZA branches when ω=0 (condition is zero).  No NTR needed.
    case TAC_INSTRUCTION_JUMP_IF_ZERO: {
        emit_xta_val(block, tail, f, instr->u.jump_if_zero.condition);
        Besm_Instr *uza = emit(block, tail, BESM_BRANCH_UZA);
        uza->name       = xstrdup(instr->u.jump_if_zero.target);
        break;
    }
    // JUMP_IF_NOT_ZERO  condition → target
    //
    // Same as above but U1A branches when ω≠0 (condition is non-zero).
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO: {
        emit_xta_val(block, tail, f, instr->u.jump_if_not_zero.condition);
        Besm_Instr *u1a = emit(block, tail, BESM_BRANCH_U1A);
        u1a->name       = xstrdup(instr->u.jump_if_not_zero.target);
        break;
    }
    // Integer width conversions (task #17).
    //
    // Under the BESM-6 target (semantic/target.c) short/int/long/pointer are all one
    // 48-bit word, so emit_cast only emits TRUNCATE/ZERO_EXTEND/SIGN_EXTEND when one
    // side is char (1 byte = 8 bits); same-size conversions become a COPY.  Therefore the
    // narrow side is always char and the mask is always 8-bit.  (Every other scalar type
    // — short/int/long/long long/float/double/long double/pointer — is one word, so there
    // are no other size mismatches.)
    //
    // Because a same-size signed→unsigned conversion is a bare COPY, BESM-6 deviates
    // from C11 §6.3.1.3p2: signed int/long is a 41-bit type (bits 42-48 zero) while
    // unsigned uses all 48 bits, and no sign extension fills bits 42-48.  A negative
    // signed value therefore keeps its 41-bit pattern instead of becoming the
    // C-conforming `value + 2^48` — e.g. `(unsigned long)-1` is 2^41-1, not 2^48-1.
    //
    // TRUNCATE  dst = (narrow)src   — keep the low 8 bits.
    // ZERO_EXTEND dst = (wider unsigned)src — same: clear all but the low 8 bits.
    case TAC_INSTRUCTION_TRUNCATE:
    case TAC_INSTRUCTION_ZERO_EXTEND: {
        const Tac_Val *src = (instr->kind == TAC_INSTRUCTION_TRUNCATE) ? instr->u.truncate.src
                                                                       : instr->u.zero_extend.src;
        const Tac_Val *dst = (instr->kind == TAC_INSTRUCTION_TRUNCATE) ? instr->u.truncate.dst
                                                                       : instr->u.zero_extend.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
        aax->name       = xstrdup("=377"); // mask low 8 bits
        emit_atx(block, tail, rd, od);
        break;
    }
    // SIGN_EXTEND  dst = (wider signed)src   — signed char → wider.
    //
    // Branchless 8-bit sign extension via the (x ^ 0x80) - 0x80 trick: after masking to 8
    // bits, flipping bit 7 and subtracting 0x80 reinterprets the value as signed 8-bit.
    // Integer AEX/A-X act on raw words under R=7, so no NTR is needed.  The leading AAX
    // discards any high garbage bits before the sign is reconstructed from bit 7.
    case TAC_INSTRUCTION_SIGN_EXTEND: {
        const Tac_Val *src = instr->u.sign_extend.src;
        const Tac_Val *dst = instr->u.sign_extend.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
        aax->name       = xstrdup("=377"); // mask low 8 bits
        Besm_Instr *aex = emit(block, tail, BESM_LOG_AEX);
        aex->name       = xstrdup("=200"); // flip sign bit (0x80)
        Besm_Instr *sub = emit(block, tail, BESM_ARITH_SUB);
        sub->name       = xstrdup("=200"); // subtract 0x80
        emit_atx(block, tail, rd, od);
        break;
    }
    // Pointer-representation conversions (task #21).
    //
    // A word pointer (int*, etc.) is a bare word address; a char*/void* fat pointer
    // carries a marker bit (48) and a byte offset (bits 47-45).
    //
    //   PTR_TO_CHAR_PTR  (int* → char*): the C standard points the result at the first
    //     byte of the object.  On BESM-6 the first (most significant) byte is byte #0,
    //     so OR in the marker plus offset 5 (exponent field 64+5*8 = 104).
    //   CHAR_PTR_TO_PTR  (char* → int*): discard the marker and offset, keeping the bare
    //     word address (clear the top 7 bits / the exponent field).
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR: {
        const Tac_Val *src = instr->u.ptr_to_char_ptr.src;
        const Tac_Val *dst = instr->u.ptr_to_char_ptr.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        Besm_Instr *aox = emit(block, tail, BESM_LOG_AOX);
        aox->name       = xstrdup("=:64"); // bit 48 (marker) + offset 5
        emit_atx(block, tail, rd, od);
        break;
    }
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR: {
        const Tac_Val *src = instr->u.char_ptr_to_ptr.src;
        const Tac_Val *dst = instr->u.char_ptr_to_ptr.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
        aax->name       = xstrdup("=0037777777777777"); // keep low 41 bits (word address)
        emit_atx(block, tail, rd, od);
        break;
    }
    // Integer ↔ floating-point conversions (task #18).
    //
    // float ≡ double on BESM-6 (same 48-bit native FP word), so every FLOAT_* mirrors the
    // matching DOUBLE_*, and FLOAT_TO_DOUBLE / DOUBLE_TO_FLOAT are bit-pattern copies.
    //
    //   copies          : load + store the word unchanged.
    //   signed int → FP : inline INT-format-then-normalize (OR the INT exponent, NTR 0,
    //                     A+X 0, NTR 7).  Correct across the full 41-bit signed range,
    //                     negatives included (a negative two's-complement int in INT-format
    //                     is already its own valid FP value).
    //   unsigned → FP   : b/utod — a 48-bit unsigned carries data in the exponent field
    //                     (bits 48-42) and would misread bit 41 as an FP sign, so the inline
    //                     trick is wrong; the helper splits the word into 24-bit halves and
    //                     recombines in FP.
    //   FP → signed int : b/dtoi — realign the mantissa to the INT exponent and mask.
    //   FP → unsigned   : b/dtou — full 48-bit extraction via the reverse 24-bit split.
    //
    // long double ≡ double ≡ float on BESM-6 (all one 48-bit native-FP word), so every
    // LONG_DOUBLE_* / *_TO_LONG_DOUBLE conversion reuses the matching double path: the
    // FP↔FP forms are bit-pattern copies, and the int↔FP forms use the same helpers.
    // (All conversion union members share a {src, dst} layout, so reading src/dst from
    // any of them is equivalent.)
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE: {
        const Tac_Val *src = instr->u.float_to_double.src;
        const Tac_Val *dst = instr->u.float_to_double.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        emit_atx(block, tail, rd, od);
        break;
    }
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
    case TAC_INSTRUCTION_INT_TO_FLOAT:
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE: {
        const Tac_Val *src = instr->u.int_to_double.src;
        const Tac_Val *dst = instr->u.int_to_double.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_xta_val(block, tail, f, src);
        Besm_Instr *aox    = emit(block, tail, BESM_LOG_AOX);
        aox->name          = xstrdup("=:64"); // OR the INT-format exponent (0150) → unnormalized FP
        Besm_Instr *ntr_on = emit(block, tail, BESM_EXP_SETR);
        ntr_on->addr       = 0;            // NTR 0: enable normalization + rounding
        emit(block, tail, BESM_ARITH_ADD); // A+X 0 (mem[0]=0): normalize to canonical FP
        Besm_Instr *ntr_off = emit(block, tail, BESM_EXP_SETR);
        ntr_off->addr       = 7; // NTR 7: restore integer mode for the caller
        emit_atx(block, tail, rd, od);
        break;
    }
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE: {
        const Tac_Val *src = instr->u.uint_to_double.src;
        const Tac_Val *dst = instr->u.uint_to_double.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_unary_helper(block, tail, f, src, "b$utod", rd, od);
        break;
    }
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
    case TAC_INSTRUCTION_FLOAT_TO_INT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT: {
        const Tac_Val *src = instr->u.double_to_int.src;
        const Tac_Val *dst = instr->u.double_to_int.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_unary_helper(block, tail, f, src, "b$dtoi", rd, od);
        break;
    }
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT: {
        const Tac_Val *src = instr->u.double_to_uint.src;
        const Tac_Val *dst = instr->u.double_to_uint.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_unary_helper(block, tail, f, src, "b$dtou", rd, od);
        break;
    }
    // ADD_PTR  dst = ptr + index * scale
    //
    // `scale` is the element size in BYTES (e.g. int → 6 = one word).  BESM-6 is
    // word-addressed, so convert to a word scale (scale / 6) and scale the index by it:
    //   word_scale == 1   : plain add
    //   word_scale == 2^k : ASN by k bits on the index (logical left shift)
    //   otherwise         : b/mul the index by word_scale
    //
    // The base is always a pointer value: the translator decays an array to its address
    // (GET_ADDRESS) before the ADD_PTR, so the base's stored value is the base address and
    // the scaled index is added with a plain A+X.
    //
    // Byte-offset addressing (scale 1: char* arithmetic, char arrays, packed char struct
    // members) builds a fat pointer through the runtime helpers — see below.
    case TAC_INSTRUCTION_ADD_PTR: {
        const Tac_Val *ptr   = instr->u.add_ptr.ptr;
        const Tac_Val *index = instr->u.add_ptr.index;
        const Tac_Val *dst   = instr->u.add_ptr.dst;
        int scale            = instr->u.add_ptr.scale;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);

        // Scale 1 = char*/void* arithmetic on a fat pointer (only char/void have size 1).
        // The base reaching this point is always a value: a fat pointer (a char* variable
        // or an array/string decay GET_ADDRESS, marker set) or a bare word address (a
        // struct-member GET_ADDRESS, marker clear — the helper treats it as byte #0).
        // A constant ±1 delta uses the dedicated b/pinc / b/pdec (no division); any other
        // delta uses b/padd, which distributes the signed byte count across the word
        // address and the 3-bit offset.
        if (scale == 1) {
            if (index->kind == TAC_VAL_CONSTANT && index->u.constant->kind == TAC_CONST_INT &&
                (index->u.constant->u.int_val == 1 || index->u.constant->u.int_val == -1)) {
                emit_xta_val(block, tail, f, ptr); // A = fat pointer
                Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
                call->name       = xstrdup(index->u.constant->u.int_val == 1 ? "b$pinc" : "b$pdec");
            } else {
                emit_xta_val(block, tail, f, ptr);   // A = base
                emit_xts_val(block, tail, f, index); // push base; A = signed byte delta
                Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
                call->name       = xstrdup("b$padd");
            }
            emit_atx(block, tail, rd, od);
            break;
        }
        if (scale % 6 != 0)
            internal_error("besm6: pointer addition with sub-word scale %d", scale);
        int word_scale = scale / 6;

        // (a) scaled index in A.
        if (index->kind == TAC_VAL_CONSTANT) {
            // Constant index: fold index * word_scale at compile time and load the
            // product as a single 41-bit immediate -- no ASN shift, no runtime b/mul.
            // This covers the common &arr[k] / p +/- k / struct-member cases (including
            // word_scale == 1, where the product is just the index).
            //
            // Attach the product as a structural constant rather than a pre-rendered literal
            // name, so the emitters apply the usual 41-bit masking.  A zero product -- the
            // very common &arr[0] / p + 0 / first-struct-member -- gets no operand at all:
            // the bare `xta` reads memory word 0, which always holds zero.
            long prod       = tac_const_int(index->u.constant) * word_scale;
            Besm_Instr *xta = emit(block, tail, BESM_MEM_XTA);
            if (prod != 0) {
                xta->konst             = tac_new_const(TAC_CONST_LONG);
                xta->konst->u.long_val = prod;
            }
        } else {
            emit_xta_val(block, tail, f, index);
            if (word_scale != 1) {
                if ((word_scale & (word_scale - 1)) == 0) {
                    int k = 0;
                    while ((1 << k) < word_scale)
                        k++;
                    Besm_Instr *asn = emit(block, tail, BESM_EXP_SHIFTN);
                    asn->addr       = 64 - k; // logical left shift by k bits
                    // A signed (possibly negative) index left-shifts its sign bits into
                    // the exponent field (bits 42-48); mask back to the 41-bit two's-
                    // complement representation so the index stays a valid signed offset
                    // (cf. the BINARY signed-multiply strength reduction above).
                    Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
                    aax->name       = xstrdup("=37777777777777"); // mask to 41 bits
                } else {
                    // b/mul multiplies two 41-bit *signed* operands.  A negated unsigned
                    // index (e.g. a runtime `p -= u`) can arrive as a full 48-bit word
                    // whose bits 42-48 are set; those corrupt the multiply, so mask the
                    // index back to the 41-bit two's-complement range first (the
                    // power-of-two path above already masks after its shift).
                    Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
                    aax->name       = xstrdup("=37777777777777");
                    // Runtime-helper multiply: push the index, load =word_scale, b/mul.
                    Besm_Instr *xts = emit(block, tail, BESM_MEM_XTS);
                    char buf[32];
                    snprintf(buf, sizeof(buf), "=%o", word_scale);
                    xts->name        = xstrdup(buf);
                    Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
                    call->name       = xstrdup("b$mul");
                }
            }
        }

        // (b) add the base (a pointer value).
        emit_arith_val(block, tail, BESM_ARITH_ADD, f, ptr);
        emit_atx(block, tail, rd, od);
        break;
    }
    // PTR_DIFF  dst = ptr_a - ptr_b   (char*/void* difference → ptrdiff_t byte count)
    //
    // Both operands are fat pointers; b/pdiff decodes each to an absolute byte position
    // (word*6 + byte#) and subtracts, returning bytepos(a) - bytepos(b).  Same binop-helper
    // convention as b/uadd: ptr_a on the stack top, ptr_b in A, result in A.  No scaling
    // (sizeof(char) == 1).
    case TAC_INSTRUCTION_PTR_DIFF: {
        const Tac_Val *dst = instr->u.ptr_diff.dst;
        int rd, od;
        lookup(f, dst->u.var_name, &rd, &od);
        emit_binop_helper(block, tail, f, instr->u.ptr_diff.ptr_a, instr->u.ptr_diff.ptr_b,
                          "b$pdiff", rd, od);
        break;
    }
    // COPY_FROM_OFFSET  dst = base[offset]
    //
    // In C:  x = s.field;   (s is a named struct variable, not a pointer)
    // TAC:   copy_from_offset src=base, offset → dst
    //
    // `offset` is a byte offset; convert to a word offset.  The base is a local frame
    // slot (REG_AUTO/REG_PAR + slot offset) or a module-level global:
    //   local  : reg ,XTA, slot_off + woff      — load the member word
    //   global : ,UTC, base  /  ,XTA, woff      — C = addr(base), then mem[C+woff]
    // Then store A into the dst Val (local frame slot or global), as in COPY.
    //
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET: {
        const char *base   = instr->u.copy_from_offset.src;
        const Tac_Val *dst = instr->u.copy_from_offset.dst;
        // Packed char member: extract the byte from its word.  The byte sits at byte#
        // = offset%6 from the MSB, so shift it down by (5 - byte#)*8 and mask to 8 bits —
        // the same shift the byte-load uses, here with a compile-time count.
        if (instr->kind == TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET) {
            int offset   = instr->u.copy_from_offset.offset;
            int woff     = offset / BESM6_WORD_BYTES;
            int byte_num = offset % BESM6_WORD_BYTES;
            int br, bo;
            if (frame_lookup(f, base, &br, &bo)) {
                emit_xta(block, tail, br, bo + woff);
            } else {
                Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
                utc->name       = xstrdup(base);
                emit_xta(block, tail, 0, woff);
            }
            Besm_Instr *asn = emit(block, tail, BESM_EXP_SHIFTN);
            asn->addr       = 64 + (5 - byte_num) * 8; // logical right shift
            Besm_Instr *aax = emit(block, tail, BESM_LOG_AAX);
            aax->name       = xstrdup("=377"); // mask low 8 bits
            int dr, doff;
            if (frame_lookup(f, dst->u.var_name, &dr, &doff)) {
                emit_atx(block, tail, dr, doff);
            } else {
                Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
                utc->name       = xstrdup(dst->u.var_name);
                emit(block, tail, BESM_MEM_ATX);
            }
            break;
        }
        int woff = member_word_offset(instr->u.copy_from_offset.offset);
        int br, bo;
        if (frame_lookup(f, base, &br, &bo)) {
            emit_xta(block, tail, br, bo + woff);
        } else {
            Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
            utc->name       = xstrdup(base);
            emit_xta(block, tail, 0, woff);
        }
        int dr, doff;
        if (frame_lookup(f, dst->u.var_name, &dr, &doff)) {
            emit_atx(block, tail, dr, doff);
        } else {
            Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
            utc->name       = xstrdup(dst->u.var_name);
            emit(block, tail, BESM_MEM_ATX);
        }
        break;
    }
    // COPY_TO_OFFSET  base[offset] = src
    //
    // In C:  s.field = x;   (s is a named struct variable, not a pointer)
    // TAC:   copy_to_offset src → dst=base, offset
    //
    // Load src into A (local/global/constant), then store at the word offset into base:
    //   local  : reg ,ATX, slot_off + woff
    //   global : ,UTC, base  /  ,ATX, woff
    //
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET: {
        const Tac_Val *src = instr->u.copy_to_offset.src;
        const char *base   = instr->u.copy_to_offset.dst;
        // Packed char member: read-modify-write one byte.  Build a fat pointer to the
        // member byte and reuse the b/stb helper (fat pointer on the stack, byte in A).
        if (instr->kind == TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET) {
            emit_member_fatptr(block, tail, f, base, instr->u.copy_to_offset.offset);
            emit_xts_val(block, tail, f, src); // push fat pointer; A = byte value
            Besm_Instr *call = emit(block, tail, BESM_BRANCH_CALL);
            call->name       = xstrdup("b$stb");
            break;
        }
        int woff = member_word_offset(instr->u.copy_to_offset.offset);
        emit_xta_val(block, tail, f, src);
        int br, bo;
        if (frame_lookup(f, base, &br, &bo)) {
            emit_atx(block, tail, br, bo + woff);
        } else {
            Besm_Instr *utc = emit(block, tail, BESM_MOD_UTC);
            utc->name       = xstrdup(base);
            emit_atx(block, tail, 0, woff);
        }
        break;
    }
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        // Frame-slot reservation only; the slot is sized in frame_build and the
        // prologue's stack extension covers it. No runtime instruction is emitted.
        break;
    default:
        internal_error("besm6: TAC instruction %d is not implemented", (int)instr->kind);
    }
}
