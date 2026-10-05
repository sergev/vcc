/*
 * libc/common/float64.c compiled for the host, its names prefixed so that they do not
 * stand in for the host's own runtime: float64_tests.cpp checks it against the host's
 * double, bit for bit.
 */
#define __adddf3      f64_adddf3
#define __subdf3      f64_subdf3
#define __muldf3      f64_muldf3
#define __divdf3      f64_divdf3
#define sqrt          f64_sqrt
#define __eqdf2       f64_eqdf2
#define __nedf2       f64_nedf2
#define __ltdf2       f64_ltdf2
#define __ledf2       f64_ledf2
#define __gtdf2       f64_gtdf2
#define __gedf2       f64_gedf2
#define __unorddf2    f64_unorddf2
#define __fixdfsi     f64_fixdfsi
#define __fixunsdfsi  f64_fixunsdfsi
#define __floatsidf   f64_floatsidf
#define __floatunsidf f64_floatunsidf
#define __extendsfdf2 f64_extendsfdf2
#define __truncdfsf2  f64_truncdfsf2
#include "../../../libc/common/float64.c"
