/*
 * <stddef.h> — common definitions (C11 §7.19), BESM-6 target.
 *
 * Freestanding header: types and macros only, no runtime.  Every BESM-6 scalar
 * occupies one 48-bit word; pointers hold a 15-bit word address.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

/* ptrdiff_t: result of subtracting two pointers.  Signed, one word (41-bit). */
typedef long ptrdiff_t;

/*
 * size_t: result of sizeof.  SIGNED on BESM-6 (41-bit, one word) -- a deliberate
 * departure from C11 §7.19, which requires an unsigned type.  Unsigned `+ - * / <'
 * are library calls here (the additive unit reads bits 48-42 as an exponent), while
 * the signed forms are single inline instructions; and no object this machine can
 * address makes a size that a 41-bit signed value cannot hold.  So size arithmetic
 * -- every `sizeof', every index -- stays inline.  See doc/Besm6_Data_Representation.md
 * and the note in v7besm's include/sys/types.h.  ptrdiff_t is signed for the same reason.
 */
typedef long size_t;

/* wchar_t: wide character.  One word; holds any BESM-6/KOI7 code point. */
typedef int wchar_t;

/*
 * max_align_t: a type whose alignment is the strictest the implementation has.
 * On the word-addressed BESM-6 every type is 1-word aligned, so any scalar will
 * do; use double for parity with hosted toolchains.
 */
typedef double max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Byte offset of MEMBER within struct/union TYPE. */
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* _STDDEF_H */
