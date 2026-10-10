/*
 * Read one byte from stdin (Unix target).
 *
 * Unbuffered: each call issues a one-byte SYS_read.  &ch is the compiler's
 * char* fat pointer to the auto slot, which b6sim's read fills; the direct ch
 * read then round-trips that byte.  Returns the byte (0..255), or -1 at EOF.
 */
#include <stdio.h>

extern int read(int fd, char *buf, int n);

int getch(void)
{
    char ch;

    if (read(0, &ch, 1) <= 0)
        return -1; /* EOF */
    return ch & 0377;
}
