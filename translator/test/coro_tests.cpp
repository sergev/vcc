//
// Lowering coroutines (translator/coro.c; backend/wasm/Plan.md §6): the provisional
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
    size_t at = yaml.find("name: range$resume");
    size_t pe = yaml.find("name: range$init");
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
)").c_str());
    for (const char *call : { "fun_name: __coro_setup", "fun_name: range$init",
                              "fun_name: __coro_resume", "fun_name: __coro_value",
                              "fun_name: __coro_done", "name: range$co" })
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
)").c_str());
    for (const char *call : { "fun_name: __builtin_stack_save", "fun_name: __builtin_alloca",
                              "fun_name: __builtin_stack_restore" })
        EXPECT_TRUE(Has(yaml, call)) << call << "\n" << yaml;
}

// Phase C4: await, and co_alloca inside a coroutine.
TEST_F(TranslateTestWasm32, AwaitNotYet)
{
    EXPECT_DEATH(CompileToYaml((std::string(range) +
                                "_Coro(int) void g(void) { _Await range(0, 1); }")
                                   .c_str()),
                 "coroutines: not yet: await");
}

TEST_F(TranslateTestWasm32, AllocaInCoroutineNotYet)
{
    EXPECT_DEATH(CompileToYaml((std::string(range) +
                                "_Coro(int) void g(void) { __co_alloca(range, 0, 0, 1); }")
                                   .c_str()),
                 "coroutines: not yet: co_alloca in a coroutine");
}

// A prototype alone lowers to nothing, and an ordinary function beside it is unchanged.
TEST_F(TranslateTestWasm32, CoroutinePrototype)
{
    std::string yaml = CompileToYaml("_Coro(int) void g(int); int f(int x) { return x + 1; }");
    EXPECT_NE(std::string::npos, yaml.find("name: f")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("name: g")) << yaml;
}
