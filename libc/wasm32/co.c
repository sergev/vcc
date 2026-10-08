/*
 * The coroutine runtime (docs/Coroutines_in_C.md; backend/wasm/Plan.md §6).  The
 * compiler lowers a coroutine f to f$resume, the body as a state machine over a frame,
 * f$init, which stores its arguments there, and f$co, its frame size and alignment.
 * The frame starts with this header; the rest is the defining unit's business.
 *
 * A trap prints its name and ends the program with status 255.
 */
#include <stddef.h>

struct co_header {
    unsigned state;           /* 0 created; k >= 1 suspended at point k; DONE; DESTROYED */
    unsigned flags;           /* RUNNING, and the signal of the resumption, shifted by 1 */
    int (*resume)(void *);    /* f$resume */
    struct co_header *task;   /* the root frame of the task */
    char *top, *limit;        /* the root's arena */
};

enum {
    STATE_DONE      = 0xfffffffeu,
    STATE_DESTROYED = 0xffffffffu,
    RUNNING         = 1,
    SIGNAL_DESTROY  = 2,
};

void putbyte(int c);
_Noreturn void exit(int status);

_Noreturn static void trap(const char *name)
{
    for (const char *s = "coroutine trap: "; *s; s++)
        putbyte(*s);
    for (; *name; name++)
        putbyte(*name);
    putbyte('\n');
    exit(255);
}

/* co_init and co_alloca: a frame of desc[0] bytes aligned to desc[1] at the front of
   `bytes` of storage, the rest its arena. */
void *__coro_setup(void *storage, size_t bytes, const unsigned *desc, int (*resume)(void *))
{
    struct co_header *h = storage;
    if (((size_t)storage & (desc[1] - 1)) != 0 || bytes < desc[0])
        trap("CO_TRAP_STORAGE");
    h->state  = 0;
    h->flags  = 0;
    h->resume = resume;
    h->task   = h;
    h->top    = (char *)storage + desc[0];
    h->limit  = (char *)storage + bytes;
    return storage;
}

/* co_resume, co_cancel and co_destroy: signal 0, 1 and 2.  Returns the co_status. */
int __coro_resume(void *p, unsigned signal)
{
    struct co_header *h = p;
    if (h->state >= STATE_DONE)
        trap("CO_TRAP_FINISHED");
    if (h->flags & RUNNING)
        trap("CO_TRAP_REENTRANT");
    if (signal == SIGNAL_DESTROY && h->state == 0) {
        h->state = STATE_DESTROYED; /* never started: no defer to run */
        return 1;
    }
    h->flags   = RUNNING | signal << 1;
    int status = h->resume(h);
    h->flags   = 0;
    return status;
}

int __coro_done(void *p)
{
    return ((struct co_header *)p)->state >= STATE_DONE;
}

/* The address of the last value yielded, `off` bytes into the frame. */
void *__coro_value(void *p, unsigned off)
{
    unsigned state = ((struct co_header *)p)->state;
    if (state == 0 || state >= STATE_DONE)
        trap("CO_TRAP_NO_VALUE");
    return (char *)p + off;
}

/* The address of the result, `off` bytes into the frame. */
void *__coro_result(void *p, unsigned off)
{
    if (((struct co_header *)p)->state != STATE_DONE)
        trap("CO_TRAP_NOT_DONE");
    return (char *)p + off;
}
