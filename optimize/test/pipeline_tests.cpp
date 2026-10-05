#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>

extern "C" {
//
// The single definition of fatal_error for the optimizer-tests binary; the
// shared PipelineTest fixture (and chapter19_tests.cpp) link against it.
//
[[noreturn]] void fatal_error(const char *message, ...)
{
    fprintf(stderr, "Fatal error: ");
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}
}

#include "pipeline_test_fixture.h"

// ---------------------------------------------------------------------------
// Constant folding
// ---------------------------------------------------------------------------

TEST_F(PipelineTest, DivisionConstantFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return 6/2; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n");
}

TEST_F(PipelineTest, AdditionConstantFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return 2+2; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 4\n");
}

TEST_F(PipelineTest, SubtractionConstantFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return 10-3; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 7\n");
}

TEST_F(PipelineTest, MultiplicationConstantFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return 3*4; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 12\n");
}

TEST_F(PipelineTest, UnaryNotFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return !0; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n");
}

TEST_F(PipelineTest, UnaryComplementFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { return ~0; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: -1\n");
}

TEST_F(PipelineTest, LongConstantFolded)
{
    EXPECT_EQ(OptimizeYaml("long f(void) { return 3L * 4L; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: long\n"
              "      value: 12\n");
}

// ---------------------------------------------------------------------------
// Multi-pass interactions
// ---------------------------------------------------------------------------

// Two optimizer iterations needed: first folds (2+3)→5, second folds (5*4)→20.
TEST_F(PipelineTest, TwoPassFolded)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { int a = 2+3; return a*4; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 20\n");
}

// ---------------------------------------------------------------------------
// Copy propagation
// ---------------------------------------------------------------------------

// Copy(x, t) → Return(t): copy prop rewrites to Return(x); dead store removes Copy.
TEST_F(PipelineTest, CopyPropSimple)
{
    EXPECT_EQ(OptimizeYaml("int g(int x) { int t = x; return t; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: %x\n");
}

// Chain a=x, b=a → Return(b) fully propagated to Return(x).
TEST_F(PipelineTest, CopyPropChain)
{
    EXPECT_EQ(OptimizeYaml("int g(int x) { int a = x; int b = a; return b; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: %x\n");
}

// sizeof is a size_t constant: va_arg's `ap - ((sizeof(T) + 7) & ~7)` folds, over
// several rounds, to a 64-bit -8. As an int constant it went through unsigned int
// and became 2^32 - 8, which the pointer arithmetic zero-extended.
TEST_F(PipelineTest, SizeofFoldsAsSizeT)
{
    OptFlags flags       = opt_flags_default();
    flags.max_iterations = 0;
    std::string yaml =
        OptimizeYaml("char *f(char *p) { return p - ((sizeof(long) + 7) & ~7); }", flags);
    EXPECT_NE(std::string::npos, yaml.find("value: 18446744073709551608")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("value: 4294967288")) << yaml;
}

// ---------------------------------------------------------------------------
// Dead store elimination
// ---------------------------------------------------------------------------

// Copy(3, t) is dead (overwritten); Copy(4, t) propagated into Return; then also dead.
TEST_F(PipelineTest, DeadStoreOverwrite)
{
    EXPECT_EQ(OptimizeYaml("int f(void) { int t = 3; t = 4; return t; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 4\n");
}

// ---------------------------------------------------------------------------
// Dead branch elimination
// ---------------------------------------------------------------------------

// if(0): fold JIZ(0,"L")→Jump("L"); then-block unreachable → removed.
TEST_F(PipelineTest, DeadBranchIfZero)
{
    EXPECT_EQ(OptimizeYaml("int h(int x) { if (0) return 1; return 2; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 2\n");
}

// if(1): fold JIZ(1,"L")→deleted; else-block unreachable → removed.
TEST_F(PipelineTest, DeadBranchIfOne)
{
    EXPECT_EQ(OptimizeYaml("int h(int x) { if (1) return 42; return 0; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 42\n");
}

// ---------------------------------------------------------------------------
// No optimization needed
// ---------------------------------------------------------------------------

// Direct return of a parameter: no temps, optimizer is a pass-through.
TEST_F(PipelineTest, NoOptNeeded)
{
    EXPECT_EQ(OptimizeYaml("int f(int x) { return x; }"),
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: %x\n");
}

// ---------------------------------------------------------------------------
// Dead store elimination must not remove loads used as indirect call targets
// ---------------------------------------------------------------------------

// load fp→t.0 must survive: t.0 is the callee in an indirect fun_call.
// Bug: DSE does not count fun_call.fun_name as a use when it is a variable.
// A call through a function pointer loaded from memory (here a pointer-to-function-pointer)
// keeps the LOAD that materializes the call target: dead-store elimination must not drop
// the temp that feeds fun_call's fun_name.  (A single-level `(*fp)(...)` is folded to a
// direct indirect call with no LOAD, so a double pointer is used to force the LOAD.)
TEST_F(PipelineTest, DeadStoreKeepsIndirectCallTarget)
{
    EXPECT_EQ(OptimizeYaml("int f(int (**pp)(int)) { return (*pp)(42); }"),
              "- instruction:\n"
              "  kind: load\n"
              "  src_ptr:\n"
              "    kind: var\n"
              "    name: %pp\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: %0\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: %0\n"
              "  indirect: true\n"
              "  args:\n"
              "    - val:\n"
              "      kind: constant\n"
              "      const:\n"
              "        kind: int\n"
              "        value: 42\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: %1\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: %1\n");
}

// ---------------------------------------------------------------------------
// Writes to observable variables survive dead-store elimination
// ---------------------------------------------------------------------------

// Regression: a write to a file-scope global must survive even though the
// global is declared in a separate external declaration. The optimizer learns
// that "g" is not a local of f (f has no automatic locals named g) and so keeps
// the store.
TEST_F(PipelineTest, GlobalWriteSurvivesDeadStore)
{
    EXPECT_EQ(OptimizeYaml("int g; void f(void) { g = 5; }"),
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n");
}

// An `extern` global has no static_variable toplevel at all, yet the per-function
// classification still recognises it as non-local and preserves the store.
TEST_F(PipelineTest, ExternGlobalWriteSurvives)
{
    EXPECT_EQ(OptimizeYaml("extern int g; void f(void) { g = 5; }"),
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n");
}

// Negative control: the dead local `x` is still removed; only the global write
// survives. Guards against over-preserving once globals are kept.
TEST_F(PipelineTest, DeadLocalRemovedAlongsideGlobalWrite)
{
    EXPECT_EQ(OptimizeYaml("int g; void f(void) { int x = 7; g = 5; }"),
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n");
}

// The symbol list follows the optimized body: a temporary or variable whose last
// use was folded or eliminated is dropped, the rest keep their types.
TEST_F(PipelineTest, LocalsPrunedToOptimizedBody)
{
    std::string yaml = OptimizeYaml("int f(int a) { int x = a + 1; int y = 7; x = y; return x * a; }");
    EXPECT_EQ(locals, "%1:int") << yaml;
}

TEST_F(PipelineTest, LocalsKeptWithoutOptimization)
{
    OptimizeYaml("int f(int a) { int x = a + 1; int y = 7; x = y; return x * a; }", OptFlags{});
    EXPECT_EQ(locals, "%x:int %0:int %y:int %1:int");
}

// ---------------------------------------------------------------------------
// Volatile: every access to a volatile object happens once, as written
// ---------------------------------------------------------------------------

// The number of times `needle` occurs in `hay`.
static int count_of(const std::string &hay, const std::string &needle)
{
    int n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
        n++;
    return n;
}

// The initializing write and both accesses of r++ survive, and the increment adds to
// the value read, not to the 0 stored before the call.
TEST_F(PipelineTest, VolatilePostIncrementRereads)
{
    std::string yaml =
        OptimizeYaml("int g(void); int f(void) { volatile int r = 0; g(); r++; return r; }");
    EXPECT_EQ(4, count_of(yaml, "volatile: true")) << yaml;
    EXPECT_NE(std::string::npos, yaml.find("  kind: binary\n  op: add\n  src1:\n    kind: var\n"))
        << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("  kind: return\n  src:\n    kind: constant")) << yaml;
}

// ++r, r += 2 and r-- each read r once and write it once.
TEST_F(PipelineTest, VolatileUpdatesReadOnceWriteOnce)
{
    std::string yaml = OptimizeYaml("int f(void) { volatile int r = 5; ++r; r += 2; r--; return r; }");
    EXPECT_EQ(8, count_of(yaml, "volatile: true")) << yaml;
    EXPECT_EQ(3, count_of(yaml, "  kind: binary\n")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("value: 8")) << yaml; // not folded
}

// The value of an assignment is the value stored, not a second read of r; both
// writes stay although the first is overwritten.
TEST_F(PipelineTest, VolatileAssignmentValueIsStored)
{
    std::string yaml = OptimizeYaml("int f(void) { volatile int r = 1; int y = (r = 3); return y; }");
    EXPECT_EQ(2, count_of(yaml, "volatile: true")) << yaml;
    EXPECT_EQ(0, count_of(yaml, "    name: %r\n  dst:")) << yaml; // r is never read
}

// Reads whose values are unused still happen.
TEST_F(PipelineTest, VolatileUnusedReadsKept)
{
    std::string yaml = OptimizeYaml("int f(void) { volatile int r = 1; r; r; return 0; }");
    EXPECT_EQ(3, count_of(yaml, "volatile: true")) << yaml;
    EXPECT_EQ(2, count_of(yaml, "    name: %r\n  dst:")) << yaml;
}
