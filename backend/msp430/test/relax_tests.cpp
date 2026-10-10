//
// MSP430 branch relaxation: jumps just within, at and just past their reach, on
// hand-built IR, each also assembled by GNU as and clang; and a loop body over 1 KB run on mspsim.
//
#include "msp430_test.h"

extern "C" {
#include "internal.h"
}

namespace {

// A function of: [`.Lt`:] `before` nops, the jump `op` to .Lt, `after` nops, and
// .Lt: ret -- or with `backward`, .Lt: at the start.  Returns the whole assembly.
std::string RelaxedAsm(Msp_Op op, int before, int after, bool backward)
{
    gen_unit_begin();
    Msp_Func *fn = msp_new_func("f", true);
    if (backward)
        msp_new_block(fn, ".Lt");
    for (int i = 0; i < before; i++)
        msp_append(fn, MSP_NOP);
    msp_append(fn, op)->opnd[0] = msp_label(".Lt");
    for (int i = 0; i < after; i++)
        msp_append(fn, MSP_NOP);
    if (!backward)
        msp_new_block(fn, ".Lt");
    msp_append(fn, MSP_RET);
    msp_relax(fn);
    FILE *f = tmpfile();
    EXPECT_NE(nullptr, f);
    msp_emit_func(f, fn);
    msp_free_func(fn);
    long len = ftell(f);
    rewind(f);
    std::string s(static_cast<size_t>(len), '\0');
    if (len > 0)
        EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
    fclose(f);
    return s;
}

// The labels and instructions but nops and directives.
std::string Skeleton(const std::string &s)
{
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

class RelaxTest : public ::testing::Test {
protected:
    // The relaxed function's skeleton; GNU as, and clang if present, must assemble it.
    std::string Relaxed(Msp_Op op, int before, int after, bool backward)
    {
        std::string s = RelaxedAsm(op, before, after, backward);
        // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
        if (msp430_tools_available()) {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            std::string base = std::string(TEST_DIR "/") + info->test_suite_name() + "." +
                               info->name() + "." + std::to_string(after + before);
            {
                std::ofstream f(base + ".s");
                f << s;
            }
            std::vector<std::string> as = split_words(MSP430_ASSEMBLER);
            as.insert(as.end(), { "-o", base + ".o", base + ".s" });
            EXPECT_EQ(0, RunTool(as, base + ".log")) << ReadFile(base + ".log");
            // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
            if (msp430_clang_available())
                EXPECT_EQ(0, RunTool({ MSP430_CLANG, "--target=msp430", "-c", "-o",
                                       base + ".clang.o", base + ".s" },
                                     base + ".log"))
                    << ReadFile(base + ".log");
        }
        return Skeleton(s);
    }
};

// Forward: a jump reaches 511 words ahead.
TEST_F(RelaxTest, ForwardJumpInRange)
{
    EXPECT_EQ(R"(f:
    jeq     .Lt
.Lt:
    ret
)",
              Relaxed(MSP_JEQ, 0, 511, false));
}

TEST_F(RelaxTest, ForwardJumpOutOfRange)
{
    EXPECT_EQ(R"(f:
    jne     .Lv0
    br      #.Lt
.Lv0:
.Lt:
    ret
)",
              Relaxed(MSP_JEQ, 0, 512, false));
}

// Backward: 512 words back.
TEST_F(RelaxTest, BackwardJumpInRange)
{
    EXPECT_EQ(R"(f:
.Lt:
    jl      .Lt
    ret
)",
              Relaxed(MSP_JL, 511, 0, true));
}

TEST_F(RelaxTest, BackwardJumpOutOfRange)
{
    EXPECT_EQ(R"(f:
.Lt:
    jge     .Lv0
    br      #.Lt
.Lv0:
    ret
)",
              Relaxed(MSP_JL, 512, 0, true));
}

// Every conditional's inverse.
TEST_F(RelaxTest, Inverses)
{
    EXPECT_NE(std::string::npos, Relaxed(MSP_JNE, 0, 600, false).find("jeq     .Lv0"));
    EXPECT_NE(std::string::npos, Relaxed(MSP_JLO, 0, 600, false).find("jhs     .Lv0"));
    EXPECT_NE(std::string::npos, Relaxed(MSP_JHS, 0, 600, false).find("jlo     .Lv0"));
    EXPECT_NE(std::string::npos, Relaxed(MSP_JGE, 0, 600, false).find("jl      .Lv0"));
}

// jn has no inverse: it jumps over a jmp to a br.
TEST_F(RelaxTest, NegativeJumpOutOfRange)
{
    EXPECT_EQ(R"(f:
    jn      .Lv1
    jmp     .Lv0
.Lv1:
    br      #.Lt
.Lv0:
.Lt:
    ret
)",
              Relaxed(MSP_JN, 0, 600, false));
}

// An unconditional jump becomes a br.
TEST_F(RelaxTest, UnconditionalJump)
{
    EXPECT_EQ(R"(f:
    jmp     .Lt
.Lt:
    ret
)",
              Relaxed(MSP_JMP, 0, 511, false));
    EXPECT_EQ(R"(f:
    br      #.Lt
.Lt:
    ret
)",
              Relaxed(MSP_JMP, 0, 512, false));
}

// A loop of over 1 KB: its backward jump and its exit are relaxed.
TEST_F(Msp430Test, RunLongLoop)
{
    SKIP_IF_NO_MSP430_TOOLS();
    // 300 adds of a constant no generator supplies: 4 bytes each, in a register.
    std::string body;
    int sum = 0;
    for (int i = 0; i < 300; i++) {
        body += "        sum = sum + " + std::to_string(i % 7 + 10) + ";\n";
        sum += 2 * (i % 7 + 10);
    }
    std::string src      = R"(int main(void) {
    int sum = 0;
    for (int i = 0; i < 2; i++) {
)" + body + R"(    }
    return sum;
}
)";
    std::string asm_text = CompileToMsp430(src.c_str()); // once: the symbols live per test
    EXPECT_NE(std::string::npos, Code(asm_text).find("br #")) << "not relaxed";
    EXPECT_EQ(std::to_string(sum) + "\n", Run(asm_text, "crt0-status.o"));
}
