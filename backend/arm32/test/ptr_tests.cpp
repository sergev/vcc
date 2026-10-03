//
// ARM32 pointers, arrays, chars and strings; libc's string and memory functions and
// malloc, compiled by our code generator.
//
#include "arm32_test.h"

// A power-of-two scale is a shifted add.
TEST_F(Arm32Test, AddPointerScaledIndex)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
int f(int *p) { int i = -1; return p[i]; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr lr, [r11, #-16]
ldr r12, [r11, #-4]
add r12, r12, lr, lsl #2
)")) << code;
}

// A scale that is not a power of two is a multiply.
TEST_F(Arm32Test, AddPointerOddScale)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
struct s { char c[3]; };
char f(struct s *p, unsigned i) { return p[i].c[0]; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(mov lr, #3
mul lr, r12, lr
ldr r12, [r11, #-4]
add r12, r12, lr
)")) << code;
}

// A load goes through the pointer in r12, a store through the one in lr.
TEST_F(Arm32Test, LoadAndStoreThroughPointers)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
void f(unsigned char *p, short *q) { *p = *q; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr r12, [r11, #-8]
ldrsh r12, [r12]
)")) << code;
    EXPECT_NE(std::string::npos, code.find("strb r12, [lr]\n")) << code;
}

// An 8-byte value moves through a pointer as two words.
TEST_F(Arm32Test, LongLongThroughPointer)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
long long f(long long *p) { return *p; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr r12, [r11, #-4]
ldr lr, [r12, #4]
ldr r12, [r12]
)")) << code;
}

TEST_F(Arm32Test, RunArraysAndPointers)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
long long sum(long long *a, int n)
{
    long long s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}
int main(void) {
    long long a[5] = { 1, 2, 3, 4, 0x500000000LL };
    int m[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    char s[] = "hello";
    char *p = s + 4;
    long long *e = &a[4];
    int k = -2;
    double d[3] = { 0.5, 1.5, 2.5 };
    double *q = d + 1;
    return (sum(a, 5) == 0x50000000aLL) + 2 * (m[1][2] == 6) + 4 * (*p == 'o')
         + 8 * (e - a == 4) + 16 * (p[k] == 'l') + 32 * (sizeof s == 6)
         + 64 * (*(e + k) == 3) + 128 * (*q + q[1] == 4.0);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunStringFunctions)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("abcxyz\n", CompileAndRunArm32(R"(
#include <string.h>
int puts(const char *s);
int main(void) {
    char buf[16];
    strcpy(buf, "abc");
    strcat(buf, "xyz");
    puts(buf);
    char z[8];
    memset(z, 'q', sizeof z);
    memcpy(z, "ab", 2);
    return (strlen(buf) == 6) + 2 * (strcmp(buf, "abd") < 0) + 4 * (strchr(buf, 'x') == buf + 3)
         + 8 * (memcmp(z, "abqq", 4) == 0) + 16 * (strncmp("abc", "abd", 2) == 0);
})"));
    EXPECT_EQ(31, exit_status);
}

// malloc: 16-byte aligned blocks after a 16-byte header; calloc zeroes; realloc copies.
TEST_F(Arm32Test, RunMalloc)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *a = malloc(10);
    char *b = malloc(1);
    int *z = calloc(4, sizeof(int));
    strcpy(a, "123456789");
    char *c = realloc(a, 100);
    return (b - a == 32) + 2 * (((unsigned long)a & 15) == 0) + 4 * (z[3] == 0)
         + 8 * (strcmp(c, "123456789") == 0);
})"));
    EXPECT_EQ(15, exit_status);
}

// The ILP32 math routines read a double's words through pointers.
TEST_F(Arm32Test, RunMathWords)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
#include <math.h>
int main(void) {
    int e;
    double ip;
    double m = frexp(48.0, &e);
    double f = modf(-2.75, &ip);
    return (ldexp(0.75, 4) == 12.0) + 2 * (m == 0.75 && e == 6) + 4 * (f == -0.75 && ip == -2.0);
})"));
    EXPECT_EQ(7, exit_status);
}
