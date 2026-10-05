#define N 64
int a[N];

void sort(int *v, int n)
{
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1 - i; j++)
            if (v[j] > v[j + 1]) {
                int t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
}

int main(void)
{
    unsigned x = 12345;
    for (int i = 0; i < N; i++) {
        x = x * 25173 + 13849;
        a[i] = (int)x;
    }
    sort(a, N);
    for (int i = 0; i < N - 1; i++)
        if (a[i] > a[i + 1])
            return 1;
    return 0;
}
