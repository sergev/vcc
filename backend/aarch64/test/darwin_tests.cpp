//
// macOS on Apple silicon (genaarch64 --darwin): Mach-O spelling, the GOT for what the
// unit does not define, Apple's arm64 calling convention, long double as double; and
// programs that run natively against libSystem.
//
#include "aarch64_test.h"

// alloca under Apple's ABI: the frame from x29 (which Apple wants anyway), the memory
// above the outgoing area, where named int arguments on the stack take 4 bytes each:
// two of them, 8 bytes, rounded to 16.
TEST_F(Aarch64Test, DarwinAlloca)
{
    std::string code = Code(CompileToAarch64(R"(
void *__builtin_alloca(unsigned long);
int g(int, int, int, int, int, int, int, int, int, int);
int f(int n)
{
    int *p = __builtin_alloca(n);
    p[0] = n;
    return g(1, 2, 3, 4, 5, 6, 7, 8, p[0], n) + p[0];
}
)"));
    EXPECT_EQ(0u, code.find("stp x29, x30, [sp, #-16]!\nmov x29, sp\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sub sp, sp, x9\nadd x19, sp, #16\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov sp, x29\nldp x29, x30, [sp], #16\nret\n")) << code;
}

// C names take a `_`, local labels an `L`; no ELF directives.
TEST_F(Aarch64Test, DarwinNames)
{
    std::string s = CompileToAarch64(R"(
int g = 1;
static int count(int n) { int k = 0; while (n--) k += g; return k; }
int main(void) { return count(3); }
)");
    EXPECT_NE(std::string::npos, s.find("    .globl  _g\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\n_g:\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\n_count:\n")) << s;
    EXPECT_NE(std::string::npos, s.find("    .globl  _main\n    .p2align 2\n_main:\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\nL")) << s;
    EXPECT_EQ(std::string::npos, s.find(".L")) << s;
    EXPECT_EQ(std::string::npos, s.find(".type")) << s;
    EXPECT_EQ(std::string::npos, s.find(".size")) << s;
    EXPECT_EQ(std::string::npos, s.find(":lo12:")) << s;
}

// The sections: initialized data, zeroes, a string constant; and a table of addresses,
// which the dynamic linker rebases, never in __TEXT.
TEST_F(Aarch64Test, DarwinSections)
{
    std::string s = CompileToAarch64(R"(
int data = 5;
int zeroes[10];
const char *msg(void) { return "text"; }
)");
    EXPECT_NE(std::string::npos, s.find("    .data\n    .globl  _data\n")) << s;
    EXPECT_NE(std::string::npos, s.find("    .section __DATA,__bss\n    .globl  _zeroes\n")) << s;
    EXPECT_NE(std::string::npos, s.find("    .const\n    .p2align 0\n__str")) << s;
    EXPECT_NE(std::string::npos, s.find(".ascii  \"text\"")) << s;
}

// An address in a static initializer: `.xword` of the `_` name.
TEST_F(Aarch64Test, DarwinAddressInitializer)
{
    std::string s = CompileToAarch64(R"(
int x;
int *p = &x;
)");
    EXPECT_NE(std::string::npos, s.find("\n_p:\n    .xword  _x\n")) << s;
}

// What the unit defines is reached PC-relative by page and offset; what it does not,
// data and a function's address alike, through its GOT entry.  A call is direct either
// way: the linker makes a stub.
TEST_F(Aarch64Test, DarwinGotAccess)
{
    std::string s = Code(CompileToAarch64(R"(
extern int ext;
extern int efn(void);
int own;
int get(void) { return ext + own; }
void *addr(void) { return (void *)efn; }
int call(void) { return efn(); }
)"));
    EXPECT_NE(std::string::npos,
              s.find("adrp x14, _ext@GOTPAGE\nldr x14, [x14, _ext@GOTPAGEOFF]\nldr w9, [x14]\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("_own@PAGE\n")) << s;
    EXPECT_NE(std::string::npos, s.find("_own@PAGEOFF\n")) << s;
    EXPECT_EQ(std::string::npos, s.find("_own@GOT")) << s;
    EXPECT_NE(std::string::npos, s.find("_efn@GOTPAGE\n")) << s;
    EXPECT_NE(std::string::npos, s.find("bl _efn\n")) << s;
}

// A named argument past the registers takes its own size and alignment on the stack:
// the char at sp+0, the short at sp+2, the int at sp+4; the float still in s0.
TEST_F(Aarch64Test, DarwinNamedStackPacking)
{
    NaiveSelection();
    std::string s = Code(CompileToAarch64(R"(
int f(long a, long b, long c, long d, long e, long f, long g, long h,
      char i, short j, int k, float l);
int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12.0f); }
)"));
    EXPECT_NE(std::string::npos, s.find("strb w9, [sp]\n")) << s;
    EXPECT_NE(std::string::npos, s.find("strh w9, [sp, #2]\n")) << s;
    EXPECT_NE(std::string::npos, s.find("str w9, [sp, #4]\n")) << s;
    EXPECT_NE(std::string::npos, s.find("fmov s0, w17\nbl _f\n")) << s;
}

// Every variadic argument goes on the stack, in an 8-byte slot, though registers are
// free; the named one still in x0.
TEST_F(Aarch64Test, DarwinVariadicOnStack)
{
    NaiveSelection();
    std::string s = Code(CompileToAarch64(R"(
int printf(const char *fmt, ...);
int main(void) { return printf("%d %f %d", 1, 2.0, 3); }
)"));
    EXPECT_NE(std::string::npos, s.find("str w9, [sp]\n")) << s;
    EXPECT_NE(std::string::npos, s.find("str d16, [sp, #8]\n")) << s;
    EXPECT_NE(std::string::npos, s.find("str w9, [sp, #16]\n")) << s;
    EXPECT_EQ(std::string::npos, s.find(", w1")) << s;
    EXPECT_EQ(std::string::npos, s.find("d0")) << s;
}

// va_start is the address of the first variadic argument: x29 + 16, past the frame
// record, when no named argument is on the stack; no register is saved.
TEST_F(Aarch64Test, DarwinVaStart)
{
    NaiveSelection();
    std::string s = Code(CompileToAarch64(R"(
#include <stdarg.h>
int first(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int r = va_arg(ap, int);
    va_end(ap);
    return r;
}
)"));
    EXPECT_NE(std::string::npos, s.find("add x10, x29, #16\nstr x10, [x9]\n")) << s;
    EXPECT_EQ(std::string::npos, s.find("q7")) << s;
}

// long double is double: in d registers, with no binary128 helper.
TEST_F(Aarch64Test, DarwinLongDouble)
{
    std::string s = Code(CompileToAarch64(R"(
long double scale(long double x, int n) { return x * n + 0.5L; }
)"));
    EXPECT_NE(std::string::npos, s.find("scvtf d1, w0\nfmul d0, d0, d1\n")) << s;
    EXPECT_NE(std::string::npos, s.find("fadd d0, d0, d17\nret\n")) << s;
    EXPECT_EQ(std::string::npos, s.find("bl ")) << s;
}

// A program against libSystem: stdio through __stdoutp, errno through __error(), the
// heap, qsort calling back into our code, setjmp, and long double through printf.
TEST_F(Aarch64Test, DarwinRunLibSystem)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    std::string out = CompileAndRunAarch64(R"(
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static jmp_buf env;
int main(void)
{
    int a[] = { 4, 1, 3, 2 };
    qsort(a, 4, sizeof a[0], cmp);
    errno = 0;
    strtol("99999999999999999999", NULL, 10);
    char *p = malloc(8);
    strcpy(p, "heap");
    fprintf(stdout, "%d%d%d%d %d %s %Lg\n", a[0], a[1], a[2], a[3], errno == ERANGE, p,
            1.5L * 3);
    free(p);
    int r = setjmp(env);
    if (r == 0)
        longjmp(env, 9);
    return r;
}
)");
    EXPECT_EQ("1234 1 heap 4.5\n", out);
    EXPECT_EQ(9, exit_status);
}

// Extern data and a library function's address through the GOT, from a program linked
// with a part clang compiled.
TEST_F(Aarch64Test, DarwinRunGot)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    std::string out = CompileAndRunWithClang(R"(
#include <stdio.h>
extern int shared[3];
int main(void)
{
    int (*say)(const char *) = puts;
    say("via the GOT");
    return shared[0] + shared[2];
}
)",
                                             "int shared[3] = { 10, 20, 30 };\n");
    EXPECT_EQ("via the GOT\n", out);
    EXPECT_EQ(40, exit_status);
}
