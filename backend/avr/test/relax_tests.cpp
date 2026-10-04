//
// AVR branch relaxation: branches just within, at and just past their reach, on
// hand-built IR; and a loop body over 128 bytes run on qemu.
//
#include "avr_test.h"

extern "C" {
#include "internal.h"
}

namespace {

// A function of: [`label`:] `before` nops, the branch `op` to .Lt, `after` nops, and
// .Lt: ret -- or with `backward`, .Lt: at the start.
std::string Relaxed(AVR_Op op, int before, int after, bool backward)
{
    gen_unit_begin();
    AVR_Func *fn = avr_new_func("f", true);
    if (backward)
        avr_new_block(fn, ".Lt");
    for (int i = 0; i < before; i++)
        avr_append(fn, AVR_NOP);
    avr_append(fn, op)->opnd[0] = avr_label(".Lt");
    for (int i = 0; i < after; i++)
        avr_append(fn, AVR_NOP);
    if (!backward)
        avr_new_block(fn, ".Lt");
    avr_append(fn, AVR_RET);
    avr_relax(fn);
    FILE *f = tmpfile();
    avr_emit_func(f, fn);
    avr_free_func(fn);
    long len = ftell(f);
    rewind(f);
    std::string s(static_cast<size_t>(len), '\0');
    if (len > 0)
        EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
    fclose(f);
    // The labels and instructions but nops.
    std::string out;
    for (size_t pos = 0, nl; pos < s.size(); pos = nl + 1) {
        nl               = s.find('\n', pos);
        std::string line = s.substr(pos, nl - pos);
        if (line.find("nop") == std::string::npos && line.compare(0, 5, "    .") != 0)
            out += line + "\n";
    }
    return out;
}

} // namespace

class RelaxTest : public ::testing::Test {};

// Forward: a branch reaches 63 words ahead.
TEST_F(RelaxTest, ForwardBranchInRange)
{
    EXPECT_EQ(R"(f:
    breq    .Lt
.Lt:
    ret
)", Relaxed(AVR_BREQ, 0, 63, false));
}

TEST_F(RelaxTest, ForwardBranchOutOfRange)
{
    EXPECT_EQ(R"(f:
    brne    .Lv0
    rjmp    .Lt
.Lv0:
.Lt:
    ret
)",
              Relaxed(AVR_BREQ, 0, 64, false));
}

// Backward: 64 words back.
TEST_F(RelaxTest, BackwardBranchInRange)
{
    EXPECT_EQ(R"(f:
.Lt:
    brlt    .Lt
    ret
)", Relaxed(AVR_BRLT, 63, 0, true));
}

TEST_F(RelaxTest, BackwardBranchOutOfRange)
{
    EXPECT_EQ(R"(f:
.Lt:
    brge    .Lv0
    rjmp    .Lt
.Lv0:
    ret
)",
              Relaxed(AVR_BRLT, 64, 0, true));
}

// rjmp reaches 2047 words ahead and 2048 back; past that it is a jmp.
TEST_F(RelaxTest, JumpInRange)
{
    EXPECT_EQ(R"(f:
    rjmp    .Lt
.Lt:
    ret
)", Relaxed(AVR_RJMP, 0, 2047, false));
    EXPECT_EQ(R"(f:
.Lt:
    rjmp    .Lt
    ret
)", Relaxed(AVR_RJMP, 2047, 0, true));
}

TEST_F(RelaxTest, JumpOutOfRange)
{
    EXPECT_EQ(R"(f:
    jmp     .Lt
.Lt:
    ret
)", Relaxed(AVR_RJMP, 0, 2048, false));
    EXPECT_EQ(R"(f:
.Lt:
    jmp     .Lt
    ret
)", Relaxed(AVR_RJMP, 2048, 0, true));
}

// A relaxed branch whose rjmp is itself out of reach becomes a jmp.
TEST_F(RelaxTest, BranchFarAway)
{
    EXPECT_EQ(R"(f:
    brne    .Lv0
    jmp     .Lt
.Lv0:
.Lt:
    ret
)",
              Relaxed(AVR_BREQ, 0, 3000, false));
}

// A loop of over 128 bytes: its backward branch and its exit are relaxed.
TEST_F(AvrTest, RunLongLoop)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string body;
    for (int i = 0; i < 20; i++)
        body += "        sum = sum + " + std::to_string(i) + ";\n";
    EXPECT_EQ("1900\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
)" +
                                          body + R"(    }
    return sum;
}
)"));
}
