//
// AArch64 code generator: golden assembly.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, ReturnConstant)
{
    EXPECT_EQ(
        "    .text\n"
        "    .globl  main\n"
        "    .p2align 2\n"
        "    .type   main, @function\n"
        "main:\n"
        "    mov     w0, #2\n"
        "    ret\n"
        "    .size   main, .-main\n",
        CompileToAarch64("int main(void) { return 2; }"));
}

TEST_F(Aarch64Test, StaticFunctionIsLocal)
{
    std::string s = CompileToAarch64("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

// Each test compiles one translation unit: the fixture's symbol table lives per test.
#define EXPECT_CODE(name, expected, src)                  \
    TEST_F(Aarch64Test, name)                             \
    {                                                     \
        EXPECT_EQ(expected, Code(CompileToAarch64(src))); \
    }

EXPECT_CODE(VoidReturn, "ret\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "ret\n", "void f(void) { }")

// One mov when all but one 16-bit chunk are zero, or all ones.
EXPECT_CODE(ConstMinusOne, "mov w0, #-1\nret\n", "int f(void) { return -1; }")
EXPECT_CODE(ConstHighChunk, "mov w0, #65536\nret\n", "int f(void) { return 65536; }")
EXPECT_CODE(ConstLongMinusTwo, "mov x0, #-2\nret\n", "long f(void) { return -2L; }")
EXPECT_CODE(ConstLongBit32, "mov x0, #4294967296\nret\n", "long f(void) { return 0x100000000L; }")
EXPECT_CODE(ConstUnsignedAllOnes, "mov w0, #-1\nret\n", "unsigned f(void) { return 0xffffffffu; }")

// Otherwise movz, and a movk for each other chunk that is not zero.
EXPECT_CODE(ConstMovzMovk, "movz w0, #22136\nmovk w0, #4660, lsl #16\nret\n",
            "int f(void) { return 0x12345678; }")
EXPECT_CODE(ConstLongMovzMovk,
            "movz x0, #57072\nmovk x0, #39612, lsl #16\nmovk x0, #22136, lsl #32\n"
            "movk x0, #4660, lsl #48\nret\n",
            "long f(void) { return 0x123456789abcdef0L; }")
EXPECT_CODE(ConstZeroChunksSkipped, "movz x0, #4660\nmovk x0, #65535, lsl #32\nret\n",
            "long f(void) { return 0x0000ffff00001234L; }")

// Mostly ones: movn, then a movk for each chunk that is not all ones.
EXPECT_CODE(ConstMovn, "movn x0, #60875\nmovk x0, #0, lsl #32\nret\n",
            "long f(void) { return (long)0xffff0000ffff1234UL; }")
