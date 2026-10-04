//
// AVR pointers, arrays and strings: loads and stores through Z, scaled pointer
// arithmetic; and the C library, compiled by genavr, called from both compilers.
//
#include "avr_test.h"

// A load: the pointer in Z, the bytes through Z+i.
EXPECT_CODE(LoadLong,
            "std Y+1, r24\nstd Y+2, r25\nldd r30, Y+1\nldd r31, Y+2\n"
            "ldd r22, Z+0\nldd r23, Z+1\nldd r24, Z+2\nldd r25, Z+3\n"
            "std Y+3, r22\nstd Y+4, r23\nstd Y+5, r24\nstd Y+6, r25\n"
            "ldd r22, Y+3\nldd r23, Y+4\nldd r24, Y+5\nldd r25, Y+6\n",
            "long f(long *p) { return *p; }")

// A store: the value and the pointer, variables before constants.
EXPECT_CODE(StoreInt,
            "std Y+1, r24\nstd Y+2, r25\nldd r30, Y+1\nldd r31, Y+2\nldi r24, 7\nldi r25, 0\n"
            "std Z+0, r24\nstd Z+1, r25\n",
            "void f(int *p) { *p = 7; }")

// An index scaled by 4 is shifted twice, by 3 multiplied.
TEST_F(AvrTest, AddPtrScaled)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("long *f(long *p, int i) { return p + i; }"));
    EXPECT_NE(std::string::npos, s.find("ldd r30, Y+1\nldd r31, Y+2\nlsl r24\nrol r25\n"
                                        "lsl r24\nrol r25\nadd r24, r30\nadc r25, r31\n"))
        << s;
}

TEST_F(AvrTest, AddPtrMultiplied)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("struct s { char c[3]; };\n"
                                      "struct s *f(struct s *p, int i) { return p + i; }"));
    EXPECT_NE(std::string::npos, s.find("ldi r22, 3\nldi r23, 0\nmul r24, r22\n")) << s;
}

// Pointer comparisons are unsigned.  (Of char pointers the frontend compares the
// difference with zero.)
TEST_F(AvrTest, PointerCompareUnsigned)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int f(int *a, int *b) { return a < b; }"));
    EXPECT_NE(std::string::npos, s.find("brlo")) << s;
}

TEST_F(AvrTest, RunPointers)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
long table[5] = { 10, -20, 30, -40, 50 };
long sum(long *p, int n)
{
    long s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
int main(void)
{
    if (sum(table, 5) != 30) return 1;
    int x = 1, y = 2;
    swap(&x, &y);
    if (x != 2 || y != 1) return 2;
    int local[30];
    for (int i = 0; i < 30; i++)
        local[i] = i * i;
    int *p = &local[29], *q = local;
    if (*p != 841 || p - q != 29) return 3;
    long long big[3] = { 1, 0x123456789aLL, 3 };
    long long *bp = big + 1;
    if (*bp != 0x123456789aLL) return 4;
    *bp = -1;
    if (big[1] != -1 || big[2] != 3) return 5;
    char s[] = "hello";
    char *c = s;
    while (*c) {
        *c = *c - 32;
        c++;
    }
    if (s[0] != 'H' || s[4] != 'O' || c - s != 5) return 6;
    return 0;
}
)"));
}

// A slot past Y+63 is addressed for &x and reached through it.
TEST_F(AvrTest, RunAddressOfFarSlot)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
void set(long *p, long v) { *p = v; }
int main(void)
{
    char pad[70];
    long x = 0;
    pad[0] = 1;
    set(&x, 123456789);
    return x == 123456789 && pad[0] == 1 ? 0 : 1;
}
)"));
}

// The C library compiled by genavr, called from our code.
TEST_F(AvrTest, RunStringLibrary)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("hello, world\n0\n", CompileAndRunBook(R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char buf[32];
    strcpy(buf, "hello");
    strcat(buf, ", world");
    if (strlen(buf) != 12) return 1;
    if (strcmp(buf, "hello, world") != 0 || strcmp("a", "b") >= 0) return 2;
    if (strchr(buf, 'w') != buf + 7 || strrchr(buf, 'o') != buf + 8) return 3;
    if (strstr(buf, "wor") != buf + 7 || strncmp(buf, "help", 3) != 0) return 4;
    char copy[32];
    memset(copy, 'x', sizeof copy);
    memcpy(copy, buf, 13);
    if (memcmp(copy, buf, 13) != 0 || copy[13] != 'x') return 5;
    memmove(copy + 1, copy, 12);
    if (copy[1] != 'h' || copy[12] != 'd') return 6;
    if (atoi("-1234") != -1234) return 7;
    puts(buf);
    return 0;
}
)"));
}

// clang's code calls the library genavr compiled: the size_t arguments agree.
TEST_F(AvrTest, RunStringLibraryFromClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", ClangRun(R"(
void putbyte(int c);
typedef unsigned int size_t;
size_t strlen(const char *s);
void *memcpy(void *d, const void *s, size_t n);
int strcmp(const char *a, const char *b);
char *strncpy(char *d, const char *s, size_t n);
int main(void)
{
    char buf[16];
    memcpy(buf, "abcdef", 7);
    if (strlen(buf) != 6 || strcmp(buf, "abcdef") != 0)
        return 1;
    strncpy(buf, "xy", 4);
    if (buf[1] != 'y' || buf[2] != 0 || buf[3] != 0 || buf[4] != 'e')
        return 2;
    putbyte('o');
    putbyte('k');
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}
