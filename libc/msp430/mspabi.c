/*
 * The MSP430 EABI helper names clang's code calls for floating point and its
 * conversions, over the libgcc-named runtime this compiler's own code calls
 * (libc/common/float32.c, float64.c, libc/ilp32/int64.c).  These take the ordinary ABI;
 * the ones with a 64-bit first operand in r8-r11 are in mspabi64.s.
 *
 * __mspabi_cmpf (and __mspabi_cmpd) return -1, 0 or 1, and 1 for an unordered pair:
 * clang tests that one result against zero for every comparison, so for a NaN its
 * `>` and `>=` come out true -- clang's choice, not repaired here.  This compiler's
 * own code calls the libgcc predicates, which answer every comparison right.
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

double __mspabi_cvtfd(float f)
{
    return __extendsfdf2(f);
}

float __mspabi_cvtdf(double d)
{
    return __truncdfsf2(d);
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
