//
// AArch64 static data and globals: sections, initializers, adrp + :lo12: addressing.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, StaticVariables)
{
    std::string s = CompileToAarch64(R"(
int counter = 5;
static long big = -1;
short zeros[4];
unsigned char bytes[3] = { 1, 2, 3 };
double d = 1.5;
int *p = &counter;
)");
    EXPECT_NE(std::string::npos, s.find(R"(    .data
    .globl  counter
    .p2align 2
    .type   counter, @object
    .size   counter, 4
counter:
    .word   5
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(big:
    .xword  -1
)")) << s;
    EXPECT_EQ(std::string::npos, s.find(".globl  big")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(    .bss
    .globl  zeros
    .p2align 1
    .type   zeros, @object
    .size   zeros, 8
zeros:
    .zero   8
)")) << s;
    EXPECT_NE(std::string::npos, s.find("bytes:\n    .byte   1\n    .byte   2\n    .byte   3\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("d:\n    .xword  0x3ff8000000000000\n")) << s;
    EXPECT_NE(std::string::npos, s.find("p:\n    .xword  counter\n")) << s;
}

// A global is reached through its page address and the low 12 bits.
TEST_F(Aarch64Test, GlobalAccess)
{
    NaiveSelection();
    std::string code = Code(CompileToAarch64(R"(
int counter;
int bump(void) { counter = counter + 1; return counter; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(adrp x14, counter
add x14, x14, :lo12:counter
ldr w9, [x14]
)")) << code;
    EXPECT_NE(std::string::npos, code.find("str w9, [x14]\n")) << code;
}

// A block-scope static is emitted after its function, as a local symbol.
TEST_F(Aarch64Test, StaticLocal)
{
    std::string s = CompileToAarch64(R"(
int next(void) { static int n = 10; n = n + 1; return n; }
)");
    size_t fn     = s.find(".size   next, .-next");
    size_t n      = s.find("\nn:\n    .word   10\n");
    EXPECT_NE(std::string::npos, n) << s;
    EXPECT_LT(fn, n) << s;
}

TEST_F(Aarch64Test, RunGlobalsAndStatics)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int counter = 40;
long total;
static unsigned char small = 250;
int next(void) { static int n; n = n + 1; return n; }
int main(void) {
    counter = counter + 2;
    total = 1000000000000l;
    small = small + 10;
    next(); next();
    return (counter == 42) + 2 * (total / 1000 == 1000000000) + 4 * (small == 4)
         + 8 * (next() == 3);
})"));
    EXPECT_EQ(15, exit_status);
}
