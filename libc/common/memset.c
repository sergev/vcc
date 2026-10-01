/*
 * memset — fill the first n bytes of s with the byte value c (C11 §7.24.6.1).
 *
 * c is converted to unsigned char before storing.
 */
#include <string.h>

void *memset(void *s, int c, size_t n)
{
    char *p = s;
    char b = (char)c;          /* value stored is (unsigned char)c */
    while (n > 0) {
        *p = b;
        p++;
        n--;
    }
    return s;
}
