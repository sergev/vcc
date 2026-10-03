//
// ARM32 variadic functions: the base standard for caller and callee, r0-r3 saved
// below the stack arguments, and <stdarg.h> as macros over that one area.
//
#include "arm32_test.h"

// r0-r3 go below the stack arguments, the named parameters are read in place, and the
// return drops them again.
TEST_F(Arm32Test, VariadicFrame)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
int first(int n, ...) { return n; }
)"));
    EXPECT_EQ(R"(push {r0, r1, r2, r3}
push {r11, lr}
mov r11, sp
ldr r0, [r11, #8]
mov sp, r11
pop {r11, lr}
add sp, sp, #16
bx lr
)",
              code);
}

// A double result goes back in r0:r1, a long long parameter is at the even pair.
TEST_F(Arm32Test, VariadicBaseStandardResult)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
double second(int n, long long k, ...) { return k; }
)"));
    EXPECT_EQ(R"(push {r0, r1, r2, r3}
push {r11, lr}
mov r11, sp
sub sp, sp, #8
ldr r0, [r11, #16]
ldr r1, [r11, #20]
bl __aeabi_l2d
str r0, [r11, #-8]
str r1, [r11, #-4]
ldr r0, [r11, #-8]
ldr r1, [r11, #-4]
mov sp, r11
pop {r11, lr}
add sp, sp, #16
bx lr
)",
              code);
}

TEST_F(Arm32Test, RunVariadic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
#include <stdarg.h>
struct s3 { char a, b, c; };
struct f2 { float a, b; };
struct big { int a[5]; };
int isum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}
double mixed(char c, ...)
{
    va_list ap, aq;
    va_start(ap, c);
    double d = va_arg(ap, double);
    long long k = va_arg(ap, long long);
    va_copy(aq, ap);
    struct s3 s = va_arg(ap, struct s3);
    struct f2 f = va_arg(ap, struct f2);
    struct big b = va_arg(ap, struct big);
    long double ld = va_arg(ap, long double);
    int again = va_arg(aq, struct s3).c;
    va_end(aq);
    va_end(ap);
    return c + d + k + s.a + s.c + f.a + f.b + b.a[0] + b.a[4] + ld + again;
}
float half(int n, ...) { return n / 2.0f; }
struct f2 pair(int n, ...) { struct f2 r = { n, n + 1 }; return r; }
int vsum(int n, va_list ap) { int s = 0; while (n--) s += va_arg(ap, int); return s; }
int fwd(int n, ...) { va_list ap; va_start(ap, n); int s = vsum(n, ap); va_end(ap); return s; }
int main(void) {
    struct s3 s = { 1, 2, 3 };
    struct f2 f = { 0.5f, 0.25f };
    struct big b = { { 10, 0, 0, 0, 20 } };
    double (*mp)(char, ...) = mixed;
    return (isum(6, 1, 2, 3, 4, 5, 6) == 21)
         + 2 * (mixed(1, 2.5, 1000000000000LL, s, f, b, 0.125L) == 1 + 2.5 + 1e12 + 1 + 3 + 0.75 + 30 + 0.125 + 3)
         + 4 * (mp(1, 2.5, 1LL, s, f, b, 0.0L) == 1 + 2.5 + 1 + 4 + 0.75 + 30 + 3)
         + 8 * (half(5) == 2.5f && pair(3).b == 4)
         + 16 * (fwd(3, 7, 8, 9) == 24);
})"));
    EXPECT_EQ(31, exit_status);
}

TEST_F(Arm32Test, RunPrintf)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("42 -7 ff hello 3.25 1e+20 x 12345678901 0.5\n", CompileAndRunArm32(R"(
#include <stdio.h>
int main(void) {
    long double h = 0.5L;
    printf("%d %d %x %s %g %g %c %lld %Lg\n", 42, -7, 255, "hello", 3.25, 1e20, 'x',
           12345678901LL, h);
    return 0;
})"));
}

// Variadic functions both ways with clang, and a va_list handed across both ways.
TEST_F(Arm32Test, RunVariadicInteropWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    const char *decls = R"(
#include <stdarg.h>
struct f2 { float a, b; };
struct s5 { char a[5]; };
)";
    std::string ours  = std::string(decls) + R"(
double theirs(int n, ...);
double theirs_v(int n, va_list ap);
float theirs_f(int n, ...);
int call_ours(void);
double ours(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = va_arg(ap, int);
    s += va_arg(ap, double);
    s += va_arg(ap, long long);
    struct f2 f = va_arg(ap, struct f2);
    struct s5 c = va_arg(ap, struct s5);
    s += f.a + f.b + c.a[0] + c.a[4];
    s += va_arg(ap, double);
    va_end(ap);
    return s;
}
double ours_v(int n, va_list ap) { double s = 0; while (n--) s += va_arg(ap, double); return s; }
double through_theirs(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = theirs_v(n, ap);
    va_end(ap);
    return s;
}
int main(void) {
    struct f2 f = { 0.5f, 0.25f };
    struct s5 c = { { 1, 0, 0, 0, 2 } };
    return (theirs(0, 1, 2.5, 3LL, f, c, 4.0) == 1 + 2.5 + 3 + 0.75 + 3 + 4)
         + 2 * (through_theirs(3, 1.0, 2.0, 4.5) == 7.5)
         + 4 * (theirs_f(7) == 3.5f) + 8 * call_ours();
})";
    std::string theirs = std::string(decls) + R"(
double ours(int n, ...);
double ours_v(int n, va_list ap);
double theirs(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = va_arg(ap, int);
    s += va_arg(ap, double);
    s += va_arg(ap, long long);
    struct f2 f = va_arg(ap, struct f2);
    struct s5 c = va_arg(ap, struct s5);
    s += f.a + f.b + c.a[0] + c.a[4];
    s += va_arg(ap, double);
    va_end(ap);
    return s;
}
double theirs_v(int n, va_list ap) { double s = 0; while (n--) s += va_arg(ap, double); return s; }
float theirs_f(int n, ...) { return n / 2.0f; }
static double through_ours(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = ours_v(n, ap);
    va_end(ap);
    return s;
}
int call_ours(void)
{
    struct f2 f = { 0.5f, 0.25f };
    struct s5 c = { { 1, 0, 0, 0, 2 } };
    return ours(0, 1, 2.5, 3LL, f, c, 4.0) == 1 + 2.5 + 3 + 0.75 + 3 + 4 &&
           through_ours(2, 1.5, 2.0) == 3.5;
}
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(15, exit_status);
}
