//
// The loop optimizations: rotation (the translator tests a loop at its bottom, behind a
// guard), the comparison with a constant mirrored to put the constant second, and
// induction-variable strength reduction with linear-function test replacement.
//
#include <gtest/gtest.h>

#include <string>

#include "pipeline_test_fixture.h"
#include "target.h"

namespace {

// The loop optimizations on, as the compiler runs them on every target but BESM-6.
OptFlags LoopFlags()
{
    OptFlags flags    = opt_flags_default();
    flags.loop_rotate = true;
    return flags;
}

struct TargetGuard {
    const Target *saved;
    explicit TargetGuard(const char *name) : saved(target_config) { target_config = target_lookup(name); }
    ~TargetGuard() { target_config = saved; }
};

int Count(const std::string &s, const std::string &what)
{
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
        n++;
    return n;
}

const char *sum_src = R"(
int sum(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)";

} // namespace

// A rotated loop: the guard and the bottom test, one conditional jump back and no
// unconditional one.
TEST_F(PipelineTest, LoopRotated)
{
    OptFlags flags = LoopFlags();
    flags.ivsr     = false;
    std::string y  = OptimizeYaml(R"(
int count(int n)
{
    int k = 0;
    while (n > 0) {
        k++;
        n = n - 2;
    }
    return k;
}
)",
                                 flags);
    EXPECT_EQ(1, Count(y, "kind: jump_if_zero")) << y;
    EXPECT_EQ(1, Count(y, "kind: jump_if_not_zero")) << y;
    EXPECT_EQ(0, Count(y, "kind: jump\n")) << y;
}

// Unrotated, as before: the test at the top and a jump back.
TEST_F(PipelineTest, LoopNotRotated)
{
    OptFlags flags    = LoopFlags();
    flags.loop_rotate = false;
    flags.ivsr        = false;
    std::string y     = OptimizeYaml("int f(int n) { int k = 0; while (n > 0) { k++; n--; } return k; }",
                                 flags);
    EXPECT_EQ(1, Count(y, "kind: jump\n")) << y;
    EXPECT_EQ(0, Count(y, "kind: jump_if_not_zero")) << y;
}

// The guard of `for (i = 0; i < n; ...)` compares 0 with n; mirrored, n comes first.
TEST_F(PipelineTest, LoopGuardMirrored)
{
    OptFlags flags = LoopFlags();
    flags.ivsr     = false;
    std::string y  = OptimizeYaml(sum_src, flags);
    EXPECT_NE(std::string::npos, y.find(R"(  op: greater_than
  src1:
    kind: var
    name: %n
  src2:
    kind: constant)"))
        << y;
}

// p[i] becomes a pointer stepped through the array, the test a comparison with the end
// pointer, and i goes: no add or compare on it is left.
TEST_F(PipelineTest, LoopIndexStrengthReduced)
{
    std::string y = OptimizeYaml(sum_src, LoopFlags());
    EXPECT_EQ(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_EQ(2, Count(y, "kind: add_ptr")) << y; // the end pointer, and the step
    EXPECT_EQ(0, Count(y, "scale: 4\n  dst:\n    kind: var\n    name: %i")) << y;
}

// Without the pass, p[i] is computed from i every iteration.
TEST_F(PipelineTest, LoopIndexNotReducedWhenOff)
{
    OptFlags flags = LoopFlags();
    flags.ivsr     = false;
    std::string y  = OptimizeYaml(sum_src, flags);
    EXPECT_NE(std::string::npos, y.find("name: %i\n")) << y;
}

// i is read after the loop, so it stays; the address is still reduced.
TEST_F(PipelineTest, LoopIndexKeptWhenRead)
{
    std::string y = OptimizeYaml(R"(
int find(int *p, int n, int x)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] == x)
            break;
    return i;
}
)",
                                 LoopFlags());
    EXPECT_NE(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_EQ(std::string::npos, y.find("    name: %p\n  index:\n    kind: var\n    name: %i")) << y;
}

// p[i] and p[i + 1] share one pointer: the second is that pointer plus one element.
TEST_F(PipelineTest, LoopNeighboursShareAPointer)
{
    std::string y = OptimizeYaml(R"(
int rises(int *p, int n)
{
    int k = 0;
    for (int i = 0; i < n - 1; i++)
        if (p[i] < p[i + 1])
            k++;
    return k;
}
)",
                                 LoopFlags());
    EXPECT_EQ(std::string::npos, y.find("name: %i\n")) << y;
    // p indexed by a variable once: the end pointer, ahead of the loop.
    EXPECT_EQ(1, Count(y, "    name: %p\n  index:\n    kind: var")) << y;
}

// An index that is not i plus a constant is left alone.
TEST_F(PipelineTest, LoopSquareIndexNotReduced)
{
    std::string y = OptimizeYaml(R"(
int f(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i * i];
    return s;
}
)",
                                 LoopFlags());
    EXPECT_NE(std::string::npos, y.find("    name: %p\n  index:\n    kind: var")) << y;
}

// A counter whose address is taken may change behind the pass's back: not an
// induction variable.
TEST_F(PipelineTest, LoopAddressTakenCounterNotReduced)
{
    std::string y = OptimizeYaml(R"(
void g(int *);
int f(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++) {
        s += p[i];
        g(&i);
    }
    return s;
}
)",
                                 LoopFlags());
    EXPECT_NE(std::string::npos, y.find("    name: %p\n  index:\n    kind: var")) << y;
}

// A downward loop: the pointer steps back, the test against the end stays a >=.
TEST_F(PipelineTest, LoopDownwardReduced)
{
    std::string y = OptimizeYaml(R"(
int sum(int *p, int n)
{
    int s = 0;
    for (int i = n - 1; i >= 0; i--)
        s += p[i];
    return s;
}
)",
                                 LoopFlags());
    EXPECT_EQ(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_NE(std::string::npos, y.find("op: greater_or_equal")) << y;
}

const char *bubble_src = R"(
void sort(int *v, int n)
{
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1 - i; j++)
            if (v[j] > v[j + 1]) {
                int t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
}
)";

// v[j] and v[j + 1], both read and then written: one pointer, stepped where v[j + 1]
// is formed and read one element lower by the store to v[j]. No second pointer is
// copied back into it.
TEST_F(PipelineTest, LoopPointerSteppedInPlace)
{
    std::string y = OptimizeYaml(bubble_src, LoopFlags());
    // The step of the inner pointer, in place.
    EXPECT_EQ(1, Count(y, "    name: %1000\n  index:\n    kind: constant\n    const:\n"
                          "      kind: long\n      value: 1\n  scale: 4\n  dst:\n"
                          "    kind: var\n    name: %1000\n"))
        << y;
    // v[j], stored to behind the step.
    EXPECT_EQ(1, Count(y, "    name: %1000\n  index:\n    kind: constant\n    const:\n"
                          "      kind: long\n      value: -1\n"))
        << y;
    // No second pointer: the end pointer and its step, and these two.
    EXPECT_EQ(4, Count(y, "kind: add_ptr")) << y;
}

// The inner bound n - 1 - i: the end pointer v + (n - 1 - i) steps down with i, the
// inner guard and the outer test compare it with v, and i goes.
TEST_F(PipelineTest, LoopCountdownBoundReduced)
{
    std::string y = OptimizeYaml(bubble_src, LoopFlags());
    EXPECT_EQ(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_EQ(std::string::npos, y.find("name: %j\n")) << y;
    // Only ahead of the loops is v indexed by a variable.
    EXPECT_EQ(1, Count(y, "    name: %v\n  index:\n    kind: var")) << y;
    // The end pointer, stepped down and compared twice: by the guard and by the test.
    EXPECT_EQ(1, Count(y, "      value: -1\n  scale: 4\n  dst:\n    kind: var\n    name: %1004\n")) << y;
    EXPECT_EQ(2, Count(y, "op: greater_than\n  src1:\n    kind: var\n    name: %1004\n")) << y;
}

// Without the pointer on n - 1 - i, i is read by something else than its test.
TEST_F(PipelineTest, LoopCounterKeptWhenBoundReadElsewhere)
{
    std::string y = OptimizeYaml(R"(
int g(int);
void f(int *v, int n)
{
    for (int i = 0; i < n; i++) {
        int m = n - i;
        v[m] = g(m);
    }
}
)",
                                 LoopFlags());
    EXPECT_NE(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_EQ(1, Count(y, "    name: %v\n  index:\n    kind: var")) << y;
}

// The address of a global array, first taken in the loop, moves ahead of it; the
// array is then walked by a pointer like any other.
TEST_F(PipelineTest, LoopGlobalAddressHoisted)
{
    std::string y = OptimizeYaml(R"(
int a[8];
void fill(int x)
{
    for (int i = 0; i < 8; i++)
        a[i] = x;
}
)",
                                 LoopFlags());
    size_t addr = y.find("kind: get_address"), label = y.find("kind: label");
    ASSERT_NE(std::string::npos, addr) << y;
    EXPECT_LT(addr, label) << y;
    EXPECT_EQ(std::string::npos, y.find("name: %i\n")) << y;
}

// Two loops whose counters share a name: the second one's goes, though the first
// reads its own.
TEST_F(PipelineTest, LoopCounterDroppedBesideAnotherLoop)
{
    std::string y = OptimizeYaml(R"(
int g(int);
int f(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += g(i);
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)",
                                 LoopFlags());
    // i's step in the first loop only.
    EXPECT_EQ(1, Count(y, "op: add\n  src1:\n    kind: var\n    name: %i\n")) << y;
}

// BESM-6 opts out of the loop optimizations: its code stays as it was.
TEST_F(PipelineTest, LoopOptimizationsOffOnBesm6)
{
    TargetGuard t("besm6");
    std::string y = OptimizeYaml(sum_src, LoopFlags());
    EXPECT_NE(std::string::npos, y.find("name: %i\n")) << y;
    EXPECT_EQ(1, Count(y, "kind: jump\n")) << y;
}

// A store through a cast pointer keeps the cast's type: copy propagation does not
// forward `(unsigned long *)&d` as the `double *` it was cast from.
TEST_F(PipelineTest, CastPointerNotForwardedIntoStore)
{
    std::string y = OptimizeYaml(R"(
double f(void)
{
    double d;
    unsigned long *w = (unsigned long *)&d;
    *w = 0;
    return d;
}
)",
                                 LoopFlags());
    EXPECT_NE(std::string::npos, y.find("kind: copy")) << y;
}
