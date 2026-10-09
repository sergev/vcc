//
// AVR frames: the prologue and epilogue, slots, parameters, and slots past Y+63.
//
#include "avr_test.h"

// Y saved and pointed at the slots, SP written with interrupts held off; parameters
// stored from their registers, the first slots.  The volatile read goes through a
// temporary.
TEST_F(AvrTest, FrameWithParameters)
{
    NaiveSelection();
    EXPECT_EQ(R"(push r28
push r29
in r28, __SP_L__
in r29, __SP_H__
sbiw r28, 10
in r0, __SREG__
cli
out __SP_H__, r29
out __SREG__, r0
out __SP_L__, r28
std Y+1, r24
std Y+2, r25
std Y+3, r20
std Y+4, r21
std Y+5, r22
std Y+6, r23
ldd r24, Y+1
ldd r25, Y+2
std Y+7, r24
std Y+8, r25
ldd r24, Y+7
ldd r25, Y+8
std Y+9, r24
std Y+10, r25
ldd r24, Y+9
ldd r25, Y+10
adiw r28, 10
in r0, __SREG__
cli
out __SP_H__, r29
out __SREG__, r0
out __SP_L__, r28
pop r29
pop r28
ret
)",
              Code(CompileToAvr("int f(int x, long y) { volatile int z = x; return z; }")));
}

// A char parameter takes a pair, r24, the next r22.
EXPECT_CODE(CharParameters, R"(std Y+1, r24
std Y+2, r22
ldd r24, Y+2
mov r25, r24
lsl r25
sbc r25, r25
)",
            "signed char f(signed char a, signed char b) { return b; }")

// Two long longs fill r25-r10, a long does not fit in r9:r8, so it and the int after
// it come on the stack: above the slots, the saved Y and the return address.
EXPECT_CODE(StackParameters,
            R"(std Y+1, r18
std Y+2, r19
std Y+3, r20
std Y+4, r21
std Y+5, r22
std Y+6, r23
std Y+7, r24
std Y+8, r25
std Y+9, r10
std Y+10, r11
std Y+11, r12
std Y+12, r13
std Y+13, r14
std Y+14, r15
std Y+15, r16
std Y+16, r17
ldd r24, Y+25
ldd r25, Y+26
)",
            "int f(long long a, long long b, long c, int d) { return d; }")

// The call-saved registers in use are pushed after Y is set up, and popped before.
TEST_F(AvrTest, SavedRegistersPushed)
{
    NaiveSelection();
    std::string s = Code(CompileToAvr("int f(long long a, long long b) { return 0; }"));
    EXPECT_NE(std::string::npos, s.find(R"(out __SP_L__, r28
push r10
push r11
push r12
)"))
        << s;
    EXPECT_NE(std::string::npos, s.find(R"(pop r11
pop r10
adiw r28, 16
)")) << s;
}

// Up to 6 bytes of slots are reserved by rcall, 2 bytes each, and released by pop.
TEST_F(AvrTest, SmallFrameByRcall)
{
    NaiveSelection();
    std::string s = Code(CompileToAvr("int f(void) { volatile int z = 3; return z; }"));
    EXPECT_EQ(0u, s.find(R"(push r28
push r29
rcall .
rcall .
in r28, __SP_L__
in r29, __SP_H__
)"))
        << s;
    EXPECT_NE(std::string::npos, s.find(R"(pop r0
pop r0
pop r0
pop r0
pop r29
pop r28
ret
)"))
        << s;
}

// alloca: a frame from Y always (here without slots), SP lowered by the size through Z
// with interrupts held off, the memory at SP + 1; the epilogue puts SP back from Y.
TEST_F(AvrTest, AllocaLeaf)
{
    std::string s = Code(CompileToAvr(R"(
void *__builtin_alloca(unsigned int);
int f(int n)
{
    char *p = __builtin_alloca(n);
    p[n - 1] = 7;
    return p[n - 1];
}
)"));
    EXPECT_EQ(0u, s.find("push r28\npush r29\nin r28, __SP_L__\nin r29, __SP_H__\n")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(in r30, __SP_L__
in r31, __SP_H__
sub r30, r26
sbc r31, r27
in r0, __SREG__
cli
out __SP_H__, r31
out __SREG__, r0
out __SP_L__, r30
adiw r30, 1
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(out __SP_H__, r29
out __SREG__, r0
out __SP_L__, r28
pop r29
pop r28
ret
)")) << s;
}

// With registers pushed below Y, the epilogue puts SP just below them before it pops
// them, then releases the frame as ever.
TEST_F(AvrTest, AllocaSavedRegisters)
{
    std::string s = Code(CompileToAvr(R"(
void *__builtin_alloca(unsigned int);
int g(int);
int f(int n)
{
    char *p = __builtin_alloca(n);
    p[0] = 3;
    int x = g(n);
    return x + p[0] + n;
}
)"));
    EXPECT_NE(std::string::npos, s.find(R"(sbiw r28, 4
in r0, __SREG__
cli
out __SP_H__, r29
out __SREG__, r0
out __SP_L__, r28
adiw r28, 4
pop r17
pop r16
pop r15
pop r14
pop r29
pop r28
ret
)")) << s;
}

// No slots and no stack arguments: no frame, Y neither saved nor set up.
TEST_F(AvrTest, Frameless)
{
    EXPECT_EQ(R"(add r24, r22
adc r25, r23
ret
)",
              Code(CompileToAvr("int f(int a, int b) { return a + b; }")));
}

// Without a frame Y holds variables, saved and restored like the other call-saved
// registers.
static const char y_source[] = R"(
int g(int x);
int f(int a, int b)
{
    int c = g(a);
    int d = g(b);
    return a * 1000 + b * 100 + c * 10 + d;
}
)";

TEST_F(AvrTest, FramelessYHoldsVariable)
{
    std::string s = Code(CompileToAvr(y_source));
    EXPECT_EQ(std::string::npos, s.find("__SP_L__")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(push r28
push r29
)")) << s;
    EXPECT_NE(std::string::npos, s.find("movw r28, ")) << s;
}

TEST_F(AvrTest, RunFramelessYHoldsVariable)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(std::string(y_source) + R"(
int g(int x) { return x + 1; }
int main(void) { return f(1, 2) == 1000 + 200 + 20 + 3 ? 0 : 1; }
)"));
    EXPECT_EQ(0, exit_status);
}

// A frame over 63 bytes (40 ints and a temporary) is reserved with subi/sbci; a slot past Y+63, or straddling
// it, is reached through Z.
TEST_F(AvrTest, LargeFrame)
{
    NaiveSelection();
    std::string src = "int f(void) {\n";
    for (int i = 0; i < 40; i++)
        src += "    volatile int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    return v31; }\n";
    std::string s = Code(CompileToAvr(src.c_str()));
    EXPECT_NE(std::string::npos, s.find(R"(subi r28, 82
sbci r29, 0
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(subi r28, 174
sbci r29, 255
)")) << s;
    // v30 at Y+61, v31 straddling Y+63, v32 past it.
    EXPECT_NE(std::string::npos, s.find(R"(std Y+61, r24
std Y+62, r25
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(movw r30, r28
adiw r30, 63
st Z+, r24
st Z+, r25
)"))
        << s;
    EXPECT_NE(std::string::npos,
              s.find(R"(movw r30, r28
subi r30, 191
sbci r31, 255
st Z+, r24
st Z+, r25
)"))
        << s;
}

// A copy chain through 40 slots, across Y+63.
TEST_F(AvrTest, RunLargeFrame)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = R"(int main(void) {
    volatile int v0 = 0x1234;
)";
    for (int i = 1; i < 40; i++)
        src += "    volatile int v" + std::to_string(i) + " = v" + std::to_string(i - 1) +
               ";\n";
    src += "    return v39; }\n";
    EXPECT_EQ("4660\n", CompileAndRunBook(src));
}
