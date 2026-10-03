//
// Run tests for the <string.h> string routines in the ARM32 libc, ported from AArch64's
// str_tests.cpp (itself from RISC-V's, and that from BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "arm32_test.h"

TEST_F(Arm32Test, StrlenBasic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("5\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", (int)strlen("HELLO"));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrlenEmpty)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", (int)strlen(""));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrlenCrossesWord)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("8\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", (int)strlen("ABCDEFGH"));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcpyBasic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCDEFG\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[10];
    strcpy(dst, "ABCDEFG");
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcpyReturnsDest)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[4];
    char *r = strcpy(dst, "XY");
    printf("%d\n", r == dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrncpyTruncates)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABC\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[8];
    strncpy(dst, "ABCDEF", 3);
    dst[3] = 0;
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrncpyPads)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[8];
    int i;
    for (i = 0; i < 8; i++) dst[i] = 'Z';
    strncpy(dst, "AB", 6);
    dst[6] = 0;
    /* "AB" then four NULs; printed length stops at first NUL. */
    printf("%d\n", (int)strlen(dst));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcatBasic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCDEF\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[10];
    strcpy(dst, "ABC");
    strcat(dst, "DEF");
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrncatBounded)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("ABCDE\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char dst[10];
    strcpy(dst, "ABC");
    strncat(dst, "DEFGH", 2);
    printf("%s\n", dst);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcmpEqual)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%d\n", strcmp("ABCD", "ABCD"));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcmpLess)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("-1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    // 'A'(65) - 'B'(66) = -1
    printf("%d\n", strcmp("ABA", "ABB"));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrcmpGreaterByLength)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("68\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    // "ABCD" > "ABC": first extra byte 'D'(68) - '\0'(0) = 68
    printf("%d\n", strcmp("ABCD", "ABC"));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrncmpBoundedEqual)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    // Differ at index 3, only 3 compared -> equal.
    printf("%d\n", strncmp("ABCX", "ABCY", 3));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrchrFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABCDEF";
    char *p = strchr(s, 'C');
    printf("%d\n", (int)(p - s));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrchrNotFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char *p = strchr("ABCDEF", 'Z');
    printf("%d\n", p == 0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrchrFindsNul)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("3\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABC";
    char *p = strchr(s, 0);
    printf("%d\n", (int)(p - s));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrrchrLast)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("4\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABCABC";
    char *p = strrchr(s, 'B');
    printf("%d\n", (int)(p - s));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrstrFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABCDEF";
    char *p = strstr(s, "CDE");
    printf("%d\n", (int)(p - s));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrstrNotFound)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char *p = strstr("ABCDEF", "XYZ");
    printf("%d\n", p == 0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrstrEmptyNeedle)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    const char *s = "ABCDEF";
    char *p = strstr(s, "");
    printf("%d\n", p == s);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrtokMultiToken)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("AB\nCD\nEF\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[16];
    strcpy(buf, "AB,CD,EF");
    char *t = strtok(buf, ",");
    while (t != 0) {
        printf("%s\n", t);
        t = strtok(0, ",");
    }
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrtokLeadingDelims)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("AB\nCD\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    char buf[16];
    strcpy(buf, ",,AB,,CD,,");
    char *t = strtok(buf, ",");
    while (t != 0) {
        printf("%s\n", t);
        t = strtok(0, ",");
    }
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrerrorKnown)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("OUT OF MEMORY\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%s\n", strerror(5));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, StrerrorUnknown)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("UNKNOWN ERROR\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <string.h>
static void body(void) {
    printf("%s\n", strerror(99));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}
