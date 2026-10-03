//
// ARM32 frame: slots below r11, copies through scratch registers, large frames.
//
#include <regex>

#include "arm32_test.h"

// A local in a slot: stored, then loaded for the return.
TEST_F(Arm32Test, LocalSlot)
{
    NaiveSelection();
    DisableOptimization();
    EXPECT_EQ(R"(push {r11, lr}
mov r11, sp
sub sp, sp, #8
mov r12, #5
str r12, [r11, #-4]
ldr r0, [r11, #-4]
mov sp, r11
pop {r11, pc}
)",
              Code(CompileToArm32("int main(void) { int a = 5; return a; }")));
}

// Each width loads and stores as itself; a char by its signedness.
TEST_F(Arm32Test, SlotWidths)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
int f(void) {
    signed char c = -1; unsigned char u = 255;
    short s = -2; unsigned short us = 2;
    int l = 7; int m = l; c = c; u = u; s = s;
    us = us;
    return m;
})"));
    for (const char *s : { "strb r12", "ldrsb r12", "ldrb r12", "strh r12", "ldrsh r12",
                           "ldrh r12", "str r12", "ldr r12", "ldr r0" })
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

// An 8-byte value moves as two words, the low one first.
TEST_F(Arm32Test, LongLongCopy)
{
    NaiveSelection();
    DisableOptimization();
    EXPECT_EQ(R"(push {r11, lr}
mov r11, sp
sub sp, sp, #16
mov r12, #0
mov lr, #1
str r12, [r11, #-8]
str lr, [r11, #-4]
ldr r12, [r11, #-8]
ldr lr, [r11, #-4]
str r12, [r11, #-16]
str lr, [r11, #-12]
ldr r0, [r11, #-16]
ldr r1, [r11, #-12]
mov sp, r11
pop {r11, pc}
)",
              Code(CompileToArm32(
                  "long long f(void) { long long a = 0x100000000LL; long long b = a; return b; }")));
}

// A double needs no VFP register for a copy, and is returned in d0.
TEST_F(Arm32Test, DoubleCopy)
{
    NaiveSelection();
    DisableOptimization();
    EXPECT_EQ(R"(push {r11, lr}
mov r11, sp
sub sp, sp, #8
movw r12, #13107
movt r12, #13107
movw lr, #13107
movt lr, #16371
str r12, [r11, #-8]
str lr, [r11, #-4]
vldr d0, [r11, #-8]
mov sp, r11
pop {r11, pc}
)",
              Code(CompileToArm32("double f(void) { double d = 1.2; return d; }")));
}

// An FP constant that is no VFP immediate is returned through r0 (and r1), which need
// no frame.
EXPECT_CODE(FloatConstantResult, R"(movw r0, #52429
movt r0, #15820
vmov s0, r0
bx lr
)",
            "float f(void) { return 0.1f; }")

// Offsets beyond ldr's reach go through the scratch register: sub by modified
// immediates; ldrh's 8-bit reach is shorter than ldr's.
TEST_F(Arm32Test, LargeFrameOffsets)
{
    NaiveSelection();
    DisableOptimization();
    std::string src = "int f(void) {\n";
    for (int i = 0; i < 1100; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    short s = 1; s = s;\n    return v0;\n}\n";
    std::string code = Code(CompileToArm32(src.c_str()));
    EXPECT_NE(std::string::npos, code.find(R"(sub sp, sp, #312
sub sp, sp, #4096
)")) << code;
    EXPECT_TRUE(std::regex_search(code, std::regex("\nsub lr, r11, #[0-9]+\n(sub lr, lr, #[0-9]+\n)?"
                                                   "str r12, \\[lr\\]\n")))
        << code;
    EXPECT_TRUE(std::regex_search(code, std::regex("\nsub r12, r11, #[0-9]+\n(sub r12, r12, #[0-9]+\n)?"
                                                   "ldrsh r12, \\[r12\\]\n")))
        << code;
}

TEST_F(Arm32Test, RunLocals)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = 40; int b = a; long long c = 2; unsigned char d = 200; double e = 1.5;
    b = b; c = c; d = d; e = e;
    return b;
})"));
    EXPECT_EQ(40, exit_status);
}

TEST_F(Arm32Test, RunLargeFrame)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    std::string src = "int main(void) {\n"; // over 4 KiB of slots
    for (int i = 0; i < 1200; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    short s = 7; signed char c = -3;\n";
    src += "    v0 = v1099; s = s; c = c;\n    return v0;\n}\n";
    EXPECT_EQ("", CompileAndRunArm32(src));
    EXPECT_EQ(1099 & 0xff, exit_status);
}

TEST_F(Arm32Test, RunEightByteResults)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
long long g(void) { long long a = 0x700000005LL; long long b = a; return b; }
int main(void) { return 9; }
)"));
    EXPECT_EQ(9, exit_status);
}
