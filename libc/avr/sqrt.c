/*
 * sqrt — the square root (C11 §7.12.7.5).  On AVR a double is binary32, so it is
 * float32.c's correctly rounded sqrtf.
 */
float sqrtf(float x);

double sqrt(double x)
{
    return sqrtf(x);
}
