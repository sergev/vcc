/*
 * <float.h> — characteristics of floating types (C11 §7.7), MSP430.
 *
 * float and double are IEEE-754 binary32 and binary64, both in software; long double
 * is binary64 too (clang's choice for the target), so every LDBL_ value equals its
 * DBL_ one.
 */
#ifndef _FLOAT_H
#define _FLOAT_H

#define FLT_RADIX       2
#define FLT_ROUNDS      1
#define FLT_EVAL_METHOD 0
#define DECIMAL_DIG     17

#define FLT_MANT_DIG  24
#define DBL_MANT_DIG  53
#define LDBL_MANT_DIG 53

#define FLT_DECIMAL_DIG  9
#define DBL_DECIMAL_DIG  17
#define LDBL_DECIMAL_DIG 17

#define FLT_DIG  6
#define DBL_DIG  15
#define LDBL_DIG 15

#define FLT_MIN_EXP  (-125)
#define DBL_MIN_EXP  (-1021)
#define LDBL_MIN_EXP (-1021)
#define FLT_MAX_EXP  128
#define DBL_MAX_EXP  1024
#define LDBL_MAX_EXP 1024

#define FLT_MIN_10_EXP  (-37)
#define DBL_MIN_10_EXP  (-307)
#define LDBL_MIN_10_EXP (-307)
#define FLT_MAX_10_EXP  38
#define DBL_MAX_10_EXP  308
#define LDBL_MAX_10_EXP 308

#define FLT_EPSILON  1.19209290e-7F
#define DBL_EPSILON  2.2204460492503131e-16
#define LDBL_EPSILON 2.2204460492503131e-16L

#define FLT_MIN  1.17549435e-38F
#define DBL_MIN  2.2250738585072014e-308
#define LDBL_MIN 2.2250738585072014e-308L

#define FLT_TRUE_MIN  1.40129846e-45F
#define DBL_TRUE_MIN  4.9406564584124654e-324
#define LDBL_TRUE_MIN 4.9406564584124654e-324L

#define FLT_MAX  3.40282347e+38F
#define DBL_MAX  1.7976931348623157e+308
#define LDBL_MAX 1.7976931348623157e+308L

#endif /* _FLOAT_H */
