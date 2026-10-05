#include "optimizer_test_fixture.h"

// ---------------------------------------------------------------------------
// Dead store elimination tests
// ---------------------------------------------------------------------------

// Label("fn") → Binary(ADD, a, b, t.0) → Copy(2, t.0) → Return(t.0)
// t.0 is overwritten by Copy before any use; Binary is a dead store and removed.
TEST_F(OptimizerTest, DeadStoreBinaryRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *bin =
        make_binary(TAC_BINARY_ADD, make_var("a"), make_var("b"), make_var("t.0"));
    Tac_Instruction *cp  = make_copy(make_const_int(2), make_var("t.0"));
    Tac_Instruction *ret = make_return(make_var("t.0"));
    entry->next          = bin;
    bin->next            = cp;
    cp->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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
              "      value: 2\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Label("fn") → Copy(3, t.0) → Return(t.0)
// copy_prop rewrites Return to Return(3); dead_store_elim then removes Copy(3, t.0).
TEST_F(OptimizerTest, DeadStoreCopyRemovedAfterCopyProp)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = cp;
    cp->next               = ret;

    OptFlags flags          = opt_flags_default();
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
              "      value: 3\n");
}

// Label("fn") → Copy(3, t.0) → Return(t.0)  (copy_prop disabled)
// t.0 is live at Return → Copy is not a dead store.
TEST_F(OptimizerTest, DeadStoreUsedSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = cp;
    cp->next               = ret;

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
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
              "    kind: var\n"
              "    name: t.0\n");
}

// Label("fn") → Store(v, ptr) → Return(ConstInt(0))
// STORE is not removable regardless of liveness.
TEST_F(OptimizerTest, DeadStoreNotStore)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *store = make_store(make_var("v"), make_var("ptr"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = store;
    store->next            = ret;

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: store\n"
              "  src:\n"
              "    kind: var\n"
              "    name: v\n"
              "  dst_ptr:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → FunCall("f") → Return(ConstInt(0))
// FUN_CALL has observable side effects and is never removed.
TEST_F(OptimizerTest, DeadStoreNotFunCall)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *call  = make_fun_call("f");
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = call;
    call->next             = ret;

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: f\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → Copy(3, g) → Return(ConstInt(0));  g is a static variable.
// Static names are seeded as live at function exit; Copy is not a dead store.
TEST_F(OptimizerTest, DeadStoreStaticSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("g"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = ret;

    // No locals → "g" is a non-temp, non-local name ⇒ observable global.
    Tac_TopLevel *tl = make_fn_tl({});

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
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
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → Copy(3, g) → Return(ConstInt(0)); g is a global (not a local).
// The function toplevel lists no locals, so "g" — a non-temp, non-local name — is
// classified as an observable global and seeded live at Exit. The store must survive.
TEST_F(OptimizerTest, DeadStoreGlobalSurvivesWithFnContext)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("g"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_TopLevel *tl = make_fn_tl({});
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
              "      value: 3\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: g\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Counterpart: a named *local* (in the function's locals list) whose store is dead
// IS removed — confirms the classification does not over-preserve.
TEST_F(OptimizerTest, DeadStoreLocalRemovedWithFnContext)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("x"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_TopLevel *tl = make_fn_tl({ "x" });
    Tac_Instruction *result = optimize_function(entry, flags, tl);

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

// Label("fn") → Copy(5, g) → FunCall("bar") → Return(ConstInt(0));  g is a static variable.
// At FunCall, static names are re-livened (callee may read them); Copy(5, g) is not a dead store.
TEST_F(OptimizerTest, DeadStoreStaticBeforeFunCallSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(5), make_var("g"));
    Tac_Instruction *call  = make_fun_call("bar");
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = call;
    call->next             = ret;

    // No locals → "g" is observable; its store before the call must survive.
    Tac_TopLevel *tl = make_fn_tl({});

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → Binary(ADD, a, b, t.0) → Unary(NEGATE, t.0, t.1) → Return(ConstInt(0))
// t.1 is dead → Unary removed; that leaves t.0 dead → Binary removed in the same pass.
TEST_F(OptimizerTest, DeadStoreCascade)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *bin =
        make_binary(TAC_BINARY_ADD, make_var("a"), make_var("b"), make_var("t.0"));
    Tac_Instruction *un  = make_unary(TAC_UNARY_NEGATE, make_var("t.0"), make_var("t.1"));
    Tac_Instruction *ret = make_return(make_const_int(0));
    entry->next          = bin;
    bin->next            = un;
    un->next             = ret;

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
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

// Label("fn") → Unary(NEGATE, a, t.0) → Return(ConstInt(0))
// t.0 is never used; Unary is removable → removed.
TEST_F(OptimizerTest, DeadStoreUnaryRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *un    = make_unary(TAC_UNARY_NEGATE, make_var("a"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = un;
    un->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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

// Label("fn") → SignExtend(x, t.0) → Return(ConstInt(0))
// t.0 is never used; conversion instructions are removable → removed.
TEST_F(OptimizerTest, DeadStoreConversionRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *conv =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_var("x"), make_var("t.0"));
    Tac_Instruction *ret = make_return(make_const_int(0));
    entry->next          = conv;
    conv->next           = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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

// Label("fn") → SignExtend(x, t.0) → Return(t.0)
// t.0 is live at Return → SignExtend survives.
TEST_F(OptimizerTest, DeadStoreConversionSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *conv =
        make_conversion(TAC_INSTRUCTION_SIGN_EXTEND, make_var("x"), make_var("t.0"));
    Tac_Instruction *ret = make_return(make_var("t.0"));
    entry->next          = conv;
    conv->next           = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: sign_extend\n"
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

// Label("fn") → Load(ptr, t.0) → Return(ConstInt(0))
// t.0 is never used; LOAD is removable → removed (ptr never added to live).
TEST_F(OptimizerTest, DeadStoreLoadRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *load  = make_load(make_var("ptr"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = load;
    load->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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

// Label("fn") → Load(ptr, t.0) → Return(t.0)
// t.0 is live at Return → Load survives.
TEST_F(OptimizerTest, DeadStoreLoadSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *load  = make_load(make_var("ptr"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = load;
    load->next             = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: load\n"
              "  src_ptr:\n"
              "    kind: var\n"
              "    name: ptr\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Label("fn") → GetAddress(x, ptr) → Return(ConstInt(0))
// ptr (the dst) is dead; GET_ADDRESS is removable → removed.
TEST_F(OptimizerTest, DeadStoreGetAddressRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ga    = make_get_address(make_var("x"), make_var("ptr"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = ga;
    ga->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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

// Label("fn") → CopyToOffset(val, "s", 0) → Return(ConstInt(0))
// COPY_TO_OFFSET is a memory mutation and is not in is_removable → always survives.
TEST_F(OptimizerTest, DeadStoreCopyToOffsetNotRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cto   = make_copy_to_offset(make_var("val"), "s", 0);
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cto;
    cto->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy_to_offset\n"
              "  src:\n"
              "    kind: var\n"
              "    name: val\n"
              "  dst: s\n"
              "  offset: 0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → GetAddress(x, ptr) → Copy(5, x) → Return(ConstInt(0))
// Alias analysis marks x as address-taken; x is seeded live at function exit.
// Copy(5, x) survives because x IS live; GetAddress is removed because ptr is dead.
TEST_F(OptimizerTest, DeadStoreAddressTakenSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ga    = make_get_address(make_var("x"), make_var("ptr"));
    Tac_Instruction *cp    = make_copy(make_const_int(5), make_var("x"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = ga;
    ga->next               = cp;
    cp->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.max_iterations    = 1; // what one round does; later rounds fold further
    flags.copy_propagation  = false;
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
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Label("fn") → Copy(1, t.0) → Copy(2, t.0) → Return(t.0)
// Two consecutive assignments to t.0; only the last one is live at Return.
// Backward pass: Copy(2, t.0) survives; Copy(1, t.0) becomes dead → removed.
TEST_F(OptimizerTest, DeadStoreDoubleOverwrite)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp1   = make_copy(make_const_int(1), make_var("t.0"));
    Tac_Instruction *cp2   = make_copy(make_const_int(2), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = cp1;
    cp1->next              = cp2;
    cp2->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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
              "      value: 2\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: t.0\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: t.0\n");
}

// Block 0: Label("fn") → Copy(5, t.0) → JIZ(cond, "B")
// Block 1: Return(ConstInt(1))
// Block 2: Label("B") → Return(ConstInt(0))
// t.0 is dead in both successors; liveness analysis computes out[block0]=∅ → Copy removed.
TEST_F(OptimizerTest, DeadStoreDeadInAllBranches)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(5), make_var("t.0"));
    Tac_Instruction *jiz   = make_jump_if_zero(make_var("cond"), "B");
    Tac_Instruction *ret1  = make_return(make_const_int(1));
    Tac_Instruction *lbl   = make_label("B");
    Tac_Instruction *ret0  = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = jiz;
    jiz->next              = ret1;
    ret1->next             = lbl;
    lbl->next              = ret0;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: jump_if_zero\n"
              "  condition:\n"
              "    kind: var\n"
              "    name: cond\n"
              "  target: B\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n"
              "- instruction:\n"
              "  kind: label\n"
              "  name: B\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Block 0: Label("fn") → Copy(3, t.0) → Jump("use")
// Block 1: Label("use") → Return(t.0)
// t.0 is live in the successor block; Copy in block 0 must not be removed.
TEST_F(OptimizerTest, DeadStoreCrossBlockNotRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = make_copy(make_const_int(3), make_var("t.0"));
    Tac_Instruction *jmp   = make_jump("use");
    Tac_Instruction *lbl   = make_label("use");
    Tac_Instruction *ret   = make_return(make_var("t.0"));
    entry->next            = cp;
    cp->next               = jmp;
    jmp->next              = lbl;
    lbl->next              = ret;

    OptFlags flags = opt_flags_default();

    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    // unreachable_elim folds the trivial jump+label into a straight sequence;
    // the important check is that Copy(3, t.0) was NOT removed.
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
              "    kind: var\n"
              "    name: t.0\n");
}

// ---------------------------------------------------------------------------
// Volatile accesses must never be removed, even when their destination is dead.
// ---------------------------------------------------------------------------

// Label("fn") → Load(p, t.0) [volatile] → Return(0)
// t.0 is never read. A plain Load would be removed (it is removable), but a
// volatile load has an observable side effect and must survive.
TEST_F(OptimizerTest, DeadStoreVolatileLoadSurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ld    = as_volatile(make_load(make_var("p"), make_var("t.0")));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = ld;
    ld->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
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
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Control for the test above: the same Load WITHOUT the volatile flag is a dead
// store (t.0 never read) and is removed.
TEST_F(OptimizerTest, DeadStoreNonVolatileLoadRemoved)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *ld    = make_load(make_var("p"), make_var("t.0"));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = ld;
    ld->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
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

// Label("fn") → Copy(5, x) [volatile] → Return(0)
// x is never read, so a plain Copy would be a dead store and removed. A volatile
// copy models a write to a volatile object and must survive.
TEST_F(OptimizerTest, DeadStoreVolatileCopySurvives)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp    = as_volatile(make_copy(make_const_int(5), make_var("x")));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp;
    cp->next               = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  volatile: true\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Two volatile writes to the same object: neither may be coalesced away. Without
// the volatile flag, Copy(1, x) would be a dead store (overwritten by Copy(2, x)).
TEST_F(OptimizerTest, DeadStoreVolatileDoubleWriteBothSurvive)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *cp1   = as_volatile(make_copy(make_const_int(1), make_var("x")));
    Tac_Instruction *cp2   = as_volatile(make_copy(make_const_int(2), make_var("x")));
    Tac_Instruction *ret   = make_return(make_const_int(0));
    entry->next            = cp1;
    cp1->next              = cp2;
    cp2->next              = ret;

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, nullptr);

    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  volatile: true\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 1\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: copy\n"
              "  volatile: true\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 2\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: x\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}

// Regression: a live store must not be dropped across a reachable-but-empty
// block. Two sequential constant-condition if-else chains; const-fold turns each
// `jz 0` into an unconditional jump, unreachable-elim empties the join blocks,
// and the dead-store liveness must still thread %1/%2 back through those empty
// blocks so the else-branch stores (%1=3, %2=5) survive into the final add.
// Before the fix the %1=3 store was deleted, leaving %1 undefined in %1+%2 (the
// program returned 5 instead of 8 — see backend Chapter6_MultipleIf for the
// end-to-end proof, which folds all the way to `return 8`). Variables are "%N"
// temporaries so the optimizer treats them as function-private (cf. is_temp_name
// in alias.c), matching how the front end lowers two `int`s.
//   fn: %1=0; %2=0; jz 0->L0; %1=2; jump L1; L0: %1=3; L1: jz 0->L2; %2=4;
//       jump L3; L2: %2=5; L3: %3=%1+%2; return %3
TEST_F(OptimizerTest, DeadStoreLiveAcrossEmptyBlock)
{
    Tac_Instruction *seq[] = {
        make_label("fn"),
        make_copy(make_const_int(0), make_var("%1")),
        make_copy(make_const_int(0), make_var("%2")),
        make_jump_if_zero(make_const_int(0), "L0"),
        make_copy(make_const_int(2), make_var("%1")),
        make_jump("L1"),
        make_label("L0"),
        make_copy(make_const_int(3), make_var("%1")),
        make_label("L1"),
        make_jump_if_zero(make_const_int(0), "L2"),
        make_copy(make_const_int(4), make_var("%2")),
        make_jump("L3"),
        make_label("L2"),
        make_copy(make_const_int(5), make_var("%2")),
        make_label("L3"),
        make_binary(TAC_BINARY_ADD, make_var("%1"), make_var("%2"), make_var("%3")),
        make_return(make_var("%3")),
    };
    int count = (int)(sizeof(seq) / sizeof(seq[0]));
    for (int i = 0; i + 1 < count; i++)
        seq[i]->next = seq[i + 1];

    OptFlags flags          = opt_flags_default();
    flags.max_iterations    = 1; // what one round does; later rounds fold further
    Tac_Instruction *result = optimize_function(seq[0], flags, nullptr);

    // Both else-branch stores reach the add: copy propagation carries the constants
    // through the empty join blocks, and dead-store elimination then drops the
    // copies (the next round folds the sum to `return 8`).
    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: binary\n"
              "  op: add\n"
              "  src1:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 3\n"
              "  src2:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 5\n"
              "  dst:\n"
              "    kind: var\n"
              "    name: %3\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: var\n"
              "    name: %3\n");
}

// Regression: the alias sets must not borrow names out of the instruction stream.
//
// Label("fn") → FunCall("bar") → Binary(ADD, g, 1, %1) → Return(ConstInt(0))
//
// "g" is a non-temp, non-local name ⇒ an observable global, and this Binary is the
// only instruction that mentions it, so the observable set is built from *this*
// Binary's var_name. %1 is a compiler temporary, dead at exit, and Binary is pure,
// so the backward removal walk frees the Binary — and then keeps walking backward to
// the FunCall, whose transfer re-livens the observable set. If that set stored the
// freed pointer rather than its own copy of the key, this reads freed memory.
// (Silent in a normal build; ASan catches it. See optimize/alias.c.)
TEST_F(OptimizerTest, DeadStoreAliasSetOutlivesFreedInstruction)
{
    Tac_Instruction *entry = make_label("fn");
    Tac_Instruction *call  = make_fun_call("bar");
    Tac_Instruction *bin =
        make_binary(TAC_BINARY_ADD, make_var("g"), make_const_int(1), make_var("%1"));
    Tac_Instruction *ret = make_return(make_const_int(0));
    entry->next          = call;
    call->next           = bin;
    bin->next            = ret;

    // No locals → "g" is observable, so the alias set records it from the Binary.
    Tac_TopLevel *tl = make_fn_tl({});

    OptFlags flags          = opt_flags_default();
    flags.copy_propagation  = false;
    Tac_Instruction *result = optimize_function(entry, flags, tl);

    // The dead Binary is gone; the call and the return survive.
    EXPECT_EQ(capture_instructions(result),
              "- instruction:\n"
              "  kind: label\n"
              "  name: fn\n"
              "- instruction:\n"
              "  kind: fun_call\n"
              "  fun_name: bar\n"
              "- instruction:\n"
              "  kind: return\n"
              "  src:\n"
              "    kind: constant\n"
              "    const:\n"
              "      kind: int\n"
              "      value: 0\n");
}
