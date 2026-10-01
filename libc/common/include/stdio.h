/*
 * <stdio.h> — input/output (C11 §7.21), BESM-6 target.
 *
 * Status: printf / sprintf / snprintf are implemented in libc.bin; the rest are
 * declared for future implementation (marked TODO).  The BESM-6 extensions block
 * at the bottom exposes the real low-level console primitives in the runtime.
 *
 * NOTE on the printf family.  The libc *definitions* read their variadic data
 * through an explicit `int args` slot (a holdover from before <stdarg.h>), but
 * callers use the ordinary ISO variadic signatures below: on the BESM-6 ABI the
 * first variadic argument simply lands in that `args` slot, so the two are
 * binary-compatible (this is how every existing run-test calls printf).
 *
 * Remember: char* / void* are FAT pointers on BESM-6 (see Besm6_Data_Representation).
 */
#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

/* Opaque file handle (no buffered file layer yet). */
typedef struct __FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* ---- implemented in libc.bin ---- */
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, int size, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);

/* ---- declared for future implementation (TODO) ---- */
int   fprintf(FILE *stream, const char *fmt, ...);
int   vprintf(const char *fmt, va_list ap);
int   vfprintf(FILE *stream, const char *fmt, va_list ap);
int   vsprintf(char *buf, const char *fmt, va_list ap);
int   vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int   scanf(const char *fmt, ...);
int   sscanf(const char *str, const char *fmt, ...);
int   fscanf(FILE *stream, const char *fmt, ...);

int   getchar(void);
int   fputs(const char *s, FILE *stream);
int   fputc(int c, FILE *stream);
int   fgetc(FILE *stream);
char *fgets(char *s, int n, FILE *stream);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *stream);
int   fflush(FILE *stream);
void  perror(const char *s);

/* ---- BESM-6 runtime extensions (implemented in libc.bin) ---- */
/*
 * Low-level console I/O.  putbyte buffers a KOI7 byte; putch folds a character
 * for the device; getch reads one byte; flush forces the output buffer out.
 */
void putbyte(int b);
void putch(unsigned ch);
int  getch(void);
void flush(void);

#endif /* _STDIO_H */
