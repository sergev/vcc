//
// ARM32 code generator: golden assembly.
//
#include "arm32_test.h"

TEST_F(Arm32Test, ReturnConstant)
{
    EXPECT_EQ("    .syntax unified\n"
              "    .arch   armv7-a\n"
              "    .arch_extension idiv\n"
              "    .fpu    vfpv3-d16\n"
              "    .eabi_attribute Tag_ABI_PCS_R9_use, 0\n"
              "    .eabi_attribute Tag_ABI_PCS_GOT_use, 1\n"
              "    .eabi_attribute Tag_ABI_PCS_wchar_t, 4\n"
              "    .eabi_attribute Tag_ABI_FP_denormal, 1\n"
              "    .eabi_attribute Tag_ABI_FP_exceptions, 0\n"
              "    .eabi_attribute Tag_ABI_FP_number_model, 3\n"
              "    .eabi_attribute Tag_ABI_align_needed, 1\n"
              "    .eabi_attribute Tag_ABI_align_preserved, 1\n"
              "    .eabi_attribute Tag_ABI_enum_size, 2\n"
              "    .eabi_attribute Tag_ABI_VFP_args, 1\n"
              "    .eabi_attribute Tag_ABI_FP_16bit_format, 1\n"
              "    .arm\n"
              "    .text\n"
              "    .globl  main\n"
              "    .p2align 2\n"
              "    .type   main, %function\n"
              "main:\n"
              "    mov     r0, #2\n"
              "    bx      lr\n"
              "    .size   main, .-main\n",
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

// Each test compiles one translation unit: the fixture's symbol table lives per test.
#define EXPECT_CODE(name, expected, src)                \
    TEST_F(Arm32Test, name)                             \
    {                                                   \
        EXPECT_EQ(expected, Code(CompileToArm32(src))); \
    }

EXPECT_CODE(VoidReturn, "bx lr\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "bx lr\n", "void f(void) { }")

// A modified immediate (8 bits rotated by an even amount) takes one mov, its
// complement one mvn.
EXPECT_CODE(ConstRotated, "mov r0, #4278190080\nbx lr\n", "unsigned f(void) { return 0xff000000u; }")
EXPECT_CODE(ConstMinusOne, "mvn r0, #0\nbx lr\n", "int f(void) { return -1; }")
EXPECT_CODE(ConstMvn, "mvn r0, #255\nbx lr\n", "int f(void) { return -256; }")
// Otherwise movw, and movt for a nonzero upper half.
EXPECT_CODE(ConstMovw, "movw r0, #4660\nbx lr\n", "int f(void) { return 0x1234; }")
EXPECT_CODE(ConstMovwMovt, "movw r0, #22136\nmovt r0, #4660\nbx lr\n",
            "int f(void) { return 0x12345678; }")
EXPECT_CODE(ConstLong, "movw r0, #4097\nmovt r0, #1\nbx lr\n", "long f(void) { return 0x11001L; }")
// A long long in r0 (low) and r1 (high).
EXPECT_CODE(ConstLongLong, "movw r0, #3584\nmovt r0, #54778\nmvn r1, #1\nbx lr\n",
            "long long f(void) { return -5000000000LL; }")
EXPECT_CODE(ConstUnsignedLongLong, "mov r0, #0\nmov r1, #1\nbx lr\n",
            "unsigned long long f(void) { return 0x100000000ULL; }")
