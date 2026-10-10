//
// What a deferred statement may not do, and the jumps that may not pass one
// (semantic/defer.c; docs/Coroutines_in_C.md, section 1).
//
#include "typecheck_fixture.h"

// --- allowed ---------------------------------------------------------------

// A loop inside a deferred statement may break and continue; a goto may leave
// blocks, jump within one, and jump back over a defer of its own block.
TEST_F(PipelineTest, DeferAllowedJumps)
{
    RunPipeline(R"(
void g(int);
void f(int x)
{
again:
    g(0);
    _Defer {
        for (int i = 0; i < x; i++) {
            if (i == 2) continue;
            if (i == 5) break;
            g(i);
        }
        goto inner;
    inner:
        g(1);
    }
    {
        _Defer g(2);
        if (x) goto out;
    }
    if (x > 1) goto again;
out:
    switch (x) {
    case 1: {
        _Defer g(3);
        break;
    }
    case 2:
        g(4);
    }
}
)");
}

// --- leaving a deferred statement ------------------------------------------

TEST_F(PipelineTest, DeferReturnInside_Neg)
{
    EXPECT_DEATH(RunPipeline("int f(void) { _Defer { return 1; } return 0; }"),
                 "'return' inside a deferred statement");
}

TEST_F(PipelineTest, DeferBreakLeaves_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { for (;;) { _Defer break; } }"),
                 "'break' statement not in a loop or switch");
}

TEST_F(PipelineTest, DeferContinueLeaves_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { for (;;) { _Defer { switch (1) { default: continue; } } } }"),
                 "'continue' statement not in a loop");
}

TEST_F(PipelineTest, DeferGotoOut_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { _Defer { goto out; } out: ; }"),
                 "'goto out' jumps into or out of a deferred statement");
}

TEST_F(PipelineTest, DeferGotoIn_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { goto in; _Defer { in: ; } }"),
                 "'goto in' jumps into or out of a deferred statement");
}

// --- jumping past a defer --------------------------------------------------

TEST_F(PipelineTest, DeferGotoForwardPast_Neg)
{
    EXPECT_DEATH(RunPipeline("void g(void); void f(void) { goto l; _Defer g(); l: g(); }"),
                 "'goto l' jumps forward past a defer");
}

TEST_F(PipelineTest, DeferGotoIntoBlockPast_Neg)
{
    EXPECT_DEATH(RunPipeline("void g(void); void f(void) { goto l; { _Defer g(); l: g(); } }"),
                 "'goto l' jumps into a block past a defer");
}

TEST_F(PipelineTest, DeferCasePast_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(
void g(void);
void f(int x) { switch (x) { case 1: _Defer g(); case 2: g(); } }
)"),
                 "'case' label past a defer or co_alloca in its switch");
}

TEST_F(PipelineTest, DeferDefaultPast_Neg)
{
    EXPECT_DEATH(RunPipeline("void g(void); void f(int x) { switch (x) { _Defer g(); default: g(); } }"),
                 "'default' label past a defer or co_alloca in its switch");
}

TEST_F(PipelineTest, DeferCaseInside_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(int x) { switch (x) { case 1: _Defer { case 2: ; } } }"),
                 "'case' label inside a deferred statement");
}
