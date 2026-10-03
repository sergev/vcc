//
// ARM32 control flow: labels, jumps and conditional branches.
//
#include "arm32_test.h"

// A test of zero is a cmp and a conditional branch; labels are local (.L).
TEST_F(Arm32Test, IfElse)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToArm32(
        "int f(void) { int a = 3; int r; if (a) r = 1; else r = 2; return r; }"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr r12, [r11, #-4]
cmp r12, #0
beq .L)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(
b .L)")) << code;
}

TEST_F(Arm32Test, LoopLabels)
{
    DisableOptimization();
    std::string text = CompileToArm32("int f(void) { int a = 3; while (a) a = a - 1; return a; }");
    EXPECT_NE(std::string::npos, text.find(R"(
.L)")) << text;
}

// A long long is zero when the or of its words is.
TEST_F(Arm32Test, LongLongCondition)
{
    NaiveSelection();
    DisableOptimization();
    std::string code =
        Code(CompileToArm32("int f(void) { long long a = 3; if (a) return 1; return 2; }"));
    EXPECT_NE(std::string::npos, code.find(R"(orrs r12, r12, lr
beq .L)")) << code;
}

TEST_F(Arm32Test, RunLoops)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
        if (i == 7)
            continue;
        sum = sum + i;
    }
    int n = 0;
    do n = n + 3; while (n < 20);
    long long big = 0x100000000LL;
    int k = 0;
    while (big) { big = 0; k = k + 1; }
    return sum + n + k; // 38 + 21 + 1
})"));
    EXPECT_EQ(60, exit_status);
}

TEST_F(Arm32Test, RunSwitchAndLogical)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int r = 0;
    for (int i = 0; i < 5; i = i + 1) {
        switch (i) {
        case 0: r = r + 1; break;
        case 3: r = r + 10;
        case 4: r = r + 100; break;
        default: r = r + 1000;
        }
    }
    int a = 0, b = 5;
    return (r == 2211) + 2 * (a || b) + 4 * !(a && b) + 8 * (b > 4 ? 1 : 0);
})"));
    EXPECT_EQ(15, exit_status);
}
