//
// x86-64 variadic functions: the register save area, va_start, and va_arg over every
// System V class, from registers and from the overflow area.
//
#include "x86_test.h"

// A variadic function saves rdi-r9, and xmm0-xmm7 unless %al is zero; va_start is
// expanded in place: gp_offset past the named n, fp_offset past the named d, the stack
// arguments above the return address.
TEST_F(X86Test, VariadicSavesRegisters)
{
    std::string code = Code(CompileToX86(R"(
#include <stdarg.h>
int f(int n, double d, ...) { va_list ap; va_start(ap, d); n = va_arg(ap, int); va_end(ap); return n; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(movq %rdi, -176(%rbp)
movq %rsi, -168(%rbp)
movq %rdx, -160(%rbp)
movq %rcx, -152(%rbp)
movq %r8, -144(%rbp)
movq %r9, -136(%rbp)
testb %al, %al
je .Lx0
movsd %xmm0, -128(%rbp)
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(movsd %xmm7, -16(%rbp)
movl %edi, )")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(movl $8, (%rax)
movl $64, 4(%rax)
leaq 16(%rbp), %r10
movq %r10, 8(%rax)
leaq -176(%rbp), %r10
movq %r10, 16(%rax)
)")) << code;
    EXPECT_NE(std::string::npos, code.find("call __va_arg\n")) << code;
    EXPECT_EQ(std::string::npos, code.find("call __va_start\n")) << code;
}

// __builtin_va_class: MEMORY 0, INTEGER 1, SSE 2, X87 3, two eightbytes as
// class0 | class1 << 2.
TEST_F(X86Test, VaClass)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
struct s16 { long a, b; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct dl { double d; long l; };
struct ld1 { long double v; };
struct fc { float f; char c; };
int main(void)
{
    static const int c[] = {
        __builtin_va_class(int), __builtin_va_class(char *), __builtin_va_class(struct s16),
        __builtin_va_class(struct s24), __builtin_va_class(float), __builtin_va_class(double),
        __builtin_va_class(long double), __builtin_va_class(struct f3),
        __builtin_va_class(struct dl), __builtin_va_class(struct ld1),
        __builtin_va_class(struct fc),
    };
    static const int want[] = { 1, 1, 5, 0, 2, 2, 3, 10, 6, 3, 1 };
    for (int i = 0; i < 11; i++)
        if (c[i] != want[i])
            return 100 + i;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// va_arg of every class, from registers and then from the overflow area, and a
// va_list handed on to another function (as a pointer: va_list is an array).
TEST_F(X86Test, RunVaArg)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
#include <stdarg.h>
struct s12 { int a, b, c; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct dl { double d; long l; };
struct ld { long l; double d; };
long sum(int n, va_list ap)
{
    long t = 0;
    for (int i = 0; i < n; i++) {
        switch (va_arg(ap, int)) {
        case 'i': t += va_arg(ap, int); break;
        case 'l': t += va_arg(ap, long); break;
        case 'd': t += (long)va_arg(ap, double); break;
        case 'L': t += (long)va_arg(ap, long double); break;
        case 's': { struct s12 v = va_arg(ap, struct s12); t += v.a + v.b + v.c; break; }
        case 'S': { struct s24 w = va_arg(ap, struct s24); t += w.a + w.b + w.c; break; }
        case 'f': { struct f3 x = va_arg(ap, struct f3); t += (long)(x.x + x.y + x.z); break; }
        case 'm': { struct dl y = va_arg(ap, struct dl); t += (long)y.d + y.l; break; }
        case 'M': { struct ld z = va_arg(ap, struct ld); t += z.l + (long)z.d; break; }
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
    struct dl m = { 2.5, 40 };
    struct ld M = { 50, 3.5 };
    long r1 = add(4, 'i', 5, 'd', 2.5, 'l', 100L, 'L', 7.5L);
    long r2 = add(16, 'i', 1, 's', s, 'S', S, 'f', f, 'm', m, 'd', 1.0, 'd', 2.0, 'd', 3.0,
                  'M', M, 'd', 4.0, 'L', 9.0L, 'm', m, 'i', 1000, 'f', f, 'M', M, 'l', 7L);
    if (r1 != 114)
        return 1;
    if (r2 != 1 + 6 + 60 + 4 + 42 + 1 + 2 + 3 + 53 + 4 + 9 + 42 + 1000 + 4 + 53 + 7)
        return 2;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// printf from libc.a, through __doprnt and va_arg.
TEST_F(X86Test, RunPrintf)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("42 -7 ff hello 2.50 x\n", CompileAndRunX86(R"(
#include <stdio.h>
int main(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%s", "hello");
    printf("%d %ld %x %s %.2f %c\n", 42, -7L, 255, buf, 2.5, 'x');
    return 0;
})"));
}

// va_list is an array: as a parameter it adjusts to a pointer, inside a struct it
// stays an array (24 bytes, copied whole), and through a pointer it decays again.
TEST_F(X86Test, RunVaListArrayType)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
#include <stdarg.h>
struct holder { int tag; va_list ap; };
_Static_assert(sizeof(va_list) == 24, "va_list is one __va_list_tag");
_Static_assert(sizeof(struct holder) == 32, "an array member");
int next(va_list *app) { return va_arg(*app, int); }
int take(va_list ap) { return va_arg(ap, int); }
int f(int n, ...)
{
    struct holder h, k;
    va_start(h.ap, n);
    k = h; // a copy of the array inside
    int a = next(&h.ap);
    int b = take(h.ap);
    int c = va_arg(k.ap, int);
    va_end(h.ap);
    return a * 100 + b * 10 + c + (sizeof(va_list) == 24) * 1000;
}
int main(void) { return f(3, 4, 5, 6) == 1454 ? 42 : 1; }
)");
    EXPECT_EQ(42, exit_status);
}
