/*
 * The MSP430 EABI helper names GCC's and clang's code calls for binary64 floating
 * point, its conversions to integers and to and from binary32, over the libgcc-named
 * runtime this compiler's own code calls (libc/common/float64.c, float32.c,
 * libc/ilp32/int64conv.c).  These take the ordinary ABI; the ones with a 64-bit first
 * operand in r8-r11 are in mspabi64.s.  Binary32 and binary64 are apart, so that a
 * program takes in only the runtime it uses.
 *
 * __mspabi_cmpd, the one comparison clang calls, is in mspabi64.s.
 */
double __extendsfdf2(float);
float __truncdfsf2(double);
long __fixdfsi(double);
unsigned long __fixunsdfsi(double);
long long __fixdfdi(double);
unsigned long long __fixunsdfdi(double);
double __floatsidf(long);
double __floatunsidf(unsigned long);
double __floatdidf(long long);
double __floatundidf(unsigned long long);

double __mspabi_cvtfd(float f)
{
    return __extendsfdf2(f);
}

float __mspabi_cvtdf(double d)
{
    return __truncdfsf2(d);
}

long __mspabi_fixdli(double d)
{
    return __fixdfsi(d);
}

unsigned long __mspabi_fixdul(double d)
{
    return __fixunsdfsi(d);
}

long long __mspabi_fixdlli(double d)
{
    return __fixdfdi(d);
}

unsigned long long __mspabi_fixdull(double d)
{
    return __fixunsdfdi(d);
}

double __mspabi_fltlid(long i)
{
    return __floatsidf(i);
}

double __mspabi_fltuld(unsigned long u)
{
    return __floatunsidf(u);
}

double __mspabi_fltllid(long long i)
{
    return __floatdidf(i);
}

double __mspabi_fltulld(unsigned long long u)
{
    return __floatundidf(u);
}

/* An int, GCC's 16-bit conversions. */
double __mspabi_fltid(int i)
{
    return __floatsidf(i);
}

double __mspabi_fltud(unsigned u)
{
    return __floatunsidf(u);
}
