char src[128], dst[128];

static void copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

static int compare(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int main(void)
{
    for (int i = 0; i < 127; i++)
        src[i] = (char)('a' + i % 26);
    src[127] = 0;
    int bad  = 0;
    for (int r = 0; r < 20; r++) {
        copy(dst, src);
        bad += compare(dst, src) != 0;
    }
    return bad;
}
