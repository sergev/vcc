//
// AVR programs, and the runtime itself, on bare-metal qemu.  The runtime tests are
// written in C for clang, whose code calls the helpers of libc.a; the expected output
// is computed here on the host.
//
#include <cstdint>

#include "avr_test.h"

// main's result comes back on USART1, and the run ends with it.
TEST_F(AvrTest, RunReturn200)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr("int main(void) { return 200; }"));
    EXPECT_EQ(200, exit_status);
}

// The status is the low byte of a 16-bit int.
TEST_F(AvrTest, RunReturnWideConstant)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr("int main(void) { return 0x1234; }"));
    EXPECT_EQ(0x34, exit_status);
}

TEST_F(AvrTest, RunBookStatus)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
    EXPECT_EQ(249, exit_status);
}

TEST_F(AvrTest, RunBookStatusMax)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("32767\n", CompileAndRunBook("int main(void) { return 32767; }"));
}

TEST_F(AvrTest, RunBookStatusMin)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("-32768\n", CompileAndRunBook("int main(void) { return -32767 - 1; }"));
}

// The output routine the runtime tests share.
static const char print_c[] = R"(
void putbyte(int c);
static void puts_(const char *s) { while (*s) putbyte(*s++); }
static void putu(unsigned long v)
{
    char b[12];
    int i = 0;
    do {
        b[i++] = '0' + v % 10u;
        v /= 10u;
    } while (v);
    while (i)
        putbyte(b[--i]);
}
static void puti(long v)
{
    if (v < 0) {
        putbyte('-');
        putu(-(unsigned long)v);
    } else {
        putu(v);
    }
}
)";

// crt0 copies .data (and .rodata) from flash and clears .bss.
TEST_F(AvrTest, RuntimeDataAndBss)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = std::string(print_c) + R"(
int data[3] = { 1000, -2, 3 };
const char text[] = "flash";
long bss[20];
int main(void)
{
    long sum = 0;
    for (int i = 0; i < 20; i++)
        sum += bss[i];
    puti(data[0] + data[1] + data[2] + sum);
    putbyte(' ');
    puts_(text);
    return 0;
}
)";
    EXPECT_EQ("1001 flash", ClangRun(src));
    EXPECT_EQ(0, exit_status);
}

static const int16_t ops16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
static const int32_t ops32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                                 0, 1, -1, 123456789, 65536, -65537 };

// Division and remainder through __divmodhi4, __udivmodhi4, __divmodsi4 and
// __udivmodsi4, and products through __mulsi3, over operands with every sign and the
// extremes; the most negative dividend over -1 and zero divisors are left out.
TEST_F(AvrTest, RuntimeDivisionAndMultiplication)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = std::string(print_c) + R"(
volatile int a16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
volatile long a32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                        0, 1, -1, 123456789, 65536, -65537 };
int main(void)
{
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int x = a16[i], y = a16[j];
            if (y != 0 && !(x == -32768 && y == -1)) {
                puti(x / y); putbyte(' '); puti(x % y); putbyte(' ');
            }
            if (y != 0) {
                putu((unsigned)x / (unsigned)y); putbyte(' ');
                putu((unsigned)x % (unsigned)y); putbyte(' ');
            }
            long p = a32[i], q = a32[j];
            if (q != 0 && !(p == -2147483647 - 1 && q == -1)) {
                puti(p / q); putbyte(' '); puti(p % q); putbyte(' ');
            }
            if (q != 0) {
                putu((unsigned long)p / (unsigned long)q); putbyte(' ');
                putu((unsigned long)p % (unsigned long)q); putbyte(' ');
            }
            puti((long)((unsigned long)p * (unsigned long)q));
            putbyte('\n');
        }
    return 0;
}
)";
    std::string expected;
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int16_t x = ops16[i], y = ops16[j];
            if (y != 0 && !(x == -32768 && y == -1))
                expected += std::to_string(x / y) + " " + std::to_string(x % y) + " ";
            if (y != 0)
                expected += std::to_string((uint16_t)x / (uint16_t)y) + " " +
                            std::to_string((uint16_t)x % (uint16_t)y) + " ";
            int32_t p = ops32[i], q = ops32[j];
            if (q != 0 && !(p == INT32_MIN && q == -1))
                expected += std::to_string(p / q) + " " + std::to_string(p % q) + " ";
            if (q != 0)
                expected += std::to_string((uint32_t)p / (uint32_t)q) + " " +
                            std::to_string((uint32_t)p % (uint32_t)q) + " ";
            expected += std::to_string((int32_t)((uint32_t)p * (uint32_t)q)) + "\n";
        }
    EXPECT_EQ(expected, ClangRun(src));
    EXPECT_EQ(0, exit_status);
}

// The 8-bit helpers, which clang does not call, against the host on every operand pair:
// "su" says no signed and no unsigned mismatch.
TEST_F(AvrTest, RuntimeDivision8Exhaustive)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = std::string(print_c) + R"(
unsigned qdiv(signed char a, signed char b);
unsigned uqdiv(unsigned char a, unsigned char b);
int main(void)
{
    int bad_s = 0, bad_u = 0;
    for (int a = 0; a < 256; a++)
        for (int b = 1; b < 256; b++) {
            signed char x = a, y = b;
            if (!(x == -128 && y == -1)) {
                unsigned e = (unsigned char)(x / y) | (unsigned)(unsigned char)(x % y) << 8;
                if (qdiv(x, y) != e) {
                    puti(a); putbyte('/'); puti(b); putbyte('\n');
                    bad_s = 1;
                }
            }
            unsigned f = (unsigned char)(a / b) | (unsigned)(a % b) << 8;
            if (uqdiv(a, b) != f) {
                puti(a); putbyte('%'); puti(b); putbyte('\n');
                bad_u = 1;
            }
        }
    putbyte(bad_s ? 'S' : 's');
    putbyte(bad_u ? 'U' : 'u');
    return 0;
}
)";
    // The quotient comes back in r24, the remainder in r25: an int, r25:r24.
    EXPECT_EQ("su", ClangRun(src, R"(    .text
    .globl  qdiv
qdiv:
    rjmp    __divmodqi4
    .globl  uqdiv
uqdiv:
    rjmp    __udivmodqi4
)"));
}

// malloc, realloc (which copies) and calloc (which clears, and refuses an overflowing
// size); a request the heap cannot meet below the stack is refused.
TEST_F(AvrTest, RuntimeMalloc)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = std::string(print_c) + R"(
void *malloc(unsigned n);
void *calloc(unsigned n, unsigned size);
void *realloc(void *p, unsigned n);
int main(void)
{
    char *m = malloc(10);
    for (int i = 0; i < 10; i++)
        m[i] = 'a' + i;
    char *r = realloc(m, 20);
    r[10] = 0;
    puts_(r);
    putbyte(' ');
    char *c = calloc(5, 3);
    int sum = 0;
    for (int i = 0; i < 15; i++)
        sum += c[i];
    puti(sum);
    puts_(calloc(300, 300) == 0 ? " overflow" : " no overflow");
    puts_(malloc(8000) == 0 ? " exhausted" : " not exhausted");
    return 0;
}
)";
    EXPECT_EQ("abcdefghij 0 overflow exhausted", ClangRun(src));
}

// A stack that ran into the canary below it is reported, with status 0xfd.
TEST_F(AvrTest, RuntimeStackOverflowReported)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = R"(
extern char __stack_canary[2];
int main(void)
{
    __stack_canary[1] = 0;
    return 0;
}
)";
    EXPECT_EQ("stack overflow\n", ClangRun(src));
    EXPECT_EQ(0xfd, exit_status);
}
