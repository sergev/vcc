/*
 * memcpy — copy n bytes from src to dest (C11 §7.24.2.1).
 *
 * The objects must not overlap; use memmove for overlapping regions.
 */
#include <string.h>

void *memcpy(void *dest, const void *src, size_t n)
{
    char *d = dest;
    const char *s = src;
    while (n > 0) {
        *d = *s;
        d++;
        s++;
        n--;
    }
    return dest;
}
