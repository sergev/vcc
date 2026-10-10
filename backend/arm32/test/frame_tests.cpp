//
// ARM32 frame: slots below r11 or above sp, copies through scratch registers, large
// frames, leaf functions without one.
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
    for (const char *s : { "strb r12", "ldrsb r12", "ldrb r12", "strh r12", "ldrsh r12", "ldrh r12",
                           "str r12", "ldr r12", "ldr r0" })
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

// An 8-byte value moves as two words, the low one first.
TEST_F(Arm32Test, LongLongCopy)
{
    NaiveSelection();
    DisableOptimization();
    EXPECT_EQ(
        R"(push {r11, lr}
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
    EXPECT_TRUE(
        std::regex_search(code, std::regex("\nsub lr, r11, #[0-9]+\n(sub lr, lr, #[0-9]+\n)?"
                                           "str r12, \\[lr\\]\n")))
        << code;
    EXPECT_TRUE(
        std::regex_search(code, std::regex("\nsub r12, r11, #[0-9]+\n(sub r12, r12, #[0-9]+\n)?"
                                           "ldrsh r12, \\[r12\\]\n")))
        << code;
}

// alloca: the frame from r11 whatever else (a leaf too), sp lowered by the size
// rounded to 8, and the epilogue that puts sp back from r11.
TEST_F(Arm32Test, AllocaLeaf)
{
    EXPECT_EQ(R"(push {r11, lr}
mov r11, sp
add r12, r0, #7
bic r12, r12, #7
sub sp, sp, r12
mov r1, sp
sub r0, r0, #1
add r0, r1, r0
mov r12, #7
strb r12, [r0]
ldrb r0, [r0]
mov sp, r11
pop {r11, pc}
)",
              Code(CompileToArm32(R"(
void *__builtin_alloca(unsigned int);
int f(int n)
{
    char *p = __builtin_alloca(n);
    p[n - 1] = 7;
    return p[n - 1];
}
)")));
}

// The memory starts above the outgoing area, rounded to 8, which lies just below the
// VFP registers saved; the epilogue finds them, and the core ones, from r11.
TEST_F(Arm32Test, AllocaAboveOutgoing)
{
    std::string code = Code(CompileToArm32(R"(
void *__builtin_alloca(unsigned int);
double h(double, int, int, int, int, int);
double f(int n, double x)
{
    double *p = __builtin_alloca(n * sizeof(double));
    p[0] = x;
    double y = h(x, 1, 2, 3, 4, n);
    return y + p[0] + x;
}
)"));
    EXPECT_EQ(0u, code.find("push {r4, r11, lr}\nadd r11, sp, #4\nsub sp, sp, #4\n"
                            "vpush {d8}\nsub sp, sp, #8\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("sub sp, sp, r12\nadd r4, sp, #8\n")) << code;
    EXPECT_NE(std::string::npos, code.find("str r0, [sp]\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find("sub sp, r11, #16\nvpop {d8}\nsub sp, r11, #4\npop {r4, r11, pc}\n"))
        << code;
}

// With r10 saved the saves end 4 bytes off an 8-byte boundary: the outgoing area of 4
// bytes takes 12, so the memory, at sp + 8, stops below r10's slot.  It runs, the
// remainder's quotient in r10 all along.
static const char *const kAllocaR10 = R"(
void *__builtin_alloca(unsigned int);
int g(int, int, int, int, int);
int f(int n, int m)
{
    int *p = __builtin_alloca(n * sizeof(int));
    p[0] = n % m;
    return g(1, 2, 3, 4, p[0]) + n % m + p[0];
}
)";

TEST_F(Arm32Test, AllocaBelowR10)
{
    std::string code = Code(CompileToArm32(kAllocaR10));
    EXPECT_EQ(0u, code.find("push {r4, r5, r11, lr}\nadd r11, sp, #8\npush {r10}\n"
                            "sub sp, sp, #12\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("sub sp, sp, r12\nadd r5, sp, #8\n")) << code;
}

TEST_F(Arm32Test, RunAllocaBelowR10)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(std::string(kAllocaR10) + R"(
int g(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
int main(void) { return f(23, 5) == 10 + 3 + 3 + 3 ? 0 : 1; }
)"));
    EXPECT_EQ(0, exit_status);
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

// A leaf that needs no stack has no prologue at all.
TEST_F(Arm32Test, LeafWithoutFrame)
{
    EXPECT_EQ("mul r0, r0, r1\nadd r0, r0, #1\nbx lr\n", Code(CompileToArm32(R"(
int leaf(int a, int b) { return a * b + 1; }
)")));
}

// Slots are addressed from sp, above the outgoing area; sp stays 8-byte aligned by one
// more register pushed.
TEST_F(Arm32Test, SlotsFromSp)
{
    EXPECT_EQ(R"(push {r11, lr}
sub sp, sp, #8
str r0, [sp, #4]
add r0, sp, #4
bl take
ldr r0, [sp, #4]
add sp, sp, #8
pop {r11, pc}
)",
              Code(CompileToArm32(R"(
void take(int *);
int slot(int x) { int y = x; take(&y); return y; }
)")));
}

// A stack argument is read above what the function pushed; a leaf saves only the
// callee-saved register it uses (r0-r3 holding the other parameters, all live).
TEST_F(Arm32Test, StackArgumentFromSp)
{
    EXPECT_EQ(
        "push {r4}\nldr r4, [sp, #4]\nadd r0, r0, r1\nadd r0, r0, r2\nadd r0, r0, r3\n"
        "add r0, r0, r4\npop {r4}\nbx lr\n",
        Code(CompileToArm32(R"(
int stackarg(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
)")));
}

// A parameter dead on entry leaves its register to a stack argument: nothing pushed.
TEST_F(Arm32Test, DeadParameterRegister)
{
    EXPECT_EQ("ldr r1, [sp]\nadd r0, r0, r1\nbx lr\n", Code(CompileToArm32(R"(
int stackarg(int a, int b, int c, int d, int e) { return a + e; }
)")));
}

// A variadic function's r0-r3 and stack arguments, from sp too.
TEST_F(Arm32Test, VariadicFromSp)
{
    EXPECT_EQ("push {r0, r1, r2, r3}\nldr r0, [sp]\nadd sp, sp, #16\nbx lr\n",
              Code(CompileToArm32(R"(
int first(int n, ...) { return n; }
)")));
}

// An offset that does not fit its instruction from sp (here a halfword 302 bytes up):
// the function is generated again, addressed from r11.
TEST_F(Arm32Test, FrameFallsBackToR11)
{
    std::string code = Code(CompileToArm32(R"(
void take(void *);
struct s { char pad[300]; short h; };
int far_half(void) { struct s v; take(&v); return v.h; }
)"));
    EXPECT_NE(std::string::npos, code.find("push {r11, lr}\nmov r11, sp\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldrsh r0, [r11, #-2]\n")) << code;
}

// Frames of every shape, from sp and from r11, run: large ones, stack arguments, a
// variadic function, values in r11.
TEST_F(Arm32Test, RunFrames)
{
    SKIP_IF_NO_ARM32_TOOLS();
    CompileAndRunArm32(R"(
#include <stdarg.h>
void take(void *p) { (void)p; }
struct s { char pad[300]; short h; };
int far_half(short k) { struct s v; v.h = k; take(&v); return v.h; }
int huge(int k) { char buf[5000]; int x = k; take(&x); buf[4999] = 3; take(buf); return x + buf[4999]; }
int many(int a, int b, int c, int d, int e, long long f, double g, int h)
{
    int x = a + b, y = c + d, z = e + h;
    take(&x);
    return x + y + z + (int)f + (int)g;
}
int vsum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}
int id(int x) { return x; }
int keep9(int k)
{
    int a = id(k), b = id(k + 1), c = id(k + 2), d = id(k + 3), e = id(k + 4), f = id(k + 5);
    int g = id(k + 6), h = id(k + 7);
    return id(a + b + c + d + e + f + g + h) + a * h;
}
int main(void)
{
    return (far_half(-7) == -7) + 2 * (huge(5) == 8) +
           4 * (many(1, 2, 3, 4, 5, 6, 7.5, 8) == 3 + 7 + 13 + 6 + 7) +
           8 * (vsum(4, 1, 2, 3, 4) == 10) + 16 * (keep9(1) == 36 + 8);
}
)");
    EXPECT_EQ(31, exit_status);
}
