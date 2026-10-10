/*
 * Append one byte to the stdout line buffer (Unix target).
 *
 * Six bytes pack big-endian into each 48-bit word of out_buff; out_cnt is the
 * current word index and out_shft the bit offset of the next byte within that
 * word (0, 8, ... 40).  A full buffer flushes to make room; a newline flushes
 * the line via the SYS_write leaf.
 *
 * Unlike the Madlen leaf — where the Dubna monitor's print-line supplies the
 * line break, so putbyte drops '\n' — the Unix target writes raw bytes, so the
 * newline is stored in the buffer and emitted like any other byte.  This file
 * also defines the shared output-buffer globals referenced by putch.c/flush.c.
 */
#include <stdio.h>

int out_cnt;
int out_shft;
int out_buff[22];

void putbyte(int b)
{
    int *p;

    b = b & 0377;
    if (out_cnt == 22) {
        flush();
    }

    p  = &out_buff[out_cnt];
    *p = *p | (b << (40 - out_shft));

    if (out_shft == 40) {
        /* next word */
        out_shft = 0;
        ++out_cnt;
    } else {
        /* next byte */
        out_shft = out_shft + 8;
    }

    if (b == '\n')
        flush(); /* emit the line, newline included */
}
