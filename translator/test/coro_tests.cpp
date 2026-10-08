//
// Lowering coroutines: phase C3 (backend/wasm/Plan.md §8).  Until then the translator
// stops at a coroutine, or at any coroutine operation, so nothing links.
//
#include "translate_test.h"

TEST_F(TranslateTestWasm32, CoroutineNotYet)
{
    EXPECT_DEATH(CompileToYaml("_Coro(int) void f(void) { _Yield 1; }"), "coroutines: not yet");
}

TEST_F(TranslateTestWasm32, CoroutineOperationNotYet)
{
    EXPECT_DEATH(CompileToYaml("int f(_Coro_frame(int, void) *p) { return __co_done(p); }"),
                 "coroutines: not yet");
}

// A prototype alone lowers to nothing, and an ordinary function beside it is unchanged.
TEST_F(TranslateTestWasm32, CoroutinePrototype)
{
    std::string yaml = CompileToYaml("_Coro(int) void g(int); int f(int x) { return x + 1; }");
    EXPECT_NE(std::string::npos, yaml.find("name: f")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("name: g")) << yaml;
}
