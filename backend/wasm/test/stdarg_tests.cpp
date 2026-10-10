//
// wasm32 variadics: the caller fills a buffer with the variable arguments, each in a
// slot of at least 4 bytes aligned to its type, and passes its address last; va_start
// expands in place, va_arg walks the buffer (stdarg.h).
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// The buffer: a char promoted to an int at 0, a double at 8, a long double at 16, a
// structure by reference as a pointer at 32 to its copy at 36.
TEST_F(WasmTest, VariadicBuffer)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(R"(
        struct Two { int a, b; };
        void v(int n, ...);
        void f(char c, struct Two t) { v(1, c, 2.0, 3.0L, t); }
    )"));
    EXPECT_NE(s.find("i32.const 48\ni32.sub\n"), std::string::npos) << s;
    EXPECT_NE(s.find("f64.const 0x1p+1\nf64.store 8\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i64.store 16\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i64.store 24\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i32.const 36\ni32.add\ni32.store 32\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i32.const 1\nlocal.get 3\ncall v\n"), std::string::npos) << s;
}

// va_start stores the buffer's address, the parameter after the named ones, into ap.
TEST_F(WasmTest, VaStart)
{
    NaiveSelection();
    std::string s = CompileToWasm(R"(
        #include <stdarg.h>
        int first(int n, ...) { va_list ap; va_start(ap, n); int k = va_arg(ap, int); va_end(ap); return k; }
    )");
    EXPECT_EQ(s.find("__va_start"), std::string::npos) << s;
    EXPECT_NE(Code(s).find("local.get 1\ni32.store 0\n"), std::string::npos) << s;
}

static const char variadic_src[] = R"(
#include <stdarg.h>
struct Two { int a, b; };
struct C { char c; };
struct D { double d; };
struct Big { long long x; char s[10]; };

/* Sums its arguments by the letters of `fmt`. */
long long PFX_sum(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    long long s = 0;
    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'i': s += va_arg(ap, int); break;
        case 'l': s += va_arg(ap, long long); break;
        case 'd': s += (long long)va_arg(ap, double); break;
        case 'L': { /* its exponent: no long double arithmetic here */
            union { long double l; unsigned long long w[2]; } u;
            u.l = va_arg(ap, long double);
            s += (long long)(u.w[1] >> 48) - 0x4000;
            break;
        }
        case 't': { struct Two t = va_arg(ap, struct Two); s += t.a * 10 + t.b; break; }
        case 'c': s += va_arg(ap, struct C).c; break;
        case 'D': s += (long long)va_arg(ap, struct D).d; break;
        case 'B': { struct Big b = va_arg(ap, struct Big); s += b.x + b.s[9]; break; }
        case 'p': s += *va_arg(ap, int *); break;
        }
    }
    va_end(ap);
    return s;
}

/* A va_list passed on, and copied. */
static long long vsum(int n, va_list ap)
{
    va_list aq;
    va_copy(aq, ap);
    long long s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(aq, int);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(aq);
    return s;
}
long long PFX_twice(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long long s = vsum(n, ap);
    va_end(ap);
    return s;
}
)";

static const char variadic_check[] = R"(
long long OTHER_sum(const char *fmt, ...);
long long OTHER_twice(int n, ...);
int PFX_check(void)
{
    struct Two t = { 3, 4 };
    struct C c = { 'A' };
    struct D d = { 2.5 };
    struct Big b = { 1000, "abcdefghi" };
    int k = 77;
    char ch = -2;
    short sh = -300;
    float f = 1.5f;
    if (OTHER_sum("i", 5) != 5)
        return 1;
    if (OTHER_sum("ilid", ch, 1LL << 40, sh, 7.75) != -2 + (1LL << 40) - 300 + 7)
        return 2;
    if (OTHER_sum("idLi", 1, (double)f, 6.0L, 9) != 1 + 1 + 1 + 9)
        return 3;
    if (OTHER_sum("tcDBp", t, c, d, b, &k) != 34 + 'A' + 2 + 1000 + 0 + 77)
        return 4;
    if (OTHER_sum("itictLi", 1, t, 2, c, t, 0.25L, 3) != 1 + 34 + 2 + 'A' + 34 - 3 + 3)
        return 5;
    if (t.a != 3 || b.s[0] != 'a')
        return 6;
    if (OTHER_twice(3, 10, 20, 30) != 120 || OTHER_twice(0) != 0)
        return 7;
    return 0;
}
)";

// `text` with every PFX replaced by `pfx` and every OTHER by `other`.
static std::string Subst(std::string text, const std::string &pfx, const std::string &other)
{
    for (const auto &[from, to] :
         { std::pair{ std::string("PFX"), pfx }, std::pair{ std::string("OTHER"), other } })
        for (size_t at; (at = text.find(from)) != std::string::npos;)
            text.replace(at, from.size(), to);
    return text;
}

// Run: every kind of variable argument, va_list passed on and copied.
TEST_F(WasmTest, RunVariadic)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(Subst(variadic_src, "our", "our") +
                                    Subst(variadic_check, "our", "our") +
                                    "int main(void) { return our_check(); }"));
    EXPECT_EQ(0, exit_status);
}

// Run: our variadic functions called by clang's code (with clang's own <stdarg.h>),
// and clang's called by ours.
TEST_F(WasmTest, RunVariadicWithClang)
{
    SKIP_IF_NO_WASM32_CLANG();
    std::string ours = Subst(variadic_src, "our", "their") + Subst(variadic_check, "our", "their") +
                       "int their_check(void);\n"
                       "int main(void) { return our_check() | their_check() << 4; }";
    std::string theirs =
        Subst(variadic_src, "their", "our") + Subst(variadic_check, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(0, exit_status);
}
