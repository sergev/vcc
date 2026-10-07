/*
 * <sys/wait.h> — waiting for a child process, hosted Linux: glibc's.
 */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG   1
#define WUNTRACED 2

#define WEXITSTATUS(x) (((x) & 0xff00) >> 8)
#define WTERMSIG(x)    ((x) & 0x7f)
#define WSTOPSIG(x)    WEXITSTATUS(x)
#define WIFEXITED(x)   (WTERMSIG(x) == 0)
#define WIFSIGNALED(x) (((signed char)(((x) & 0x7f) + 1) >> 1) > 0)
#define WIFSTOPPED(x)  (((x) & 0xff) == 0x7f)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif /* _SYS_WAIT_H */
