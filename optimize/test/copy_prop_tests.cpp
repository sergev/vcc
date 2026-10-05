#include "optimizer_test_fixture.h"

// ---------------------------------------------------------------------------
// Copy propagation tests
// ---------------------------------------------------------------------------

// Label("fn") → Copy(ConstInt(3), t.0) → Return(Var("t.0"))
// copy_prop substitutes t.0 → ConstInt(3) at the Return; the Copy survives
// (dead-store elimination is off).
TEST_F(OptimizerTest, CopyPropBasicConst)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = copy;
    copy->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n");
}

// Label("fn") → Copy(4,t.0) → JIZ(flag,"Else") → Copy(3,t.0) → Label("Else") → Return(t.0)
// The two incoming copies for t.0 at "Else" disagree (4 vs 3), so Return is NOT rewritten.
TEST_F(OptimizerTest, CopyPropCrossBranchNotPropagated)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp4   = make_copy(make_const_int(4), make_var("t.0"));
    Tac_Instruction *jiz   = make_jump_if_zero(make_var("flag"), "Else");
    Tac_Instruction *cp3   = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *lbl   = make_label("Else");
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = cp4;
    cp4->next              = jiz;
    jiz->next              = cp3;
    cp3->next              = lbl;
    lbl->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 4\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: jump_if_zero\n"
              "  condition:\n"
              "    kind: var\n"
              "    name: flag\n"
              "  target: Else\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: label\n"
              "  name: Else\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Label("fn") → Copy(5, g) → FunCall("bar") → Return(g)
// g is declared as a static variable; FunCall kills its copy.  Return(g) must NOT be rewritten.
TEST_F(OptimizerTest, CopyPropFunCallKillsStaticCopy)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(5), make_var("g"));
    Tac_Instruction *call  = make_fun_call("bar");
    Tac_Instruction *ret   = make_return(make_var("g"));
    entry->next            = copy;
    copy->next             = call;
    call->next             = ret;

    // No locals → "g" is observable; the call kills the (g→5) copy so the
    // Return(g) is not rewritten to Return(5).
    const Tac_TopLevel *tl = make_fn_tl({});

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, tl);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: bar\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: g\n");
}

// Label("fn") → Copy(Var("t.0"), Var("t.0")) → Return(ConstInt(0))
// The self-copy is detected and removed; Return is unchanged.
TEST_F(OptimizerTest, CopyPropSelfCopyRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *self  = make_copy(make_var("t.0"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = self;
    self->next             = ret;

    OptFlags flags          = opt_flags_default();
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

// ---------------------------------------------------------------------------
// Copy propagation — extended coverage
// ---------------------------------------------------------------------------

// Copy(Var("a"), Var("b")) → Return(Var("b"))
// Variable-to-variable copy: Return substitutes b → a.
TEST_F(OptimizerTest, CopyPropVarToVar)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_var("a"), make_var("b"));
    Tac_Instruction *ret   = make_return(make_var("b"));
    entry->next            = copy;
    copy->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: var\n"
              "    name: a\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: b\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: a\n");
}

// Copy(3, x) → Copy(x, y) → Return(y)
// The second Copy's src is substituted before apply_transfer records it,
// so {y → 3} is generated in one pass; Return(y) then becomes Return(3).
TEST_F(OptimizerTest, CopyPropChain)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp_x  = make_copy(make_const_int(3), make_var("x"));
    Tac_Instruction *cp_y  = make_copy(make_var("x"), make_var("y"));
    Tac_Instruction *ret   = make_return(make_var("y"));
    entry->next            = cp_x;
    cp_x->next             = cp_y;
    cp_y->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: y\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n");
}

// Copy(3, t.0) → Unary(NEGATE, x, t.0) → Return(t.0)
// The Unary redefines t.0; apply_transfer kills {t.0→3}; Return is not substituted.
TEST_F(OptimizerTest, CopyPropKilledByRedefine)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *unary = make_unary(TAC_UNARY_NEGATE, make_var("x"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = copy;
    copy->next             = unary;
    unary->next            = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: unary\n"
              "  op: negate\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Copy(a, b) → Copy(c, a) → Return(b)
// Redefining "a" also kills {b→a} because its src-var is "a".
// Return(b) is NOT substituted — prevents the unsound propagation Return(a).
TEST_F(OptimizerTest, CopyPropSrcVarKilledOnSrcRedefine)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp1   = make_copy(make_var("a"), make_var("b"));
    Tac_Instruction *cp2   = make_copy(make_var("c"), make_var("a"));
    Tac_Instruction *ret   = make_return(make_var("b"));
    entry->next            = cp1;
    cp1->next              = cp2;
    cp2->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: var\n"
              "    name: a\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: b\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: var\n"
              "    name: c\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: a\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: b\n");
}

// Copy(y, x) → GetAddress(x, ptr) → Return(ptr)
// subst_instruction intentionally skips the src of GET_ADDRESS: replacing &x
// with &y would be semantically wrong.  GetAddress src stays Var("x").
TEST_F(OptimizerTest, CopyPropGetAddrSrcNotSubstituted)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_var("y"), make_var("x"));
    Tac_Instruction *ga    = make_get_address(make_var("x"), make_var("ptr"));
    Tac_Instruction *ret   = make_return(make_var("ptr"));
    entry->next            = copy;
    copy->next             = ga;
    ga->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: var\n"
              "    name: y\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: get_address\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: ptr\n");
}

// GetAddress(x, ptr) → Copy(5, x) → Store(val, ptr) → Return(x)
// Pre-analysis marks x as address-taken; apply_transfer for Store kills {x→5}.
// Return(x) is not substituted.
TEST_F(OptimizerTest, CopyPropAddressTakenKilledByStore)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ga    = make_get_address(make_var("x"), make_var("ptr"));
    Tac_Instruction *copy  = make_copy(make_const_int(5), make_var("x"));
    Tac_Instruction *store = make_store(make_var("val"), make_var("ptr"));
    Tac_Instruction *ret   = make_return(make_var("x"));
    entry->next            = ga;
    ga->next               = copy;
    copy->next             = store;
    store->next            = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: get_address\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: store\n"
              "  src:\n"
              "    kind: var\n"
              "    name: val\n"
              "  dst_ptr:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n");
}

// GetAddress(x, ptr) → Copy(5, x) → FunCall("bar") → Return(x)
// x is address-taken; apply_transfer for FunCall kills copies of address-taken vars.
// Return(x) is not substituted.
TEST_F(OptimizerTest, CopyPropFunCallKillsAddressTaken)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ga    = make_get_address(make_var("x"), make_var("ptr"));
    Tac_Instruction *copy  = make_copy(make_const_int(5), make_var("x"));
    Tac_Instruction *call  = make_fun_call("bar");
    Tac_Instruction *ret   = make_return(make_var("x"));
    entry->next            = ga;
    ga->next               = copy;
    copy->next             = call;
    call->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: get_address\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: bar\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: x\n");
}

// Copy(g, x) → Store(5, p) → Return(x), x and p private, g a global.
// p may point at g (its address taken in some other function), so the store
// kills the (x → g) copy: Return(x) must not become Return(g).
TEST_F(OptimizerTest, CopyPropStoreKillsStaticCopy)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_var("g"), make_var("x"));
    Tac_Instruction *store = make_store(make_const_int(5), make_var("p"));
    Tac_Instruction *ret   = make_return(make_var("x"));
    entry->next            = copy;
    copy->next             = store;
    store->next            = ret;

    const Tac_TopLevel *tl = make_fn_tl({ "x", "p" });

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, tl);

    const Tac_Instruction *last = result;
    while (last->next)
        last = last->next;
    ASSERT_EQ(last->kind, TAC_INSTRUCTION_RETURN);
    ASSERT_EQ(last->u.return_.src->kind, TAC_VAL_VAR);
    EXPECT_STREQ(last->u.return_.src->u.var_name, "x");
}

// Copy(i, p) → Load(p, x), with i an unsigned int and p a pointer of the same size
// (a cast, lowered as a COPY on a 32-bit target). Forwarding i into the load's
// pointer operand would give it an integer: the copy is not propagated.
TEST_F(OptimizerTest, CopyPropKeepsValueClass)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_var("i"), make_var("p"));
    Tac_Instruction *load  = make_load(make_var("p"), make_var("x"));
    Tac_Instruction *ret   = make_return(make_var("x"));
    entry->next            = copy;
    copy->next             = load;
    load->next             = ret;

    Tac_TopLevel *tl            = make_fn_tl({ "i", "p", "x" });
    Tac_Param *i                = tl->u.function.locals;
    i->type                     = tac_new_type(TAC_TYPE_UINT);
    i->next->type               = tac_new_type(TAC_TYPE_POINTER);
    i->next->type->u.pointer.target_type = tac_new_type(TAC_TYPE_INT);
    i->next->next->type         = tac_new_type(TAC_TYPE_INT);

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, tl);

    const Tac_Instruction *in = result;
    while (in && in->kind != TAC_INSTRUCTION_LOAD)
        in = in->next;
    ASSERT_NE(in, nullptr);
    EXPECT_STREQ(in->u.load.src_ptr->u.var_name, "p");
}

// Copy(1, flag) → JIZ(flag, "Else") → Return(1) → Label("Else") → Return(0)
// copy_prop substitutes flag→1 into the JIZ condition (Var→ConstInt).
// constant_fold only folds JIZ(0,…) → Jump; JIZ(nonzero) is left as-is,
// so the Else block remains reachable.  The important observable is that the
// condition operand changed from Var("flag") to ConstInt(1).
TEST_F(OptimizerTest, CopyPropSubstInCondition)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(1), make_var("flag"));
    Tac_Instruction *jiz   = make_jump_if_zero(make_var("flag"), "Else");
    Tac_Instruction *ret1  = make_return(make_const_int(1));
    Tac_Instruction *lbl   = make_label("Else");
    Tac_Instruction *ret0  = make_return(make_const_int(0));
    entry->next            = copy;
    copy->next             = jiz;
    jiz->next              = ret1;
    ret1->next             = lbl;
    lbl->next              = ret0;

    OptFlags flags          = opt_flags_default();
    flags.max_iterations    = 1; // what one round does; later rounds fold further
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: flag\n"
              "- instruction:\n"
              "  kind: jump_if_zero\n"
              "  condition:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n"
              "  target: Else\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n"
              "- instruction:\n"
              "  kind: label\n"
              "  name: Else\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Copy(7, x) → FunCall("bar", args=[x]) → Return(0)
// copy_prop substitutes x→7 in the FunCall's arg list.
TEST_F(OptimizerTest, CopyPropSubstInFunCallArgs)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(7), make_var("x"));
    Tac_Instruction *call  = make_fun_call_with_arg("bar", make_var("x"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = copy;
    copy->next             = call;
    call->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 7\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: bar\n"
              "  args:\n"
              "    - val:\n"
              "      kind: constant\n"
              "      const:\n"
              "        kind: int\n"
              "        value: 7\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Copy(42, x) → JIZ(cond, "End") → Unary(NEGATE, y, z) → Label("End") → Return(x)
// Out-set of both predecessors of "End" contains {x→42}; meet preserves it.
// Return(x) is substituted to Return(ConstInt(42)).
TEST_F(OptimizerTest, CopyPropCrossBlock)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(42), make_var("x"));
    Tac_Instruction *jiz   = make_jump_if_zero(make_var("cond"), "End");
    Tac_Instruction *unary = make_unary(TAC_UNARY_NEGATE, make_var("y"), make_var("z"));
    Tac_Instruction *lbl   = make_label("End");
    Tac_Instruction *ret   = make_return(make_var("x"));
    entry->next            = copy;
    copy->next             = jiz;
    jiz->next              = unary;
    unary->next            = lbl;
    lbl->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 42\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: jump_if_zero\n"
              "  condition:\n"
              "    kind: var\n"
              "    name: cond\n"
              "  target: End\n"
              "- instruction:\n"
              "  kind: unary\n"
              "  op: negate\n"
              "  src:\n"
              "    kind: var\n"
              "    name: y\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: z\n"
              "- instruction:\n"
              "  kind: label\n"
              "  name: End\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 42\n");
}

// ---------------------------------------------------------------------------
// Volatile accesses must not participate in copy propagation.
// ---------------------------------------------------------------------------

// Label("fn") → Copy(a, b) [volatile] → Return(b)
// A volatile copy is not a propagatable copy: it generates no (b → a) pair, so
// the Return keeps reading b rather than being rewritten to read a.
TEST_F(OptimizerTest, CopyPropVolatileCopyNotPropagated)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = as_volatile(make_copy(make_var("a"), make_var("b")));
    Tac_Instruction *ret   = make_return(make_var("b"));
    entry->next            = copy;
    copy->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  volatile: true\n"
              "  src:\n"
              "    kind: var\n"
              "    name: a\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: b\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: b\n");
}

// Label("fn") → Copy(7, p) → Load(p, t.0) [volatile] → Return(t.0)
// The plain Copy makes (p → 7) available, but a volatile load's operands must
// not be rewritten: its src_ptr stays p (it re-reads through the pointer), it is
// not turned into a load from the constant 7.
TEST_F(OptimizerTest, CopyPropVolatileLoadOperandNotSubstituted)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *copy  = make_copy(make_const_int(7), make_var("p"));
    Tac_Instruction *ld    = as_volatile(make_load(make_var("p"), make_var("t.0")));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = copy;
    copy->next             = ld;
    ld->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 7\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: p\n"
              "- instruction:\n"
              "  kind: load\n"
              "  volatile: true\n"
              "  src_ptr:\n"
              "    kind: var\n"
              "    name: p\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Label("L1") [entry/loop header] → Binary(n > 0, c) → JIZ(c,"L0") → Copy(d,n)
// → Jump("L1") → Label("L0") → Return(n)
// The Copy(d,n) on the loop back-edge generates (n → d), and the back-edge re-enters
// the entry block L1.  But the entry's in-set is the dataflow boundary (empty): on the
// first entry n holds the parameter and d is undefined, so the loop-header read "n > 0"
// must NOT be rewritten to "d > 0".  Guards the entry-block boundary condition.
TEST_F(OptimizerTest, CopyPropBackEdgeIntoEntryNotPropagated)
{
    Tac_Instruction *entry = make_label("L1");
    Tac_Instruction *cmp =
        make_binary(TAC_BINARY_GREATER_THAN, make_var("n"), make_const_int(0), make_var("c"));
    Tac_Instruction *jiz  = make_jump_if_zero(make_var("c"), "L0");
    Tac_Instruction *cp   = make_copy(make_var("d"), make_var("n"));
    Tac_Instruction *jmp  = make_jump("L1");
    Tac_Instruction *lbl  = make_label("L0");
    Tac_Instruction *ret  = make_return(make_var("n"));
    entry->next           = cmp;
    cmp->next             = jiz;
    jiz->next             = cp;
    cp->next              = jmp;
    jmp->next             = lbl;
    lbl->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.dead_store_elim   = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: L1\n"
              "- instruction:\n"
              "  kind: binary\n"
              "  op: greater_than\n"
              "  src1:\n"
              "    kind: var\n"
              "    name: n\n"
              "  src2:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: c\n"
              "- instruction:\n"
              "  kind: jump_if_zero\n"
              "  condition:\n"
              "    kind: var\n"
              "    name: c\n"
              "  target: L0\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  src:\n"
              "    kind: var\n"
              "    name: d\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: n\n"
              "- instruction:\n"
              "  kind: jump\n"
              "  target: L1\n"
              "- instruction:\n"
              "  kind: label\n"
              "  name: L0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: n\n");
}
