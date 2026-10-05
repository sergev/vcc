//
// Common-subexpression elimination (optimize/cse.c): the pass on hand-built TAC,
// then the whole pipeline on C source.
//
#include <string>

#include "optimizer_test_fixture.h"
#include "pipeline_test_fixture.h"

extern "C" {
void eliminate_common_subexpressions(OptCfg *cfg, const Tac_TopLevel *fn);
}

class CseTest : public OptimizerTest {
protected:
    // Run the pass alone (after unreachable-code elimination, which marks the
    // reachable blocks) and return the resulting list.
    static Tac_Instruction *RunCse(Tac_Instruction *body, const Tac_TopLevel *fn = nullptr)
    {
        OptCfg *cfg = cfg_build(body);
        eliminate_unreachable(cfg);
        eliminate_common_subexpressions(cfg, fn);
        Tac_Instruction *result = cfg_flatten(cfg);
        cfg_free(cfg);
        return result;
    }

    static std::string Val(const Tac_Val *v)
    {
        if (!v)
            return "-";
        if (v->kind == TAC_VAL_VAR)
            return v->u.var_name;
        const Tac_Const *c = v->u.constant;
        switch (c->kind) {
        case TAC_CONST_INT:
            return std::to_string(c->u.int_val);
        case TAC_CONST_LONG:
            return std::to_string(c->u.long_val) + "L";
        default:
            return "c" + std::to_string(c->kind);
        }
    }

    // One line per instruction, e.g. "%1 = add a b", "%2 = %1", "ret %2".
    static std::string Show(const Tac_Instruction *body)
    {
        std::string out;
        for (const Tac_Instruction *i = body; i; i = i->next) {
            switch (i->kind) {
            case TAC_INSTRUCTION_BINARY:
                out += Val(i->u.binary.dst) + " = op" + std::to_string(i->u.binary.op) + " " +
                       Val(i->u.binary.src1) + " " + Val(i->u.binary.src2);
                break;
            case TAC_INSTRUCTION_UNARY:
                out += Val(i->u.unary.dst) + " = un" + std::to_string(i->u.unary.op) + " " +
                       Val(i->u.unary.src);
                break;
            case TAC_INSTRUCTION_COPY:
                out += Val(i->u.copy.dst) + " = " + Val(i->u.copy.src);
                break;
            case TAC_INSTRUCTION_TRUNCATE:
                out += Val(i->u.truncate.dst) + " = trunc " + Val(i->u.truncate.src);
                break;
            case TAC_INSTRUCTION_GET_ADDRESS:
                out += Val(i->u.get_address.dst) + " = &" + Val(i->u.get_address.src);
                break;
            case TAC_INSTRUCTION_STORE:
                out += "*" + Val(i->u.store.dst_ptr) + " = " + Val(i->u.store.src);
                break;
            case TAC_INSTRUCTION_LOAD:
            case TAC_INSTRUCTION_LOAD_BYTE:
                out += Val(i->u.load.dst) + " = *" + Val(i->u.load.src_ptr);
                break;
            case TAC_INSTRUCTION_COPY_FROM_OFFSET:
                out += Val(i->u.copy_from_offset.dst) + " = " + i->u.copy_from_offset.src + "." +
                       std::to_string(i->u.copy_from_offset.offset);
                break;
            case TAC_INSTRUCTION_COPY_TO_OFFSET:
                out += std::string(i->u.copy_to_offset.dst) + "." +
                       std::to_string(i->u.copy_to_offset.offset) + " = " +
                       Val(i->u.copy_to_offset.src);
                break;
            case TAC_INSTRUCTION_FUN_CALL:
                out += std::string("call ") + i->u.fun_call.fun_name;
                break;
            case TAC_INSTRUCTION_RETURN:
                out += "ret " + Val(i->u.return_.src);
                break;
            case TAC_INSTRUCTION_LABEL:
                out += std::string(i->u.label.name) + ":";
                break;
            case TAC_INSTRUCTION_JUMP:
                out += std::string("jump ") + i->u.jump.target;
                break;
            case TAC_INSTRUCTION_JUMP_IF_ZERO:
                out += "jz " + Val(i->u.jump_if_zero.condition) + " " + i->u.jump_if_zero.target;
                break;
            default:
                out += "kind" + std::to_string(i->kind);
                break;
            }
            out += "\n";
        }
        return out;
    }

    static Tac_Instruction *load(const char *p, const char *dst)
    {
        return make_load(make_var(p), make_var(dst));
    }

    static Tac_Instruction *member(const char *agg, int offset, const char *dst)
    {
        Tac_Instruction *i           = tac_new_instruction(TAC_INSTRUCTION_COPY_FROM_OFFSET);
        i->u.copy_from_offset.src    = xstrdup(agg);
        i->u.copy_from_offset.offset = offset;
        i->u.copy_from_offset.dst    = make_var(dst);
        return i;
    }

    static Tac_Instruction *add(const char *a, const char *b, const char *dst)
    {
        return make_binary(TAC_BINARY_ADD, make_var(a), make_var(b), make_var(dst));
    }
};

// t1 = a+b; t2 = a+b → t2 = t1.
TEST_F(CseTest, SameBlock)
{
    Tac_Instruction *body = chain({ add("a", "b", "%1"), add("a", "b", "%2"),
                                    make_binary(TAC_BINARY_MULTIPLY, make_var("%1"),
                                                make_var("%2"), make_var("%3")),
                                    make_return(make_var("%3")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "%2 = %1\n"
                                  "%3 = op2 %1 %2\n"
                                  "ret %3\n");
}

// a+b is found again as b+a; a-b is not found as b-a.
TEST_F(CseTest, Commutative)
{
    Tac_Instruction *body = chain(
        { add("a", "b", "%1"), add("b", "a", "%2"),
          make_binary(TAC_BINARY_SUBTRACT, make_var("a"), make_var("b"), make_var("%3")),
          make_binary(TAC_BINARY_SUBTRACT, make_var("b"), make_var("a"), make_var("%4")),
          make_return(make_var("%4")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "%2 = %1\n"
                                  "%3 = op1 a b\n"
                                  "%4 = op1 b a\n"
                                  "ret %4\n");
}

// Redefining an operand kills the expression.
TEST_F(CseTest, OperandRedefined)
{
    Tac_Instruction *body = chain({ add("a", "b", "%1"), make_copy(make_const_int(1), make_var("a")),
                                    add("a", "b", "%2"), make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "a = 1\n"
                                  "%2 = op0 a b\n"
                                  "ret %2\n");
}

// Redefining the holder kills the expression.
TEST_F(CseTest, HolderRedefined)
{
    Tac_Instruction *body = chain({ add("a", "b", "x"), make_copy(make_const_int(1), make_var("x")),
                                    add("a", "b", "%2"), make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "x = op0 a b\n"
                                  "x = 1\n"
                                  "%2 = op0 a b\n"
                                  "ret %2\n");
}

// x = x + 1 does not make x + 1 available: x has changed.
TEST_F(CseTest, SelfReferenceNotGenerated)
{
    Tac_Instruction *body =
        chain({ make_binary(TAC_BINARY_ADD, make_var("x"), make_const_int(1), make_var("x")),
                make_binary(TAC_BINARY_ADD, make_var("x"), make_const_int(1), make_var("%2")),
                make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "x = op0 x 1\n"
                                  "%2 = op0 x 1\n"
                                  "ret %2\n");
}

// Recomputing an expression into its own holder is deleted.
TEST_F(CseTest, RecomputationIntoHolderDeleted)
{
    Tac_Instruction *body =
        chain({ add("a", "b", "%1"), add("a", "b", "%1"), make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "ret %1\n");
}

// Both arms of a diamond compute a+b into the same holder: available after the
// merge. Into different holders: not available.
TEST_F(CseTest, DiamondSameHolder)
{
    Tac_Instruction *body = chain({ make_jump_if_zero(make_var("c"), "else"), add("a", "b", "%1"),
                                    make_jump("end"), make_label("else"), add("a", "b", "%1"),
                                    make_label("end"), add("a", "b", "%2"),
                                    make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "jz c else\n"
                                  "%1 = op0 a b\n"
                                  "jump end\n"
                                  "else:\n"
                                  "%1 = op0 a b\n"
                                  "end:\n"
                                  "%2 = %1\n"
                                  "ret %2\n");
}

TEST_F(CseTest, DiamondDifferentHolders)
{
    Tac_Instruction *body = chain({ make_jump_if_zero(make_var("c"), "else"), add("a", "b", "%1"),
                                    make_jump("end"), make_label("else"), add("a", "b", "%3"),
                                    make_label("end"), add("a", "b", "%2"),
                                    make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "jz c else\n"
                                  "%1 = op0 a b\n"
                                  "jump end\n"
                                  "else:\n"
                                  "%3 = op0 a b\n"
                                  "end:\n"
                                  "%2 = op0 a b\n"
                                  "ret %2\n");
}

// An expression computed ahead of a loop stays available inside it, as long as
// the loop does not change its operands.
TEST_F(CseTest, AvailableInLoop)
{
    Tac_Instruction *body = chain(
        { add("a", "b", "%1"), make_label("top"), add("a", "b", "%2"),
          make_binary(TAC_BINARY_ADD, make_var("i"), make_var("%2"), make_var("%3")),
          make_copy(make_var("%3"), make_var("i")), make_jump_if_zero(make_var("i"), "top"),
          make_return(make_var("i")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "top:\n"
                                  "%2 = %1\n"
                                  "%3 = op0 i %2\n"
                                  "i = %3\n"
                                  "jz i top\n"
                                  "ret i\n");
}

// The loop changes an operand: the expression is not available at its top.
TEST_F(CseTest, KilledInLoop)
{
    Tac_Instruction *body =
        chain({ add("a", "b", "%1"), make_label("top"), add("a", "b", "%2"),
                make_copy(make_var("%2"), make_var("a")), make_jump_if_zero(make_var("a"), "top"),
                make_return(make_var("a")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "top:\n"
                                  "%2 = op0 a b\n"
                                  "a = %2\n"
                                  "jz a top\n"
                                  "ret a\n");
}

// The loop is entered through a block that unreachable-code elimination
// empties (its jump falls through anyway). The empty block still carries a+b
// in %1 into the loop head; left out of the meet, it would let the back edge's
// %2 = a+b look available on entry, and the first computation of %2 be deleted.
TEST_F(CseTest, EmptyBlockIntoLoop)
{
    Tac_Instruction *body = chain(
        { add("a", "b", "%1"), make_jump_if_zero(make_var("c"), "out"), make_jump("top"),
          make_label("top"), make_jump_if_zero(make_var("i"), "out"), add("a", "b", "%2"),
          make_binary(TAC_BINARY_SUBTRACT, make_var("i"), make_const_int(1), make_var("i")),
          make_store(make_var("i"), make_var("%2")), make_jump("top"), make_label("out"),
          make_return(make_var("i")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "jz c out\n"
                                  "top:\n"
                                  "jz i out\n"
                                  "%2 = %1\n"
                                  "i = op1 i 1\n"
                                  "*%2 = i\n"
                                  "jump top\n"
                                  "out:\n"
                                  "ret i\n");
}

// A call kills an expression over a global; one over a private local survives.
TEST_F(CseTest, CallKillsGlobal)
{
    Tac_Instruction *body = chain(
        { make_binary(TAC_BINARY_ADD, make_var("g"), make_const_int(1), make_var("%1")),
          add("x", "y", "%2"), make_fun_call("f"),
          make_binary(TAC_BINARY_ADD, make_var("g"), make_const_int(1), make_var("%3")),
          add("x", "y", "%4"), make_return(make_var("%4")) });
    const Tac_TopLevel *fn = make_fn_tl({ "x", "y", "%1", "%2", "%3", "%4" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%1 = op0 g 1\n"
                                      "%2 = op0 x y\n"
                                      "call f\n"
                                      "%3 = op0 g 1\n"
                                      "%4 = %2\n"
                                      "ret %4\n");
}

// A store through a pointer kills expressions over a global and over an
// address-taken local; one over a private local survives.
TEST_F(CseTest, StoreKillsAliased)
{
    Tac_Instruction *body = chain(
        { make_get_address(make_var("t"), make_var("%p")),
          make_binary(TAC_BINARY_ADD, make_var("g"), make_const_int(1), make_var("%1")),
          make_binary(TAC_BINARY_ADD, make_var("t"), make_const_int(1), make_var("%2")),
          add("x", "y", "%3"), make_store(make_const_int(0), make_var("q")),
          make_binary(TAC_BINARY_ADD, make_var("g"), make_const_int(1), make_var("%4")),
          make_binary(TAC_BINARY_ADD, make_var("t"), make_const_int(1), make_var("%5")),
          add("x", "y", "%6"), make_return(make_var("%6")) });
    const Tac_TopLevel *fn =
        make_fn_tl({ "x", "y", "t", "q", "%p", "%1", "%2", "%3", "%4", "%5", "%6" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%p = &t\n"
                                      "%1 = op0 g 1\n"
                                      "%2 = op0 t 1\n"
                                      "%3 = op0 x y\n"
                                      "*q = 0\n"
                                      "%4 = op0 g 1\n"
                                      "%5 = op0 t 1\n"
                                      "%6 = %3\n"
                                      "ret %6\n");
}

// A global destination is never a holder, nor rewritten.
TEST_F(CseTest, GlobalDestinationUntouched)
{
    Tac_Instruction *body = chain({ add("x", "y", "g"), add("x", "y", "h"), add("x", "y", "%1"),
                                    make_return(make_var("%1")) });
    const Tac_TopLevel *fn = make_fn_tl({ "x", "y", "%1" });
    EXPECT_EQ(Show(RunCse(body, fn)), "g = op0 x y\n"
                                      "h = op0 x y\n"
                                      "%1 = op0 x y\n"
                                      "ret %1\n");
}

// A volatile instruction is neither a holder nor rewritten.
TEST_F(CseTest, VolatileUntouched)
{
    Tac_Instruction *body = chain({ as_volatile(add("a", "b", "%1")), add("a", "b", "%2"),
                                    as_volatile(add("a", "b", "%3")), make_return(make_var("%3")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a b\n"
                                  "%2 = op0 a b\n"
                                  "%3 = op0 a b\n"
                                  "ret %3\n");
}

// Constants of different kinds are different operands.
TEST_F(CseTest, ConstantKindsDistinct)
{
    Tac_Instruction *body =
        chain({ make_binary(TAC_BINARY_ADD, make_var("a"), make_const_int(1), make_var("%1")),
                make_binary(TAC_BINARY_ADD, make_var("a"), make_const_long(1), make_var("%2")),
                make_binary(TAC_BINARY_ADD, make_var("a"), make_const_int(1), make_var("%3")),
                make_return(make_var("%3")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = op0 a 1\n"
                                  "%2 = op0 a 1L\n"
                                  "%3 = %1\n"
                                  "ret %3\n");
}

// A holder of another type cannot stand in for the destination: TRUNCATE x to
// char and to short spell alike (no dst_kind supplied), but the types differ.
TEST_F(CseTest, DifferentTypeNotMerged)
{
    Tac_Instruction *body =
        chain({ make_conversion(TAC_INSTRUCTION_TRUNCATE, make_var("x"), make_var("%1")),
                make_conversion(TAC_INSTRUCTION_TRUNCATE, make_var("x"), make_var("%2")),
                make_conversion(TAC_INSTRUCTION_TRUNCATE, make_var("x"), make_var("%3")),
                make_return(make_var("%3")) });
    Tac_TopLevel *fn = make_fn_tl({ "x", "%1", "%2", "%3" });
    Tac_Param *p     = fn->u.function.locals;
    p->type          = tac_new_type(TAC_TYPE_LONG);
    p->next->type    = tac_new_type(TAC_TYPE_SCHAR);
    p->next->next->type       = tac_new_type(TAC_TYPE_SHORT);
    p->next->next->next->type = tac_new_type(TAC_TYPE_SCHAR);
    EXPECT_EQ(Show(RunCse(body, fn)), "%1 = trunc x\n"
                                      "%2 = trunc x\n"
                                      "%3 = %1\n"
                                      "ret %3\n");
}

// The address of a global does not depend on its value: &g is available
// across an assignment to g, and across a call.
TEST_F(CseTest, AddressAcrossAssignment)
{
    Tac_Instruction *body = chain(
        { make_get_address(make_var("g"), make_var("%1")), make_copy(make_const_int(1), make_var("g")),
          make_fun_call("f"), make_get_address(make_var("g"), make_var("%2")),
          make_return(make_var("%2")) });
    const Tac_TopLevel *fn = make_fn_tl({ "%1", "%2" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%1 = &g\n"
                                      "g = 1\n"
                                      "call f\n"
                                      "%2 = %1\n"
                                      "ret %2\n");
}

// The address of a frame slot is not held: it costs one instruction to redo.
TEST_F(CseTest, LocalAddressNotHeld)
{
    Tac_Instruction *body = chain({ make_get_address(make_var("x"), make_var("%1")),
                                    make_get_address(make_var("x"), make_var("%2")),
                                    make_return(make_var("%2")) });
    const Tac_TopLevel *fn = make_fn_tl({ "x", "%1", "%2" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%1 = &x\n"
                                      "%2 = &x\n"
                                      "ret %2\n");
}

// ---------------------------------------------------------------------------
// Memory reads.
// ---------------------------------------------------------------------------

// *p read twice with nothing written in between: the second read is a copy.
TEST_F(CseTest, LoadReused)
{
    Tac_Instruction *body = chain({ load("p", "%1"), load("p", "%2"), add("%1", "%2", "%3"),
                                    make_return(make_var("%3")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "%2 = %1\n"
                                  "%3 = op0 %1 %2\n"
                                  "ret %3\n");
}

// Any store may write *p, whatever pointer it goes through.
TEST_F(CseTest, LoadKilledByStore)
{
    Tac_Instruction *body = chain({ load("p", "%1"), make_store(make_const_int(0), make_var("q")),
                                    load("p", "%2"), make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "*q = 0\n"
                                  "%2 = *p\n"
                                  "ret %2\n");
}

TEST_F(CseTest, LoadKilledByCall)
{
    Tac_Instruction *body = chain(
        { load("p", "%1"), make_fun_call("f"), load("p", "%2"), make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "call f\n"
                                  "%2 = *p\n"
                                  "ret %2\n");
}

// p may point at the global g, or at the address-taken local x; not at the
// private local y.
TEST_F(CseTest, LoadKilledByAliasedWrite)
{
    Tac_Instruction *body = chain(
        { make_get_address(make_var("x"), make_var("%9")), load("p", "%1"),
          make_copy(make_const_int(1), make_var("g")), load("p", "%2"),
          make_copy(make_const_int(1), make_var("x")), load("p", "%3"),
          make_copy(make_const_int(1), make_var("y")), load("p", "%4"),
          make_return(make_var("%4")) });
    const Tac_TopLevel *fn = make_fn_tl({ "p", "x", "y", "%1", "%2", "%3", "%4", "%9" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%9 = &x\n"
                                      "%1 = *p\n"
                                      "g = 1\n"
                                      "%2 = *p\n"
                                      "x = 1\n"
                                      "%3 = *p\n"
                                      "y = 1\n"
                                      "%4 = %3\n"
                                      "ret %4\n");
}

// Redefining the pointer kills a read through it.
TEST_F(CseTest, LoadKilledByPointerChange)
{
    Tac_Instruction *body = chain({ load("p", "%1"), make_copy(make_var("q"), make_var("p")),
                                    load("p", "%2"), make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "p = q\n"
                                  "%2 = *p\n"
                                  "ret %2\n");
}

// A volatile read is neither a holder nor reused.
TEST_F(CseTest, VolatileLoadUntouched)
{
    Tac_Instruction *body = chain({ as_volatile(load("p", "%1")), as_volatile(load("p", "%2")),
                                    make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "%2 = *p\n"
                                  "ret %2\n");
}

// A read ahead of a loop that stores nothing stays available inside it.
TEST_F(CseTest, LoadAvailableInLoop)
{
    Tac_Instruction *body = chain(
        { load("p", "%1"), make_label("top"), load("p", "%2"),
          make_binary(TAC_BINARY_ADD, make_var("i"), make_var("%2"), make_var("%3")),
          make_copy(make_var("%3"), make_var("i")), make_jump_if_zero(make_var("i"), "top"),
          make_return(make_var("i")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "top:\n"
                                  "%2 = %1\n"
                                  "%3 = op0 i %2\n"
                                  "i = %3\n"
                                  "jz i top\n"
                                  "ret i\n");
}

// A byte read through a fat pointer is reused like a word read, but not for it.
TEST_F(CseTest, LoadByteReused)
{
    Tac_Instruction *b1 = load("p", "%1");
    Tac_Instruction *b2 = load("p", "%2");
    b1->kind = b2->kind   = TAC_INSTRUCTION_LOAD_BYTE;
    Tac_Instruction *body = chain({ b1, load("p", "%3"), b2, make_return(make_var("%2")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = *p\n"
                                  "%3 = *p\n"
                                  "%2 = %1\n"
                                  "ret %2\n");
}

// A member read is reused until its aggregate is written; a write to another
// aggregate leaves it.
TEST_F(CseTest, MemberRead)
{
    Tac_Instruction *body = chain(
        { member("s", 4, "%1"), make_copy_to_offset(make_const_int(1), "t", 4),
          member("s", 4, "%2"), member("s", 8, "%3"),
          make_copy_to_offset(make_const_int(2), "s", 0), member("s", 4, "%4"),
          make_return(make_var("%4")) });
    EXPECT_EQ(Show(RunCse(body)), "%1 = s.4\n"
                                  "t.4 = 1\n"
                                  "%2 = %1\n"
                                  "%3 = s.8\n"
                                  "s.0 = 2\n"
                                  "%4 = s.4\n"
                                  "ret %4\n");
}

// A store through a pointer kills a member read of an address-taken aggregate,
// not of a private one.
TEST_F(CseTest, MemberReadAndStore)
{
    Tac_Instruction *body = chain(
        { make_get_address(make_var("s"), make_var("%9")), member("s", 0, "%1"),
          member("t", 0, "%2"), make_store(make_const_int(0), make_var("q")),
          member("s", 0, "%3"), member("t", 0, "%4"), make_return(make_var("%4")) });
    const Tac_TopLevel *fn = make_fn_tl({ "s", "t", "q", "%1", "%2", "%3", "%4", "%9" });
    EXPECT_EQ(Show(RunCse(body, fn)), "%9 = &s\n"
                                      "%1 = s.0\n"
                                      "%2 = t.0\n"
                                      "*q = 0\n"
                                      "%3 = s.0\n"
                                      "%4 = %2\n"
                                      "ret %4\n");
}

// ---------------------------------------------------------------------------
// Store-to-load forwarding.
// ---------------------------------------------------------------------------

// *p = v; x = *p  →  x = v.
TEST_F(CseTest, ForwardStoredVariable)
{
    Tac_Instruction *body = chain({ make_store(make_var("v"), make_var("p")), load("p", "%1"),
                                    make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "*p = v\n"
                                  "%1 = v\n"
                                  "ret %1\n");
}

// A stored constant is forwarded too, when it is of the destination's type.
TEST_F(CseTest, ForwardStoredConstant)
{
    Tac_Instruction *body =
        chain({ make_store(make_const_int(7), make_var("p")), load("p", "%1"),
                make_store(make_const_long(8), make_var("p")), load("p", "%2"),
                make_return(make_var("%1")) });
    Tac_TopLevel *fn            = make_fn_tl({ "p", "%1", "%2" });
    fn->u.function.locals->type = tac_new_type(TAC_TYPE_POINTER);
    fn->u.function.locals->type->u.pointer.target_type = tac_new_type(TAC_TYPE_INT);
    fn->u.function.locals->next->type                  = tac_new_type(TAC_TYPE_INT);
    fn->u.function.locals->next->next->type            = tac_new_type(TAC_TYPE_INT);
    EXPECT_EQ(Show(RunCse(body, fn)), "*p = 7\n"
                                      "%1 = 7\n"
                                      "*p = 8L\n"
                                      "%2 = *p\n" // a long 8 is not an int
                                      "ret %1\n");
}

// *p = 1; *q = 2; x = *p: q may be p, so nothing is forwarded.
TEST_F(CseTest, ForwardKilledByOtherStore)
{
    Tac_Instruction *body =
        chain({ make_store(make_const_int(1), make_var("p")),
                make_store(make_const_int(2), make_var("q")), load("p", "%1"),
                make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "*p = 1\n"
                                  "*q = 2\n"
                                  "%1 = *p\n"
                                  "ret %1\n");
}

TEST_F(CseTest, ForwardKilledByCall)
{
    Tac_Instruction *body = chain({ make_store(make_var("v"), make_var("p")), make_fun_call("f"),
                                    load("p", "%1"), make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "*p = v\n"
                                  "call f\n"
                                  "%1 = *p\n"
                                  "ret %1\n");
}

// The stored variable changing kills the fact.
TEST_F(CseTest, ForwardKilledBySourceChange)
{
    Tac_Instruction *body = chain({ make_store(make_var("v"), make_var("p")),
                                    make_copy(make_const_int(3), make_var("v")), load("p", "%1"),
                                    make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "*p = v\n"
                                  "v = 3\n"
                                  "%1 = *p\n"
                                  "ret %1\n");
}

// A byte store truncates: nothing is forwarded from it.
TEST_F(CseTest, NoForwardFromByteStore)
{
    Tac_Instruction *st   = make_store(make_var("v"), make_var("p"));
    Tac_Instruction *ld   = load("p", "%1");
    st->kind              = TAC_INSTRUCTION_STORE_BYTE;
    ld->kind              = TAC_INSTRUCTION_LOAD_BYTE;
    Tac_Instruction *body = chain({ st, ld, make_return(make_var("%1")) });
    EXPECT_EQ(Show(RunCse(body)), "kind" + std::to_string(TAC_INSTRUCTION_STORE_BYTE) +
                                      "\n"
                                      "%1 = *p\n"
                                      "ret %1\n");
}

// ---------------------------------------------------------------------------
// The whole pipeline, on C source.
// ---------------------------------------------------------------------------

class CsePipelineTest : public PipelineTest {
protected:
    std::string Optimize(const char *src, bool cse)
    {
        OptFlags flags = opt_flags_default();
        flags.cse      = cse;
        return OptimizeYaml(src, flags);
    }
};

// x*y + x*y multiplies once (three binaries without CSE).
TEST_F(CsePipelineTest, RepeatedProduct)
{
    EXPECT_EQ(KindHistogram(Optimize("int f(int x, int y) { return x * y + x * y; }", true)),
              "binary=2 return=1");
}

TEST_F(CsePipelineTest, RepeatedProductNoCse)
{
    EXPECT_EQ(KindHistogram(Optimize("int f(int x, int y) { return x * y + x * y; }", false)),
              "binary=3 return=1");
}

// a[i] = a[i] + 1 computes the element address once.
TEST_F(CsePipelineTest, ElementAddress)
{
    EXPECT_EQ(KindHistogram(Optimize("void f(int *a, long i) { a[i] = a[i] + 1; }", true)),
              "add_ptr=1 binary=1 load=1 store=1");
}

TEST_F(CsePipelineTest, ElementAddressNoCse)
{
    EXPECT_EQ(KindHistogram(Optimize("void f(int *a, long i) { a[i] = a[i] + 1; }", false)),
              "add_ptr=2 binary=1 load=1 store=1");
}

// A call between the two computations of an expression over a global keeps
// both: g + 1 is computed twice.
TEST_F(CsePipelineTest, GlobalAcrossCall)
{
    EXPECT_EQ(KindHistogram(Optimize(
                  "int g; void h(void); int f(void) { int a = g + 1; h(); return a + (g + 1); }",
                  true)),
              "binary=3 fun_call=1 return=1");
}


// p->x * p->x reads p->x once.
TEST_F(CsePipelineTest, RepeatedMemberThroughPointer)
{
    EXPECT_EQ(KindHistogram(Optimize(
                  "struct P { int x, y; }; int f(struct P *p) { return p->x * p->x; }", true)),
              "add_ptr=1 binary=1 load=1 return=1");
}

// a[i].x + a[i].y computes the element address once.
TEST_F(CsePipelineTest, ArrayOfStructs)
{
    EXPECT_EQ(KindHistogram(Optimize("struct P { int x, y; }; int f(struct P *a, long i) "
                                     "{ return a[i].x + a[i].y; }",
                                     true)),
              "add_ptr=3 binary=1 load=2 return=1"); // 4 add_ptr without CSE
}


// *p = x; return *p reads nothing back.
TEST_F(CsePipelineTest, StoreThenLoad)
{
    EXPECT_EQ(KindHistogram(Optimize("int f(int *p, int x) { *p = x; return *p; }", true)),
              "return=1 store=1");
}
