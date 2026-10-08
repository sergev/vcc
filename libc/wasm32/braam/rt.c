/*
 * The Braam process runtime (backend/wasm/Plan.md §7.2), as braam-core's src/proc/rt.cpp
 * is in C++: one root task, a coroutine that awaits main and then flushes what main
 * left buffered.  Each step resumes it; when it suspends, the braam_call it yielded
 * goes to the kernel with a token of its own, and _resume brings the reply back to it.
 * The exports themselves are in exports.s.
 */
#include <braam.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

coro(braam_call *) int main(int argc, char **argv);
void __braam_sys_async(unsigned op, unsigned token, const void *ptr, unsigned len);

/* The size of the block for the root's frame and the arena of everything main awaits:
   64 KiB (taskbytes.c), or the program's own definition. */
extern const unsigned __braam_task_bytes;

static coro(braam_call *) int braam_root(int argc, char **argv)
{
    int status = await main(argc, argv);
    await fflush(NULL);
    return status;
}

static co_frame(braam_call *, int) *root;
static braam_call *pending; /* the call the kernel will answer, or NULL */
static unsigned token;
static int exited;
static unsigned signals;

/* 0 = exited, 1 = suspended: what _start and _resume return. */
static int step(void)
{
    if (co_resume(root) == CO_DONE) {
        exited = 1;
        braam_sys_sync(BRAAM_SYS_EXIT, (unsigned)co_result(root), 0, 0);
        return 0;
    }
    braam_call *c = co_value(root);
    c->token      = ++token;
    pending       = c;
    __braam_sys_async(c->op, c->token, c->ptr, c->len);
    return 1;
}

static unsigned get_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

/* argv from the blob the host placed: u32 argc, then u32 length and the bytes of each
   word.  The words are copied, each with its NUL; the environment after them is not
   read yet. */
static char **parse_argv(const unsigned char *blob, unsigned len, int *argc)
{
    unsigned n = len >= 4 ? get_u32(blob) : 0, at = 4, bytes = 0;
    unsigned k;
    for (k = 0; k < n && at + 4 <= len; k++) {
        unsigned size = get_u32(blob + at);
        if (size > len - at - 4)
            break;
        at += 4 + size;
        bytes += size + 1;
    }
    n           = k;
    char **argv = malloc((n + 1) * sizeof(char *) + bytes);
    if (!argv) {
        *argc = 0;
        return NULL;
    }
    char *to = (char *)(argv + n + 1);
    at       = 4;
    for (k = 0; k < n; k++) {
        unsigned size = get_u32(blob + at);
        memcpy(to, blob + at + 4, size);
        to[size] = 0;
        argv[k]  = to;
        to += size + 1;
        at += 4 + size;
    }
    argv[n] = NULL;
    *argc   = (int)n;
    return argv;
}

int __braam_start(unsigned blob, unsigned len)
{
    if (root)
        return exited ? 0 : 1;
    int argc;
    char **argv   = parse_argv((const unsigned char *)blob, len, &argc);
    void *storage = malloc(__braam_task_bytes); /* 16-aligned */
    if (!storage) {
        exited = 1;
        braam_sys_sync(BRAAM_SYS_EXIT, 1, 0, 0);
        return 0;
    }
    root = co_init(storage, __braam_task_bytes, braam_root, argc, argv);
    return step();
}

int __braam_resume(unsigned tok, unsigned reply, unsigned len)
{
    if (!pending || pending->token != tok) {
        /* An answer nobody waits for: its call went with a destroyed frame. */
        free((void *)reply);
        return exited ? 0 : 1;
    }
    pending->reply     = (void *)reply;
    pending->reply_len = len;
    pending            = NULL;
    return step();
}

void __braam_signal(unsigned n)
{
    signals |= 1u << (n & 31);
}

/* A call whose coroutine is gone, answered or not. */
void __braam_forget(braam_call *c)
{
    if (pending == c)
        pending = NULL;
}
