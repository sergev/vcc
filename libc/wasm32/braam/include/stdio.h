/*
 * <stdio.h> for Braam (backend/wasm/Plan.md §7.3).  The formatting half is libc/common's
 * and never blocks: printf, puts, putchar, fputs and fputc append to the stream's
 * buffer, which grows rather than waits.  The blocking half is a coroutine yielding
 * braam_call *: fflush writes a buffer out.  The runtime flushes stdout and stderr when
 * main returns; exit() cannot (stdlib.h).
 */
#ifndef _STDIO_H
#define _STDIO_H

#include <braam.h>
#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct __FILE {
    int fd;
    char *buf; /* what is waiting to be written */
    size_t len, cap;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Formatting, into stdout's buffer. */
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, int size, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);

/* Into a stream's buffer. */
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);

/* Write the buffer out: 0, or EOF with errno set.  NULL flushes stdout and stderr. */
coro(braam_call *) int fflush(FILE *stream);

/* The runtime's console primitives: putbyte and putch append a byte to stdout's
   buffer; flush does nothing (a write is a syscall, so only fflush may make one). */
void putbyte(int b);
void putch(unsigned ch);
void flush(void);

#endif /* _STDIO_H */
