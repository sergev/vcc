/*
 * The binary32 square root, through binary64's: the double root of a float, rounded
 * once more to float, is the correctly rounded float root, since 53 >= 2 * 24 + 2.
 */
double sqrt(double x);

float sqrtf(float x)
{
    return (float)sqrt(x);
}
