//
// x86-64 pointers, arrays and chars: loads and stores through r10, lea for pointer
// arithmetic, and the byte-pointer kinds as plain operations.
//
#include "x86_test.h"

#define EXPECT_HAS(name, expected, src)                            \
    TEST_F(X86Test, name)                                          \
    {                                                              \
        NaiveSelection();                                          \
        std::string code = Code(CompileToX86(src));                \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

// A load extends by the pointee's type; a store writes its width.
EXPECT_HAS(LoadSignedChar, "movq -8(%rbp), %r10\nmovsbl (%r10), %eax\n",
           "int f(signed char *p) { return *p; }")
EXPECT_HAS(LoadUnsignedShort, "movq -8(%rbp), %r10\nmovzwl (%r10), %eax\n",
           "int f(unsigned short *p) { return *p; }")
EXPECT_HAS(StoreConstant, "movq -8(%rbp), %r10\nmovq $-7, (%r10)\n", "void f(long *p) { *p = -7; }")
EXPECT_HAS(StoreVariable, "movq -8(%rbp), %r10\nmovsbl -13(%rbp), %eax\nmovb %al, (%r10)\n",
           "void f(char *p, int c) { *p = c; }")
EXPECT_HAS(LoadDouble, "movq -8(%rbp), %r10\nmovsd (%r10), %xmm14\n",
           "double f(double *p) { return *p; }")

// The index scaled by lea; a constant index folds into the displacement; another
// scale multiplies.
EXPECT_HAS(IndexInt,
           "movslq -12(%rbp), %rax\nmovq %rax, -24(%rbp)\nmovq -8(%rbp), %rax\n"
           "movq -24(%rbp), %r10\nleaq (%rax,%r10,4), %rax\n",
           "int f(int *p, int i) { return p[i]; }")
EXPECT_HAS(IndexConstant, "movq -8(%rbp), %rax\nleaq 24(%rax), %rax\n",
           "long f(long *p) { return p[3]; }")
EXPECT_HAS(IndexOddScale, "imulq $12, %r10, %r10\nleaq (%rax,%r10,1), %rax\n",
           "int f(int (*p)[3], long i) { return p[i][0]; }")
EXPECT_HAS(PointerDiff, "movq -8(%rbp), %rax\nsubq -16(%rbp), %rax\n",
           "long f(char *a, char *b) { return a - b; }")

TEST_F(X86Test, RunArraysAndStrings)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
#include <string.h>
static char buf[16] = "hello";
int sum(int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}
int main(void) {
    int a[5] = { 1, 2, 3, 4, 5 };
    double d[2] = { 1.5, 2.5 };
    long double ld[2] = { 3, 4 };
    char *p = buf;
    signed char sc = -1;
    strcat(buf, ", x86");
    if (strcmp(buf, "hello, x86") != 0 || strlen(p) != 10 || p[7] != 'x')
        return 1;
    if (sum(a, 5) != 15 || &a[4] - &a[1] != 3 || *(a + 2) != 3)
        return 2;
    if (d[1] - d[0] != 1.0 || ld[1] * ld[0] != 12)
        return 3;
    if (sc != -1 || (unsigned char)sc != 255)
        return 4;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}
