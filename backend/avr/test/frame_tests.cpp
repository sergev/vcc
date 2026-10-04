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
EXPECT_CODE(CharParameters, "std Y+1, r24\nstd Y+2, r22\nldd r24, Y+2\nmov r25, r24\nlsl r25\n"
                            "sbc r25, r25\n",
            "signed char f(signed char a, signed char b) { return b; }")

// Two long longs fill r25-r10, a long does not fit in r9:r8, so it and the int after
// it come on the stack: above the slots, the saved Y and the return address.
EXPECT_CODE(StackParameters,
            "std Y+1, r18\nstd Y+2, r19\nstd Y+3, r20\nstd Y+4, r21\nstd Y+5, r22\n"
            "std Y+6, r23\nstd Y+7, r24\nstd Y+8, r25\nstd Y+9, r10\nstd Y+10, r11\n"
            "std Y+11, r12\nstd Y+12, r13\nstd Y+13, r14\nstd Y+14, r15\nstd Y+15, r16\n"
            "std Y+16, r17\nldd r24, Y+25\nldd r25, Y+26\n",
            "int f(long long a, long long b, long c, int d) { return d; }")

// The call-saved registers in use are pushed after Y is set up, and popped before.
TEST_F(AvrTest, SavedRegistersPushed)
{
    NaiveSelection();
    std::string s = Code(CompileToAvr("int f(long long a, long long b) { return 0; }"));
    EXPECT_NE(std::string::npos, s.find("out __SP_L__, r28\npush r10\npush r11\npush r12\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("pop r11\npop r10\nadiw r28, 16\n")) << s;
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
    EXPECT_NE(std::string::npos, s.find("subi r28, 82\nsbci r29, 0\n")) << s;
    EXPECT_NE(std::string::npos, s.find("subi r28, 174\nsbci r29, 255\n")) << s;
    // v30 at Y+61, v31 straddling Y+63, v32 past it.
    EXPECT_NE(std::string::npos, s.find("std Y+61, r24\nstd Y+62, r25\n")) << s;
    EXPECT_NE(std::string::npos, s.find("movw r30, r28\nadiw r30, 63\nst Z+, r24\nst Z+, r25\n"))
        << s;
    EXPECT_NE(std::string::npos,
              s.find("movw r30, r28\nsubi r30, 191\nsbci r31, 255\nst Z+, r24\nst Z+, r25\n"))
        << s;
}

// A copy chain through 40 slots, across Y+63.
TEST_F(AvrTest, RunLargeFrame)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string src = "int main(void) {\n    volatile int v0 = 0x1234;\n";
    for (int i = 1; i < 40; i++)
        src += "    volatile int v" + std::to_string(i) + " = v" + std::to_string(i - 1) +
               ";\n";
    src += "    return v39; }\n";
    EXPECT_EQ("4660\n", CompileAndRunBook(src));
}
