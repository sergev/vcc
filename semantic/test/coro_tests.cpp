//
// The compile-time rules of coroutines (semantic/coroutines.c, and semantic/defer.c for
// the jumps past a co_alloca; docs/Coroutines_in_C.md, docs/Coroutines_Internals.md §3.2).
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

// --- allowed ----------------------------------------------------------------

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

// --- the target -------------------------------------------------------------

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

// --- declarations -----------------------------------------------------------

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

// --- naming a coroutine -----------------------------------------------------

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
                 "Coroutine 'range' may only be named in");
}

TEST_F(CoroTest, AllocaArguments_Neg)
{
    EXPECT_DEATH(RunPipeline(With("void f(void) { co_alloca(range, 0, 1); }").c_str()),
                 "wrong number of arguments");
}

// --- the operations ---------------------------------------------------------

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

// --- yield and await --------------------------------------------------------

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

// --- co_alloca in its block -------------------------------------------------

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

// --- Braam (wasm32-braam: the runtime awaits main) -----------------------------

namespace {
class BraamMainTest : public CoroTest {
protected:
    void SetUp() override
    {
        CoroTest::SetUp();
        target_config = target_lookup("wasm32-braam");
    }
};
} // namespace

static const char *const braam_call = "typedef struct braam_call braam_call;\n";

TEST_F(BraamMainTest, CoroutineMain)
{
    EXPECT_EQ(1, target_config->braam);
    RunPipeline((std::string(braam_call) +
                 "_Coro(braam_call *) int main(int argc, char **argv) { return argc; }")
                    .c_str());
}

TEST_F(BraamMainTest, PlainMain)
{
    EXPECT_DEATH(RunPipeline("int main(void) { return 0; }"),
                 "on Braam, main is coro\\(braam_call \\*\\) int main\\(int, char \\*\\*\\)");
}

TEST_F(BraamMainTest, OtherYieldType)
{
    EXPECT_DEATH(RunPipeline("_Coro(int) int main(int argc, char **argv) { return 0; }"),
                 "the yield type is not braam_call");
}

TEST_F(BraamMainTest, NoArguments)
{
    EXPECT_DEATH(RunPipeline((std::string(braam_call) +
                              "_Coro(braam_call *) int main(void) { return 0; }")
                                 .c_str()),
                 "the parameters are not");
}

TEST_F(BraamMainTest, VoidResult)
{
    EXPECT_DEATH(RunPipeline((std::string(braam_call) +
                              "_Coro(braam_call *) void main(int c, char **v) { }")
                                 .c_str()),
                 "it does not return int");
}

// --- coro_ptr ---------------------------------------------------------------

static const char *const tasks = R"(
#include <coro.h>
coro(int) int a(void *arg) { yield 1; return 0; }
coro(int) int b(void) { yield 2; return 1; }
)";

// A coroutine that takes (void) or (void *) converts to its coro_ptr: in a static table,
// an assignment, an argument, a comparison; the operations take the pointer.
TEST_F(CoroTest, CoroPtr)
{
    RunPipeline((std::string(tasks) + R"(
static coro_ptr(int, int) table[] = { a, b, 0 };
static int run(coro_ptr(int, int) p, void *arg)
{
    co_frame(int, int) *f = co_alloca(p, 0, arg);
    co_resume(f);
    return co_value(f) + (int)co_sizeof(p) + (int)co_alignof(p);
}
coro(int) int c(void *arg)
{
    coro_ptr(int, int) p = arg ? a : b;
    return await p(arg) + await table[1]();
}
int main(void)
{
    static char storage[256];
    coro_ptr(int, int) p = b;
    co_frame(int, int) *f = co_init(storage, sizeof storage, table[0]);
    return run(a, 0) + (p == table[1]) + (p != 0) + co_done(f);
}
)").c_str());
}

TEST_F(CoroTest, CoroPtrParams_Neg)
{
    EXPECT_DEATH(RunPipeline(With("coro_ptr(int, void) p = range;").c_str()),
                 "may only be named in co_init, co_alloca, co_sizeof, co_alignof or await, or "
                 "used as a coro_ptr when it takes \\(void\\) or \\(void \\*\\)");
}

TEST_F(CoroTest, CoroPtrTypes_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) + "coro_ptr(char, int) p = a;").c_str()),
                 "Incompatible types");
}

TEST_F(CoroTest, CoroPtrAssign_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) +
                              "void f(coro_ptr(int, int) p, co_frame(int, int) *q) { q = p; }")
                                 .c_str()),
                 "");
}

TEST_F(CoroTest, CoroPtrCall_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) +
                              "void f(coro_ptr(int, int) p) { p(0); }").c_str()),
                 "A coro_ptr can only be called by await");
}

TEST_F(CoroTest, CoroPtrArguments_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) +
                              "coro(int) int g(coro_ptr(int, int) p) { return await p(0, 0); }")
                                 .c_str()),
                 "at most one argument");
}

TEST_F(CoroTest, CoroPtrYield_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) +
                              "coro(char) int g(coro_ptr(int, int) p) { return await p(0); }")
                                 .c_str()),
                 "another yield type");
}

TEST_F(CoroTest, CoroPtrSizeof_Neg)
{
    EXPECT_DEATH(RunPipeline((std::string(tasks) +
                              "int f(int *p) { return (int)co_sizeof(p + 1); }").c_str()),
                 "needs the name of a coroutine or a coro_ptr");
}

// --- the lint for frames in automatic storage -------------------------------

static const char *const lint_gen = R"(
#include <coro.h>
coro(int) void gen(int n) { defer n = 0; for (int i = 0; i < n; i++) yield i; }
)";

// The warnings `body` makes, after the generator (in a test: RunPipeline is the
// fixture's).
#define Warnings(t, body)                                              \
    ([&]() {                                                           \
        testing::internal::CaptureStderr();                            \
        RunPipeline((std::string(lint_gen) + (body)).c_str());         \
        return testing::internal::GetCapturedStderr();                 \
    }())

TEST_F(CoroTest, LintEscape)
{
    EXPECT_EQ("warning: f: the frame of 'gen' outlives its storage 'buf', an automatic object: "
              "use static or allocated storage\n",
              Warnings(this, R"(
co_frame(int, void) *keep;
void f(void)
{
    char buf[256];
    keep = co_init(buf, sizeof buf, gen, 3);
}
)"));
}

TEST_F(CoroTest, LintReturned)
{
    EXPECT_NE(std::string::npos, Warnings(this, R"(
co_frame(int, void) *f(void)
{
    char buf[256];
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    return p;
}
)").find("warning: f: the frame of 'gen' outlives its storage 'buf'"));
}

TEST_F(CoroTest, LintLeftSuspended)
{
    EXPECT_EQ("warning: f: the frame of 'gen' in 'buf' may be left suspended at the end of the "
              "block, its defers never run: co_destroy it, or use co_alloca\n",
              Warnings(this, R"(
int f(void)
{
    char buf[256];
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    co_resume(p);
    return co_value(p);
}
)"));
}

// No warning: run to the end, destroyed, in static storage, awaited, or in an inner
// block whose frame variable is the outer block's but whose storage is the outer's too.
TEST_F(CoroTest, LintQuiet)
{
    EXPECT_EQ("", Warnings(this, R"(
int loop(void)
{
    char buf[256];
    int s = 0;
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    while (co_resume(p) == CO_SUSPENDED)
        s += co_value(p);
    return s;
}
int destroyed(void)
{
    char buf[256];
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    co_resume(p);
    int v = co_value(p);
    co_destroy(p);
    return v;
}
int kept(void)
{
    static char buf[256];
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    co_resume(p);
    return co_value(p);
}
coro(int) void awaited(void)
{
    char buf[256];
    co_frame(int, void) *p = co_init(buf, sizeof buf, gen, 3);
    await p;
}
int inner(void)
{
    char buf[256];
    co_frame(int, void) *p;
    {
        p = co_init(buf, sizeof buf, gen, 3);
    }
    while (!co_done(p))
        co_resume(p);
    return 0;
}
)"));
}

// A local pointer to memory from elsewhere is not automatic storage; &x of a local is.
TEST_F(CoroTest, LintPointerStorage)
{
    EXPECT_EQ("", Warnings(this, R"(
void *malloc(unsigned long);
co_frame(int, void) *keep;
void f(void)
{
    void *storage = malloc(256);
    keep = co_init(storage, 256, gen, 3);
}
)"));
}

TEST_F(CoroTest, LintAddressOf)
{
    EXPECT_NE(std::string::npos, Warnings(this, R"(
co_frame(int, void) *keep;
void f(void)
{
    struct { _Alignas(16) char b[256]; } s;
    keep = co_init(&s, sizeof s, gen, 3);
}
)").find("outlives its storage 's'"));
}
