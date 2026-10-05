#include "optimizer_test_fixture.h"

// ---------------------------------------------------------------------------
// Jump folding tests
// ---------------------------------------------------------------------------

// JumpIfZero(ConstInt(0), "T")  →  Jump("T")
TEST_F(OptimizerTest, JumpFoldJIZZero)
{
    Tac_Instruction *body = make_jump_if_zero(make_const_int(0), "T");
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_JUMP);
    EXPECT_STREQ(body->u.jump.target, "T");
}

// JumpIfZero(ConstInt(1), "T")  →  deleted
TEST_F(OptimizerTest, JumpFoldJIZNonzero)
{
    Tac_Instruction *ret  = make_return(nullptr);
    Tac_Instruction *body = make_jump_if_zero(make_const_int(1), "T");
    body->next            = ret;
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_RETURN);
}

// JumpIfNotZero(ConstInt(1), "T")  →  Jump("T")
TEST_F(OptimizerTest, JumpFoldJINZNonzero)
{
    Tac_Instruction *body = make_jump_if_not_zero(make_const_int(1), "T");
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_JUMP);
    EXPECT_STREQ(body->u.jump.target, "T");
}

// JumpIfNotZero(ConstInt(0), "T")  →  deleted
TEST_F(OptimizerTest, JumpFoldJINZZero)
{
    Tac_Instruction *ret  = make_return(nullptr);
    Tac_Instruction *body = make_jump_if_not_zero(make_const_int(0), "T");
    body->next            = ret;
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_RETURN);
}

// JumpIfZero(ConstDouble(0.0), "T")  →  Jump("T")
TEST_F(OptimizerTest, JumpFoldJIZDoubleZero)
{
    Tac_Instruction *body = make_jump_if_zero(make_const_double(0.0), "T");
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_JUMP);
    EXPECT_STREQ(body->u.jump.target, "T");
}

// JumpIfZero(Var("x"), "T")  →  unchanged
TEST_F(OptimizerTest, JumpFoldJIZVarUnchanged)
{
    Tac_Instruction *body = make_jump_if_zero(make_var("x"), "T");
    body                  = constant_fold(body);

    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->kind, TAC_INSTRUCTION_JUMP_IF_ZERO);
    EXPECT_EQ(body->u.jump_if_zero.condition->kind, TAC_VAL_VAR);
}

// ---------------------------------------------------------------------------
// Unreachable code elimination tests
// ---------------------------------------------------------------------------

// Label("fn") → Return(Var("x")) → Return(NULL)  →  backstop Return(NULL) removed.
TEST_F(OptimizerTest, UnreachableBackstopReturn)
{
    Tac_Instruction *lbl  = make_label("fn");
    Tac_Instruction *ret1 = make_return(make_var("x"));
    Tac_Instruction *ret2 = make_return(nullptr);
    lbl->next             = ret1;
    ret1->next            = ret2;

    OptCfg *cfg = cfg_build(lbl);
    eliminate_unreachable(cfg);
    Tac_Instruction *result = cfg_flatten(cfg);
    cfg_free(cfg);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n");
}

// Label("fn") → Jump("End") → Return(Var("x")) → Label("End") → Return(NULL)
// The block containing Return(Var("x")) is unreachable.
TEST_F(OptimizerTest, UnreachableDeadBranch)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *jmp   = make_jump("End");
    Tac_Instruction *ret_x = make_return(make_var("x"));
    Tac_Instruction *lbl   = make_label("End");
    Tac_Instruction *ret0  = make_return(nullptr);
    entry->next            = jmp;
    jmp->next              = ret_x;
    ret_x->next            = lbl;
    lbl->next              = ret0;

    OptCfg *cfg = cfg_build(entry);
    eliminate_unreachable(cfg);
    Tac_Instruction *result = cfg_flatten(cfg);
    cfg_free(cfg);

    // Dead block gone; useless Jump("End") and unused Label("End") also cleaned up.
    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: return\n");
}

// Label("fn") → JumpIfZero(ConstInt(0), "Else") → Return(ConstInt(1)) → Label("Else") →
// Return(ConstInt(0)) constant_fold turns JIZ(0,"Else") into Jump("Else"); unreachable elim removes
// the dead then-block; post-cleanup removes the useless Jump and unused Label("Else").
TEST_F(OptimizerTest, UnreachableDeadElseBranch)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *jiz   = make_jump_if_zero(make_const_int(0), "Else");
    Tac_Instruction *ret1  = make_return(make_const_int(1));
    Tac_Instruction *lbl   = make_label("Else");
    Tac_Instruction *ret0  = make_return(make_const_int(0));
    entry->next            = jiz;
    jiz->next              = ret1;
    ret1->next             = lbl;
    lbl->next              = ret0;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// A void function may end in a loop's conditional jump: its trailing return is
// gone, so the fall-through edge leads to the Exit, not to a block past the end.
TEST_F(OptimizerTest, CondJumpEndsFunction)
{
    Tac_Instruction *body = chain({ make_label("top"), make_fun_call("bar"),
                                    make_jump_if_not_zero(make_var("x"), "top") });
    Tac_Instruction *result = optimize_function(body, opt_flags_default(), nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, TAC_INSTRUCTION_LABEL);
}
