/*
 * <uchar.h> — Unicode utilities (C11 §7.28), BESM-6 target.
 *
 * char16_t/char32_t are one unsigned word each (the word is wide enough for
 * either).  TODO: implement the conversion routines in libc.bin.
 */
#ifndef _UCHAR_H
#define _UCHAR_H

#include <wchar.h> /* mbstate_t */
#include <stddef.h>

typedef unsigned char16_t;
typedef unsigned char32_t;

size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#endif /* _UCHAR_H */
