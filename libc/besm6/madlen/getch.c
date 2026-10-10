/*
 * Read one character from standard input; return 0 at end of file.
 *
 * C rewrite of read.b.  The Dubna monitor delivers the current input line in
 * MONCARD* (25 words, 6 bytes each) and the input position in KCOUNT*; monread_
 * advances to the next line.  These are external monitor symbols.
 *
 * Bytes are extracted from the word-packed line via a char* view of MONCARD*.
 */
#include <stdio.h>

extern int moncard_[25]; /* MONCARD* : current input line buffer */
extern int kcount_[23];  /* KCOUNT*  : monitor input position */
extern void monread_(void);

int read_idx; /* index of the next byte in MONCARD* */
int read_len; /* length of data in MONCARD* */
int read_ptr; /* saved monitor position for readdrum() */
int read_dev;

int getch(void)
{
    char *line = (char *)&moncard_[0];
    int ch;

    if (read_idx == 0) {
        ch = (unsigned)moncard_[0] >> 40;
        if (ch == '*') {
            /* End of input data. */
            return 0;
        }
        if (ch == 0) {
            /* Bad input data. */
            return 0;
        }

        /* Find end of line. */
        read_len = 79;
        while (read_len >= 0) {
            if (line[read_len] != ' ')
                goto done;
            --read_len;
        }
    done:
        ++read_len;
    }

    if (read_idx == read_len) {
        /* Save read pointer for readdrum(). */
        read_ptr = kcount_[0];
        read_dev = kcount_[1];

        /* Read next line. */
        monread_();
        read_idx = 0;
        return '\n';
    }

    ch = line[read_idx];
    ++read_idx;
    return ch;
}
