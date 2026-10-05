//
// MSP430 static data: sections, alignment, every initializer kind, addresses of data
// and functions alike, and static locals.
//
#include "msp430_test.h"

TEST_F(Msp430Test, DataSections)
{
    std::string s = CompileToMsp430(R"(
        int d = 5;
        long b;
        static const char msg[] = "hi";
        const char *f(void) { return msg; }
    )");
    EXPECT_NE(std::string::npos, s.find("    .data\n    .globl  d\n    .p2align 1\n"
                                        "    .type   d, @object\n    .size   d, 2\nd:\n"
                                        "    .short  5\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("    .bss\n    .globl  b\n    .p2align 1\n"
                                        "    .type   b, @object\n    .size   b, 4\nb:\n"
                                        "    .zero   4\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("msg:\n    .ascii  \"hi\"\n    .byte   0\n")) << s;
}

// A char object is not aligned; anything wider is, to 2.
TEST_F(Msp430Test, DataAlignment)
{
    std::string s = CompileToMsp430("char c = 1; char s[3] = \"ab\"; int i = 2;");
    EXPECT_EQ(std::string::npos, s.find(".p2align 1\n    .type   c,")) << s;
    EXPECT_EQ(std::string::npos, s.find(".p2align 1\n    .type   s,")) << s;
    EXPECT_NE(std::string::npos, s.find(".p2align 1\n    .type   i,")) << s;
}

// Every initializer kind: integers by width, binary32 and binary64 bits, and
// addresses -- of data and of functions alike, plain byte addresses.
TEST_F(Msp430Test, DataInitializers)
{
    std::string s = CompileToMsp430(R"(
        signed char c = -1;
        unsigned u = 65535;
        long l = -2;
        long long ll = 0x123456789LL;
        float f = 1.5f;
        double d = 0.1;
        long double ld = 2.0L;
        int arr[4] = { 1, 2 };
        int g(void) { return 0; }
        int (*fp)(void) = g;
        int *ip = &arr[1];
    )");
    for (const char *e : { "c:\n    .byte   -1\n", "u:\n    .short  65535\n",
                           "l:\n    .long   -2\n", "ll:\n    .quad   4886718345\n",
                           "f:\n    .long   0x3fc00000\n", "d:\n    .quad   0x3fb999999999999a\n",
                           "ld:\n    .quad   0x4000000000000000\n",
                           "arr:\n    .short  1\n    .short  2\n    .zero   4\n",
                           "fp:\n    .short  g\n", "ip:\n    .short  arr+2\n" })
        EXPECT_NE(std::string::npos, s.find(e)) << e << s;
}

// Globals are reached absolutely; an address is an immediate.
TEST_F(Msp430Test, GlobalAccessAbsolute)
{
    std::string code = Code(CompileToMsp430(R"(
        int a, b;
        int *p;
        void f(void) { a = b; p = &a; }
    )"));
    EXPECT_NE(std::string::npos, code.find("mov &b, &a\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov #a, r12\n")) << code;
}

// Block-scope statics of the same name stay apart, and assemble as they are named.
TEST_F(Msp430Test, StaticLocals)
{
    std::string s = CompileToMsp430(R"(
        int f(void) { static int n = 1; return n++; }
        int g(void) { static int n = 10; return n++; }
    )");
    EXPECT_NE(std::string::npos, s.find("n:\n    .short  1\n")) << s;
    EXPECT_NE(std::string::npos, s.find("n$1:\n    .short  10\n")) << s;
}

// Run: initialized data, bss, rodata, pointers in data, static locals.
TEST_F(Msp430Test, RunGlobals)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        int arr[5] = { 10, 20, 30 };
        long big = 123456789L;
        long long huge = -5;
        int zero[10];
        const int table[3] = { 7, 8, 9 };
        int *ptr = &arr[2];
        static char text[] = "hello";
        int counter(void) { static int n = 100; return n++; }
        int add(int x) { return x + 1; }
        int (*fp)(int) = add;
        int main(void)
        {
            int sum = 0;
            for (int i = 0; i < 10; i++)
                sum += zero[i];
            if (sum != 0) return 1;
            if (arr[0] + arr[1] + arr[2] + arr[3] + arr[4] != 60) return 2;
            if (big != 123456789L || huge != -5) return 3;
            if (*ptr != 30 || table[2] != 9) return 4;
            if (text[4] != 'o' || text[5] != 0) return 5;
            counter();
            if (counter() != 101) return 6;
            if (fp(41) != 42) return 7;
            big += 1;
            return big == 123456790L ? 0 : 8;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
