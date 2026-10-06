static int a[300];

int main()
{
    for (int i = 0; i < 300; i++)
        a[i] = (i * 7919) % 300;

    for (int i = 0; i < 300; i++)
        for (int j = 0; j + 1 < 300 - i; j++)
            if (a[j] > a[j + 1]) {
                int t    = a[j];
                a[j]     = a[j + 1];
                a[j + 1] = t;
            }

    for (int i = 0; i + 1 < 300; i++)
        if (a[i] > a[i + 1])
            return 1;
}
