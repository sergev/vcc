//
// Run tests for the <string.h> string routines in the x86-64 libc, ported from AArch64's
// str_tests.cpp (itself from RISC-V's, and that from BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "x86_test.h"

TEST_F(X86Test, StrlenBasic)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("5\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrlenEmpty)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrlenCrossesWord)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("8\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcpyBasic)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("ABCDEFG\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcpyReturnsDest)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrncpyTruncates)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("ABC\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrncpyPads)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcatBasic)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("ABCDEF\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrncatBounded)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("ABCDE\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcmpEqual)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcmpLess)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("-1\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrcmpGreaterByLength)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("68\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrncmpBoundedEqual)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrchrFound)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrchrNotFound)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrchrFindsNul)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("3\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrrchrLast)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("4\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrstrFound)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("2\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrstrNotFound)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrstrEmptyNeedle)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("1\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrtokMultiToken)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("AB\nCD\nEF\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrtokLeadingDelims)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("AB\nCD\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrerrorKnown)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("OUT OF MEMORY\n", CompileAndRunX86(R"PROG(
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

TEST_F(X86Test, StrerrorUnknown)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("UNKNOWN ERROR\n", CompileAndRunX86(R"PROG(
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

// Plain char is signed on x86-64: a byte over 127 reads as negative, yet the
// comparisons order bytes as unsigned char, and a search finds the byte whether it is
// given as an int or as a char.
TEST_F(X86Test, StrSignedChar)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("-23 1 233\n1 1 1\n1 1 3\n1 4\n", CompileAndRunX86(R"PROG(
#include <stdio.h>
#include <string.h>
int main(void) {
    char hi[] = "a\xe9z\x80";
    char c = hi[1];
    printf("%d %d %d\n", c, c < 0, (unsigned char)c);
    printf("%d %d %d\n", strcmp("\x80", "a") > 0, strncmp("ab\xff", "ab\x01", 3) > 0,
           memcmp("\xfe", "\x02", 1) > 0);
    printf("%d %d %d\n", (int)(strchr(hi, 0xe9) - hi), (int)(strchr(hi, (char)0xe9) - hi),
           (int)(strrchr(hi, 0x80) - hi));
    printf("%d %d\n", (int)((char *)memchr(hi, -23, 4) - hi), (int)strlen(hi));
    return 0;
}
)PROG"));
}
