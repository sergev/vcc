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
    EXPECT_EQ("addi\tsp, sp, -16\n"
              "sd\tra, 8(sp)\n"
              "sd\ts0, 0(sp)\n"
              "addi\ts0, sp, 16\n"
              "addi\tsp, sp, -48\n"
              "li\tt0, 1\n"
              "sb\tt0, -18(s0)\n"
              "lbu\tt0, -18(s0)\n"
              "sb\tt0, -17(s0)\n"
              "li\tt0, 2\n"
              "sd\tt0, -40(s0)\n"
              "ld\tt0, -40(s0)\n"
              "sd\tt0, -32(s0)\n"
              "li\tt0, 3\n"
              "sw\tt0, -44(s0)\n"
              "li\tt6, 4602678819172646912\n"
              "fmv.d.x\tft0, t6\n"
              "fsd\tft0, -56(s0)\n"
              "lw\ta0, -44(s0)\n"
              "addi\tsp, s0, -16\n"
              "ld\tra, 8(sp)\n"
              "ld\ts0, 0(sp)\n"
              "addi\tsp, sp, 16\n"
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
    EXPECT_NE(std::string::npos, s.find("addi\ts0, sp, 16\nli\tt0, 2400\nsub\tsp, sp, t0\n"))
        << s.substr(0, 200);
    EXPECT_NE(std::string::npos, s.find("li\tt0, 599\nli\tt6, -2416\nadd\tt6, s0, t6\nsw\tt0, 0(t6)\n"));
}

