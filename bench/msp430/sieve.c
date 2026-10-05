#define N 2000
char flags[N + 1];

int main(void)
{
    int count = 0;
    for (int i = 2; i <= N; i++)
        flags[i] = 1;
    for (int i = 2; i <= N; i++)
        if (flags[i]) {
            count++;
            for (int k = i + i; k <= N; k += i)
                flags[k] = 0;
        }
    return count != 303;
}
