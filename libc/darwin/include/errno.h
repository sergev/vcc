/*
 * <errno.h> — errors (C11 §7.5), hosted macOS: libSystem's errno and the XNU numbers.
 */
#ifndef _ERRNO_H
#define _ERRNO_H

int *__error(void);
#define errno (*__error())

#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD       10
#define EDEADLK      11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define ENOTBLK      15
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOTTY       25
#define ETXTBSY      26
#define EFBIG        27
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EMLINK       31
#define EPIPE        32
#define EDOM         33
#define ERANGE       34
#define EAGAIN       35
#define EWOULDBLOCK  EAGAIN
#define EINPROGRESS  36
#define EALREADY     37
#define ENOTSOCK     38
#define ENOTSUP      45
#define EADDRINUSE   48
#define ECONNRESET   54
#define ETIMEDOUT    60
#define ECONNREFUSED 61
#define ELOOP        62
#define ENAMETOOLONG 63
#define ENOTEMPTY    66
#define ENOLCK       77
#define ENOSYS       78
#define EOVERFLOW    84
#define ECANCELED    89
#define ENOMSG       91
#define EILSEQ       92
#define EOPNOTSUPP   102

#endif /* _ERRNO_H */
