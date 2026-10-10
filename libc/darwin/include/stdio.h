/*
 * <stdio.h> — input/output (C11 §7.21), hosted macOS: libSystem's, whose streams are
 * __stdinp, __stdoutp and __stderrp.
 */
#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct __sFILE FILE;

typedef long long fpos_t;

#define EOF          (-1)
#define BUFSIZ       1024
#define FOPEN_MAX    20
#define FILENAME_MAX 1024
#define L_tmpnam     1024
#define TMP_MAX      308915776

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin  __stdinp
#define stdout __stdoutp
#define stderr __stderrp

int remove(const char *path);
int rename(const char *old, const char *new);
FILE *tmpfile(void);
char *tmpnam(char *s);

int fclose(FILE *stream);
int fflush(FILE *stream);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

int fprintf(FILE *stream, const char *fmt, ...);
int fscanf(FILE *stream, const char *fmt, ...);
int printf(const char *fmt, ...);
int scanf(const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vfscanf(FILE *stream, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int vscanf(const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsscanf(const char *str, const char *fmt, va_list ap);

int fgetc(FILE *stream);
char *fgets(char *s, int n, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int getc(FILE *stream);
int getchar(void);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
int ungetc(int c, FILE *stream);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fgetpos(FILE *stream, fpos_t *pos);
int fseek(FILE *stream, long offset, int whence);
int fsetpos(FILE *stream, const fpos_t *pos);
long ftell(FILE *stream);
void rewind(FILE *stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

/* POSIX and BSD */
FILE *fdopen(int fd, const char *mode);
int fileno(FILE *stream);
FILE *open_memstream(char **bufp, size_t *sizep);
int pclose(FILE *stream);
FILE *popen(const char *command, const char *mode);
void setbuffer(FILE *stream, char *buf, int size);

#endif /* _STDIO_H */
