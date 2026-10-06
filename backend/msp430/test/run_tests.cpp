//
// MSP430 programs, and the runtime itself, on mspsim.  The runtime tests are written in
// C for GCC, whose code calls the helpers of libc.a; the expected output is computed
// here on the host.
//
#include <gtest/gtest-spi.h>

#include <cstdint>

#include "msp430_test.h"

// main's result is mspsim's exit status.
TEST_F(Msp430Test, RunReturn200)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430("int main(void) { return 200; }"));
    EXPECT_EQ(200, exit_status);
}

// The status is the low byte of a 16-bit int.
TEST_F(Msp430Test, RunReturnWideConstant)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430("int main(void) { return 0x1234; }"));
    EXPECT_EQ(0x34, exit_status);
}

// mspsim's own statuses (124 cycle limit, 125 asleep, 132 illegal instruction) are
// also results a program may return; its report tells them apart.
TEST_F(Msp430Test, RunReturnSimulatorStatus)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430("int main(void) { return 132; }"));
    EXPECT_EQ(132, exit_status);
}

TEST_F(Msp430Test, RunBookStatus)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
    EXPECT_EQ(249, exit_status);
}

TEST_F(Msp430Test, RunBookStatusMax)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("32767\n", CompileAndRunBook("int main(void) { return 32767; }"));
}

TEST_F(Msp430Test, RunBookStatusMin)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("-32768\n", CompileAndRunBook("int main(void) { return -32767 - 1; }"));
}

// A program that does not stop itself fails the run, though mspsim's status (132 for
// an illegal instruction) could pass for a result: the fixture reports it.
TEST_F(Msp430Test, RunIllegalInstructionFails)
{
    SKIP_IF_NO_MSP430_TOOLS();
    ::testing::TestPartResultArray results;
    std::string out;
    {
        ::testing::ScopedFakeTestPartResultReporter intercept(
            ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
            &results);
        out = RunAssembly(R"(    .text
    .globl  main
main:
    .short  0
)");
    }
    EXPECT_EQ("ERROR", out);
    ASSERT_EQ(1, results.size());
    EXPECT_NE(std::string::npos,
              std::string(results.GetTestPartResult(0).message()).find("did not stop itself"))
        << results.GetTestPartResult(0).message();
    EXPECT_NE(std::string::npos,
              std::string(results.GetTestPartResult(0).message()).find("Illegal instruction"))
        << results.GetTestPartResult(0).message();
}

// Hand-written assembly prints through putbyte, and returns a status.
TEST_F(Msp430Test, RuntimePutbyte)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("Hi\n", RunAssembly(R"(    .text
    .globl  main
main:
    mov     #'H', r12
    call    #putbyte
    mov     #'i', r12
    call    #putbyte
    mov     #10, r12
    call    #putbyte
    mov     #42, r12
    ret
)"));
    EXPECT_EQ(42, exit_status);
}

// The output routine the runtime tests share.
static const char print_c[] = R"(
void putbyte(int c);
static void puts_(const char *s) { while (*s) putbyte(*s++); }
static void putu(unsigned long v)
{
    char b[12];
    int i = 0;
    do {
        b[i++] = '0' + v % 10u;
        v /= 10u;
    } while (v);
    while (i)
        putbyte(b[--i]);
}
static void puti(long v)
{
    if (v < 0) {
        putbyte('-');
        putu(-(unsigned long)v);
    } else {
        putu(v);
    }
}
)";

// crt0 copies .data from its load address and clears .bss; .rodata stays in ROM.
TEST_F(Msp430Test, RuntimeDataAndBss)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string src = std::string(print_c) + R"(
int data[3] = { 1000, -2, 3 };
char odd[3] = "ab";
const char text[] = "rom";
long bss[20];
int main(void)
{
    long sum = 0;
    for (int i = 0; i < 20; i++)
        sum += bss[i];
    puti(data[0] + data[1] + data[2] + sum);
    putbyte(' ');
    puts_(odd);
    putbyte(' ');
    puts_(text);
    return 0;
}
)";
    EXPECT_EQ("1001 ab rom", GccRun(src));
    EXPECT_EQ(0, exit_status);
}

static const int16_t ops16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
static const int32_t ops32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                                 0, 1, -1, 123456789, 65536, -65537 };

// Division and remainder through the __mspabi_div*/rem* helpers, and products through
// __mspabi_mpyi and __mspabi_mpyl, over operands with every sign and the extremes; the
// most negative dividend over -1 and zero divisors are left out.
TEST_F(Msp430Test, RuntimeDivisionAndMultiplication)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string src = std::string(print_c) + R"(
volatile int a16[] = { 32767, -32768, 1000, -1000, 7, -7, 0, 1, -1, 12345, 255, -129 };
volatile long a32[] = { 2147483647, -2147483647 - 1, 100000, -100000, 7, -7,
                        0, 1, -1, 123456789, 65536, -65537 };
int main(void)
{
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int x = a16[i], y = a16[j];
            if (y != 0 && !(x == -32768 && y == -1)) {
                puti(x / y); putbyte(' '); puti(x % y); putbyte(' ');
            }
            if (y != 0) {
                putu((unsigned)x / (unsigned)y); putbyte(' ');
                putu((unsigned)x % (unsigned)y); putbyte(' ');
            }
            puti((int)((unsigned)x * (unsigned)y)); putbyte(' ');
            long p = a32[i], q = a32[j];
            if (q != 0 && !(p == -2147483647 - 1 && q == -1)) {
                puti(p / q); putbyte(' '); puti(p % q); putbyte(' ');
            }
            if (q != 0) {
                putu((unsigned long)p / (unsigned long)q); putbyte(' ');
                putu((unsigned long)p % (unsigned long)q); putbyte(' ');
            }
            puti((long)((unsigned long)p * (unsigned long)q));
            putbyte('\n');
        }
    return 0;
}
)";
    std::string expected;
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++) {
            int16_t x = ops16[i], y = ops16[j];
            if (y != 0 && !(x == -32768 && y == -1))
                expected += std::to_string(x / y) + " " + std::to_string(x % y) + " ";
            if (y != 0)
                expected += std::to_string((uint16_t)x / (uint16_t)y) + " " +
                            std::to_string((uint16_t)x % (uint16_t)y) + " ";
            expected += std::to_string((int16_t)(uint16_t)((uint32_t)(uint16_t)x * (uint16_t)y)) + " ";
            int32_t p = ops32[i], q = ops32[j];
            if (q != 0 && !(p == INT32_MIN && q == -1))
                expected += std::to_string(p / q) + " " + std::to_string(p % q) + " ";
            if (q != 0)
                expected += std::to_string((uint32_t)p / (uint32_t)q) + " " +
                            std::to_string((uint32_t)p % (uint32_t)q) + " ";
            expected += std::to_string((int32_t)((uint32_t)p * (uint32_t)q)) + "\n";
        }
    EXPECT_EQ(expected, GccRun(src));
    EXPECT_EQ(0, exit_status);
}

// The helpers' corner cases, called directly: a zero divisor gives an all-ones quotient
// and the dividend as the remainder; the most negative dividend over -1 gives itself.
TEST_F(Msp430Test, RuntimeDivisionCorners)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string src = std::string(print_c) + R"(
unsigned __mspabi_divu(unsigned, unsigned);
unsigned __mspabi_remu(unsigned, unsigned);
int __mspabi_divi(int, int);
int __mspabi_remi(int, int);
unsigned long __mspabi_divul(unsigned long, unsigned long);
unsigned long __mspabi_remul(unsigned long, unsigned long);
long __mspabi_divli(long, long);
long __mspabi_remli(long, long);
int main(void)
{
    putu(__mspabi_divu(1234, 0)); putbyte(' ');
    putu(__mspabi_remu(1234, 0)); putbyte(' ');
    puti(__mspabi_divi(-32767 - 1, -1)); putbyte(' ');
    puti(__mspabi_remi(-32767 - 1, -1)); putbyte(' ');
    putu(__mspabi_divul(123456789, 0)); putbyte(' ');
    putu(__mspabi_remul(123456789, 0)); putbyte(' ');
    puti(__mspabi_divli(-2147483647 - 1, -1)); putbyte(' ');
    puti(__mspabi_remli(-2147483647 - 1, -1));
    return 0;
}
)";
    EXPECT_EQ("65535 1234 -32768 0 4294967295 123456789 -2147483648 0", GccRun(src));
}

// Shifts of a long by a variable count through __mspabi_slll, __mspabi_srll and
// __mspabi_sral, for every count 0..31.
TEST_F(Msp430Test, RuntimeLongShifts)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string src = std::string(print_c) + R"(
volatile long v[] = { -2023406815L, 0x12345678L, 1, -1 };
int main(void)
{
    for (int i = 0; i < 4; i++)
        for (volatile int n = 0; n < 32; n++) {
            long x = v[i];
            putu((unsigned long)x << n); putbyte(' ');
            putu((unsigned long)x >> n); putbyte(' ');
            puti(x >> n); putbyte('\n');
        }
    return 0;
}
)";
    static const int32_t v[] = { -2023406815, 0x12345678, 1, -1 };
    std::string expected;
    for (int32_t x : v)
        for (int n = 0; n < 32; n++)
            expected += std::to_string((uint32_t)x << n) + " " +
                        // cppcheck-suppress shiftTooManyBitsSigned ; arithmetic shift expected
                        std::to_string((uint32_t)x >> n) + " " + std::to_string(x >> n) + "\n";
    EXPECT_EQ(expected, GccRun(src));
}

// A stack that ran into the canary below it is reported, with status 0xfd.
TEST_F(Msp430Test, RuntimeStackOverflowReported)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string src = R"(
extern char __stack_canary[2];
int main(void)
{
    __stack_canary[1] = 0;
    return 0;
}
)";
    EXPECT_EQ("stack overflow\n", GccRun(src));
    EXPECT_EQ(0xfd, exit_status);
}

// The types of <stddef.h> and <stdint.h> are GCC's: wchar_t is long, wint_t unsigned int,
// sig_atomic_t and the fast 8-bit types int.
TEST_F(Msp430Test, RunHeaderTypes)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        #include <stddef.h>
        #include <stdint.h>
        int main(void)
        {
            if (sizeof(wchar_t) != 4 || WCHAR_MAX != 2147483647L || WCHAR_MIN >= 0)
                return 1;
            if (WINT_MIN != 0 || WINT_MAX != 65535U || SIG_ATOMIC_MAX != 32767)
                return 2;
            if (sizeof(int_fast8_t) != 2 || INT_FAST8_MAX != 32767 || UINT_FAST8_MAX != 65535U)
                return 3;
            wchar_t w = -70000L;
            return w < 0 ? 0 : 4;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// setjmp/longjmp from libc.a: a jump out of nested frames, longjmp(env, 0) arriving as
// 1, and a second setjmp on the same buffer.
static const char setjmp_program[] = R"(
#include <setjmp.h>
static jmp_buf env;
static int depth;
__attribute__((noinline)) static void dive(int n, int val)
{
    depth = n;
    if (n == 5)
        longjmp(env, val);
    dive(n + 1, val);
}
int main(void)
{
    volatile int round = 0;
    int r = setjmp(env);
    round++;
    if (round == 1) {
        if (r != 0)
            return 1;
        dive(0, 7);
    }
    if (round == 2) {
        if (r != 7 || depth != 5)
            return 2;
        dive(0, 0);
    }
    if (round == 3 && r != 1)
        return 3;
    return round == 3 ? 42 : 4;
}
)";

TEST_F(Msp430Test, RunSetjmpLongjmp)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = setjmp_program;
    src.replace(src.find("__attribute__((noinline)) "), 26, "");
    EXPECT_EQ("", CompileAndRunMsp430(src));
    EXPECT_EQ(42, exit_status);
}

// The same from GCC's and clang's code, which keep values in the call-saved registers,
// with our <setjmp.h>.
TEST_F(Msp430Test, RunSetjmpLongjmpGccClang)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string src = setjmp_program;
    std::vector<std::string> flags = { "-O1", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I",
                                       TEST_MODEL_INCLUDE_DIR, "-I", TEST_COMMON_INCLUDE_DIR };
    EXPECT_EQ("", Run("", "crt0.o", &src, flags, ".gcc"));
    EXPECT_EQ(42, exit_status);
    // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
    if (!msp430_clang_available())
        return;
    EXPECT_EQ("", Run(msp430_clang_config(), "", "crt0.o", &src, flags, ".clang"));
    EXPECT_EQ(42, exit_status);
}

// The loops the induction-variable pass rewrites: a pointer stepped through the array in
// place of the index, the test against an end pointer, the index gone when nothing else
// reads it. main returns the number of the first wrong result, or 0.
TEST_F(Msp430Test, RunReducedLoops)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
int a[10] = { 5, 3, 9, 1, 7, 2, 8, 6, 4, 0 };
long l[4] = { 100000, 200000, 300000, 400000 };

static int up(int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }
static int down(int *p, int n) { int s = 0; for (int i = n - 1; i >= 0; i--) s = s * 2 + p[i]; return s; }
static int rises(int *p, int n)
{
    int k = 0;
    for (int i = 0; i + 1 < n; i++)
        if (p[i] < p[i + 1])
            k++;
    return k;
}
static int find(int *p, int n, int x)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] == x)
            break;
    return i;
}
static unsigned evens(int *p, unsigned n) { unsigned s = 0; for (unsigned i = 0; i < n; i += 2) s += p[i]; return s; }
static long wide(long *p, int n) { long s = 0; for (int i = 0; i != n; i++) s += p[i]; return s; }
static void sort(int *v, int n)
{
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1 - i; j++)
            if (v[j] > v[j + 1]) {
                int t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
}

int main(void)
{
    if (up(a, 10) != 45) return 1;
    if (up(a, 0) != 0) return 2;
    if (down(a, 4) != ((1 * 2 + 9) * 2 + 3) * 2 + 5) return 3;
    if (rises(a, 10) != 3) return 4;
    if (find(a, 10, 7) != 4 || find(a, 10, 11) != 10) return 5;
    if (evens(a, 10) != 5 + 9 + 7 + 8 + 4) return 6;
    if (wide(l, 4) != 1000000) return 7;
    sort(a, 10);
    for (int i = 0; i < 10; i++)
        if (a[i] != i) return 8;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}
