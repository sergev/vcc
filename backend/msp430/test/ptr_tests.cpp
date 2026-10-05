//
// MSP430 pointers, arrays, chars and strings: loads and stores through the pointer's
// register (or r15, loaded with it), index scaling, byte access; and runs, the C library
// built by genmsp430 included.
//
#include "msp430_test.h"

// A load through a pointer in a register: the word into the pointer's own register last.
EXPECT_CODE(LoadThroughPointer, R"(mov 2(r12), r13
mov @r12, r12
ret
)", "long f(long *p) { return *p; }")

// Through r15 from a pointer in memory, straight to the destination's slot.
TEST_F(Msp430Test, LoadThroughPointerInMemory)
{
    NoRegalloc();
    std::string code = Code(CompileToMsp430("long f(long *p) { return *p; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov @r1, r15
mov @r15, 2(r1)
mov 2(r15), 4(r1)
)"))
        << code;
}

// A store takes its value straight from the immediate (or memory).
EXPECT_CODE(StoreThroughPointer, R"(mov #1234, 0(r12)
ret
)", "void f(int *p) { *p = 1234; }")

// A char goes through a pointer as a byte.
EXPECT_CODE(CharThroughPointer, R"(mov.b @r13, r13
mov.b r13, 0(r12)
ret
)", "void f(char *p, char *q) { *p = *q; }")

// The index scaled by shifts for a power of two, by __mspabi_mpyi otherwise.
TEST_F(Msp430Test, IndexScaling)
{
    std::string code = Code(CompileToMsp430(R"(
        long a(long *p, int i) { return p[i]; }
        struct T { char c[3]; };
        char b(struct T *p, int i) { return p[i].c[1]; }
    )"));
    // In r15: the result goes where the pointer is.
    EXPECT_NE(std::string::npos, code.find(R"(mov r13, r15
rla r15
rla r15
add r12, r15
mov r15, r12
)")) << code;
    // The pointer, in a register the call clobbers, kept on the stack.
    EXPECT_NE(std::string::npos, code.find(R"(push r12
mov r13, r12
mov #3, r13
call #__mspabi_mpyi
pop r15
add r15, r12
)")) << code;
}

// A constant index folds into one add.
TEST_F(Msp430Test, ConstantIndex)
{
    std::string code = Code(CompileToMsp430("int f(int *p) { return p[3]; }"));
    EXPECT_NE(std::string::npos, code.find("add #6, r12\n")) << code;
}

// The address of a slot is SP plus its offset (none at offset 0).
TEST_F(Msp430Test, AddressOfLocal)
{
    std::string code = Code(
        CompileToMsp430("void g(int *); void f(void) { int x, y; g(&x); g(&y); }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, r12
call #g
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, r12
add #2, r12
)")) << code;
}

// Pointers, arrays, chars and the string library, run.
TEST_F(Msp430Test, RunPointersAndStrings)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        #include <string.h>
        int sum(const int *a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
        long lsum(long (*m)[3], int rows)
        {
            long s = 0;
            for (int i = 0; i < rows; i++)
                for (int j = 0; j < 3; j++)
                    s += m[i][j] * (j + 1);
            return s;
        }
        void rev(char *s) { char *e = s + strlen(s) - 1; while (s < e) { char t = *s; *s++ = *e; *e-- = t; } }
        int main(void)
        {
            int a[5] = { 1, 2, 3, 4, 5 };
            if (sum(a, 5) != 15) return 1;
            long m[2][3] = { { 1, 2, 3 }, { 100000, 200000, 300000 } };
            if (lsum(m, 2) != 1400014L) return 2;
            int *p = &a[4], *q = &a[1];
            if (p - q != 3 || !(q < p) || *--p != 4) return 3;
            char buf[16];
            strcpy(buf, "msp430");
            rev(buf);
            if (strcmp(buf, "034psm") != 0) return 4;
            if (strlen(buf) != 6 || strchr(buf, 'p') != buf + 3) return 5;
            char c = (char)200; // plain char is unsigned
            if (c < 0 || c != 200) return 6;
            signed char sc = (signed char)200;
            if (sc != -56) return 7;
            unsigned char bytes[4] = { 1, 2, 3, 4 };
            unsigned long w;
            memcpy(&w, bytes, 4);
            if (w != 0x04030201UL) return 8;
            char *hi = (char *)0x8100, *lo = (char *)0x7f00;
            if (!(hi > lo)) return 9; // pointer compares are unsigned
            memset(buf, 'z', 3);
            buf[3] = 0;
            if (strcmp(buf, "zzz") != 0 || memcmp(buf, "zzy", 3) <= 0) return 10;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
