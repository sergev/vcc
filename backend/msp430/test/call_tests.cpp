//
// MSP430 calls: the arguments by the EABI rules as clang applies them, results,
// indirect calls; and runs of every rule, caller and callee both ours (T17 checks them
// against clang).
//
#include "msp430_test.h"

// int, long, int: r12, r14:r13, r15 -- a long need not start on an even register.
TEST_F(Msp430Test, ArgsInOrder)
{
    std::string code = Code(CompileToMsp430("int g(int, long, int); int f(void) { return g(1, 2, 3); }"));
    EXPECT_NE(std::string::npos,
              code.find(R"(mov #1, r12
mov #2, r13
mov #0, r14
mov #3, r15
call #g
)"))
        << code;
}

// A long with only r15 left: low word in r15, high word on the stack.
TEST_F(Msp430Test, ArgSplitLong)
{
    std::string code = Code(CompileToMsp430(
        "int k(int, int, int, long); int f(void) { return k(1, 2, 3, 0x40005L); }"));
    EXPECT_NE(std::string::npos, code.find("mov #4, 0(r1)\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov #5, r15
call #k
)")) << code;
}

// A long long that does not fit goes on the stack, and later ints still take registers;
// a long after that goes on the stack whole.
TEST_F(Msp430Test, ArgBackfillAfterStack)
{
    std::string code = Code(CompileToMsp430(
        "int u(int, long long, int, int, long); int f(void) { return u(1, 2, 3, 4, 5); }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov #2, 0(r1)
mov #0, 2(r1)
mov #0, 4(r1)
mov #0, 6(r1)
mov #5, 8(r1)
mov #0, 10(r1)
)"))
        << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov #1, r12
mov #3, r13
mov #4, r14
call #u
)"))
        << code;
}

// A double after three ints goes on the stack; the int after it takes r15.
TEST_F(Msp430Test, ArgDoubleOnStack)
{
    std::string code = Code(CompileToMsp430(
        "int u(int, int, int, double, int); int f(void) { return u(1, 2, 3, 4.0, 5); }"));
    EXPECT_NE(std::string::npos, code.find("mov #16400, 6(r1)\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov #5, r15
call #u
)")) << code;
}

// A structure goes on the stack whatever its size, and does not stop a later long
// splitting.
TEST_F(Msp430Test, ArgStructOnStack)
{
    std::string code = Code(CompileToMsp430(R"(
        struct S { int a; };
        int u(struct S, int, int, int, long);
        int f(struct S *p) { return u(*p, 1, 2, 3, 4); }
    )"));
    EXPECT_NE(std::string::npos, code.find("mov #0, 2(r1)\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov #4, r15
call #u
)")) << code;
}

// A char goes extended to its register.
TEST_F(Msp430Test, ArgCharExtended)
{
    std::string code = Code(CompileToMsp430(
        "int g(signed char); signed char c; int f(void) { return g(c); }"));
    EXPECT_NE(std::string::npos, code.find(R"(sxt r12
call #g
)")) << code;
}

// A variadic callee takes every argument on the stack.
TEST_F(Msp430Test, ArgsVariadicOnStack)
{
    std::string code = Code(CompileToMsp430(
        "int v(int, ...); int f(void) { return v(1, 2L, 3); }"));
    EXPECT_NE(std::string::npos,
              code.find(R"(mov #1, 0(r1)
mov #2, 2(r1)
mov #0, 4(r1)
mov #3, 6(r1)
call #v
)"))
        << code;
}

// Through a pointer: loaded into r11, not called through an SP-relative operand.
TEST_F(Msp430Test, IndirectCall)
{
    std::string code = Code(CompileToMsp430("int f(int (*fp)(int)) { return fp(7); }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov #7, r12
mov 0(r1), r11
call r11
)")) << code;
}

// Results: r12, r13:r12, r15:r12.
TEST_F(Msp430Test, ResultsStored)
{
    std::string code = Code(CompileToMsp430(
        "long long g(void); long long x; void f(void) { x = g(); }"));
    EXPECT_NE(std::string::npos, code.find(R"(call #g
mov r12, 0(r1)
mov r13, 2(r1)
mov r14, 4(r1)
mov r15, 6(r1)
)"))
        << code;
}

// Every rule, caller and callee ours: each callee checks what it got.
TEST_F(Msp430Test, RunCallRules)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        struct S2 { int a; };
        struct S5 { char c[5]; };
        int in_order(int a, long b, int c) { return a == 1 && b == 70000L && c == 3; }
        int split(int a, int b, int c, long d) { return a + b + c == 6 && d == -70000L; }
        int backfill(int a, long long b, int c, int d, long e)
        {
            return a == 1 && b == 0x123456789aLL && c == 3 && d == 4 && e == 123456L;
        }
        int dbl(int a, int b, int c, double d, int e)
        {
            union { double d; unsigned w[4]; } u; // its bits: no FP runtime yet
            u.d = d;
            return a + b + c == 6 && u.w[3] == 0x4010 && u.w[0] == 0 && e == 5;
        }
        int structs(struct S2 s, int a, int b, int c, long d, struct S5 t)
        {
            return s.a == 9 && a + b + c == 6 && d == 99999L && t.c[0] == 'h' && t.c[4] == 'o';
        }
        int chars(signed char a, unsigned char b, char c) { return a == -2 && b == 250 && c == 'x'; }
        int many(int a, int b, int c, int d, int e, int f, long g)
        {
            return a + b + c + d + e + f == 21 && g == -1;
        }
        int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
        int twice(int (*f)(int), int x) { return f(f(x)); }
        int inc(int x) { return x + 1; }
        int main(void)
        {
            struct S2 s = { 9 };
            struct S5 t = { "hello" };
            if (!in_order(1, 70000L, 3)) return 1;
            if (!split(1, 2, 3, -70000L)) return 2;
            if (!backfill(1, 0x123456789aLL, 3, 4, 123456L)) return 3;
            if (!dbl(1, 2, 3, 4.0, 5)) return 4;
            if (!structs(s, 1, 2, 3, 99999L, t)) return 5;
            if (!chars(-2, 250, 'x')) return 6;
            if (!many(1, 2, 3, 4, 5, 6, -1L)) return 7;
            if (fib(15) != 610) return 8;
            if (twice(inc, 40) != 42) return 9;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
