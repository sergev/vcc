//
// AVR calls: arguments in registers from r25 down, the rest on the stack; results;
// and calls across to clang-compiled code, both ways.
//
#include "avr_test.h"

// An int in r25:r24, a long in r23:r20; the result from r25:r24.
EXPECT_CODE(CallRegisterArguments,
            R"(ldi r24, 1
ldi r25, 0
ldi r20, 2
ldi r21, 0
ldi r22, 0
ldi r23, 0
call g
std Y+1, r24
std Y+2, r25
ldd r24, Y+1
ldd r25, Y+2
)",
            "int g(int a, long b);\nint f(void) { return g(1, 2L); }")

// A char takes a pair, extended by the sender.
TEST_F(AvrTest, CallCharArgument)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr("int g(signed char c, int i);\n"
                                      "int f(void) { return g(-1, 2); }"));
    EXPECT_NE(std::string::npos, s.find(R"(ldi r24, 255
ldi r25, 255
ldi r22, 2
ldi r23, 0
call g
)"))
        << s;
}

// The long does not fit in r9:r8, so it and the int after it go on the stack: pushed
// last first and high byte first, a constant through r26; after the call SP is
// restored through Z.
TEST_F(AvrTest, CallStackArguments)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr(
        "int g(long long a, long long b, long c, int d);\n"
        "int f(void) { return g(1, 2, 0x01020304, 0x0506); }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(ldi r26, 5
push r26
ldi r26, 6
push r26
ldi r26, 1
push r26
ldi r26, 2
push r26
ldi r26, 3
push r26
ldi r26, 4
push r26
)"))
        << s;
    EXPECT_NE(std::string::npos,
              s.find(R"(call g
in r30, __SP_L__
in r31, __SP_H__
adiw r30, 6
in r0, __SREG__
cli
out __SP_H__, r31
out __SREG__, r0
out __SP_L__, r30
std Y+1, r24
)"))
        << s;
}

// Two bytes of stack arguments are released with pop.
TEST_F(AvrTest, CallReleaseByPop)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr(
        R"(int g(long long a, long long b, long c, int d);
int h(long long a, long long b, int c, int d, int e);
int f(void) { return h(1, 2, 3, 4, 5); })"));
    EXPECT_NE(std::string::npos, s.find(R"(call h
pop r0
pop r0
)")) << s;
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
