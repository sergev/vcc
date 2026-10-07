//
// ARM32 peephole pass and compare-and-branch fusion: what each rewrite makes of
// typical code, and programs whose results must not change.
//
#include "arm32_test.h"
#include "../../common/test/bitfield_run.h"

// A loop: the compare fused with its branch, the index folded into the load.
TEST_F(Arm32Test, PeepholeLoop)
{
    EXPECT_EQ(R"(mov r2, #0
add r3, r0, r1, lsl #2
cmp r1, #0
ble .LL0
ldr r1, [r0]
add r2, r2, r1
add r0, r0, #4
cmp r0, r3
blo .L3
mov r0, r2
bx lr
)",
              Code(CompileToArm32(R"(
int sum(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)")));
}

// A diamond and a triangle become conditional instructions; FP ones too, the inverse
// condition right for a NaN (le is true when unordered).
TEST_F(Arm32Test, PeepholeConditionalExecution)
{
    std::string code = Code(CompileToArm32(R"(
int sel(int c, int a, int b) { int r; if (c) r = a + 1; else r = b - 1; return r; }
int clamp(int x) { if (x < 0) x = 0; return x; }
double fmax2(double a, double b) { return a > b ? a : b; }
)"));
    EXPECT_NE(std::string::npos, code.find("cmp r0, #0\naddne r0, r1, #1\nsubeq r0, r2, #1\nbx lr\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("cmp r0, #0\nmovlt r0, #0\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find("vcmp.f64 d0, d1\nvmrs APSR_nzcv, fpscr\nvmovle.f64 d0, d1\nbx lr\n"))
        << code;
}

// mul + add is mla, mul + sub mls; a shift folds into operand2, an add into the address,
// a mask into tst.
TEST_F(Arm32Test, PeepholeFolds)
{
    std::string code = Code(CompileToArm32(R"(
int madd(int a, int b, int c) { return a * b + c; }
int msub(int a, int b, int c) { return c - a * b; }
int shl(int a, int b) { return a + (b << 3); }
int third(int *p) { return p[3]; }
int mask(int x) { if (x & 8) return 3; return 4; }
)"));
    EXPECT_NE(std::string::npos, code.find("mla r0, r0, r1, r2\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mls r0, r0, r1, r2\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("add r0, r0, r1, lsl #3\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr r0, [r0, #12]\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("tst r0, #8\nbeq ")) << code;
}

// A long long compare fused with its branch; a long long in a frame slot stored with
// strd; a constant word as an immediate.
TEST_F(Arm32Test, PeepholeLongLong)
{
    std::string code = Code(CompileToArm32(R"(
int lless(long long a, long long b) { if (a < b) return 1; return 2; }
void take(long long *);
long long pair(long long a) { long long x = a; take(&x); return x + 1; }
)"));
    EXPECT_NE(std::string::npos, code.find("cmp r0, r2\nsbcs r12, r1, r3\nbge ")) << code;
    EXPECT_NE(std::string::npos, code.find("strd r0, r1, [sp]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("adds r0, r12, #1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("adc r1, lr, #0\n")) << code;
}

// A move back to where a value came from goes; a load of what was just stored goes.
TEST_F(Arm32Test, PeepholeMovesAndReloads)
{
    std::string code = Code(CompileToArm32(R"(
int f(int x);
int g(int a, int b) { return f(a) + f(b) * a; }
void take(int *);
int reload(int a) { int x = a; take(&x); x = a + 5; return x; }
)"));
    EXPECT_NE(std::string::npos, code.find("mov r6, r0\nmov r5, r1\nbl f\n")) << code;
    EXPECT_NE(std::string::npos, code.find("add r0, r4, #5\nstr r0, [sp, #4]\nadd sp, sp, #8\n"))
        << code;
}

// Programs made of the shapes the pass rewrites, their results unchanged: conditional
// execution with NaNs, flags carried through long long arithmetic, folded addresses and
// shifts, frame slots paired.
TEST_F(Arm32Test, RunPeepholeShapes)
{
    SKIP_IF_NO_ARM32_TOOLS();
    CompileAndRunArm32(R"(
void take(void *p) { (void)p; }
int sel(int c, int a, int b) { int r; if (c) r = a + 1; else r = b - 1; return r; }
int clamp(int x) { if (x < 0) x = 0; return x; }
double fmax2(double a, double b) { return a > b ? a : b; }
int fless(float a, float b) { return a < b ? 10 : 20; }
int bits(unsigned x) { int n = 0; while (x) { if (x & 1) n++; x >>= 1; } return n; }
int idx(int *a, int i, int j) { return a[i] + a[j] * 3 + (i << 2) - (j >> 1); }
long long lmix(long long a, long long b) { long long c = a + 0x100000001LL; return a < b ? c - b : c + b; }
unsigned short hw(unsigned short *p, int i) { return p[i] + p[i + 1]; }
long long slots(long long a, long long b) { long long x = a, y = b; take(&x); take(&y); return x * 3 + y; }
int main(void)
{
    int a[4] = { 5, 6, 7, 8 };
    unsigned short h[3] = { 65535, 2, 3 };
    double nan = 0.0 / 0.0;
    return (sel(1, 4, 9) == 5 && sel(0, 4, 9) == 8) + 2 * (clamp(-5) == 0 && clamp(7) == 7) +
           4 * (fmax2(1, 2) == 2 && fmax2(3, 2) == 3 && fmax2(nan, 2) == 2) +
           8 * (fless(1, 2) == 10 && fless(2, 1) == 20 && fless((float)nan, 1) == 20) +
           16 * (bits(0xf0f0) == 8) + 32 * (idx(a, 1, 3) == 6 + 24 + 4 - 1) +
           64 * (lmix(1, 2) == 0x100000000LL && lmix(5, -3) == 0x100000003LL &&
                 hw(h, 0) == 1 && hw(h, 1) == 5 && slots(1LL << 40, 7) == (3LL << 40) + 7);
}
)");
    EXPECT_EQ(127, exit_status);
}

// A volatile local stays in its slot: the store is not followed into the reload, and
// two accesses are two, not an ldrd/strd.
TEST_F(Arm32Test, PeepholeKeepsVolatileReload)
{
    EXPECT_NE(std::string::npos,
              Code(CompileToArm32("int f(int a) { volatile int x = a; return x; }"))
                  .find("sub sp, sp, #8\nstr r0, [sp, #4]\nldr r0, [sp, #4]\nadd sp, sp, #8\nbx lr\n"));
}
TEST_F(Arm32Test, PeepholeKeepsVolatileUnpaired)
{
    std::string code =
        Code(CompileToArm32("int g(int a, int b) { volatile int x = b, y = a; return x - y; }"));
    EXPECT_EQ(std::string::npos, code.find("ldrd ")) << code;
    EXPECT_EQ(std::string::npos, code.find("strd ")) << code;
}

// Bit-fields: a read is ubfx or sbfx, a store bfi (the masks' movw/movt gone with the
// shifts), a store of zero bfc; a narrow unit is stored without its extension.
TEST_F(Arm32Test, PeepholeBitfields)
{
    std::string code = Code(CompileToArm32(R"(
struct S { unsigned a : 3; int b : 5; unsigned c : 12; unsigned d : 12; };
unsigned get_c(struct S *p) { return p->c; }
int get_b(struct S *p) { return p->b; }
void set_c(struct S *p, unsigned v) { p->c = v; }
void set_b(struct S *p, int v) { p->b = v; }
void clear_c(struct S *p) { p->c = 0; }
void bump_d(struct S *p) { p->d++; }
)"));
    EXPECT_NE(std::string::npos, code.find("ldr r0, [r0]\nubfx r0, r0, #8, #12\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldrb r0, [r0]\nsbfx r0, r0, #3, #5\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr r0, [r2]\nbfi r0, r1, #8, #12\nstr r0, [r2]\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("ldrb r0, [r3]\nbfi r0, r1, #3, #5\nstrb r0, [r3]\nbx lr\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("ldr r0, [r1]\nbfc r0, #8, #12\nstr r0, [r1]\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find("ubfx r0, r2, #4, #12\nadd r0, r0, #1\nbfi r2, r0, #4, #12\n"))
        << code;
    EXPECT_EQ(std::string::npos, code.find("orr")) << code;
    EXPECT_EQ(std::string::npos, code.find("movw")) << code;
    EXPECT_EQ(std::string::npos, code.find("uxt")) << code;
}

// The same shapes written out by hand; and what they are not: a mask with a hole, a
// shifted value read again (the mask alone is a ubfx), a mask an immediate already.  The
// movw of a mask has lr saved, and keeps it saved once gone.
TEST_F(Arm32Test, PeepholeShiftMask)
{
    std::string code = Code(CompileToArm32(R"(
unsigned ext(unsigned x) { return (x >> 13) & 0x7ff; }
int sext(int x) { return (x << 7) >> 20; }
unsigned ins(unsigned x, unsigned v) { return (x & 0xfff000ff) | (v & 0xfff) << 8; }
unsigned hole(unsigned x) { return (x >> 4) & 0x505; }
unsigned again(unsigned x) { unsigned t = x >> 4; return (t & 0xfff) + t; }
unsigned low(unsigned x) { return x & 0xff; }
)"));
    EXPECT_NE(std::string::npos, code.find("push {lr}\nubfx r0, r0, #13, #11\npop {pc}\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sbfx r0, r0, #13, #12\nbx lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("push {lr}\nbfi r0, r1, #8, #12\npop {pc}\n")) << code;
    EXPECT_NE(std::string::npos, code.find("lsr r0, r0, #4\nmovw lr, #1285\nand r0, r0, lr\n")) << code;
    EXPECT_NE(std::string::npos, code.find("lsr r1, r0, #4\nubfx r0, r1, #0, #12\nadd r0, r0, r1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("and r0, r0, #255\nbx lr\n")) << code;
}

// Bit-fields and shift-and-mask expressions computed as the host computes them.
TEST_F(Arm32Test, RunPeepholeBitfields)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ(BitfieldRunExpected(), CompileAndRunArm32(kBitfieldRunProgram));
}
