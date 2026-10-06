//
// MMIX frames: slots off $254, parameters stored from their registers, stack parameters
// left where they came in, and offsets past the 8-bit field through $255.
//
#include "mmix_test.h"

// No slots: no frame at all; a leaf returns straight in $0.
EXPECT_CODE(FramelessConstant, R"(setl $0, #7
pop 1, 0
)", "int f(void) { return 7; }")

// Register parameters go to their slots; the frame is reserved and released around the
// body.
EXPECT_CODE(ParamsStored,
            R"(subu $254, $254, 16
sto $0, $254, 0
sto $1, $254, 8
ldo $0, $254, 8
addu $254, $254, 16
pop 1, 0
)",
            "long f(long a, long b) { return b; }")

// Each slot in its own width: a char is a byte, loaded back with its sign.
EXPECT_CODE(CharParam,
            R"(subu $254, $254, 8
stbu $0, $254, 0
ldb $0, $254, 0
addu $254, $254, 8
pop 1, 0
)",
            "signed char f(signed char c) { return c; }")

EXPECT_CODE(UnsignedShortParam,
            R"(subu $254, $254, 8
stwu $0, $254, 0
ldwu $0, $254, 0
addu $254, $254, 8
pop 1, 0
)",
            "unsigned short f(unsigned short c) { return c; }")

// A float comes and goes as its binary32 bits, sign-extended, as GCC passes it.
EXPECT_CODE(FloatParam,
            R"(subu $254, $254, 8
sttu $0, $254, 0
ldt $0, $254, 0
addu $254, $254, 8
pop 1, 0
)",
            "float f(float x) { return x; }")

// Slots are aligned to their types: a char, then an int at 4, a long at 8.
EXPECT_CODE(SlotsAligned,
            R"(subu $254, $254, 16
stbu $0, $254, 0
sttu $1, $254, 4
sto $2, $254, 8
ldt $0, $254, 4
addu $254, $254, 16
pop 1, 0
)",
            "int f(char c, int i, long l) { return i; }")

static const char seventeen[] = "long a0, long a1, long a2, long a3, long a4, long a5, long a6, "
                                "long a7, long a8, long a9, long a10, long a11, long a12, "
                                "long a13, long a14, long a15";

// The 17th parameter and later stay where they came in, 8 bytes each above the frame,
// a narrow one in the last bytes of its slot (big-endian).
TEST_F(MmixTest, StackParamsAboveFrame)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        (std::string("long f(") + seventeen + ", long a16, int a17) { return a16; }\n" +
         "int g(" + seventeen + ", long a16, int a17) { return a17; }\n")
            .c_str()));
    EXPECT_NE(std::string::npos, code.find("subu $254, $254, 128\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldo $0, $254, 128\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldt $0, $254, 140\n")) << code;
}

// A slot past 255 bytes is reached through $255, and so is a frame over 255 bytes: `a`
// at 0, `big` at 8, `b` at 312, a temporary at 320, 328 bytes in all.
EXPECT_CODE(LargeFrame,
            R"(setl $255, #148
subu $254, $254, $255
sto $0, $254, 0
ldo $248, $254, 0
setl $255, #138
sto $248, $254, $255
setl $255, #138
ldo $248, $254, $255
setl $255, #140
sto $248, $254, $255
setl $255, #140
ldo $0, $254, $255
setl $255, #148
addu $254, $254, $255
pop 1, 0
)",
            "long f(long a) { char big[300]; volatile long b = a; return b; }")

// Run: a frame over 255 bytes, and a slot past it.
TEST_F(MmixTest, RunLargeFrame)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        int main(void)
        {
            long big[40];
            int x = 77;
            int y = x;
            return y;
        }
    )"));
    EXPECT_EQ(77, exit_status);
}
