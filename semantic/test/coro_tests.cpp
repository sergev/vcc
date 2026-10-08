//
// The compile-time rules of coroutines (semantic/coroutines.c, and semantic/defer.c for
// the jumps past a co_alloca; docs/Coroutines_in_C.md, backend/wasm/Plan.md §2.4).
//
#include "target.h"
#include "typecheck_fixture.h"

namespace {
// Coroutines exist on wasm32 only.
class CoroTest : public PipelineTest {
    const Target *saved = nullptr;

protected:
    void SetUp() override
    {
        PipelineTest::SetUp();
        saved         = target_config;
        target_config = target_lookup("wasm32");
    }
    void TearDown() override
    {
        target_config = saved;
        PipelineTest::TearDown();
    }
};
} // namespace

// The examples of the tutorial, with the short names of <coro.h>.
static const char *const range = R"(
#include <coro.h>
coro(int) void range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        if (yield i == CO_CANCEL)
            return;
}
)";

static std::string With(const char *body)
{
    return std::string(range) + body;
}

// --- allowed ---------------------------------------------------------------

TEST_F(CoroTest, Generator)
{
    RunPipeline(With(R"(
int print_range(void)
{
    int sum = 0;
    co_frame(int, void) *f = co_alloca(range, 0, 0, 10);
    while (co_resume(f) == CO_SUSPENDED)
        sum += co_value(f);
    return sum + co_done(f) + (int)co_sizeof(range) + (int)co_alignof(range);
}
)").c_str());
}

// Both forms of await; the result of a coroutine; a frame type through a typedef, and
// its Y through another; a prototype and its definition.
TEST_F(CoroTest, Delegation)
{
    RunPipeline(R"(
#include <coro.h>
typedef struct { int fd; char *buf; long len; long out; } io_req;
typedef io_req *req_ptr;
coro(io_req *) long read_exact(int fd, char *p, long n);
coro(req_ptr) long read_exact(int fd, char *p, long n)
{
    long got = 0;
    while (got < n) {
        io_req r = { fd, p + got, n - got, 0 };
        if ((yield &r) == CO_CANCEL)
            return -1;
        if (r.out <= 0)
            return got;
        got += r.out;
    }
    return got;
}
typedef co_frame(io_req *, long) reader;
coro(io_req *) int read_header(int fd, char *h)
{
    long n = await read_exact(fd, h, 16) + 1;
    char buf[256];
    reader *sub = co_init(buf, sizeof buf, read_exact, fd, h, 16);
    n += await sub;
    n += await co_alloca(read_exact, 0, fd, h, 4);
    return n == 17 ? 0 : -1;
}
void drive(reader *r)
{
    if (!co_done(r))
        co_destroy(r);
    co_cancel(r);
    long v = co_result(r);
    (void)v;
}
)");
}

// A coroutine of void: bare yield, co_frame(void, T), defer in a coroutine, yield in
// the head of a loop, a co_alloca in the head of an if.
TEST_F(CoroTest, VoidYield)
{
    RunPipeline(R"(
#include <coro.h>
void g(int);
coro(void) int ticks(int n)
{
    defer g(0);
    int i = 0;
    while (yield != CO_CANCEL && i < n)
        i++;
    return i;
}
int run(void)
{
    if (co_resume(co_alloca(ticks, 0, 3)) == CO_DONE)
        return 1;
    co_frame(void, int) *t = co_alloca(ticks, 0, 2);
    while (co_resume(t) == CO_SUSPENDED)
        ;
    return co_result(t);
}
)");
}

// --- the target ------------------------------------------------------------

TEST_F(PipelineTest, CoroutinesNotOnTarget_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f(void) { _Yield 1; }"),
                 "coroutines are not supported on target");
}

TEST_F(PipelineTest, CoOpNotOnTarget_Neg)
{
    EXPECT_DEATH(RunPipeline("int f(_Coro_frame(int, void) *p) { return __co_done(p); }"),
                 "coroutines are not supported on target");
}

// --- declarations ----------------------------------------------------------

TEST_F(CoroTest, Variadic_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f(int n, ...);"), "cannot be variadic");
}

TEST_F(CoroTest, Inline_Neg)
{
    EXPECT_DEATH(RunPipeline("inline _Coro(int) void f(void) { _Yield 1; }"),
                 "cannot be inline");
}

TEST_F(CoroTest, Noreturn_Neg)
{
    EXPECT_DEATH(RunPipeline("_Noreturn _Coro(int) void f(void);"), "cannot be _Noreturn");
}

TEST_F(CoroTest, NoPrototype_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f();"), "needs a prototype");
}

TEST_F(CoroTest, Main_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) int main(void) { return 0; }"),
                 "main cannot be a coroutine");
}

TEST_F(CoroTest, YieldArray_Neg)
{
    EXPECT_DEATH(RunPipeline("typedef int A[2]; _Coro(A) void f(void);"),
                 "yield type of a coroutine cannot be an array");
}

TEST_F(CoroTest, FrameArray_Neg)
{
    EXPECT_DEATH(RunPipeline("typedef int A[2]; _Coro_frame(A, void) *p;"),
                 "yield type of a co_frame cannot be an array");
}

TEST_F(CoroTest, OnVariable_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) int x;"), "_Coro on 'x', which is not a function");
}

TEST_F(CoroTest, OnFunctionPointer_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void) { _Coro(int) void (*p)(void); }"),
                 "_Coro on 'p', which is not a function");
}

TEST_F(CoroTest, TwoCoro_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) _Coro(int) void f(void);"), "More than one _Coro");
}

TEST_F(CoroTest, ConflictingYield_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f(void); _Coro(long) void f(void) { _Yield 1; }"),
                 "Conflicting declarations for function f");
}

TEST_F(CoroTest, CoroAndFunction_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(void); _Coro(int) void f(void);"),
                 "Conflicting declarations for function f");
}

// --- naming a coroutine ----------------------------------------------------

TEST_F(CoroTest, DirectCall_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { range(0, 1); }").c_str()),
                 "Coroutine 'range' may only be named in");
}

TEST_F(CoroTest, AsValue_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void *f(void) { return (void *)range; }").c_str()),
                 "Coroutine 'range' may only be named in");
}

TEST_F(CoroTest, AddressOf_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { void *p = &range; }").c_str()),
                 "Coroutine 'range' may only be named in");
}

TEST_F(CoroTest, InitNotCoroutine_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(
void g(int);
void f(void) { char buf[64]; __co_init(buf, sizeof buf, g, 1); }
)"),
                 "co_init: 'g' is not a coroutine");
}

TEST_F(CoroTest, SizeofExpression_Neg)
{
    EXPECT_DEATH(RunPipeline(With("unsigned long f(void) { return co_sizeof(range + 1); }").c_str()),
                 "co_sizeof needs the name of a coroutine");
}

TEST_F(CoroTest, AllocaArguments_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { co_alloca(range, 0, 1); }").c_str()),
                 "wrong number of arguments");
}

// --- the operations --------------------------------------------------------

TEST_F(CoroTest, ResumeNotFrame_Neg)
{
    EXPECT_DEATH(RunPipeline("int f(char *p) { return __co_resume(p); }"),
                 "co_resume needs a co_frame pointer");
}

TEST_F(CoroTest, ValueOfVoid_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(_Coro_frame(void, int) *p) { __co_value(p); }"),
                 "co_value of a coroutine that yields void");
}

TEST_F(CoroTest, ResultOfVoid_Neg)
{
    EXPECT_DEATH(RunPipeline("void f(_Coro_frame(int, void) *p) { __co_result(p); }"),
                 "co_result of a coroutine that returns void");
}

TEST_F(CoroTest, FrameTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { co_frame(long, void) *p = co_alloca(range, 0, 0, 1); }").c_str()),
                 "Cannot convert type for assignment");
}

// --- yield and await -------------------------------------------------------

TEST_F(CoroTest, YieldOutside_Neg)
{
    EXPECT_DEATH(RunPipeline("int f(void) { return _Yield 1; }"), "yield outside a coroutine");
}

TEST_F(CoroTest, AwaitOutside_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { _Await range(0, 1); }").c_str()),
                 "await outside a coroutine");
}

TEST_F(CoroTest, YieldInDefer_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f(void) { _Defer _Yield 1; }"),
                 "yield inside a deferred statement");
}

TEST_F(CoroTest, AwaitInDefer_Neg)
{
    EXPECT_DEATH(RunPipeline(With("coro(int) void f(void) { defer await range(0, 1); }").c_str()),
                 "await inside a deferred statement");
}

TEST_F(CoroTest, YieldValueInVoid_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(void) void f(void) { _Yield 1; }"),
                 "yield with a value in a coroutine that yields void");
}

TEST_F(CoroTest, BareYield_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) void f(void) { _Yield; }"),
                 "yield without a value in a coroutine that yields a value");
}

TEST_F(CoroTest, AwaitOtherYield_Neg)
{
    EXPECT_DEATH(RunPipeline(With("coro(long) void f(void) { await range(0, 1); }").c_str()),
                 "await of a coroutine with another yield type");
}

TEST_F(CoroTest, AwaitFrameOtherYield_Neg)
{
    EXPECT_DEATH(RunPipeline("_Coro(void) void f(_Coro_frame(int, void) *p) { _Await p; }"),
                 "await of a coroutine with another yield type");
}

TEST_F(CoroTest, AwaitFunction_Neg)
{
    EXPECT_DEATH(RunPipeline("int g(void); _Coro(int) void f(void) { _Await g(); }"),
                 "await needs a call of a coroutine or a co_frame pointer");
}

// --- co_alloca in its block ------------------------------------------------

TEST_F(CoroTest, AllocaInLoopHead_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { while (co_resume(co_alloca(range, 0, 0, 1))) ; }").c_str()),
                 "co_alloca in the head of a loop");
}

TEST_F(CoroTest, GotoPastAlloca_Neg)
{
    EXPECT_DEATH(RunPipeline(With(R"(
void f(int x)
{
    if (x)
        goto l;
    co_frame(int, void) *p = co_alloca(range, 0, 0, 1);
l:
    x++;
}
)").c_str()),
                 "goto l jumps forward past a defer or co_alloca");
}

TEST_F(CoroTest, GotoIntoIfPastAlloca_Neg)
{
    EXPECT_DEATH(RunPipeline(With(R"(
void f(int x)
{
    goto l;
    if (co_resume(co_alloca(range, 0, 0, 1))) {
    l:
        x++;
    }
}
)").c_str()),
                 "goto l jumps forward past a defer or co_alloca");
}

TEST_F(CoroTest, CasePastAlloca_Neg)
{
    EXPECT_DEATH(RunPipeline(With(R"(
void f(int x)
{
    switch (x) {
    case 1:
        co_resume(co_alloca(range, 0, 0, 1));
    case 2:
        x++;
    }
}
)").c_str()),
                 "'case' label past a defer or co_alloca in its switch");
}

// A goto back over a co_alloca of its own block is allowed, as over a defer.
TEST_F(CoroTest, GotoBackOverAlloca)
{
    RunPipeline(With(R"(
void f(int x)
{
again:
    co_resume(co_alloca(range, 0, 0, 1));
    if (x--)
        goto again;
}
)").c_str());
}
