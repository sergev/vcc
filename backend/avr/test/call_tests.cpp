//
// AVR calls: arguments in registers from r25 down, the rest on the stack; results;
// and calls across to clang-compiled code, both ways.
//
#include "avr_test.h"

// An int in r25:r24, a long in r23:r20; the result from r25:r24.
EXPECT_CODE(CallRegisterArguments,
            "ldi r24, 1\nldi r25, 0\nldi r20, 2\nldi r21, 0\nldi r22, 0\nldi r23, 0\n"
            "call g\nstd Y+1, r24\nstd Y+2, r25\nldd r24, Y+1\nldd r25, Y+2\n",
            "int g(int a, long b);\nint f(void) { return g(1, 2L); }")

// A char takes a pair, extended by the sender.
TEST_F(AvrTest, CallCharArgument)
{
    std::string s = Body(CompileToAvr("int g(signed char c, int i);\n"
                                      "int f(void) { return g(-1, 2); }"));
    EXPECT_NE(std::string::npos, s.find("ldi r24, 255\nldi r25, 255\nldi r22, 2\nldi r23, 0\n"
                                        "call g\n"))
        << s;
}

// The long does not fit in r9:r8, so it and the int after it go on the stack: pushed
// last first and high byte first; after the call SP is restored through Z.
TEST_F(AvrTest, CallStackArguments)
{
    std::string s = Body(CompileToAvr(
        "int g(long long a, long long b, long c, int d);\n"
        "int f(void) { return g(1, 2, 0x01020304, 0x0506); }"));
    EXPECT_NE(std::string::npos,
              s.find("ldi r24, 6\nldi r25, 5\npush r25\npush r24\n"
                     "ldi r22, 4\nldi r23, 3\nldi r24, 2\nldi r25, 1\n"
                     "push r25\npush r24\npush r23\npush r22\n"))
        << s;
    EXPECT_NE(std::string::npos,
              s.find("call g\nin r30, __SP_L__\nin r31, __SP_H__\nadiw r30, 6\n"
                     "in r0, __SREG__\ncli\nout __SP_H__, r31\nout __SREG__, r0\n"
                     "out __SP_L__, r30\nstd Y+1, r24\n"))
        << s;
}

// Two bytes of stack arguments are released with pop.
TEST_F(AvrTest, CallReleaseByPop)
{
    std::string s = Body(CompileToAvr(
        "int g(long long a, long long b, long c, int d);\n"
        "int h(long long a, long long b, int c, int d, int e);\n"
        "int f(void) { return h(1, 2, 3, 4, 5); }"));
    EXPECT_NE(std::string::npos, s.find("call h\npop r0\npop r0\n")) << s;
}

// Our code calls clang's with arguments of every size, in registers and on the stack.
TEST_F(AvrTest, RunCallClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", CompileAndRunWithClang(R"(
void putbyte(int c);
long g(signed char a, int b, long c, long long d, unsigned char e, int f);
long h(long long a, long long b, long c, int d);
int main(void)
{
    if (g(-3, 1000, 100000, 5000000000LL, 200, -7) != -3 + 1000 + 100000 + 5 + 200 - 7)
        return 1;
    if (h(1, 2, 300000, 4) != 1 + 2 + 300000 + 4)
        return 2;
    putbyte('o');
    putbyte('k');
    return 0;
}
)",
                                     R"(
long g(signed char a, int b, long c, long long d, unsigned char e, int f)
{
    return a + b + c + (long)(d - 4999999995LL) + e + f;
}
long h(long long a, long long b, long c, int d)
{
    return (long)a + (long)b + c + d;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// clang's code calls ours: our parameters come from registers and the stack.
TEST_F(AvrTest, RunCalledByClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", CompileAndRunWithClang(R"(
long g(signed char a, int b, long c, long long d, unsigned char e, int f)
{
    return a + b + c + (long)(d >> 30) + e + f;
}
long h(long long a, long long b, long c, int d)
{
    return (long)a + (long)b + c + d;
}
signed char k(signed char c) { return c - 1; }
)",
                                     R"(
void putbyte(int c);
long g(signed char a, int b, long c, long long d, unsigned char e, int f);
long h(long long a, long long b, long c, int d);
signed char k(signed char c);
int main(void)
{
    if (g(-3, 1000, 100000, 5000000000LL, 200, -7) != -3 + 1000 + 100000 + 4 + 200 - 7)
        return 1;
    if (h(1, 2, 300000, 4) != 1 + 2 + 300000 + 4)
        return 2;
    if (k(-128) != 127)
        return 3;
    putbyte('o');
    putbyte('k');
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Recursion: every call has its own frame.
TEST_F(AvrTest, RunRecursion)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("6765\n", CompileAndRunBook(R"(
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { return fib(20); }
)"));
}
