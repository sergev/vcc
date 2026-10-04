/*
 * The 64-bit multiply the code generator calls, by shifts and adds: a C multiply of
 * long long would call this very function.  Signed and unsigned agree in the low 64
 * bits of the product.
 */
unsigned long long __muldi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r = 0;
    while (b) {
        if (b & 1)
            r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}
