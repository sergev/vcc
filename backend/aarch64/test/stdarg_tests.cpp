//
// AArch64 variadic functions: the register save areas, va_start, and va_arg over
// every argument class (backend/aarch64/Plan.md, A18).
//
#include "aarch64_test.h"

// A variadic function saves the argument registers past its named parameters.
TEST_F(Aarch64Test, VariadicSavesRegisters)
{
    std::string code = Code(CompileToAarch64(R"(
#include <stdarg.h>
int f(int n, double d, ...) { va_list ap; va_start(ap, d); n = va_arg(ap, int); va_end(ap); return n; }
)"));
    EXPECT_NE(std::string::npos, code.find("str x1, [x29, #")) << code;
    EXPECT_EQ(std::string::npos, code.find("str x0, [x29, #-")) << code; // n is not saved twice
    EXPECT_NE(std::string::npos, code.find("str q1, [x29, #")) << code;
    EXPECT_NE(std::string::npos, code.find("bl __va_arg\n")) << code;
    EXPECT_EQ(std::string::npos, code.find("bl __va_start\n")) << code;
}

// __builtin_va_class: general, by reference, and FP as element size * 8 + count.
TEST_F(Aarch64Test, VaClass)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
struct s16 { long a, b; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct d4 { double d[4]; };
union fu { float f; struct { float a; } s; };
struct mixed { float f; double d; };
int main(void)
{
    static const int c[] = {
        __builtin_va_class(int), __builtin_va_class(char *), __builtin_va_class(struct s16),
        __builtin_va_class(struct s24), __builtin_va_class(float), __builtin_va_class(double),
        __builtin_va_class(long double), __builtin_va_class(struct f3),
        __builtin_va_class(struct d4), __builtin_va_class(union fu),
        __builtin_va_class(struct mixed),
    };
    static const int want[] = { 0, 0, 0, 1, 33, 65, 129, 35, 68, 33, 0 };
    for (int i = 0; i < 11; i++)
        if (c[i] != want[i])
            return 100 + i;
    return 0;
})"));
    EXPECT_EQ(0, exit_status);
}

// va_arg of every class, from registers and then from the stack, and a va_list handed
// on to another function.
TEST_F(Aarch64Test, RunVaArg)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
#include <stdarg.h>
struct s12 { int a, b, c; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct al { _Alignas(16) long a; long b; };
long sum(int n, va_list ap)
{
    long t = 0;
    for (int i = 0; i < n; i++) {
        switch (va_arg(ap, int)) {
        case 'i': t += va_arg(ap, int); break;
        case 'l': t += va_arg(ap, long); break;
        case 'd': t += (long)va_arg(ap, double); break;
        case 's': { struct s12 v = va_arg(ap, struct s12); t += v.a + v.b + v.c; break; }
        case 'S': { struct s24 w = va_arg(ap, struct s24); t += w.a + w.b + w.c; break; }
        case 'f': { struct f3 x = va_arg(ap, struct f3); t += (long)(x.x + x.y + x.z); break; }
        case 'a': { struct al y = va_arg(ap, struct al); t += y.a + y.b; break; }
        }
    }
    return t;
}
long add(int n, ...)
{
    va_list ap, aq;
    va_start(ap, n);
    va_copy(aq, ap);
    long t = sum(n, ap);
    long u = sum(n, aq);
    va_end(aq);
    va_end(ap);
    return t == u ? t : -1;
}
int main(void)
{
    struct s12 s = { 1, 2, 3 };
    struct s24 S = { 10, 20, 30 };
    struct f3 f = { 0.5f, 1.5f, 2.0f };
    struct al a = { 7, 8 };
    long r1 = add(3, 'i', 5, 'd', 2.5, 'l', 100L);
    long r2 = add(14, 'i', 1, 's', s, 'S', S, 'f', f, 'd', 1.0, 'd', 2.0, 'd', 3.0, 'd', 4.0,
                  'd', 5.0, 'd', 6.0, 'f', f, 'a', a, 'i', 1000, 'f', f);
    return (r1 == 107) + 2 * (r2 == 1 + 6 + 60 + 4 + 21 + 4 + 15 + 1000 + 4);
})"));
    EXPECT_EQ(3, exit_status);
}
