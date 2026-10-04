//
// x86-64 programs, and the runtime itself, on bare-metal qemu.
//
#include "x86_test.h"

// The whole status byte comes back: qemu's exit device alone would lose bit 7.
TEST_F(X86Test, RunReturn200)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("", CompileAndRunX86("int main(void) { return 200; }"));
    EXPECT_EQ(200, exit_status);
}

TEST_F(X86Test, RunReturnWideConstant)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("", CompileAndRunX86("int main(void) { return 0x12345678; }"));
    EXPECT_EQ(0x78, exit_status);
}

TEST_F(X86Test, RunBookStatus)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
    EXPECT_EQ(249, exit_status);
}

TEST_F(X86Test, RunBookStatusMax)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("2147483647\n", CompileAndRunBook("int main(void) { return 2147483647; }"));
}

TEST_F(X86Test, RunBookStatusMin)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("-2147483648\n", CompileAndRunBook("int main(void) { return -2147483647 - 1; }"));
}

// crt0 enables SSE and the x87: an unaligned load, an SSE and an x87 operation.
TEST_F(X86Test, RuntimeUnalignedLoadSseX87)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("", RunAssembly(R"(    .text
    .globl  main
main:
    movl    buf+1(%rip), %eax       # bytes 1-4 of {1, 4}
    shrl    $24, %eax               # 4
    movsd   onehalf(%rip), %xmm0
    addsd   %xmm0, %xmm0
    cvttsd2si %xmm0, %ecx           # 3
    addl    %ecx, %eax
    fldt    ld(%rip)                # 1.5L
    fadd    %st(0), %st(0)
    fistpl  tmp(%rip)               # 3
    addl    tmp(%rip), %eax
    ret
    .data
buf:
    .long   1, 4
onehalf:
    .double 1.5
ld:
    .quad   0xc000000000000000
    .short  0x3fff
tmp:
    .long   0
)"));
    EXPECT_EQ(10, exit_status);
}

// An exception is reported, and ends the run with status 255 instead of hanging.
TEST_F(X86Test, RuntimeInvalidOpcodeReported)
{
    SKIP_IF_NO_X86_TOOLS();
    std::string out = RunAssembly(R"(    .text
    .globl  main
main:
    ud2
)");
    EXPECT_EQ(255, exit_status);
    EXPECT_NE(std::string::npos, out.find("exception: vector 0000000000000006 ")) << out;
}

// Only 0-4 GiB is mapped: a load above it page-faults, with the address in cr2.
TEST_F(X86Test, RuntimePageFaultReported)
{
    SKIP_IF_NO_X86_TOOLS();
    std::string out = RunAssembly(R"(    .text
    .globl  main
main:
    movabsq $0x100000000, %rax
    movl    (%rax), %eax
    ret
)");
    EXPECT_EQ(255, exit_status);
    EXPECT_NE(std::string::npos, out.find("exception: vector 000000000000000e error=")) << out;
    EXPECT_NE(std::string::npos, out.find("cr2=0000000100000000")) << out;
}

// A value left on the x87 stack would corrupt later long double code: crt0 catches it.
TEST_F(X86Test, RuntimeX87StackLeftNonEmpty)
{
    SKIP_IF_NO_X86_TOOLS();
    std::string out = RunAssembly(R"(    .text
    .globl  main
main:
    fld1
    xorl    %eax, %eax
    ret
)");
    EXPECT_EQ(254, exit_status);
    EXPECT_NE(std::string::npos, out.find("x87 stack not empty after main")) << out;
}
