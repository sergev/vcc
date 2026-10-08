/*
 * The streams of Braam's stdio (backend/wasm/Plan.md §7.3): a buffer each, which the
 * formatting functions fill without blocking and fflush writes out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static FILE in_file = { 0 }, out_file = { 1 }, err_file = { 2 };
FILE *stdin  = &in_file;
FILE *stdout = &out_file;
FILE *stderr = &err_file;

int fputc(int c, FILE *f)
{
    if (f->len == f->cap) {
        size_t cap = f->cap ? 2 * f->cap : 256;
        char *buf  = realloc(f->buf, cap);
        if (!buf)
            return EOF;
        f->buf = buf;
        f->cap = cap;
    }
    f->buf[f->len++] = (char)c;
    return (unsigned char)c;
}

int fputs(const char *s, FILE *f)
{
    for (; *s; s++)
        if (fputc(*s, f) == EOF)
            return EOF;
    return 0;
}

/* Where putbyte writes: stdout, or the stream of the fprintf running. */
static FILE *current = &out_file;

void putbyte(int b)
{
    fputc(b, current);
}

void putch(unsigned c)
{
    fputc((int)c, current);
}

extern int __doprnt(const char *fmt, va_list ap, char *buf, int size, int to_buf);

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    FILE *was = current;
    current   = f;
    int n     = __doprnt(fmt, ap, 0, 0, 0);
    current   = was;
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

void flush(void)
{
}

coro(braam_call *) int fflush(FILE *f)
{
    if (!f) {
        int a = await fflush(stdout);
        int b = await fflush(stderr);
        return a == EOF || b == EOF ? EOF : 0;
    }
    if (f->len == 0)
        return 0;
    ssize_t n = await write(f->fd, f->buf, f->len);
    f->len    = 0;
    return n < 0 ? EOF : 0;
}
