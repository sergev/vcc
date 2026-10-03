//
// ARM32 code generator: golden assembly.
//
#include "arm32_test.h"

TEST_F(Arm32Test, ReturnConstant)
{
    EXPECT_EQ(R"(    .syntax unified
    .arch   armv7-a
    .arch_extension idiv
    .fpu    vfpv3-d16
    .eabi_attribute Tag_ABI_PCS_R9_use, 0
    .eabi_attribute Tag_ABI_PCS_GOT_use, 1
    .eabi_attribute Tag_ABI_PCS_wchar_t, 4
    .eabi_attribute Tag_ABI_FP_denormal, 1
    .eabi_attribute Tag_ABI_FP_exceptions, 0
    .eabi_attribute Tag_ABI_FP_number_model, 3
    .eabi_attribute Tag_ABI_align_needed, 1
    .eabi_attribute Tag_ABI_align_preserved, 1
    .eabi_attribute Tag_ABI_enum_size, 2
    .eabi_attribute Tag_ABI_VFP_args, 1
    .eabi_attribute Tag_ABI_FP_16bit_format, 1
    .arm
    .text
    .globl  main
    .p2align 2
    .type   main, %function
main:
    mov     r0, #2
    bx      lr
    .size   main, .-main
)",
              CompileToArm32("int main(void) { return 2; }"));
}

// The header goes out once per translation unit, ahead of the first toplevel.
TEST_F(Arm32Test, HeaderOncePerModule)
{
    std::string s = CompileToArm32("int f(void) { return 1; } int g(void) { return 2; }");
    EXPECT_EQ(0u, s.find("    .syntax unified\n")) << s;
    EXPECT_EQ(s.find(".syntax"), s.rfind(".syntax")) << s;
}

TEST_F(Arm32Test, StaticFunctionIsLocal)
{
    std::string s = CompileToArm32("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

EXPECT_CODE(VoidReturn, "bx lr\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "bx lr\n", "void f(void) { }")

// A modified immediate (8 bits rotated by an even amount) takes one mov, its
// complement one mvn.
EXPECT_CODE(ConstRotated, R"(mov r0, #4278190080
bx lr
)",
            "unsigned f(void) { return 0xff000000u; }")
EXPECT_CODE(ConstMinusOne, R"(mvn r0, #0
bx lr
)",
            "int f(void) { return -1; }")
EXPECT_CODE(ConstMvn, R"(mvn r0, #255
bx lr
)",
            "int f(void) { return -256; }")
// Otherwise movw, and movt for a nonzero upper half.
EXPECT_CODE(ConstMovw, R"(movw r0, #4660
bx lr
)",
            "int f(void) { return 0x1234; }")
EXPECT_CODE(ConstMovwMovt, R"(movw r0, #22136
movt r0, #4660
bx lr
)",
            "int f(void) { return 0x12345678; }")
EXPECT_CODE(ConstLong, R"(movw r0, #4097
movt r0, #1
bx lr
)",
            "long f(void) { return 0x11001L; }")
// A long long in r0 (low) and r1 (high).
EXPECT_CODE(ConstLongLong, R"(movw r0, #3584
movt r0, #54778
mvn r1, #1
bx lr
)",
            "long long f(void) { return -5000000000LL; }")
EXPECT_CODE(ConstUnsignedLongLong, R"(mov r0, #0
mov r1, #1
bx lr
)",
            "unsigned long long f(void) { return 0x100000000ULL; }")
