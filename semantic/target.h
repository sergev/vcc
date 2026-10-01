#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Target descriptor: size and alignment of primitive scalar types for one
// supported architecture.  All values are in bytes (C addressable units,
// i.e. in the same unit as sizeof() returns).  sizeof(char) == 1 always
// and is not stored here; schar/uchar are likewise always 1 byte.
// sizeof(enum) == sizeof(int) by convention.
//
// _Bool has a size of its own because C11 leaves its width implementation-defined
// and a byte is the wrong choice on a word-addressed machine: there the char sizes
// *mean* byte-packed storage and fat byte pointers, which would give `_Bool a[4]`
// six-per-word packing and `_Bool *p` a read-modify-write store, all to carry one
// bit.  BESM-6 therefore gives _Bool int's representation (one word); the
// byte-addressed targets keep the 1-byte _Bool their ABIs specify.
//
typedef struct {
    const char *name;
    size_t bool_size, bool_align;
    size_t short_size, short_align;
    size_t int_size, int_align;
    size_t long_size, long_align;
    size_t llong_size, llong_align; // long long / unsigned long long
    size_t float_size, float_align;
    size_t double_size, double_align;
    size_t ldouble_size, ldouble_align; // long double
    size_t pointer_size, pointer_align;
    // Signed integer value width in bits (two's-complement, sign bit included).
    // Normally <type>_size*8, but BESM-6 signed ints are 41-bit inside a 48-bit
    // word.  The unsigned value width is always the full storage <type>_size*8.
    int short_bits, int_bits, long_bits, llong_bits;
    // Signedness of plain `char` (target-defined in C): 1 = signed, 0 = unsigned.
    // `signed char` and `unsigned char` are unaffected (always signed/unsigned).
    int char_signed;
    // Right-shift of a *signed* operand: 0 = arithmetic (sign-preserving, the usual
    // C-implementation choice), 1 = logical (zero-fill).  BESM-6's shift unit does no
    // sign extension, so its backend lowers signed `>>` to a logical shift; the constant
    // folder consults this flag to fold the same value the backend would emit.
    int right_shift_is_logical;
    // Minimum alignment (bytes) of any struct/union.  On a word-addressed target the
    // smallest addressable/copyable unit is a machine word, so aggregates are padded up
    // to a whole word (BESM-6 = 6); otherwise this is 1 (natural C packing).  This keeps
    // array element strides a word multiple, so &arr[i] never lands mid-word.
    size_t aggregate_align;
} Target;

// Active target.  Defaults to x86_64.  Set this before calling any
// pipeline function (typecheck_decl, translate, etc.).
extern const Target *target_config;

// Look up a target by name (e.g. "x86_64", "besm6").
// Returns NULL if the name is not recognised.
const Target *target_lookup(const char *name);

// Print all known target names to stderr, one per line.
void target_list(void);

// True when the smallest addressable unit is a machine word wider than a byte
// (BESM-6): a scalar char then occupies a whole word, its value in the low byte.
int target_word_addressed(void);

// Sign-extend the low `w` bits of `bits` to a 64-bit signed value (w in (0,64)).
// w<=0 or w>=64 means "no narrowing": return the full 64-bit pattern.  Both constant
// folders (semantic/typecheck.c and optimize/const_fold.c) wrap folded results to a
// target value width through these two helpers, so they narrow identically.
int64_t sign_narrow(uint64_t bits, int w);

// Mask `bits` to the low `w` bits (w in (0,64)).  w<=0 or w>=64 returns `bits`.
uint64_t unsigned_narrow(uint64_t bits, int w);

#ifdef __cplusplus
}
#endif
