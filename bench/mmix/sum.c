static int a[10000];

int main(void)
{
    for (int i = 0; i < 10000; i++)
        a[i] = i;

    long s = 0;
    for (int r = 0; r < 10; r++)
        for (int i = 0; i < 10000; i++)
            s += a[i];

    return s == 499950000L ? 0 : 1;
}
