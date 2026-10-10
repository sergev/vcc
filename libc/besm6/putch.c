/*
 * Append a multi-character word to the stdout buffer.
 *
 * C rewrite of write.b.  A word holds up to six bytes packed big-endian; this
 * emits them left to right, skipping leading zero bytes.  `unsigned` makes the
 * `>>` a logical shift (matching B).
 */
#include <stdio.h>

void putch(unsigned ch)
{
    int shift = 40, b;

    while (shift > 0) {
        b = (ch >> shift) & 0377;
        if (b)
            goto putchr;
        shift = shift - 8;
    }
    b = ch;
putchr:
    putbyte(b);
    if (shift > 0) {
        shift = shift - 8;
        b     = ch >> shift;
        goto putchr;
    }
}
