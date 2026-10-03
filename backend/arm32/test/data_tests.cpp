//
// ARM32 static data and globals: sections, initializers, movw/movt addressing.
//
#include "arm32_test.h"

TEST_F(Arm32Test, StaticVariables)
{
    std::string s = CompileToArm32(R"(
int counter = 5;
static long long big = -2;
short zeros[4];
unsigned char bytes[3] = { 1, 2, 3 };
double d = 1.5;
long double ld = 0.1L;
int *p = &counter;
int arr[4];
int *q = &arr[2];
)");
    EXPECT_NE(std::string::npos, s.find(R"(    .data
    .globl  counter
    .p2align 2
    .type   counter, %object
    .size   counter, 4
counter:
    .word   5
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(    .p2align 3
    .type   big, %object
    .size   big, 8
big:
    .word   0xfffffffe, 0xffffffff
)")) << s;
    EXPECT_EQ(std::string::npos, s.find(".globl  big")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(    .bss
    .globl  zeros
    .p2align 1
    .type   zeros, %object
    .size   zeros, 8
zeros:
    .zero   8
)")) << s;
    EXPECT_NE(std::string::npos, s.find("bytes:\n    .byte   1\n    .byte   2\n    .byte   3\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("d:\n    .word   0x00000000, 0x3ff80000\n")) << s;
    // A long double is the double nearest its binary128 value.
    EXPECT_NE(std::string::npos, s.find("    .size   ld, 8\nld:\n    .word   0x9999999a, 0x3fb99999\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("p:\n    .word   counter\n")) << s;
    EXPECT_NE(std::string::npos, s.find("q:\n    .word   arr+8\n")) << s;
}

// A global is reached through its address, from movw and movt.
TEST_F(Arm32Test, GlobalAccess)
{
    std::string code = Code(CompileToArm32(R"(
int counter;
int bump(void) { counter = counter + 1; return counter; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(movw r12, #:lower16:counter
movt r12, #:upper16:counter
ldr r12, [r12]
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(movw lr, #:lower16:counter
movt lr, #:upper16:counter
str r12, [lr]
)")) << code;
}

// A block-scope static is emitted after its function, as a local symbol; a second of
// the same name is name$N.
TEST_F(Arm32Test, StaticLocal)
{
    std::string s = CompileToArm32(R"(
int next(void) { static int n = 10; n = n + 1; return n; }
int other(void) { static int n = 20; return n; }
)");
    size_t fn = s.find(".size   next, .-next");
    size_t n  = s.find("\nn:\n    .word   10\n");
    EXPECT_NE(std::string::npos, n) << s;
    EXPECT_LT(fn, n) << s;
    EXPECT_NE(std::string::npos, s.find("\nn$1:\n    .word   20\n")) << s;
}

TEST_F(Arm32Test, RunGlobalsAndStatics)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int counter = 40;
long long total;
static unsigned char small = 250;
int *p = &counter;
int next(void) { static int n; n = n + 1; return n; }
int other(void) { static int n = 20; return n; }
int main(void) {
    counter = counter + 2;
    total = 1000000000000LL;
    small = small + 10;
    next(); next();
    return (counter == 42) + 2 * (total - 999999999999LL == 1) + 4 * (small == 4)
         + 8 * (next() == 3) + 16 * (other() == 20) + 32 * (p == &counter);
})"));
    EXPECT_EQ(63, exit_status);
}
