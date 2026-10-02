//
// AArch64 programs, and the runtime itself, on bare-metal qemu.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, RunReturn2)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64("int main(void) { return 2; }"));
    EXPECT_EQ(2, exit_status);
}

TEST_F(Aarch64Test, RunReturnWideConstant)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64("int main(void) { return 0x12345678; }"));
    EXPECT_EQ(0x78, exit_status);
}

TEST_F(Aarch64Test, RunBookStatus)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
}

TEST_F(Aarch64Test, RunBookStatusMax)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("2147483647\n", CompileAndRunBook("int main(void) { return 2147483647; }"));
}

TEST_F(Aarch64Test, RunBookStatusMin)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("-2147483648\n", CompileAndRunBook("int main(void) { return -2147483647 - 1; }"));
}

// crt0 turns the MMU on: RAM is Normal memory, where an unaligned load is legal (it
// faults on the Device memory of a disabled MMU); FP/SIMD is enabled.
TEST_F(Aarch64Test, RuntimeUnalignedLoadAndFloatingPoint)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", RunAssembly(R"(
    .text
    .globl  main
main:
    adrp    x0, buf
    add     x0, x0, :lo12:buf
    ldr     x1, [x0, #1]
    lsr     x1, x1, #56         // the low byte of the second word
    fmov    d0, #1.5
    fadd    d0, d0, d0
    fcvtzs  w2, d0
    add     w0, w1, w2          // 4 + 3
    ret
    .data
buf:
    .quad   1, 4
)"));
    EXPECT_EQ(7, exit_status);
}

// An exception is reported, and ends the run with status 255 instead of hanging.
TEST_F(Aarch64Test, RuntimeExceptionReported)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    std::string out = RunAssembly(R"(
    .text
    .globl  main
main:
    mov     x0, #0x80000000     // unmapped: translation fault
    ldr     x1, [x0]
    ret
)");
    EXPECT_EQ(255, exit_status);
    EXPECT_NE(std::string::npos,
              out.find("exception: vector 0000000000000004 esr=0000000096000005"))
        << out;
    EXPECT_NE(std::string::npos, out.find("far=0000000080000000")) << out;
}
