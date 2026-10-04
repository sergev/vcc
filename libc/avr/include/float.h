/*
 * <float.h> — characteristics of floating types (C11 §5.2.4.2.2), AVR.
 *
 * float, double and long double are all IEEE 754 binary32, in software, as clang's
 * and avr-gcc's default for the target.
 */
#ifndef _FLOAT_H
#define _FLOAT_H

#define FLT_RADIX       2
#define FLT_ROUNDS      1
#define FLT_EVAL_METHOD 0
#define DECIMAL_DIG     9

#define FLT_MANT_DIG     24
#define FLT_DECIMAL_DIG  9
#define FLT_DIG          6
#define FLT_MIN_EXP      (-125)
#define FLT_MIN_10_EXP   (-37)
#define FLT_MAX_EXP      128
#define FLT_MAX_10_EXP   38
#define FLT_MAX          3.40282347e+38F
#define FLT_EPSILON      1.19209290e-7F
#define FLT_MIN          1.17549435e-38F
#define FLT_TRUE_MIN     1.40129846e-45F
#define FLT_HAS_SUBNORM  1

#define DBL_MANT_DIG     24
#define DBL_DECIMAL_DIG  9
#define DBL_DIG          6
#define DBL_MIN_EXP      (-125)
#define DBL_MIN_10_EXP   (-37)
#define DBL_MAX_EXP      128
#define DBL_MAX_10_EXP   38
#define DBL_MAX          3.40282347e+38
#define DBL_EPSILON      1.19209290e-7
#define DBL_MIN          1.17549435e-38
#define DBL_TRUE_MIN     1.40129846e-45
#define DBL_HAS_SUBNORM  1

#define LDBL_MANT_DIG    24
#define LDBL_DECIMAL_DIG 9
#define LDBL_DIG         6
#define LDBL_MIN_EXP     (-125)
#define LDBL_MIN_10_EXP  (-37)
#define LDBL_MAX_EXP     128
#define LDBL_MAX_10_EXP  38
#define LDBL_MAX         3.40282347e+38L
#define LDBL_EPSILON     1.19209290e-7L
#define LDBL_MIN         1.17549435e-38L
#define LDBL_TRUE_MIN    1.40129846e-45L
#define LDBL_HAS_SUBNORM 1

#endif /* _FLOAT_H */
