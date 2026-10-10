/*
 * <sys/types.h> — POSIX data types, hosted macOS on Apple silicon: libSystem's.
 */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

typedef long ssize_t;
typedef long long off_t;
typedef int pid_t;
typedef unsigned short mode_t;
typedef int dev_t;
typedef unsigned long long ino_t;
typedef unsigned short nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long long blkcnt_t;
typedef int blksize_t;

#endif /* _SYS_TYPES_H */
