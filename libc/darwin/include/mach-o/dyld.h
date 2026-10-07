/*
 * <mach-o/dyld.h> — the dynamic linker, hosted macOS: the one call vcc makes.
 */
#ifndef _MACH_O_DYLD_H
#define _MACH_O_DYLD_H

#include <stdint.h>

int _NSGetExecutablePath(char *buf, uint32_t *bufsize);

#endif /* _MACH_O_DYLD_H */
