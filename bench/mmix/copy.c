static char src[10000], dst[10000];
int main(void)
{
    for (int i = 0; i < 9999; i++)
        src[i] = 'a' + i % 26;
    long n = 0;
    for (int r = 0; r < 10; r++) {
        char *d = dst;
        const char *s = src;
        while ((*d++ = *s++) != 0)
            n++;
    }
    return n == 99990 ? 0 : 1;
}
