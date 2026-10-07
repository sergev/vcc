/*
 * <unistd.h> — POSIX system interface, hosted Linux: glibc's.
 */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int     access(const char *path, int mode);
int     chdir(const char *path);
int     close(int fd);
int     dup(int fd);
int     dup2(int fd, int fd2);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);
_Noreturn void _exit(int status);
pid_t   fork(void);
char   *getcwd(char *buf, size_t size);
pid_t   getpid(void);
int     isatty(int fd);
off_t   lseek(int fd, off_t offset, int whence);
int     pipe(int fds[2]);
ssize_t read(int fd, void *buf, size_t n);
ssize_t readlink(const char *path, char *buf, size_t size);
int     rmdir(const char *path);
unsigned sleep(unsigned seconds);
int     unlink(const char *path);
ssize_t write(int fd, const void *buf, size_t n);

extern char *optarg;
extern int   optind, opterr, optopt;
int          getopt(int argc, char *const argv[], const char *optstring);

#endif /* _UNISTD_H */
