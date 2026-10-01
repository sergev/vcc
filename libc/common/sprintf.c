/*
 * Formatted output into an unbounded caller buffer.
 *
 * Equivalent to snprintf with no size limit: writes the formatted string plus a
 * terminating NUL into buf and returns its length.  Shares the engine __doprnt
 * with printf (the target's doprnt.c).  A large nominal size stands in for
 * "unbounded".
 */
#include <stdio.h>

extern int __doprnt(const char *fmt, va_list ap, char *buf, int size, int to_buf);

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = __doprnt(fmt, ap, buf, 1 << 24, 1);
    va_end(ap);
    return n;
}
