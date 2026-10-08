/*
 * <unistd.h> for Braam: the descriptor calls, coroutines yielding braam_call *, so a
 * program calls them with await (backend/wasm/Plan.md §7.3).  On an error they return
 * -1 and set errno.
 */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <braam.h>
#include <stddef.h>

typedef int ssize_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Up to n bytes; 0 at the end of the input. */
coro(braam_call *) ssize_t read(int fd, void *buf, size_t n);
/* All n bytes, unless an error stops it. */
coro(braam_call *) ssize_t write(int fd, const void *buf, size_t n);
coro(braam_call *) int close(int fd);

#endif /* _UNISTD_H */
