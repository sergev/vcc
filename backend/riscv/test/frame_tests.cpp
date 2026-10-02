//
// Frame layout: slots, prologue and epilogue, offsets beyond 12 bits.
//
#include "riscv_test.h"

// Slots are laid out below the saved ra/s0 by the type's size and alignment; the
// unoptimized TAC copies each initializer through a temporary.
TEST_F(RiscvTest, FrameSlots)
{
    DisableOptimization();
    riscv_regalloc      = false;
    riscv_peephole      = false;
    riscv_frame_pointer = true;
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
    riscv_regalloc = false;
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


// A variadic function saves a0-a7 below the incoming stack arguments, its ra and s0
// below those; the named parameter in a0 lives in its save slot.  With the frame
// pointer kept, all of it is addressed from s0.
TEST_F(RiscvTest, FrameVariadic)
{
    riscv_frame_pointer = true;
    std::string s = Code(CompileToRiscv("long f(long n, ...) { return *(&n + 2); }"));
    EXPECT_EQ(0u, s.find(R"(addi sp, sp, -80
sd ra, 8(sp)
sd s0, 0(sp)
addi s0, sp, 80
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(sd a0, -64(s0)
sd a1, -56(s0)
sd a2, -48(s0)
sd a3, -40(s0)
sd a4, -32(s0)
sd a5, -24(s0)
sd a6, -16(s0)
sd a7, -8(s0)
)")) << s;
    EXPECT_NE(std::string::npos, s.find(", s0, -64\n")) << s; // &n
    EXPECT_NE(std::string::npos, s.find(R"(addi sp, s0, -80
ld ra, 8(sp)
ld s0, 0(sp)
addi sp, sp, 80
ret
)")) << s;
}

// Without a frame pointer the same frame is addressed from sp; a function that makes
// no call saves neither ra nor s0, so the frame is just the save area.
TEST_F(RiscvTest, FrameVariadicFromSp)
{
    std::string s = Code(CompileToRiscv("long f(long n, ...) { return *(&n + 2); }"));
    EXPECT_EQ(0u, s.find("addi sp, sp, -64\nsd a0, 0(sp)\n")) << s;
    EXPECT_NE(std::string::npos, s.find("add a0, sp, t1\n")) << s; // &n + 2
    EXPECT_NE(std::string::npos, s.find("addi sp, sp, 64\nret\n")) << s;
}

// A function that makes calls but keeps nothing in its frame saves only ra.
TEST_F(RiscvTest, FrameOnlyRa)
{
    EXPECT_EQ(R"(addi sp, sp, -16
sd ra, 8(sp)
call g
addi a0, a0, 1
call g
ld ra, 8(sp)
addi sp, sp, 16
ret
)",
              Code(CompileToRiscv("long g(long);\nlong f(long a) { return g(g(a) + 1); }")));
}

// An _Alignas local keeps its alignment; the slots then move up into the unused
// header only as far as that alignment allows.  Statics are aligned too.
TEST_F(RiscvTest, FrameAlignedSlot)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
long id(long x) { return x; }
char pad1;
_Alignas(64) char g[3];
int main(void)
{
    _Alignas(16) char buf[16];
    static char pad2;
    static _Alignas(32) char s;
    if (id((long)buf) % 16 != 0) return 1;
    if ((long)g % 64 != 0) return 2;
    if ((long)&s % 32 != 0) return 3;
    return pad1 + pad2;
}
)"));
    EXPECT_EQ(0, exit_status);
}
