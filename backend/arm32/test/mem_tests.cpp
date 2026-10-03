//
// Run tests for the <string.h> mem* routines in the ARM32 libc, ported from AArch64's
// mem_tests.cpp (itself from RISC-V's, and that from BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "arm32_test.h"

TEST_F(Arm32Test, MemsetFillChar)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("AAAAAAAA\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[9];
    memset(buf, 'A', 8);
    buf[8] = 0;
    printf("%s\n", buf);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemsetReturnsDest)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[4];
    char *r = memset(buf, 'X', 4);
    printf("%d\n", r == buf);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemsetZeroBounded)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("AZZZE\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[6];
    buf[0] = 'A';
    buf[1] = 'B';
    buf[2] = 'C';
    buf[3] = 'D';
    buf[4] = 'E';
    buf[5] = 0;
    memset(buf + 1, 'Z', 3);
    printf("%s\n", buf);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcpyString)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[9];
    memcpy(dst, "ABCDEFGH", 8);
    dst[8] = 0;
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcpyReturnsDest)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[4];
    char *r = memcpy(dst, "XYZ", 3);
    printf("%d\n", r == dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcpyPartial)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCZZZ\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[7];
    memset(dst, 'Z', 6);
    dst[6] = 0;
    memcpy(dst, "ABCDEF", 3);
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcmpEqual)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", memcmp("ABCD", "ABCD", 4));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcmpLess)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("-1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    // 'A'(65) - 'B'(66) = -1
    printf("%d\n", memcmp("AAAA", "ABAA", 4));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcmpGreater)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    // 'B'(66) - 'A'(65) = 1
    printf("%d\n", memcmp("ABAA", "AAAA", 4));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemcmpBoundedPrefix)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", memcmp("ABCX", "ABCY", 3));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemmoveNonOverlapping)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[9];
    memmove(dst, "ABCDEFGH", 8);
    dst[8] = 0;
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemmoveOverlapForward)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("BCDEF\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[8];
    memcpy(buf, "ABCDEF", 6);
    buf[6] = 0;
    memmove(buf, buf + 1, 5);
    buf[5] = 0;
    printf("%s\n", buf);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemmoveOverlapBackward)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("AABCDE\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[8];
    memcpy(buf, "ABCDEF", 6);
    buf[6] = 0;
    memmove(buf + 1, buf, 5);
    printf("%s\n", buf);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemmoveReturnsDest)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[4];
    char *r = memmove(dst, "XYZ", 3);
    printf("%d\n", r == dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemchrFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABCDEF";
    char *p = memchr(s, 'C', 6);
    printf("%d\n", (int)(p - s));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemchrNotFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char *p = memchr("ABCDEF", 'Z', 6);
    printf("%d\n", p == 0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MemchrBounded)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char *p = memchr("ABCDEF", 'D', 3);
    printf("%d\n", p == 0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, MallocAlignedAndDistinct)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1 1 1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    char *a = malloc(3);
    char *b = malloc(5);
    printf("%d %d %d\n", ((long)a & 15) == 0, ((long)b & 15) == 0, b >= a + 3);
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, CallocZeroes)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int *p = calloc(10, sizeof(int));
    int sum = 0;
    for (int i = 0; i < 10; i++)
        sum += p[i];
    printf("%d\n", sum);
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, ReallocGrowKeepsContents)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("HELLO WORLD\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *p = malloc(6);
    strcpy(p, "HELLO");
    malloc(32);
    p = realloc(p, 12);
    strcat(p, " WORLD");
    printf("%s\n", p);
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, ReallocShrinkAndNull)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1 ABC 1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *p = malloc(16);
    strcpy(p, "ABC");
    char *q = realloc(p, 4);
    char *r = realloc(NULL, 8);
    printf("%d %s %d\n", q == p, q, r != NULL && r != p);
    return 0;
}
)PROG"));
}
