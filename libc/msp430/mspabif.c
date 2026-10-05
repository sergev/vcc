/*
 * The MSP430 EABI helper names GCC's and clang's code calls for binary32 floating
 * point and its conversions to integers, over the libgcc-named runtime this compiler's
 * own code calls (libc/common/float32.c, libc/ilp32/int64conv.c).  These take the ordinary ABI; the ones with a 64-bit first
 * operand in r8-r11 are in mspabi64.s.  Binary32 and binary64 are apart, so that a
 * program takes in only the runtime it uses.
 *
 * __mspabi_cmpf returns -1, 0 or 1, and 1 for an unordered pair: clang tests that one
 * result against zero for every comparison, so for a NaN its `>` and `>=` come out
 * true -- clang's choice, not repaired here.  GCC's code and this compiler's call the
 * libgcc predicates, which answer every comparison right.
 */
float __addsf3(float, float);
float __subsf3(float, float);
float __mulsf3(float, float);
float __divsf3(float, float);
signed char __ltsf2(float, float);
long __fixsfsi(float);
unsigned long __fixunssfsi(float);
long long __fixsfdi(float);
unsigned long long __fixunssfdi(float);
float __floatsisf(long);
float __floatunsisf(unsigned long);
float __floatdisf(long long);
float __floatundisf(unsigned long long);

float __mspabi_addf(float a, float b)
{
    return __addsf3(a, b);
}

float __mspabi_subf(float a, float b)
{
    return __subsf3(a, b);
}

float __mspabi_mpyf(float a, float b)
{
    return __mulsf3(a, b);
}

float __mspabi_divf(float a, float b)
{
    return __divsf3(a, b);
}

int __mspabi_cmpf(float a, float b)
{
    return __ltsf2(a, b);
}

long __mspabi_fixfli(float f)
{
    return __fixsfsi(f);
}

unsigned long __mspabi_fixful(float f)
{
    return __fixunssfsi(f);
}

long long __mspabi_fixflli(float f)
{
    return __fixsfdi(f);
}

unsigned long long __mspabi_fixfull(float f)
{
    return __fixunssfdi(f);
}

float __mspabi_fltlif(long i)
{
    return __floatsisf(i);
}

float __mspabi_fltulf(unsigned long u)
{
    return __floatunsisf(u);
}

float __mspabi_fltllif(long long i)
{
    return __floatdisf(i);
}

float __mspabi_fltullf(unsigned long long u)
{
    return __floatundisf(u);
}

/* An int, GCC's 16-bit conversions. */
float __mspabi_fltif(int i)
{
    return __floatsisf(i);
}

float __mspabi_fltuf(unsigned u)
{
    return __floatunsisf(u);
}
