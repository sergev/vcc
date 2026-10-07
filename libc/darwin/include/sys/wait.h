/*
 * <sys/wait.h> — waiting for a child process, hosted macOS: libSystem's.
 */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG   1
#define WUNTRACED 2

#define _WSTATUS(x)    ((x) & 0177)
#define WIFEXITED(x)   (_WSTATUS(x) == 0)
#define WEXITSTATUS(x) (((x) >> 8) & 0xff)
#define WIFSIGNALED(x) (_WSTATUS(x) != 0177 && _WSTATUS(x) != 0)
#define WTERMSIG(x)    _WSTATUS(x)
#define WIFSTOPPED(x)  (_WSTATUS(x) == 0177 && (x) != 0x13)
#define WSTOPSIG(x)    ((x) >> 8)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif /* _SYS_WAIT_H */
