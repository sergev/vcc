#define N 1024
unsigned char buf[N];

unsigned crc16(const unsigned char *p, int n)
{
    unsigned crc = 0xffff;
    while (n-- > 0) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            if (crc & 1)
                crc = (crc >> 1) ^ 0xa001;
            else
                crc >>= 1;
    }
    return crc;
}

int main(void)
{
    for (int i = 0; i < N; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    return crc16(buf, N) == 0 ? 1 : 0;
}
