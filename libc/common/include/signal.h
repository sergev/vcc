/*
 * <signal.h> — signal handling (C11 §7.14), BESM-6 target.
 *
 * TODO: implement signal()/raise() in libc.bin.  The Dubna environment has no
 * POSIX signal delivery, so a real implementation may support only raise() of
 * synchronous conditions; the surface is provided for source portability.
 */
#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGINT  2
#define SIGILL  4
#define SIGABRT 6
#define SIGFPE  8
#define SIGSEGV 11
#define SIGTERM 15

void (*signal(int sig, void (*handler)(int)))(int);
int  raise(int sig);

#endif /* _SIGNAL_H */
