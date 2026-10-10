//
// Bit-fields: layout against the reference compilers, and constraints.
//
#include "bitfield_layouts.h"
#include "translate_test.h"

class BitfieldTest : public TranslateTest {
protected:
    // The bytes of a struct of `size` bytes with only member `f` all ones, in hex.
    static std::string Ones(const FieldDef *f, int size)
    {
        std::vector<unsigned> bytes(size);
        if (!f->bf.width) {
            for (size_t i = 0; i < get_size(f->type); i++)
                bytes[f->offset + i] = 0xff;
        } else {
            int unit_bits = f->bf.unit_size * 8;
            for (int k = 0; k < f->bf.width; k++) {
                int bit  = f->bf.pos + k;
                int byte = target_config->big_endian ? (unit_bits - 1 - bit) / 8 : bit / 8;
                bytes[f->offset + byte] |= 1u << (bit % 8);
            }
        }
        std::string out;
        char hex[3];
        for (unsigned b : bytes) {
            snprintf(hex, sizeof hex, "%02x", b);
            out += hex;
        }
        return out;
    }

    // Compile every struct of the table for `target`, and compare each layout.
    void CheckLayouts(const char *target)
    {
        target_config = target_lookup(target);
        std::string src;
        for (const LayoutCase &l : bitfield_layouts) {
            if (strcmp(l.target, target) == 0)
                src += std::string("struct ") + l.tag + " { " + l.body + " }; struct " + l.tag +
                       " v_" + l.tag + ";\n";
        }
        Tac_TopLevel *tac = CompileUnit(src.c_str());
        int cases         = 0;
        for (const LayoutCase &l : bitfield_layouts) {
            if (strcmp(l.target, target) != 0)
                continue;
            cases++;
            const StructDef *d = structtab_find(l.tag);
            std::string got    = "size=" + std::to_string(d->size) +
                              " align=" + std::to_string(d->alignment);
            for (const FieldDef *f = d->members; f; f = f->next)
                got += std::string(" ") + f->name + "=" + Ones(f, d->size);
            EXPECT_EQ(got, l.layout) << target << " struct " << l.tag << " { " << l.body << " }";
        }
        EXPECT_EQ(cases, 18);
        tac_free_toplevel(tac);
    }
};

TEST_F(BitfieldTest, LayoutX86_64) { CheckLayouts("x86_64"); }
TEST_F(BitfieldTest, LayoutAarch64) { CheckLayouts("aarch64"); }
TEST_F(BitfieldTest, LayoutAarch64Darwin) { CheckLayouts("aarch64-darwin"); }
TEST_F(BitfieldTest, LayoutArm32) { CheckLayouts("arm32"); }
TEST_F(BitfieldTest, LayoutRiscv64) { CheckLayouts("riscv64"); }
TEST_F(BitfieldTest, LayoutRiscv32) { CheckLayouts("riscv32"); }
TEST_F(BitfieldTest, LayoutAvr) { CheckLayouts("avr"); }
TEST_F(BitfieldTest, LayoutMsp430) { CheckLayouts("msp430"); }
TEST_F(BitfieldTest, LayoutMmix) { CheckLayouts("mmix"); }
TEST_F(BitfieldTest, LayoutWasm32) { CheckLayouts("wasm32"); }

// C11 §6.7.2.1p4-5, §6.5.3.2p1, §6.5.3.4p1.
TEST_F(TranslateTestRiscv, BitfieldWidthExceedsType)
{
    EXPECT_DEATH(CompileUnit("struct S { int x : 33; };"), "exceeds its type");
    EXPECT_DEATH(CompileUnit("struct S { _Bool b : 2; };"), "exceeds its type");
    EXPECT_DEATH(CompileUnit("struct S { char c : 9; };"), "exceeds its type");
}

TEST_F(TranslateTestRiscv, BitfieldNegativeWidth)
{
    EXPECT_DEATH(CompileUnit("struct S { int x : -1; };"), "negative width");
}

TEST_F(TranslateTestRiscv, BitfieldNamedZeroWidth)
{
    EXPECT_DEATH(CompileUnit("struct S { int x : 0; };"), "zero width");
}

TEST_F(TranslateTestRiscv, BitfieldNonIntegerType)
{
    EXPECT_DEATH(CompileUnit("struct S { double d : 3; };"), "non-integer type");
}

TEST_F(TranslateTestRiscv, BitfieldAddress)
{
    EXPECT_DEATH(CompileUnit("struct S { int x : 3; } s; int *f(void) { return &s.x; }"),
                 "address of a bit-field");
}

TEST_F(TranslateTestRiscv, BitfieldSizeof)
{
    EXPECT_DEATH(CompileUnit("struct S { int x : 3; } s; int f(void) { return sizeof s.x; }"),
                 "'sizeof' to a bit-field");
}

// Unnamed bit-fields are not members: a designator and a positional initializer pass
// over them, and a member access after one finds its own name.
TEST_F(TranslateTestRiscv, BitfieldUnnamedNotMembers)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct S { int a : 3; int : 5; int b : 4; int : 0; char c; };
        struct S s = { 1, 2, 'z' };
        struct S t = { .c = 'y', .b = -1 };
        int f(void) { return s.b + t.c; }
    )");
    const StructDef *d = structtab_find("S");
    std::string names;
    for (const FieldDef *m = d->members; m; m = m->next)
        names += std::string(m->name) + " ";
    EXPECT_EQ(names, "a b c ");
    tac_free_toplevel(tac);
}

// A struct's TAC type has one unsigned member for each bit-field storage unit, in offset
// order among the others: what a backend finds at an offset, and what the ABI
// classifiers see.
TEST_F(TranslateTestX86, BitfieldStorageUnitMembers)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct S { char c; int a : 4; int b : 20; double d; unsigned e : 1; };
        double f(struct S s) { return s.d; }
    )");
    const Tac_Type *t = SymbolType(tac, "f", "%s");
    EXPECT_EQ(TypeStr(t), "struct S(24,8)");
    EXPECT_EQ(Members(t), "c@0:schar @0:uint @1:uchar d@8:double @16:uchar");
    tac_free_toplevel(tac);
}

// On AVR a struct's TAC type lists clang's bit-field access units, as clang's IR types
// show them -- { i40, i8, i16, [2 x i16] }, { i16, i16 }, { i8, i8 } -- since the ABI
// passes a structure flattened into them.
TEST_F(TranslateTestAvr, BitfieldAccessUnits)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct W { long q : 30; int r : 7; char s; short t : 11; int u[2]; };
        struct T1 { int a : 4; int b : 4; int c : 8; int d : 12; };
        struct T3 { int a : 3; int : 0; int b : 3; };
        int f(struct W w, struct T1 t1, struct T3 t3) { return w.s + t1.a + t3.b; }
    )");
    EXPECT_EQ(Members(SymbolType(tac, "f", "%w")), "@0:[5]uchar s@5:schar @6:uint u@8:[2]int");
    EXPECT_EQ(Members(SymbolType(tac, "f", "%t1")), "@0:uint @2:uint");
    EXPECT_EQ(Members(SymbolType(tac, "f", "%t3")), "@0:uchar @1:uchar");
    tac_free_toplevel(tac);
}
