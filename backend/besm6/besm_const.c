#include "besm.h"
#include "internal.h"
#include "tac.h"

// Signed integers occupy the low 41 bits of the 48-bit word (raw two's complement,
// exponent field zero); unsigned integers occupy all 48 bits.  This dialect-independent
// bit pattern is what each emitter renders into its own literal syntax.
#define BESM_MASK41 UINT64_C(0x1FFFFFFFFFF)
#define BESM_MASK48 UINT64_C(0xFFFFFFFFFFFF)

Besm_ConstWord besm_const_word(const Tac_Const *c)
{
    Besm_ConstWord w = { 0 };
    switch (c->kind) {
    case TAC_CONST_INT:
        w.word = (uint64_t)((int64_t)c->u.int_val & (int64_t)BESM_MASK41);
        break;
    case TAC_CONST_LONG:
        w.word = (uint64_t)(c->u.long_val & (int64_t)BESM_MASK41);
        break;
    case TAC_CONST_LONG_LONG:
        w.word = (uint64_t)(c->u.long_long_val & (int64_t)BESM_MASK41);
        break;
    case TAC_CONST_SCHAR:
        w.word = (uint64_t)((int64_t)c->u.char_val & (int64_t)BESM_MASK41);
        break;
    case TAC_CONST_UINT:
        w.word = (uint64_t)(c->u.uint_val & BESM_MASK48);
        break;
    case TAC_CONST_ULONG:
        w.word = (uint64_t)(c->u.ulong_val & BESM_MASK48);
        break;
    case TAC_CONST_ULONG_LONG:
        w.word = (uint64_t)(c->u.ulong_long_val & BESM_MASK48);
        break;
    case TAC_CONST_UCHAR:
        w.word = (uint64_t)c->u.uchar_val;
        break;
    case TAC_CONST_FLOAT:
    case TAC_CONST_DOUBLE:
    case TAC_CONST_LONG_DOUBLE:
        // float ≡ double ≡ long double on BESM-6 (one 48-bit native-FP word).
        w.is_real  = true;
        w.real_val = (c->kind == TAC_CONST_FLOAT)         ? (double)c->u.float_val
                     : (c->kind == TAC_CONST_LONG_DOUBLE) ? f128_to_double(c->u.long_double_val)
                                                          : c->u.double_val;
        break;
    default:
        internal_error("besm6: constant of kind %d", (int)c->kind);
    }
    return w;
}

// A zero constant needs no literal word: the operand is dropped and the instruction reads
// memory word 0, which always holds zero.  Real 0.0 (and -0.0, which compares equal) encodes
// to the all-zero word too, so both branches collapse to the same bare instruction.
bool besm_const_is_zero(const Tac_Const *c)
{
    Besm_ConstWord w = besm_const_word(c);
    return w.is_real ? w.real_val == 0.0 : w.word == 0;
}
