/*
 * <inttypes.h> — format conversion of integer types (C11 §7.8), AVR: the 16-bit types
 * and the pointer types are int, the 32-bit ones long, the 64-bit and max types long
 * long.
 */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "ld"
#define PRId64  "lld"
#define PRIdMAX "lld"
#define PRIdPTR "d"
#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "li"
#define PRIi64  "lli"
#define PRIiMAX "lli"
#define PRIiPTR "i"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "lu"
#define PRIu64  "llu"
#define PRIuMAX "llu"
#define PRIuPTR "u"
#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  "lo"
#define PRIo64  "llo"
#define PRIoMAX "llo"
#define PRIoPTR "o"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "lx"
#define PRIx64  "llx"
#define PRIxMAX "llx"
#define PRIxPTR "x"
#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "lX"
#define PRIX64  "llX"
#define PRIXMAX "llX"
#define PRIXPTR "X"

#define SCNd8   "hhd"
#define SCNd16  "d"
#define SCNd32  "ld"
#define SCNd64  "lld"
#define SCNdMAX "lld"
#define SCNdPTR "d"
#define SCNu8   "hhu"
#define SCNu16  "u"
#define SCNu32  "lu"
#define SCNu64  "llu"
#define SCNuMAX "llu"
#define SCNuPTR "u"
#define SCNx8   "hhx"
#define SCNx16  "x"
#define SCNx32  "lx"
#define SCNx64  "llx"
#define SCNxMAX "llx"
#define SCNxPTR "x"

#endif /* _INTTYPES_H */
