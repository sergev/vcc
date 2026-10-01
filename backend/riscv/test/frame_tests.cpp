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
    EXPECT_EQ(R"(addi sp, sp, -16
sd ra, 8(sp)
sd s0, 0(sp)
addi s0, sp, 16
addi sp, sp, -48
li t0, 1
sb t0, -18(s0)
lbu t0, -18(s0)
sb t0, -17(s0)
li t0, 2
sd t0, -40(s0)
ld t0, -40(s0)
sd t0, -32(s0)
li t0, 3
sw t0, -44(s0)
li t6, 4602678819172646912
fmv.d.x ft0, t6
fsd ft0, -56(s0)
lw a0, -44(s0)
addi sp, s0, -16
ld ra, 8(sp)
ld s0, 0(sp)
addi sp, sp, 16
ret
)",
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
    EXPECT_NE(std::string::npos, s.find(R"(addi s0, sp, 16
li t0, 2400
sub sp, sp, t0
)"))
        << s.substr(0, 200);
    EXPECT_NE(std::string::npos, s.find(R"(li t0, 599
li t6, -2416
add t6, s0, t6
sw t0, 0(t6)
)"));
}

