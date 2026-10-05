#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "tac.h"
#include "wio.h"
#include "xalloc.h"

extern "C" int xalloc_debug;

class TacBinaryTest : public ::testing::Test {
protected:
    char tmppath[32];

    void SetUp() override
    {
        // Enable to debug allocations:
        // xalloc_debug = 1;

        strncpy(tmppath, "/tmp/tac_binary_XXXXXX", sizeof(tmppath));
        int fd = mkstemp(tmppath);
        close(fd);
    }

    void TearDown() override
    {
        unlink(tmppath);
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    Tac_Program *roundtrip(const Tac_Program *prog)
    {
        WFILE wout;
        wopen(&wout, tmppath, "w");
        tac_export_program(&wout, prog);
        wclose(&wout);

        WFILE win;
        wopen(&win, tmppath, "r");
        Tac_Program *result = tac_import_program(&win);
        wclose(&win);
        return result;
    }

    static Tac_Val *make_const_int(int v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_INT);
        c->u.int_val    = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_var(const char *name)
    {
        Tac_Val *val    = tac_new_val(TAC_VAL_VAR);
        val->u.var_name = xstrdup(name);
        return val;
    }

    static Tac_TopLevel *make_empty_function(const char *name, bool global)
    {
        Tac_TopLevel *tl      = tac_new_toplevel(TAC_TOPLEVEL_FUNCTION);
        tl->u.function.name   = xstrdup(name);
        tl->u.function.global = global;
        return tl;
    }
};

// ---------------------------------------------------------------------------
// Program-level
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, EmptyProgram)
{
    Tac_Program *orig = tac_new_program();
    Tac_Program *copy = roundtrip(orig);

    ASSERT_NE(nullptr, copy);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, EmptyFunction)
{
    Tac_Program *orig = tac_new_program();
    orig->decls       = make_empty_function("foo", true);
    Tac_Program *copy = roundtrip(orig);

    ASSERT_NE(nullptr, copy);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, FunctionNoreturnFlag)
{
    Tac_Program *orig          = tac_new_program();
    orig->decls                = make_empty_function("die", true);
    orig->decls->u.function.noret    = true;
    orig->decls->u.function.variadic = true;
    Tac_Program *copy = roundtrip(orig);

    ASSERT_NE(nullptr, copy);
    EXPECT_TRUE(copy->decls->u.function.noret);
    EXPECT_TRUE(copy->decls->u.function.variadic);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// _Alignas alignments of a static variable and a static local survive the round trip.
TEST_F(TacBinaryTest, StaticAlignment)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = make_empty_function("f", true);
    Tac_StaticLocal *sl                   = tac_new_static_local();
    sl->name                              = xstrdup("s");
    sl->type                              = tac_new_type(TAC_TYPE_INT);
    sl->alignment                         = 32;
    orig->decls->u.function.static_locals = sl;
    Tac_TopLevel *sv                      = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    sv->u.static_variable.name            = xstrdup("g");
    sv->u.static_variable.type            = tac_new_type(TAC_TYPE_INT);
    sv->u.static_variable.alignment       = 16;
    orig->decls->next                     = sv;
    Tac_Program *copy                     = roundtrip(orig);

    ASSERT_NE(nullptr, copy);
    EXPECT_EQ(32, copy->decls->u.function.static_locals->alignment);
    EXPECT_EQ(16, copy->decls->next->u.static_variable.alignment);
    EXPECT_TRUE(tac_compare_program(orig, copy));
    sv->u.static_variable.alignment = 8;
    EXPECT_FALSE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, FunctionWithParams)
{
    Tac_Program *orig = tac_new_program();
    orig->decls       = make_empty_function("add", true);

    Tac_Param *p1                  = tac_new_param();
    p1->name                       = xstrdup("x");
    Tac_Param *p2                  = tac_new_param();
    p2->name                       = xstrdup("y");
    p1->next                       = p2;
    orig->decls->u.function.params = p1;

    Tac_Program *copy = roundtrip(orig);

    ASSERT_NE(nullptr, copy);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Return instruction
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, NullReturn)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, ReturnAllConstKinds)
{
    const struct {
        Tac_ConstKind kind;
        const char *label;
    } cases[] = {
        { TAC_CONST_INT, "int" },
        { TAC_CONST_LONG, "long" },
        { TAC_CONST_LONG_LONG, "long_long" },
        { TAC_CONST_UINT, "uint" },
        { TAC_CONST_ULONG, "ulong" },
        { TAC_CONST_ULONG_LONG, "ulong_long" },
        { TAC_CONST_FLOAT, "float" },
        { TAC_CONST_DOUBLE, "double" },
        { TAC_CONST_SCHAR, "schar" },
        { TAC_CONST_UCHAR, "uchar" },
    };

    for (const auto &tc : cases) {
        Tac_Const *c = tac_new_const(tc.kind);
        switch (tc.kind) {
        case TAC_CONST_INT:
            c->u.int_val = -99;
            break;
        case TAC_CONST_LONG:
            c->u.long_val = 100000L;
            break;
        case TAC_CONST_LONG_LONG:
            c->u.long_long_val = -9000000000LL;
            break;
        case TAC_CONST_UINT:
            c->u.uint_val = 7u;
            break;
        case TAC_CONST_ULONG:
            c->u.ulong_val = 123456ul;
            break;
        case TAC_CONST_ULONG_LONG:
            c->u.ulong_long_val = 999999999999ull;
            break;
        case TAC_CONST_FLOAT:
            c->u.float_val = 1.5f;
            break;
        case TAC_CONST_DOUBLE:
            c->u.double_val = 3.14;
            break;
        case TAC_CONST_SCHAR:
            c->u.char_val = 65;
            break;
        case TAC_CONST_UCHAR:
            c->u.uchar_val = 200;
            break;
        default:
            break;
        }
        Tac_Val *val                 = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant              = c;
        Tac_Instruction *i           = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        i->u.return_.src             = val;
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        orig->decls->u.function.body = i;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "kind=" << tc.label;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// A multi-character constant can exceed 32 bits (up to 48 on BESM-6); the binary
// stream must carry the full 64-bit host value without truncation.
TEST_F(TacBinaryTest, WideIntConstRoundTrip)
{
    const struct {
        Tac_ConstKind kind;
        uint64_t value; // 'abcde' = 0x6162636465 (40b), 'abcdef' = 0x616263646566 (48b)
    } cases[] = {
        { TAC_CONST_INT, 0x6162636465ULL },
        { TAC_CONST_UINT, 0x616263646566ULL },
    };

    for (const auto &tc : cases) {
        Tac_Const *c = tac_new_const(tc.kind);
        if (tc.kind == TAC_CONST_INT)
            c->u.int_val = (int64_t)tc.value;
        else
            c->u.uint_val = tc.value;
        Tac_Val *val                 = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant              = c;
        Tac_Instruction *i           = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        i->u.return_.src             = val;
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        orig->decls->u.function.body = i;

        Tac_Program *copy = roundtrip(orig);
        ASSERT_NE(nullptr, copy);
        const Tac_Const *rc = copy->decls->u.function.body->u.return_.src->u.constant;
        if (tc.kind == TAC_CONST_INT)
            EXPECT_EQ((uint64_t)rc->u.int_val, tc.value);
        else
            EXPECT_EQ(rc->u.uint_val, tc.value);

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

TEST_F(TacBinaryTest, ReturnVar)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src         = make_var("tmp.0");
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Conversion instructions
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AllConversionInstructions)
{
    Tac_InstructionKind kinds[] = {
        TAC_INSTRUCTION_SIGN_EXTEND,    TAC_INSTRUCTION_TRUNCATE,
        TAC_INSTRUCTION_ZERO_EXTEND,    TAC_INSTRUCTION_DOUBLE_TO_INT,
        TAC_INSTRUCTION_DOUBLE_TO_UINT, TAC_INSTRUCTION_INT_TO_DOUBLE,
        TAC_INSTRUCTION_UINT_TO_DOUBLE,
    };

    for (auto kind : kinds) {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(kind);
        instr->u.sign_extend.src     = make_var("s");
        instr->u.sign_extend.dst     = make_var("d");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "kind=" << (int)kind;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// Unary instruction
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AllUnaryInstructions)
{
    Tac_UnaryOperator ops[] = { TAC_UNARY_COMPLEMENT, TAC_UNARY_NEGATE, TAC_UNARY_NOT };

    for (auto op : ops) {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_UNARY);
        instr->u.unary.op            = op;
        instr->u.unary.src           = make_var("src");
        instr->u.unary.dst           = make_var("dst");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "op=" << (int)op;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// Binary instruction
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AllBinaryInstructions)
{
    Tac_BinaryOperator ops[] = {
        TAC_BINARY_ADD,          TAC_BINARY_SUBTRACT,         TAC_BINARY_MULTIPLY,
        TAC_BINARY_DIVIDE,       TAC_BINARY_REMAINDER,        TAC_BINARY_EQUAL,
        TAC_BINARY_NOT_EQUAL,    TAC_BINARY_LESS_THAN,        TAC_BINARY_LESS_OR_EQUAL,
        TAC_BINARY_GREATER_THAN, TAC_BINARY_GREATER_OR_EQUAL, TAC_BINARY_BITWISE_AND,
        TAC_BINARY_BITWISE_OR,   TAC_BINARY_BITWISE_XOR,      TAC_BINARY_LEFT_SHIFT,
        TAC_BINARY_RIGHT_SHIFT,
    };

    for (auto op : ops) {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_BINARY);
        instr->u.binary.op           = op;
        instr->u.binary.src1         = make_var("a");
        instr->u.binary.src2         = make_var("b");
        instr->u.binary.dst          = make_var("c");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "op=" << (int)op;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// Copy, GetAddress, Load, Store
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, CopyAndGetAddress)
{
    for (auto kind : { TAC_INSTRUCTION_COPY, TAC_INSTRUCTION_GET_ADDRESS }) {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(kind);
        instr->u.copy.src            = make_var("x");
        instr->u.copy.dst            = make_var("y");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "kind=" << (int)kind;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

TEST_F(TacBinaryTest, LoadAndStore)
{
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_LOAD);
        instr->u.load.src_ptr        = make_var("ptr");
        instr->u.load.dst            = make_var("val");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_STORE);
        instr->u.store.src           = make_var("val");
        instr->u.store.dst_ptr       = make_var("ptr");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// The byte/decay fat-pointer variants are distinct instruction kinds and must
// survive the binary round-trip with their kind preserved.
TEST_F(TacBinaryTest, ByteAndDecayKinds)
{
    const Tac_InstructionKind kinds[] = {
        TAC_INSTRUCTION_GET_ADDRESS_BYTE,  TAC_INSTRUCTION_GET_ADDRESS_DECAY,
        TAC_INSTRUCTION_LOAD_BYTE,         TAC_INSTRUCTION_STORE_BYTE,
        TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET, TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET,
    };
    for (Tac_InstructionKind kind : kinds) {
        Tac_Program *orig      = tac_new_program();
        orig->decls            = make_empty_function("f", true);
        Tac_Instruction *instr = tac_new_instruction(kind);
        switch (kind) {
        case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
        case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
            instr->u.get_address.src = make_var("a");
            instr->u.get_address.dst = make_var("b");
            break;
        case TAC_INSTRUCTION_LOAD_BYTE:
            instr->u.load.src_ptr = make_var("ptr");
            instr->u.load.dst     = make_var("val");
            break;
        case TAC_INSTRUCTION_STORE_BYTE:
            instr->u.store.src     = make_var("val");
            instr->u.store.dst_ptr = make_var("ptr");
            break;
        case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
            instr->u.copy_to_offset.src    = make_var("val");
            instr->u.copy_to_offset.dst    = xstrdup("s");
            instr->u.copy_to_offset.offset = 1;
            break;
        case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
            instr->u.copy_from_offset.src    = xstrdup("s");
            instr->u.copy_from_offset.offset = 1;
            instr->u.copy_from_offset.dst    = make_var("val");
            break;
        default:
            break;
        }
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        ASSERT_NE(copy->decls->u.function.body, nullptr);
        EXPECT_EQ(copy->decls->u.function.body->kind, kind);
        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// Volatile flag
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, VolatileFlagRoundTrip)
{
    // A volatile load survives the binary round-trip with the flag preserved.
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_LOAD);
        instr->is_volatile           = true;
        instr->u.load.src_ptr        = make_var("ptr");
        instr->u.load.dst            = make_var("val");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        ASSERT_NE(copy->decls->u.function.body, nullptr);
        EXPECT_TRUE(copy->decls->u.function.body->is_volatile);
        tac_free_program(orig);
        tac_free_program(copy);
    }
    // A volatile store too.
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_STORE);
        instr->is_volatile           = true;
        instr->u.store.src           = make_var("val");
        instr->u.store.dst_ptr       = make_var("ptr");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        ASSERT_NE(copy->decls->u.function.body, nullptr);
        EXPECT_TRUE(copy->decls->u.function.body->is_volatile);
        tac_free_program(orig);
        tac_free_program(copy);
    }
    // The flag is significant: an otherwise-identical volatile vs non-volatile
    // instruction must NOT compare equal (guards against silently dropping it).
    {
        Tac_Program *vol            = tac_new_program();
        vol->decls                  = make_empty_function("f", true);
        Tac_Instruction *vi         = tac_new_instruction(TAC_INSTRUCTION_LOAD);
        vi->is_volatile             = true;
        vi->u.load.src_ptr          = make_var("ptr");
        vi->u.load.dst              = make_var("val");
        vol->decls->u.function.body = vi;

        Tac_Program *plain            = tac_new_program();
        plain->decls                  = make_empty_function("f", true);
        Tac_Instruction *pi           = tac_new_instruction(TAC_INSTRUCTION_LOAD);
        pi->u.load.src_ptr            = make_var("ptr");
        pi->u.load.dst                = make_var("val");
        plain->decls->u.function.body = pi;

        EXPECT_FALSE(tac_compare_program(vol, plain));
        tac_free_program(vol);
        tac_free_program(plain);
    }
}

// ---------------------------------------------------------------------------
// AddPtr
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AddPtr)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
    instr->u.add_ptr.ptr         = make_var("p");
    instr->u.add_ptr.index       = make_var("i");
    instr->u.add_ptr.scale       = 4;
    instr->u.add_ptr.dst         = make_var("r");
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// PtrDiff
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, PtrDiff)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_PTR_DIFF);
    instr->u.ptr_diff.ptr_a      = make_var("p");
    instr->u.ptr_diff.ptr_b      = make_var("q");
    instr->u.ptr_diff.dst        = make_var("r");
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// CopyToOffset / CopyFromOffset
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, CopyToAndFromOffset)
{
    {
        Tac_Program *orig              = tac_new_program();
        orig->decls                    = make_empty_function("f", true);
        Tac_Instruction *instr         = tac_new_instruction(TAC_INSTRUCTION_COPY_TO_OFFSET);
        instr->u.copy_to_offset.src    = make_var("s");
        instr->u.copy_to_offset.dst    = xstrdup("agg");
        instr->u.copy_to_offset.offset = 8;
        orig->decls->u.function.body   = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
    {
        Tac_Program *orig                = tac_new_program();
        orig->decls                      = make_empty_function("f", true);
        Tac_Instruction *instr           = tac_new_instruction(TAC_INSTRUCTION_COPY_FROM_OFFSET);
        instr->u.copy_from_offset.src    = xstrdup("agg");
        instr->u.copy_from_offset.offset = 16;
        instr->u.copy_from_offset.dst    = make_var("d");
        orig->decls->u.function.body     = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// Jump and Label
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, JumpInstructions)
{
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_JUMP);
        instr->u.jump.target         = xstrdup("loop_end");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
    {
        Tac_Program *orig               = tac_new_program();
        orig->decls                     = make_empty_function("f", true);
        Tac_Instruction *instr          = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_ZERO);
        instr->u.jump_if_zero.condition = make_var("cond");
        instr->u.jump_if_zero.target    = xstrdup("false_lbl");
        orig->decls->u.function.body    = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
    {
        Tac_Program *orig                   = tac_new_program();
        orig->decls                         = make_empty_function("f", true);
        Tac_Instruction *instr              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
        instr->u.jump_if_not_zero.condition = make_var("cond");
        instr->u.jump_if_not_zero.target    = xstrdup("true_lbl");
        orig->decls->u.function.body        = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
    {
        Tac_Program *orig            = tac_new_program();
        orig->decls                  = make_empty_function("f", true);
        Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_LABEL);
        instr->u.label.name          = xstrdup("loop_start");
        orig->decls->u.function.body = instr;

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy));
        tac_free_program(orig);
        tac_free_program(copy);
    }
}

// ---------------------------------------------------------------------------
// FunCall
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, FunCallWithArgs)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
    instr->u.fun_call.fun_name   = xstrdup("printf");
    Tac_Val *arg1                = make_var("fmt");
    Tac_Val *arg2                = make_var("x");
    arg1->next                   = arg2;
    instr->u.fun_call.args       = arg1;
    instr->u.fun_call.dst        = make_var("ret");
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, FunCallNoArgNoDst)
{
    Tac_Program *orig            = tac_new_program();
    orig->decls                  = make_empty_function("f", true);
    Tac_Instruction *instr       = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
    instr->u.fun_call.fun_name   = xstrdup("rand");
    orig->decls->u.function.body = instr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Instruction sequence
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, InstructionSequence)
{
    Tac_Program *orig = tac_new_program();
    orig->decls       = make_empty_function("f", true);

    Tac_Instruction *lbl = tac_new_instruction(TAC_INSTRUCTION_LABEL);
    lbl->u.label.name    = xstrdup("entry");

    Tac_Instruction *ret = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    ret->u.return_.src   = make_const_int(0);

    lbl->next                    = ret;
    orig->decls->u.function.body = lbl;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AllScalarTypes)
{
    Tac_TypeKind kinds[] = {
        TAC_TYPE_SCHAR,      TAC_TYPE_UCHAR,     TAC_TYPE_SHORT,  TAC_TYPE_INT,
        TAC_TYPE_LONG,       TAC_TYPE_LONG_LONG, TAC_TYPE_USHORT, TAC_TYPE_UINT,  TAC_TYPE_ULONG,
        TAC_TYPE_ULONG_LONG, TAC_TYPE_FLOAT,     TAC_TYPE_DOUBLE, TAC_TYPE_VOID,
    };

    for (auto kind : kinds) {
        Tac_Program *orig                     = tac_new_program();
        orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
        orig->decls->u.static_variable.name   = xstrdup("v");
        orig->decls->u.static_variable.global = false;
        orig->decls->u.static_variable.type   = tac_new_type(kind);

        Tac_Program *copy = roundtrip(orig);
        EXPECT_TRUE(tac_compare_program(orig, copy)) << "type kind=" << (int)kind;

        tac_free_program(orig);
        tac_free_program(copy);
    }
}

TEST_F(TacBinaryTest, FunType)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("fp");
    orig->decls->u.static_variable.global = false;

    Tac_Type *fun                       = tac_new_type(TAC_TYPE_FUN_TYPE);
    Tac_Type *param1                    = tac_new_type(TAC_TYPE_INT);
    Tac_Type *param2                    = tac_new_type(TAC_TYPE_LONG);
    param1->next                        = param2;
    Tac_Type *ret                       = tac_new_type(TAC_TYPE_VOID);
    fun->u.fun_type.param_types         = param1;
    fun->u.fun_type.ret_type            = ret;
    orig->decls->u.static_variable.type = fun;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, PointerType)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("p");
    orig->decls->u.static_variable.global = false;

    Tac_Type *ptr                       = tac_new_type(TAC_TYPE_POINTER);
    Tac_Type *target                    = tac_new_type(TAC_TYPE_LONG);
    ptr->u.pointer.target_type          = target;
    orig->decls->u.static_variable.type = ptr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, ArrayType)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("arr");
    orig->decls->u.static_variable.global = false;

    Tac_Type *arr                       = tac_new_type(TAC_TYPE_ARRAY);
    Tac_Type *elem                      = tac_new_type(TAC_TYPE_INT);
    arr->u.array.elem_type              = elem;
    arr->u.array.size                   = 10;
    orig->decls->u.static_variable.type = arr;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, StructureType)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("obj");
    orig->decls->u.static_variable.global = false;

    Tac_Type *st                        = tac_new_type(TAC_TYPE_STRUCTURE);
    st->u.structure.tag                 = xstrdup("Point");
    orig->decls->u.static_variable.type = st;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Static variable with all init kinds
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, StaticVariableAllInits)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("data");
    orig->decls->u.static_variable.global = false;
    orig->decls->u.static_variable.type   = tac_new_type(TAC_TYPE_SCHAR);

    Tac_StaticInit *i8            = tac_new_static_init(TAC_STATIC_INIT_I8);
    i8->u.char_val                = -1;
    Tac_StaticInit *i32           = tac_new_static_init(TAC_STATIC_INIT_I32);
    i32->u.int_val                = 1000;
    Tac_StaticInit *i64           = tac_new_static_init(TAC_STATIC_INIT_I64);
    i64->u.long_val               = 1000000000LL;
    Tac_StaticInit *u8            = tac_new_static_init(TAC_STATIC_INIT_U8);
    u8->u.uchar_val               = 255;
    Tac_StaticInit *u32           = tac_new_static_init(TAC_STATIC_INIT_U32);
    u32->u.uint_val               = 4000000000u;
    Tac_StaticInit *u64           = tac_new_static_init(TAC_STATIC_INIT_U64);
    u64->u.ulong_val              = 18000000000ull;
    Tac_StaticInit *flt           = tac_new_static_init(TAC_STATIC_INIT_FLOAT);
    flt->u.float_val              = 1.5f;
    Tac_StaticInit *dbl           = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
    dbl->u.double_val             = 2.718;
    Tac_StaticInit *zero          = tac_new_static_init(TAC_STATIC_INIT_ZERO);
    zero->u.zero_bytes            = 16;
    Tac_StaticInit *str           = tac_new_static_init(TAC_STATIC_INIT_STRING);
    str->u.string.val             = xstrdup("hello");
    str->u.string.len             = 5;
    str->u.string.null_terminated = false;
    Tac_StaticInit *ptr           = tac_new_static_init(TAC_STATIC_INIT_POINTER);
    ptr->u.pointer.name           = xstrdup("arr");

    i8->next                                 = i32;
    i32->next                                = i64;
    i64->next                                = u8;
    u8->next                                 = u32;
    u32->next                                = u64;
    u64->next                                = flt;
    flt->next                                = dbl;
    dbl->next                                = zero;
    zero->next                               = str;
    str->next                                = ptr;
    orig->decls->u.static_variable.init_list = i8;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

TEST_F(TacBinaryTest, StaticVariableStringNullTerminated)
{
    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("s");
    orig->decls->u.static_variable.global = true;
    orig->decls->u.static_variable.type   = tac_new_type(TAC_TYPE_SCHAR);

    Tac_StaticInit *si                       = tac_new_static_init(TAC_STATIC_INIT_STRING);
    si->u.string.val                         = xstrdup("world");
    si->u.string.len                         = 5;
    si->u.string.null_terminated             = true;
    orig->decls->u.static_variable.init_list = si;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// A decoded string literal may hold embedded NUL bytes ("a\0c" is three bytes long),
// so the wire format carries an explicit length instead of a terminator.
TEST_F(TacBinaryTest, StaticVariableStringWithEmbeddedNul)
{
    static const char bytes[] = "a\0c\0z"; // 5 bytes, three of them past a NUL

    Tac_Program *orig                     = tac_new_program();
    orig->decls                           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    orig->decls->u.static_variable.name   = xstrdup("s");
    orig->decls->u.static_variable.global = true;
    orig->decls->u.static_variable.type   = tac_new_type(TAC_TYPE_SCHAR);

    Tac_StaticInit *si                       = tac_new_static_init(TAC_STATIC_INIT_STRING);
    si->u.string.val                         = xmemdup(bytes, sizeof(bytes) - 1);
    si->u.string.len                         = sizeof(bytes) - 1;
    si->u.string.null_terminated             = true;
    orig->decls->u.static_variable.init_list = si;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    const Tac_StaticInit *back = copy->decls->u.static_variable.init_list;
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->u.string.len, sizeof(bytes) - 1);
    EXPECT_EQ(memcmp(back->u.string.val, bytes, sizeof(bytes) - 1), 0);

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Static constant
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, StaticConstant)
{
    Tac_Program *orig                   = tac_new_program();
    orig->decls                         = tac_new_toplevel(TAC_TOPLEVEL_STATIC_CONSTANT);
    orig->decls->u.static_constant.name = xstrdup("PI");
    orig->decls->u.static_constant.type = tac_new_type(TAC_TYPE_DOUBLE);
    Tac_StaticInit *si                  = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
    si->u.double_val                    = 3.14159;
    orig->decls->u.static_constant.init = si;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Multiple toplevels in one program
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, MultipleToplevels)
{
    Tac_Program *orig = tac_new_program();

    // function
    Tac_TopLevel *fn     = make_empty_function("main", true);
    Tac_Instruction *ret = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    ret->u.return_.src   = make_const_int(0);
    fn->u.function.body  = ret;

    // static variable
    Tac_TopLevel *sv                = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    sv->u.static_variable.name      = xstrdup("g");
    sv->u.static_variable.global    = true;
    sv->u.static_variable.type      = tac_new_type(TAC_TYPE_INT);
    Tac_StaticInit *si_sv           = tac_new_static_init(TAC_STATIC_INIT_I32);
    si_sv->u.int_val                = 42;
    sv->u.static_variable.init_list = si_sv;

    // static constant
    Tac_TopLevel *sc           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_CONSTANT);
    sc->u.static_constant.name = xstrdup("E");
    sc->u.static_constant.type = tac_new_type(TAC_TYPE_DOUBLE);
    Tac_StaticInit *si_sc      = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
    si_sc->u.double_val        = 2.71828;
    sc->u.static_constant.init = si_sc;

    fn->next    = sv;
    sv->next    = sc;
    orig->decls = fn;

    Tac_Program *copy = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, copy));

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// AllocateLocal instruction (task #23)
// ---------------------------------------------------------------------------

TEST_F(TacBinaryTest, AllocateLocal)
{
    Tac_Program *orig                 = tac_new_program();
    orig->decls                       = make_empty_function("f", true);
    Tac_Instruction *instr            = tac_new_instruction(TAC_INSTRUCTION_ALLOCATE_LOCAL);
    instr->u.allocate_local.name      = xstrdup("%s");
    instr->u.allocate_local.size      = 40; // target bytes
    instr->u.allocate_local.alignment = 4;  // target bytes
    orig->decls->u.function.body      = instr;

    Tac_Program *copy = roundtrip(orig);
    ASSERT_NE(nullptr, copy);
    EXPECT_TRUE(tac_compare_program(orig, copy));
    EXPECT_EQ(copy->decls->u.function.body->kind, TAC_INSTRUCTION_ALLOCATE_LOCAL);
    EXPECT_STREQ(copy->decls->u.function.body->u.allocate_local.name, "%s");
    EXPECT_EQ(copy->decls->u.function.body->u.allocate_local.size, 40);
    EXPECT_EQ(copy->decls->u.function.body->u.allocate_local.alignment, 4);

    tac_free_program(orig);
    tac_free_program(copy);
}

// ---------------------------------------------------------------------------
// Typed TAC: symbol types, function and callee types, externs
// ---------------------------------------------------------------------------

static Tac_Type *fun_type_int_variadic()
{
    Tac_Type *ft                 = tac_new_type(TAC_TYPE_FUN_TYPE);
    ft->u.fun_type.param_types   = tac_new_type(TAC_TYPE_INT);
    ft->u.fun_type.ret_type      = tac_new_type(TAC_TYPE_VOID);
    ft->u.fun_type.variadic      = true;
    return ft;
}

TEST_F(TacBinaryTest, TypedFunctionRoundTrip)
{
    Tac_Program *orig = tac_new_program();
    Tac_TopLevel *fn  = make_empty_function("f", true);
    fn->u.function.type = fun_type_int_variadic();

    Tac_Param *p                     = tac_new_param();
    p->name                          = xstrdup("%x");
    p->type                          = tac_new_type(TAC_TYPE_INT);
    fn->u.function.params            = p;
    Tac_Param *l                     = tac_new_param();
    l->name                          = xstrdup("%1");
    l->type                          = tac_new_type(TAC_TYPE_POINTER);
    l->type->u.pointer.target_type   = tac_new_type(TAC_TYPE_UCHAR);
    fn->u.function.locals            = l;

    Tac_Instruction *call          = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
    call->u.fun_call.fun_name      = xstrdup("printf");
    call->u.fun_call.args          = make_var("%x");
    call->u.fun_call.fun_type      = fun_type_int_variadic();
    fn->u.function.body            = call;
    orig->decls                    = fn;

    Tac_TopLevel *ext     = tac_new_toplevel(TAC_TOPLEVEL_EXTERN);
    ext->u.extern_.name   = xstrdup("errno");
    ext->u.extern_.type   = tac_new_type(TAC_TYPE_INT);
    fn->next              = ext;

    Tac_Program *result = roundtrip(orig);
    EXPECT_TRUE(tac_compare_program(orig, result));
    EXPECT_TRUE(tac_compare_toplevel(ext, result->decls->next));
    ASSERT_NE(result->decls->u.function.locals, nullptr);
    EXPECT_EQ(result->decls->u.function.locals->type->kind, TAC_TYPE_POINTER);
    EXPECT_TRUE(result->decls->u.function.body->u.fun_call.fun_type->u.fun_type.variadic);

    tac_free_program(orig);
    tac_free_program(result);
}

TEST_F(TacBinaryTest, CloneTypeIsDeepAndEqual)
{
    Tac_Type *a = fun_type_int_variadic();
    Tac_Type *b = tac_clone_type(a);
    EXPECT_NE(a, b);
    EXPECT_NE(a->u.fun_type.param_types, b->u.fun_type.param_types);
    EXPECT_TRUE(tac_compare_type(a, b));
    b->u.fun_type.variadic = false;
    EXPECT_FALSE(tac_compare_type(a, b));
    tac_free_type(a);
    tac_free_type(b);
}

TEST_F(TacBinaryTest, TypeStr)
{
    Tac_Type *ft = fun_type_int_variadic();
    char *s      = tac_type_str(ft);
    EXPECT_STREQ(s, "fn(int, ...) -> void");
    xfree(s);
    tac_free_type(ft);

    Tac_Type *arr                = tac_new_type(TAC_TYPE_ARRAY);
    arr->u.array.size            = 4;
    arr->u.array.elem_type       = tac_new_type(TAC_TYPE_POINTER);
    arr->u.array.elem_type->u.pointer.target_type = tac_new_type(TAC_TYPE_STRUCTURE);
    arr->u.array.elem_type->u.pointer.target_type->u.structure.tag  = xstrdup("S");
    arr->u.array.elem_type->u.pointer.target_type->u.structure.size      = 12;
    arr->u.array.elem_type->u.pointer.target_type->u.structure.alignment = 4;
    s = tac_type_str(arr);
    EXPECT_STREQ(s, "[4]*struct S(12,4)");
    xfree(s);
    tac_free_type(arr);
}

TEST_F(TacBinaryTest, RejectsStreamWithoutMagic)
{
    WFILE wout;
    wopen(&wout, tmppath, "w");
    wputw(0x54414332, &wout); // 'TAC2'
    wclose(&wout);

    WFILE win;
    wopen(&win, tmppath, "r");
    EXPECT_FALSE(tac_import_begin_stream(&win));
    wclose(&win);
}
