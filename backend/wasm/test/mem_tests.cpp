//
// Run tests for the <string.h> mem* routines in the wasm32 libc, ported from ARM32's
// (from AArch64's, RISC-V's and BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

TEST_F(WasmTest, MemsetFillChar)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("AAAAAAAA\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemsetReturnsDest)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemsetZeroBounded)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("AZZZE\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcpyString)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcpyReturnsDest)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcpyPartial)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("ABCZZZ\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcmpEqual)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcmpLess)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("-1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcmpGreater)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemcmpBoundedPrefix)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemmoveNonOverlapping)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("ABCDEFGH\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemmoveOverlapForward)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("BCDEF\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemmoveOverlapBackward)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("AABCDE\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemmoveReturnsDest)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemchrFound)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemchrNotFound)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MemchrBounded)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, MallocAlignedAndDistinct)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1 1 1\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, CallocZeroes)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, ReallocGrowKeepsContents)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("HELLO WORLD\n", CompileAndRunWasm(R"PROG(
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

TEST_F(WasmTest, ReallocShrinkAndNull)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1 ABC 1\n", CompileAndRunWasm(R"PROG(
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
