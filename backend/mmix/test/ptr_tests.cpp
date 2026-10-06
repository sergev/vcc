//
// MMIX pointers, arrays, chars and strings: loads and stores by the pointee's width and
// signedness, scaled addition with 2addu..16addu, byte order big-endian, and the C
// library built by genmmix.
//
#include "mmix_test.h"

// A load follows the pointee's signedness, so the value comes extended; a store takes
// the pointee's width.
TEST_F(MmixTest, LoadsAndStores)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        long f1(signed char *p) { return *p; }
        unsigned long f2(unsigned short *p) { return *p; }
        void f3(int *p, int v) { *p = v; }
        float f4(float *p) { return *p; }
    )"));
    EXPECT_NE(std::string::npos, code.find("ldo $249,$254,0\nldb $248,$249,0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldo $249,$254,0\nldwu $248,$249,0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldo $249,$254,0\nsttu $248,$249,0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldo $249,$254,0\nldsf $248,$249,0\n")) << code;
}

// An index scaled by 2..16 is one 2addu..16addu (8addu $x,$i,$p = 8i + p); another scale
// a mulu; a constant index an offset.
TEST_F(MmixTest, AddPtr)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        long *f1(long *p, long i) { return p + i; }
        short *f2(short *p, long i) { return p + i; }
        struct T { char c[24]; };
        struct T *f3(struct T *p, long i) { return p + i; }
        int *f4(int *p) { return p + 3; }
    )"));
    EXPECT_NE(std::string::npos, code.find("8addu $248,$249,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("2addu $248,$249,$248\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mulu $249,$249,24\naddu $248,$248,$249\n")) << code;
    EXPECT_NE(std::string::npos, code.find("addu $248,$248,12\n")) << code;
}

// Pointer comparisons are unsigned.  (The frontend compares two char pointers through
// their difference.)
TEST_F(MmixTest, PointerCompare)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("int f(long *a, long *b) { return a < b; }"));
    EXPECT_NE(std::string::npos, code.find("cmpu $248,$248,$249\nzsn $248,$248,1\n")) << code;
}

// Run: arrays and pointers, 2-D arrays, pointer differences, and the byte order: an
// int's most significant byte comes first.
TEST_F(MmixTest, RunPointers)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        long sum(long *p, int n) { long s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }
        int main(void)
        {
            long a[5] = { 1, 2, 3, 4, 5 };
            if (sum(a, 5) != 15) return 1;
            int m[3][4];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    m[i][j] = 10 * i + j;
            if (m[2][3] != 23 || *(*(m + 1) + 2) != 12) return 2;
            long *p = &a[4], *q = &a[1];
            if (p - q != 3) return 3;
            int x = 0x01020304;
            unsigned char *b = (unsigned char *)&x;
            if (b[0] != 1 || b[1] != 2 || b[2] != 3 || b[3] != 4) return 4;
            short s = -2;
            signed char *sc = (signed char *)&s;
            if (sc[0] != -1 || sc[1] != -2) return 5;
            long big = 0x0102030405060708L;
            int *hi = (int *)&big;
            if (hi[0] != 0x01020304 || hi[1] != 0x05060708) return 6;
            char c = (char)200;
            char *pc = &c;
            if (*pc != -56) return 7;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: strings through the C library that genmmix compiled into libc.a.
TEST_F(MmixTest, RunStrings)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("Hello, MMIX\n", CompileAndRunMmix(R"(
        #include <stdio.h>
        #include <stdlib.h>
        #include <string.h>
        int main(void)
        {
            char buf[32];
            strcpy(buf, "Hello");
            strcat(buf, ", MMIX");
            if (strlen(buf) != 11) return 1;
            if (strcmp(buf, "Hello, MMIX") != 0) return 2;
            if (strchr(buf, 'M') != buf + 7) return 3;
            char copy[32];
            memset(copy, 'x', sizeof copy);
            memcpy(copy, buf, 12);
            if (memcmp(copy, buf, 12) != 0 || copy[12] != 'x') return 4;
            memmove(copy + 1, copy, 5);
            if (strncmp(copy, "HHello", 6) != 0) return 5;
            if (atoi("-1234") != -1234) return 6;
            puts(buf);
            flush();
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
