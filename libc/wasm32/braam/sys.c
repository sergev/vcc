/*
 * The asynchronous system calls of Braam (backend/wasm/Plan.md §7.3): braam_sys, the
 * one primitive, and the descriptor calls on it, each a coroutine yielding braam_call *.
 */
#include <braam.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int errno;

void __braam_forget(braam_call *c);

coro(braam_call *) int braam_sys(unsigned op, const void *payload, unsigned len, void *out,
                                 unsigned cap, unsigned *got)
{
    braam_call c = { op, 0, payload, len, NULL, 0 };
    defer __braam_forget(&c); /* destroyed while waiting: the reply goes unclaimed */
    yield &c;
    int status = -8; /* Io: a reply too short for its status */
    unsigned n = 0;
    if (c.reply_len >= 4) {
        const unsigned char *r = c.reply;
        status = (int)(r[0] | r[1] << 8 | r[2] << 16 | (unsigned)r[3] << 24);
        n      = c.reply_len - 4 < cap ? c.reply_len - 4 : cap;
        memcpy(out, r + 4, n);
    }
    free(c.reply);
    if (got)
        *got = n;
    return status;
}

unsigned braam_now(void)
{
    return (unsigned)braam_sys_sync(BRAAM_SYS_NOW, 0, 0, 0);
}

/* A Braam error as errno, and -1. */
static int failed(int status)
{
    errno = 32 - status;
    return -1;
}

/* A read of standard input flushes standard output first, as a terminal's line
   discipline would: a prompt is out before the answer is asked for. */
coro(braam_call *) ssize_t read(int fd, void *buf, size_t n)
{
    if (fd == 0)
        await fflush(stdout);
    if (n == 0)
        return 0;
    unsigned max = n > BRAAM_READ_MAX ? BRAAM_READ_MAX : (unsigned)n;
    unsigned got;
    int st = await braam_sys(BRAAM_SYS_OP(BRAAM_SYS_READ, fd), &max, 4, buf, max, &got);
    return st < 0 ? failed(st) : (ssize_t)got;
}

coro(braam_call *) ssize_t write(int fd, const void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        unsigned chunk = n - done > BRAAM_READ_MAX ? BRAAM_READ_MAX : (unsigned)(n - done);
        int st = await braam_sys(BRAAM_SYS_OP(BRAAM_SYS_WRITE, fd), (const char *)buf + done,
                                 chunk, NULL, 0, NULL);
        if (st < 0)
            return failed(st);
        if (st == 0)
            break;
        done += (unsigned)st;
    }
    return (ssize_t)done;
}

coro(braam_call *) int open(const char *path, int flags)
{
    int st = await braam_sys(BRAAM_SYS_OP(BRAAM_SYS_OPEN, flags), path, strlen(path), NULL, 0,
                             NULL);
    return st < 0 ? failed(st) : st;
}

coro(braam_call *) int close(int fd)
{
    int st = await braam_sys(BRAAM_SYS_OP(BRAAM_SYS_CLOSE, fd), NULL, 0, NULL, 0, NULL);
    return st < 0 ? failed(st) : 0;
}

coro(braam_call *) int sleep_ms(unsigned ms)
{
    int st = await braam_sys(BRAAM_SYS_SLEEP, &ms, 4, NULL, 0, NULL);
    return st < 0 ? failed(st) : 0;
}
