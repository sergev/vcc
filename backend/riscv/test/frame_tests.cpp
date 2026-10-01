//
// Frame layout: slots, prologue and epilogue, offsets beyond 12 bits.
//
#include "riscv_test.h"

// Slots are laid out below the saved ra/s0 by the type's size and alignment; the
// unoptimized TAC copies each initializer through a temporary.
TEST_F(RiscvTest, FrameSlots)
{
    DisableOptimization();
    std::string s = CompileToRiscv(
        "int main(void) { char c = 1; long l = 2; int i = 3; double d = 0.5; return i; }");
    EXPECT_EQ("addi sp, sp, -16\n"
              "sd ra, 8(sp)\n"
              "sd s0, 0(sp)\n"
              "addi s0, sp, 16\n"
              "addi sp, sp, -48\n"
              "li t0, 1\n"
              "sb t0, -18(s0)\n"
              "lbu t0, -18(s0)\n"
              "sb t0, -17(s0)\n"
              "li t0, 2\n"
              "sd t0, -40(s0)\n"
              "ld t0, -40(s0)\n"
              "sd t0, -32(s0)\n"
              "li t0, 3\n"
              "sw t0, -44(s0)\n"
              "li t6, 4602678819172646912\n"
              "fmv.d.x ft0, t6\n"
              "fsd ft0, -56(s0)\n"
              "lw a0, -44(s0)\n"
              "addi sp, s0, -16\n"
              "ld ra, 8(sp)\n"
              "ld s0, 0(sp)\n"
              "addi sp, sp, 16\n"
              "ret\n",
              Code(s));
}

// 600 ints: the frame needs li/sub, the far slots li/add through t6.
static std::string ManyLocals()
{
    std::string src = "int main(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    return src + "    return v599;\n}\n";
}

TEST_F(RiscvTest, FrameLargeOffsets)
{
    DisableOptimization();
    std::string s = Code(CompileToRiscv(ManyLocals().c_str()));
    EXPECT_NE(std::string::npos, s.find("addi s0, sp, 16\nli t0, 2400\nsub sp, sp, t0\n"))
        << s.substr(0, 200);
    EXPECT_NE(std::string::npos, s.find("li t0, 599\nli t6, -2416\nadd t6, s0, t6\nsw t0, 0(t6)\n"));
}

