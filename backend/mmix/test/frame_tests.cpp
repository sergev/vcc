//
// MMIX frames: slots off $254 (or $253 with a frame pointer), parameters stored from
// their registers, stack parameters left where they came in, and offsets past the 8-bit
// field through $255.
//
#include "mmix_test.h"

// No slots: no frame at all; a leaf returns straight in $0.
EXPECT_CODE(FramelessConstant, R"(setl $0, #7
pop 1, 0
)",
            "int f(void) { return 7; }")

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

// --frame-pointer: the slots from $253, which the prologue saves in a slot of its own
// and sets to $254; the epilogue sets $254 from it and loads it back.
TEST_F(MmixTest, FramePointerOption)
{
    NaiveSelection();
    mmix_frame_pointer = true;
    EXPECT_EQ(R"(subu $254, $254, 24
sto $253, $254, 16
set $253, $254
sto $0, $253, 0
sto $1, $253, 8
ldo $0, $253, 8
set $254, $253
ldo $253, $254, 16
addu $254, $254, 24
pop 1, 0
)",
              Code(CompileToMmix("long f(long a, long b) { return b; }")));
    mmix_frame_pointer = false;
}

// alloca: the frame from $253 (without --frame-pointer), $254 lowered by the size
// rounded to 8 through $248, the memory above the outgoing arguments; the epilogue sets
// $254 back from $253.
TEST_F(MmixTest, AllocaLeaf)
{
    EXPECT_EQ(R"(subu $254, $254, 8
sto $253, $254, 0
set $253, $254
addu $248, $0, 7
andn $248, $248, 7
subu $254, $254, $248
addu $2, $254, 0
sto $0, $2, 0
addu $0, $0, 1
set $254, $253
ldo $253, $254, 0
addu $254, $254, 8
pop 1, 0
)",
              Code(CompileToMmix(R"(
void *__builtin_alloca(unsigned long);
long f(long n)
{
    long *p = __builtin_alloca(n);
    p[0] = n;
    return p[0] + 1;
}
)")));
}

TEST_F(MmixTest, AllocaAboveOutgoing)
{
    std::string code = Code(CompileToMmix(R"(
void *__builtin_alloca(unsigned long);
long g(long, long, long, long, long, long, long, long, long, long, long, long, long, long,
       long, long, long, long);
long f(long n, long k)
{
    long *p = __builtin_alloca(n);
    p[0] = k;
    return g(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, p[0], k) + p[0];
}
)"));
    EXPECT_NE(std::string::npos, code.find("subu $254, $254, $248\naddu $4, $254, 16\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sto $1, $254, 0\nsto $1, $254, 8\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find("set $254, $253\nldo $253, $254, 16\naddu $254, $254, 24\npop 1, 0\n"))
        << code;
}

static const char seventeen[] =
    "long a0, long a1, long a2, long a3, long a4, long a5, long a6, "
    "long a7, long a8, long a9, long a10, long a11, long a12, "
    "long a13, long a14, long a15";

// The 17th parameter and later stay where they came in, 8 bytes each above the frame,
// a narrow one in the last bytes of its slot (big-endian).
TEST_F(MmixTest, StackParamsAboveFrame)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix((std::string("long f(") + seventeen +
                                           ", long a16, int a17) { return a16; }\n" + "int g(" +
                                           seventeen + ", long a16, int a17) { return a17; }\n")
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
