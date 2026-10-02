//
// AArch64 pointers, arrays, chars and strings; libc's string and memory functions and
// malloc, compiled by our code generator.
//
#include "aarch64_test.h"

// A power-of-two scale is a shifted add (the front end has widened the int index).
TEST_F(Aarch64Test, AddPointerScaledIndex)
{
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
long f(long *p) { int i = -1; return p[i]; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(sxtw x9, w9
str x9, [x29, #-24]
ldr x9, [x29, #-8]
ldr x10, [x29, #-24]
add x9, x9, x10, lsl #3
)")) << code;
}

// A scale that is not a power of two is a multiply.
TEST_F(Aarch64Test, AddPointerOddScale)
{
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
struct s { char c[3]; };
char f(struct s *p, unsigned long i) { return p[i].c[0]; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(mov x11, #3
mul x10, x10, x11
add x9, x9, x10
)")) << code;
}

TEST_F(Aarch64Test, LoadAndStoreThroughPointers)
{
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
void f(unsigned char *p, short *q) { *p = *q; }
)"));
    EXPECT_NE(std::string::npos, code.find("ldr x12, [x29, #-16]\nldrsh w9, [x12]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("strb w9, [x13]\n")) << code;
}

TEST_F(Aarch64Test, RunArraysAndPointers)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
long sum(long *a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
int main(void) {
    long a[5] = { 1, 2, 3, 4, 5 };
    int m[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    char s[] = "hello";
    char *p = s + 4;
    long *e = &a[4];
    int k = -2;
    return (sum(a, 5) == 15) + 2 * (m[1][2] == 6) + 4 * (*p == 'o') + 8 * (e - a == 4)
         + 16 * (p[k] == 'l') + 32 * (sizeof s == 6) + 64 * (*(e + k) == 3);
})"));
    EXPECT_EQ(127, exit_status);
}

TEST_F(Aarch64Test, RunStringFunctions)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("abcxyz\n", CompileAndRunAarch64(R"(
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
TEST_F(Aarch64Test, RunMalloc)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
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
