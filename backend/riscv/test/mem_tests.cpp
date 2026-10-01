//
// Run tests for the <string.h> mem* routines in the RISC-V libc, ported from the BESM-6
// mem_tests.cpp.  The expected output is what the host C library prints for the
// same program.
//
#include "riscv_test.h"

TEST_F(RiscvTest, MemsetFillChar)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("AAAAAAAA\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemsetReturnsDest)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemsetZeroBounded)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("AZZZE\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcpyString)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcpyReturnsDest)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcpyPartial)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("ABCZZZ\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcmpEqual)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcmpLess)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcmpGreater)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemcmpBoundedPrefix)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemmoveNonOverlapping)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemmoveOverlapForward)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("BCDEF\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemmoveOverlapBackward)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("AABCDE\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemmoveReturnsDest)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemchrFound)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemchrNotFound)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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

TEST_F(RiscvTest, MemchrBounded)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunRiscv(R"PROG(
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
