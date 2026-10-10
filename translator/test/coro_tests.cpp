//
// Lowering coroutines (translator/coro.c; docs/Coroutines_Internals.md §5): the provisional
// function, split after the optimizer into f$resume, f$init and f$co.
//
#include "translate_test.h"

static const char *const range = R"(
_Coro(int) void range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        if (_Yield i == 1)
            return;
}
)";

static bool Has(const std::string &yaml, const std::string &what)
{
    return yaml.find(what) != std::string::npos;
}

// The toplevels a coroutine becomes, f$resume over the frame pointer alone.
TEST_F(TranslateTestWasm32, CoroutineSplit)
{
    std::string yaml = CompileToYaml(range);
    EXPECT_TRUE(Has(yaml, "name: range$resume")) << yaml;
    EXPECT_TRUE(Has(yaml, "name: range$init")) << yaml;
    EXPECT_TRUE(Has(yaml, "name: range$co")) << yaml;
    EXPECT_FALSE(Has(yaml, "__coro_suspend")) << yaml; // every suspension is split
    EXPECT_TRUE(Has(yaml, "name: %co.resume1")) << yaml;
    EXPECT_FALSE(Has(yaml, "name: %co.resume2")) << yaml;
    // range$resume's one parameter, the frame, and no other.
    size_t at        = yaml.find("name: range$resume");
    size_t pe        = yaml.find("name: range$init");
    std::string head = yaml.substr(at, pe - at);
    EXPECT_TRUE(Has(head, "name: %.fp")) << head;
    EXPECT_FALSE(Has(head, "name: %lo")) << head;
    // The frame: the header, the value, lo, hi and i.
    at = yaml.find("name: range$co");
    EXPECT_TRUE(Has(yaml.substr(at), "value: 40")) << yaml.substr(at);
}

// f$init stores the arguments; the operations call the runtime.
TEST_F(TranslateTestWasm32, CoroutineOperations)
{
    std::string yaml = CompileToYaml((std::string(range) + R"(
int f(void)
{
    char buf[64];
    _Coro_frame(int, void) *p = __co_init(buf, sizeof buf, range, 1, 2);
    int n = 0;
    while (__co_resume(p) == 0)
        n += __co_value(p);
    return n + __co_done(p) + (int)__co_sizeof(range);
}
)")
                                         .c_str());
    for (const char *call :
         { "fun_name: __coro_setup", "fun_name: range$init", "fun_name: __coro_resume",
           "fun_name: __coro_value", "fun_name: __coro_done", "name: range$co" })
        EXPECT_TRUE(Has(yaml, call)) << call << "\n" << yaml;
}

// co_alloca in a function: the shadow stack saved, the frame allocated, and released at
// the end of the block.
TEST_F(TranslateTestWasm32, CoroutineAlloca)
{
    std::string yaml = CompileToYaml((std::string(range) + R"(
void f(void)
{
    _Coro_frame(int, void) *p = __co_alloca(range, 0, 1, 2);
    __co_resume(p);
}
)")
                                         .c_str());
    for (const char *call : { "fun_name: __builtin_stack_save", "fun_name: __builtin_alloca",
                              "fun_name: __builtin_stack_restore" })
        EXPECT_TRUE(Has(yaml, call)) << call << "\n" << yaml;
}

// An arena await: the sub-coroutine's frame pushed on the task's arena and set up in
// the awaiter's task, resumed until done by a direct call of range$resume (not through
// the runtime's __coro_resume), then popped; one suspension of its own.
TEST_F(TranslateTestWasm32, CoroutineAwait)
{
    std::string yaml = CompileToYaml(
        (std::string(range) + "_Coro(int) void g(void) { _Await range(0, 1); }").c_str());
    for (const char *call :
         { "fun_name: __coro_push", "fun_name: __coro_setup", "fun_name: range$init",
           "fun_name: range$resume", "fun_name: __coro_pop", "name: %co.resume1" })
        EXPECT_TRUE(Has(yaml, call)) << call << "\n" << yaml;
    size_t at = yaml.find("name: g$resume");
    ASSERT_NE(std::string::npos, at) << yaml;
    EXPECT_FALSE(Has(yaml.substr(at), "name: %co.resume2")) << yaml.substr(at);
    EXPECT_FALSE(Has(yaml.substr(at), "fun_name: __coro_resume")) << yaml.substr(at);
}

// An await of a frame pointer does not know the coroutine: it resumes through the
// runtime.
TEST_F(TranslateTestWasm32, CoroutineAwaitFrame)
{
    std::string yaml = CompileToYaml(
        (std::string(range) + "_Coro(int) void g(_Coro_frame(int, void) *p) { _Await p; }")
            .c_str());
    size_t at = yaml.find("name: g$resume");
    ASSERT_NE(std::string::npos, at) << yaml;
    EXPECT_TRUE(Has(yaml.substr(at), "fun_name: __coro_resume")) << yaml.substr(at);
}

// co_alloca in a coroutine takes the arena, not the shadow stack.
TEST_F(TranslateTestWasm32, CoroutineAllocaInCoroutine)
{
    std::string yaml = CompileToYaml(
        (std::string(range) + "_Coro(int) void g(void) { __co_alloca(range, 0, 0, 1); }").c_str());
    size_t at = yaml.find("name: g$resume");
    ASSERT_NE(std::string::npos, at) << yaml;
    std::string g = yaml.substr(at);
    EXPECT_TRUE(Has(g, "fun_name: __coro_push")) << g;
    EXPECT_TRUE(Has(g, "fun_name: __coro_pop")) << g;
    EXPECT_FALSE(Has(g, "__builtin_stack_save")) << g;
}

// A prototype alone lowers to nothing, and an ordinary function beside it is unchanged.
TEST_F(TranslateTestWasm32, CoroutinePrototype)
{
    std::string yaml = CompileToYaml("_Coro(int) void g(int); int f(int x) { return x + 1; }");
    EXPECT_NE(std::string::npos, yaml.find("name: f")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("name: g")) << yaml;
}

// The dispatch: two suspension points are a chain of compares, three a jump table on
// the state, whose entry 0 and default are the body's start.
TEST_F(TranslateTestWasm32, CoroutineDispatchChain)
{
    std::string two = CompileToYaml("_Coro(int) void g(void) { _Yield 1; _Yield 2; }");
    EXPECT_FALSE(Has(two, "kind: jump_table")) << two;
}

TEST_F(TranslateTestWasm32, CoroutineDispatchTable)
{
    std::string three = CompileToYaml("_Coro(int) void h(void) { _Yield 1; _Yield 2; _Yield 3; }");
    for (const char *s :
         { "kind: jump_table", "- %co.start", "- %co.resume3", "default: %co.start" })
        EXPECT_TRUE(Has(three, s)) << s << "\n" << three;
}

// Elsewhere the backend has no jump table, and the dispatch is a chain of compares
// however many points there are.  The labels carry the coroutine's name, since two
// coroutines of a unit share the assembler's namespace.
TEST_F(TranslateTestX86, CoroutineDispatchChainOnly)
{
    std::string yaml = CompileToYaml(
        "_Coro(int) void h(void) { _Yield 1; _Yield 2; _Yield 3; }\n"
        "_Coro(int) void k(void) { _Yield 1; }");
    EXPECT_FALSE(Has(yaml, "kind: jump_table")) << yaml;
    for (const char *s : { "name: %co.resume3.h", "name: %co.resume1.k" })
        EXPECT_TRUE(Has(yaml, s)) << s << "\n" << yaml;
}

// The frame header follows the target: 2 unsigned and 4 pointers, 40 bytes on LP64,
// so the value is read 40 bytes into the frame.
TEST_F(TranslateTestX86, CoroutineHeaderLp64)
{
    std::string yaml = CompileToYaml("int r(_Coro_frame(int, void) *f) { return __co_value(f); }");
    size_t at        = yaml.find("fun_name: __coro_value");
    ASSERT_NE(std::string::npos, at) << yaml;
    EXPECT_TRUE(Has(yaml.substr(at, 400), "value: 40")) << yaml.substr(at, 400);
}

// alloca: every target has it on its stack, so the TAC holds the builtin alone.
static int Count(const std::string &yaml, const std::string &what)
{
    int n = 0;
    for (size_t at = yaml.find(what); at != std::string::npos; at = yaml.find(what, at + 1))
        n++;
    return n;
}

static const char *const alloca_fn = R"(
void *__builtin_alloca(unsigned long);
int g(int);
void f(int n)
{
    char *p = __builtin_alloca(n);
    p[0] = 3;
    if (n > 10) {
        g(p[0]);
        return;
    }
    if (n > 5) {
        g(n);
        return;
    }
    g(0);
}
)";

// Where the backend has the builtins, the epilogue gives the memory back.
static void ExpectOnStack(const std::string &yaml)
{
    EXPECT_EQ(1, Count(yaml, "fun_name: __builtin_alloca")) << yaml;
    EXPECT_FALSE(Has(yaml, "stack_save")) << yaml;
    EXPECT_FALSE(Has(yaml, "stack_restore")) << yaml;
    EXPECT_FALSE(Has(yaml, "__coro_alloca")) << yaml;
}

TEST_F(TranslateTestWasm32, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTestX86, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTestRiscv, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTestAvr, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTestMsp430, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTestMmix, AllocaOnStack)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}

TEST_F(TranslateTest, AllocaOnStackBesm6)
{
    ExpectOnStack(CompileToYaml(alloca_fn));
}
