//
// MMIX control flow: branches on one register against zero, L:n labels unique in the
// translation unit, and branches beyond their 16-bit range expanded by as -x and ld.
//
#include <set>

#include "mmix_test.h"

// A conditional jump tests the value itself: bz or bnz.
TEST_F(MmixTest, BranchOnValue)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("long f(long a) { if (a) return 1; return 2; }"));
    EXPECT_NE(std::string::npos, code.find(R"(ldo $248, $254, 0
bz $248, L:)")) << code;
}

// A loop jumps back to its top; every label is L: and defined once in the unit.
TEST_F(MmixTest, LabelsUniqueInUnit)
{
    NaiveSelection();
    std::string s = CompileToMmix(R"(
        long f(long n) { long s = 0; while (n > 0) { s = s + n; n = n - 1; } return s; }
        long g(long n) { long s = 0; for (long i = 0; i < n; i = i + 1) { if (i == 3) continue; s = s + i; } return s; }
    )");
    std::set<std::string> labels;
    size_t pos = 0;
    while ((pos = s.find(R"(
L:)", pos)) != std::string::npos) {
        size_t end        = s.find(":\n", pos + 3);
        std::string label = s.substr(pos + 1, end - pos - 1);
        EXPECT_TRUE(labels.insert(label).second) << "defined twice: " << label << "\n" << s;
        pos = end;
    }
    EXPECT_GE(labels.size(), 4u) << s;
    EXPECT_NE(std::string::npos, Code(s).find("jmp L:")) << s;
}

// Run: loops with break and continue, nested, and the short-circuit operators.
TEST_F(MmixTest, RunLoops)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        int main(void)
        {
            long s = 0;
            for (long i = 0; i < 100; i = i + 1) {
                if (i % 7 == 0)
                    continue;
                if (i > 90)
                    break;
                for (long j = 0; j < 3; j = j + 1)
                    s = s + (j == 1 && i > 50 || j == 2);
            }
            int k = 0;
            do
                k = k + 3;
            while (k < 20);
            return s == 113 && k == 21 ? 0 : 1;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// A branch reaches 64 K instructions either way (256 KB); the selection emits only the
// short forms, and as -x with the linker turns one out of range into an inverted short
// branch around a go.  Each of these branches crosses 256 KB of code that never runs:
// a forward bz, a forward jmp, a backward pbnz.  (A C body that large takes the
// frontend seconds to compile, so the filler is .skip.)
TEST_F(MmixTest, RunBranchesBeyondRange)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", RunAssembly(R"(    .text
    .global main
    .p2align 2
main:
    setl    $1, 0
    setl    $2, 3
L:1:
    bz      $2, L:4
    subu    $2, $2, 1
    jmp     L:2
    .skip   262200
L:2:
    addu    $1, $1, 10
    pbnz    $1, L:1
    .skip   262200
L:4:
    set     $0, $1
    pop     1, 0
)"));
    EXPECT_EQ(30, exit_status);
}
