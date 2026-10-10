//
// Control flow and liveness over TAC (backend/common/flow.c).
//
#include <cstdarg>
#include <cstdlib>

#include "backend_test.h"

extern "C" {
#include "flow.h"
}

// The libraries call fatal_error(); defined once for the backend-tests binary.
extern "C" void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

class FlowTest : public BackendTest {
protected:
    Tac_TopLevel *all{};
    Flow *flow{};

    FlowTest() : BackendTest("riscv64") {}

    void TearDown() override
    {
        if (flow)
            flow_free(flow);
        if (all)
            tac_free_toplevel(all);
        BackendTest::TearDown();
    }

    // Unoptimized TAC of `src`, and the flow of its function `name`.
    void Build(const char *src, const char *name = "f")
    {
        DisableOptimization();
        all = CompileToTac(src);
        for (const Tac_TopLevel *t = all; t; t = t->next)
            if (t->kind == TAC_TOPLEVEL_FUNCTION && strcmp(t->u.function.name, name) == 0)
                flow = flow_build(t);
        ASSERT_NE(nullptr, flow);
    }

    bool Has(const Flow_Set *s, const char *name)
    {
        int v = flow_var(flow, name);
        EXPECT_GE(v, 0) << name;
        return v >= 0 && flow_has(s, v);
    }

    // The block ending in a RETURN of a constant.
    const Flow_Block *ConstantReturn()
    {
        for (int b = 0; b < flow->nblocks; b++) {
            const Tac_Instruction *in = flow->instrs[flow->blocks[b].last];
            if (in->kind == TAC_INSTRUCTION_RETURN && in->u.return_.src &&
                in->u.return_.src->kind == TAC_VAL_CONSTANT)
                return &flow->blocks[b];
        }
        return nullptr;
    }
};

TEST_F(FlowTest, StraightLine)
{
    Build("int f(int a, int b) { int c = a + b; return c * a; }");
    EXPECT_EQ(1, flow->nblocks);
    const Flow_Block *entry = &flow->blocks[0];
    EXPECT_EQ(0, entry->nsucc);
    EXPECT_TRUE(Has(entry->live_in, "%a"));
    EXPECT_TRUE(Has(entry->live_in, "%b"));
    EXPECT_FALSE(Has(entry->live_in, "%c"));
    EXPECT_TRUE(Has(entry->def, "%c"));
    for (int v = 0; v < flow->nvars; v++)
        EXPECT_FALSE(flow_has(entry->live_out, v)) << flow->names[v];

    // Stepping back over the body from the exit gives the entry set.
    Flow_Set *live = flow_set_new(flow);
    for (int i = entry->last; i >= entry->first; i--)
        flow_step(flow, flow->instrs[i], live);
    for (int v = 0; v < flow->nvars; v++)
        EXPECT_EQ(flow_has(entry->live_in, v), flow_has(live, v)) << flow->names[v];
    xfree(live);
}

TEST_F(FlowTest, Branch)
{
    Build("int f(int a, int b) { if (a) return b; return 0; }");
    EXPECT_GT(flow->nblocks, 1);
    EXPECT_TRUE(Has(flow->blocks[0].live_in, "%a"));
    EXPECT_TRUE(Has(flow->blocks[0].live_in, "%b"));
    const Flow_Block *ret0 = ConstantReturn();
    ASSERT_NE(nullptr, ret0);
    EXPECT_FALSE(Has(ret0->live_in, "%b"));
}

TEST_F(FlowTest, Loop)
{
    Build("int f(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }");
    // Some block jumps back, and everything the loop needs is live into its target.
    int header = -1;
    for (int b = 0; b < flow->nblocks; b++)
        for (int k = 0; k < flow->blocks[b].nsucc; k++)
            if (flow->blocks[b].succ[k] <= b)
                header = flow->blocks[b].succ[k];
    ASSERT_GE(header, 0);
    EXPECT_TRUE(Has(flow->blocks[header].live_in, "%n"));
    EXPECT_TRUE(Has(flow->blocks[header].live_in, "%s"));
    EXPECT_TRUE(Has(flow->blocks[header].live_in, "%i"));
    EXPECT_FALSE(Has(flow->blocks[0].live_in, "%s"));
    EXPECT_FALSE(Has(flow->blocks[0].live_in, "%i"));
}

TEST_F(FlowTest, AddressTaken)
{
    Build("int f(int a) { int x = a; int *p = &x; struct { int m; } s = { 1 }; return *p + s.m; }");
    EXPECT_TRUE(Has(flow->in_memory, "%x"));
    EXPECT_FALSE(Has(flow->in_memory, "%a"));
    EXPECT_FALSE(Has(flow->in_memory, "%p"));
}

// A member store keeps the aggregate live: the other members survive it.
TEST_F(FlowTest, MemberStoreIsUse)
{
    Build(
        "struct S { int a, b; };\n"
        "int f(int x) { struct S s; s.a = 1; s.b = x; return s.a; }");
    EXPECT_TRUE(Has(flow->blocks[0].live_in, "%s"));
}

// Code after a call to a _Noreturn function starts a new block, not reached by fall-through.
TEST_F(FlowTest, NoreturnEndsBlock)
{
    Build(
        "_Noreturn void exit(int);\n"
        "int f(int a) { if (a) exit(a); return 0; }");
    for (int b = 0; b < flow->nblocks; b++) {
        const Flow_Block *blk = &flow->blocks[b];
        if (flow->instrs[blk->last]->kind == TAC_INSTRUCTION_FUN_CALL_NORETURN) {
            EXPECT_EQ(0, blk->nsucc);
            return;
        }
    }
    ADD_FAILURE() << "no block ends in a _Noreturn call";
}
