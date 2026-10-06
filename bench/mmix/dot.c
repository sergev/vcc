static double x[5000], y[5000];
int main(void)
{
    for (int i = 0; i < 5000; i++) {
        x[i] = i * 0.5;
        y[i] = 2.0;
    }
    double s = 0;
    for (int r = 0; r < 10; r++)
        for (int i = 0; i < 5000; i++)
            s += x[i] * y[i];
    return s == 124975000.0 ? 0 : 1;
}
