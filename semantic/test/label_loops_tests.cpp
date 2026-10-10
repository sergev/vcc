#include "typecheck_fixture.h"

static Stmt *fn_first_stmt(ExternalDecl *fn)
{
    return fn->u.function.body->u.compound->u.stmt;
}

static Stmt *compound_first(Stmt *s)
{
    return s->u.compound->u.stmt;
}

static Stmt *compound_second(Stmt *s)
{
    return s->u.compound->next->u.stmt;
}

// while(1){} gets end and continue labels.
TEST_F(LabelLoopsTest, WhileLoopLabels)
{
    RunLabelLoops("int f(void) { while (1) {} }");

    Stmt *ws = fn_first_stmt(program->decls);
    ASSERT_EQ(ws->kind, STMT_WHILE);
    EXPECT_STREQ(ws->loop_end_label, "%L0");
    EXPECT_STREQ(ws->loop_continue_label, "%L1");
}

// for(;;){} gets end and continue labels.
TEST_F(LabelLoopsTest, ForLoopLabels)
{
    RunLabelLoops("int f(void) { for (;;) {} }");

    Stmt *fs = fn_first_stmt(program->decls);
    ASSERT_EQ(fs->kind, STMT_FOR);
    EXPECT_STREQ(fs->loop_end_label, "%L0");
    EXPECT_STREQ(fs->loop_continue_label, "%L1");
}

// do{}while(1); gets end and continue labels.
TEST_F(LabelLoopsTest, DoWhileLoopLabels)
{
    RunLabelLoops("int f(void) { do {} while (1); }");

    Stmt *ds = fn_first_stmt(program->decls);
    ASSERT_EQ(ds->kind, STMT_DO_WHILE);
    EXPECT_STREQ(ds->loop_end_label, "%L0");
    EXPECT_STREQ(ds->loop_continue_label, "%L1");
}

// switch gets only an end label; continue label stays NULL.
TEST_F(LabelLoopsTest, SwitchLabels)
{
    RunLabelLoops("int f(void) { switch (1) {} }");

    Stmt *ss = fn_first_stmt(program->decls);
    ASSERT_EQ(ss->kind, STMT_SWITCH);
    EXPECT_STREQ(ss->loop_end_label, "%L0");
    EXPECT_EQ(ss->loop_continue_label, nullptr);
}

// break inside while targets the loop's end label.
TEST_F(LabelLoopsTest, BreakInWhile)
{
    // `void` return: the loop's break exits to the end of the function, which
    // would otherwise be a missing return in a non-void function.
    RunLabelLoops("void f(void) { while (1) { break; } }");

    Stmt *ws  = fn_first_stmt(program->decls);
    Stmt *brk = compound_first(ws->u.while_stmt.body);
    ASSERT_EQ(brk->kind, STMT_BREAK);
    EXPECT_STREQ(brk->branch_target_label, ws->loop_end_label);
}

// continue inside while targets the loop's continue label.
TEST_F(LabelLoopsTest, ContinueInWhile)
{
    RunLabelLoops("int f(void) { while (1) { continue; } }");

    Stmt *ws   = fn_first_stmt(program->decls);
    Stmt *cont = compound_first(ws->u.while_stmt.body);
    ASSERT_EQ(cont->kind, STMT_CONTINUE);
    EXPECT_STREQ(cont->branch_target_label, ws->loop_continue_label);
}

// break inside a switch case targets the switch's end label.
TEST_F(LabelLoopsTest, BreakInSwitch)
{
    RunLabelLoops("int f(void) { switch (1) { case 1: break; } }");

    Stmt *sw = fn_first_stmt(program->decls);
    ASSERT_EQ(sw->kind, STMT_SWITCH);
    Stmt *cs = compound_first(sw->u.switch_stmt.body);
    ASSERT_EQ(cs->kind, STMT_CASE);
    Stmt *brk = cs->u.case_stmt.stmt;
    ASSERT_EQ(brk->kind, STMT_BREAK);
    EXPECT_STREQ(brk->branch_target_label, sw->loop_end_label);
}

// break inside an inner for (nested in a while) targets only the inner loop.
TEST_F(LabelLoopsTest, NestedLoopBreakInner)
{
    RunLabelLoops("int f(void) { while (1) { for (;;) { break; } } }");

    Stmt *ws = fn_first_stmt(program->decls);
    ASSERT_EQ(ws->kind, STMT_WHILE);
    Stmt *fs = compound_first(ws->u.while_stmt.body);
    ASSERT_EQ(fs->kind, STMT_FOR);
    Stmt *brk = compound_first(fs->u.for_stmt.body);
    ASSERT_EQ(brk->kind, STMT_BREAK);
    EXPECT_STREQ(brk->branch_target_label, fs->loop_end_label);
    EXPECT_STRNE(brk->branch_target_label, ws->loop_end_label);
}

// continue after an inner for loop targets the outer while's continue label.
TEST_F(LabelLoopsTest, NestedLoopContinueOuter)
{
    RunLabelLoops("int f(void) { while (1) { for (;;) {} continue; } }");

    Stmt *ws = fn_first_stmt(program->decls);
    ASSERT_EQ(ws->kind, STMT_WHILE);
    Stmt *cont = compound_second(ws->u.while_stmt.body);
    ASSERT_EQ(cont->kind, STMT_CONTINUE);
    EXPECT_STREQ(cont->branch_target_label, ws->loop_continue_label);
}

// continue in a switch (nested in a while) skips the switch and targets the loop.
TEST_F(LabelLoopsTest, ContinueInSwitchInsideLoop)
{
    RunLabelLoops("int f(void) { while (1) { switch (1) { case 1: continue; } } }");

    Stmt *ws = fn_first_stmt(program->decls);
    ASSERT_EQ(ws->kind, STMT_WHILE);
    Stmt *sw = compound_first(ws->u.while_stmt.body);
    ASSERT_EQ(sw->kind, STMT_SWITCH);
    Stmt *cs = compound_first(sw->u.switch_stmt.body);
    ASSERT_EQ(cs->kind, STMT_CASE);
    Stmt *cont = cs->u.case_stmt.stmt;
    ASSERT_EQ(cont->kind, STMT_CONTINUE);
    EXPECT_STREQ(cont->branch_target_label, ws->loop_continue_label);
}

// break outside any loop or switch is a fatal error.
TEST_F(LabelLoopsTest, BreakOutsideLoop)
{
    ASSERT_EXIT(RunLabelLoops("int f(void) { break; }"), ::testing::ExitedWithCode(1),
                "'break' statement not in a loop or switch");
}

// continue outside any loop is a fatal error.
TEST_F(LabelLoopsTest, ContinueOutsideLoop)
{
    ASSERT_EXIT(RunLabelLoops("int f(void) { continue; }"), ::testing::ExitedWithCode(1),
                "'continue' statement not in a loop");
}
