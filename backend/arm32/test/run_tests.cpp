//
// ARM32 programs, and the runtime itself, on bare-metal qemu.
//
#include "arm32_test.h"

TEST_F(Arm32Test, RunReturn2)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32("int main(void) { return 2; }"));
    EXPECT_EQ(2, exit_status);
}

TEST_F(Arm32Test, RunReturnWideConstant)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32("int main(void) { return 0x12345678; }"));
    EXPECT_EQ(0x78, exit_status);
}

TEST_F(Arm32Test, RunBookStatus)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
}

TEST_F(Arm32Test, RunBookStatusMax)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("2147483647\n", CompileAndRunBook("int main(void) { return 2147483647; }"));
}

TEST_F(Arm32Test, RunBookStatusMin)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("-2147483648\n", CompileAndRunBook("int main(void) { return -2147483647 - 1; }"));
}

// crt0 turns the MMU on: RAM is Normal memory, where an unaligned load is legal (it
// faults on the Strongly-ordered memory of a disabled MMU); VFP is enabled; sdiv is
// there.
TEST_F(Arm32Test, RuntimeUnalignedLoadFloatingPointDivide)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", RunAssembly(R"(    .syntax unified
    .arm
    .text
    .globl  main
main:
    movw    r0, #:lower16:buf
    movt    r0, #:upper16:buf
    ldr     r1, [r0, #1]
)"                                                        // bytes 1-4 of {1, 4}
                              "    lsr     r1, r1, #24\n" // 4
                              R"(    vmov.f64 d0, #1.5
    vadd.f64 d0, d0, d0
    vcvt.s32.f64 s0, d0
    vmov    r2, s0
)"                                                        // 3
                              R"(    mov     r3, #-42
    mov     r12, #21
    sdiv    r3, r3, r12
)"                                                        // -2
                              R"(    add     r0, r1, r2
    add     r0, r0, r3
)"                                                        // 4 + 3 - 2
                              R"(    bx      lr
    .data
buf:
    .word   1, 4
)"));
    EXPECT_EQ(5, exit_status);
}

// An exception is reported, and ends the run with status 255 instead of hanging.
TEST_F(Arm32Test, RuntimeExceptionReported)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string out = RunAssembly(R"(    .syntax unified
    .arm
    .text
    .globl  main
main:
    mov     r0, #0x80000000
)" // unmapped: translation fault
                                  R"(    ldr     r1, [r0]
    bx      lr
)");
    EXPECT_EQ(255, exit_status);
    EXPECT_NE(std::string::npos, out.find("exception: vector 00000004 lr=")) << out;
    EXPECT_NE(std::string::npos, out.find("dfsr=00000005 dfar=80000000")) << out;
}

// An undefined instruction too: the handler leaves the exception mode, which has no
// stack, before calling exit.
TEST_F(Arm32Test, RuntimeUndefinedInstructionReported)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string out = RunAssembly(R"(    .syntax unified
    .arm
    .text
    .globl  main
main:
    udf     #0
    bx      lr
)");
    EXPECT_EQ(255, exit_status);
    EXPECT_NE(std::string::npos, out.find("exception: vector 00000001 lr=")) << out;
}
