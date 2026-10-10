//
// The wording of the type checker's diagnostics: the names and types they give,
// in the style of docs/Technical_Reference.md, "Diagnostics".
//
#include "typecheck_fixture.h"

// va_start (__va_start(&ap) on the targets that have it) in a function without '...'.
TEST_F(PipelineTest, VaStartInFixedArgumentFunction)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void __va_start(void *ap);
int f(int n)
{
    char *ap;
    __va_start(&ap);
    return n;
}
)SRC"),
                 "'va_start' used in a function with fixed arguments");
}

TEST_F(PipelineTest, VaStartInVariadicFunction)
{
    RunPipeline(R"SRC(
void __va_start(void *ap);
int f(int n, ...)
{
    char *ap;
    __va_start(&ap);
    return n;
}
)SRC");
}
