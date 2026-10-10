//
// The frontend on a 16-bit int: AVR's int, unsigned int, size_t, ptrdiff_t and pointers
// are two bytes, long four.  Each case is one place the frontend used to assume an int
// of at least 32 bits, or a pointer as wide as long.
//
#include "translate_test.h"

// The value of the static initializer of `name` in a CompileToYaml dump.
static std::string StaticValue(const std::string &yaml, const char *name)
{
    size_t at = yaml.find(std::string("  name: ") + name + "\n");
    if (at == std::string::npos)
        return "<no " + std::string(name) + ">";
    at = yaml.find("value: ", at);
    if (at == std::string::npos)
        return "<no value>";
    at += 7;
    return yaml.substr(at, yaml.find('\n', at) - at);
}

// C11 §6.4.4.1: the type of an integer constant is the first of its list that holds the
// value.  A decimal constant never becomes unsigned; a hexadecimal one may.
TEST_F(TranslateTestAvr, IntegerConstantTypes)
{
    std::string yaml = CompileToYaml(R"(
        int s1 = sizeof(40000);       /* long */
        int s2 = sizeof(0x8000);      /* unsigned int */
        int s3 = sizeof(32767);       /* int */
        int s4 = sizeof(65535u);      /* unsigned int */
        int s5 = sizeof(65536u);      /* unsigned long */
        int s6 = sizeof(0x7fffffff);  /* long */
        int s7 = sizeof(0x80000000);  /* unsigned long */
        int s8 = sizeof(2147483648);  /* long long */
        int c1 = -1 < 0x8000;         /* -1 converts to unsigned int: 0 */
        int c2 = -1 < 40000;          /* compared as long: 1 */
        long v = 40000;
    )");
    EXPECT_EQ(StaticValue(yaml, "s1"), "4");
    EXPECT_EQ(StaticValue(yaml, "s2"), "2");
    EXPECT_EQ(StaticValue(yaml, "s3"), "2");
    EXPECT_EQ(StaticValue(yaml, "s4"), "2");
    EXPECT_EQ(StaticValue(yaml, "s5"), "4");
    EXPECT_EQ(StaticValue(yaml, "s6"), "4");
    EXPECT_EQ(StaticValue(yaml, "s7"), "4");
    EXPECT_EQ(StaticValue(yaml, "s8"), "8");
    EXPECT_EQ(StaticValue(yaml, "c1"), "0");
    EXPECT_EQ(StaticValue(yaml, "c2"), "1");
    EXPECT_EQ(StaticValue(yaml, "v"), "40000");
}

// The same rule on a 32-bit int: 0x80000000 is an unsigned int, not a long.
TEST_F(TranslateTestX86, HexConstantTakesUnsignedInt)
{
    std::string yaml = CompileToYaml(R"(
        int s = sizeof(0x80000000);
        int c = -1 < 0x80000000;
        int d = sizeof(2147483648);
    )");
    EXPECT_EQ(StaticValue(yaml, "s"), "4");
    EXPECT_EQ(StaticValue(yaml, "c"), "0");
    EXPECT_EQ(StaticValue(yaml, "d"), "8");
}

// A 16-bit int is a 2-byte static initializer.
TEST_F(TranslateTestAvr, StaticIntIsTwoBytes)
{
    std::string yaml = CompileToYaml("int i = -2; unsigned u = 65535; char *p = (char *)0x1234;");
    EXPECT_NE(yaml.find("kind: i16\n      value: -2\n"), std::string::npos) << yaml;
    EXPECT_NE(yaml.find("kind: u16\n      value: 65535\n"), std::string::npos) << yaml;
    EXPECT_NE(yaml.find("kind: i16\n      value: 4660\n"), std::string::npos) << yaml;
}

// C11 §6.3.1.1p2: int cannot hold every unsigned short, so it promotes to unsigned int.
TEST_F(TranslateTestAvr, UnsignedShortPromotesToUnsignedInt)
{
    std::string yaml = CompileToYaml("unsigned f(unsigned short a) { return a + 1; }");
    EXPECT_NE(yaml.find("op: add_unsigned"), std::string::npos) << yaml;
}

TEST_F(TranslateTestX86, UnsignedShortPromotesToInt)
{
    std::string yaml = CompileToYaml("unsigned f(unsigned short a) { return a + 1; }");
    EXPECT_EQ(yaml.find("op: add_unsigned"), std::string::npos) << yaml;
}

// sizeof yields size_t, an unsigned int: -sizeof(int) is 65534, not -2.
TEST_F(TranslateTestAvr, SizeofIsUnsignedInt)
{
    std::string yaml = CompileToYaml("long n = (long)-sizeof(int); int z = -1 < sizeof(int);");
    EXPECT_EQ(StaticValue(yaml, "n"), "65534");
    EXPECT_EQ(StaticValue(yaml, "z"), "0");
}

// A pointer difference is a ptrdiff_t, an int; widening it to long is a sign extension.
TEST_F(TranslateTestAvr, PointerDifferenceIsInt)
{
    Tac_TopLevel *tac = CompileUnit("long f(char *p, char *q) { return p - q; }");
    const Tac_Type *t = SymbolType(tac, "f", "%0");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->kind, TAC_TYPE_INT);
    tac_free_toplevel(tac);
}

TEST_F(TranslateTestAvr, PointerDifferenceWidensToLong)
{
    std::string yaml = CompileToYaml("long g(char *p, char *q) { return p - q; }");
    EXPECT_NE(yaml.find("kind: sign_extend"), std::string::npos) << yaml;
}

// A long index converts to ptrdiff_t, the int as wide as a pointer.
TEST_F(TranslateTestAvr, LongIndexTruncatesToInt)
{
    std::string yaml = CompileToYaml("char f(char *p, long i) { return p[i]; }");
    EXPECT_NE(yaml.find("kind: truncate"), std::string::npos) << yaml;
}

// A pointer is narrower than long: converting it to long zero-extends, as clang's
// ptrtoint does, and converting a long to a pointer truncates.
TEST_F(TranslateTestAvr, PointerToLongZeroExtends)
{
    std::string yaml = CompileToYaml("long f(char *p) { return (long)p; }");
    EXPECT_NE(yaml.find("kind: zero_extend"), std::string::npos) << yaml;
}

TEST_F(TranslateTestAvr, LongToPointerTruncates)
{
    std::string yaml = CompileToYaml("char *g(long v) { return (char *)v; }");
    EXPECT_NE(yaml.find("kind: truncate"), std::string::npos) << yaml;
}

// C11 §6.7.2.2p2: an enumerator is representable as an int; one that fits only
// unsigned int wraps, as gcc and clang accept it.
TEST_F(TranslateTestAvr, EnumConstantRange)
{
    std::string yaml = CompileToYaml("enum e { A = 65535, B = -32768 }; int a = A, b = B;");
    EXPECT_EQ(StaticValue(yaml, "a"), "-1");
    EXPECT_EQ(StaticValue(yaml, "b"), "-32768");
}

TEST_F(TranslateTestAvr, EnumConstantTooWide)
{
    EXPECT_DEATH(CompileToYaml("enum e { C = 65536 };"), "does not fit type int");
}

// C11 §6.8.4.2p5: case values are compared after conversion to the promoted controlling
// type, so 0 and 65536 are duplicates.
TEST_F(TranslateTestAvr, SwitchDuplicateAfterConversion)
{
    EXPECT_DEATH(CompileToYaml(R"(
        int f(int x) { switch (x) { case 0: return 1; case 65536: return 2; } return 0; }
    )"),
                 "duplicate case value");
}

// A multi-character constant has type int: two characters fit, three do not.
TEST_F(TranslateTestAvr, CharacterConstantWidth)
{
    std::string yaml = CompileToYaml("int ab = 'ab';");
    EXPECT_EQ(StaticValue(yaml, "ab"), "24930");
}

TEST_F(TranslateTestAvr, CharacterConstantTooLong)
{
    EXPECT_DEATH(CompileToYaml("int abc = 'abc';"), "character constant too long");
}

// double and long double are IEEE single, like float: every double constant is rounded
// once to binary32, whether from the source, folded, or converted from an integer.
TEST_F(TranslateTestAvr, DoubleIsSingle)
{
    std::string yaml = CompileToYaml(R"(
        double d1 = 0.1;
        double d2 = 16777217.0;
        double d3 = 16777217L;
        double d4 = 1e39;
        double d5 = 1.0 / 3;
        float f1 = 1.0f / 3;
        int e1 = (long double)0.1 == 0.1;
        int e2 = 0.1f == 0.1;
        int e3 = 16777217.0 == 16777216.0;
    )");
    EXPECT_EQ(StaticValue(yaml, "d1"), "0x1.99999ap-4");
    EXPECT_EQ(StaticValue(yaml, "d2"), "0x1p+24");
    EXPECT_EQ(StaticValue(yaml, "d3"), "0x1p+24");
    EXPECT_EQ(StaticValue(yaml, "d4"), "inf");
    EXPECT_EQ(StaticValue(yaml, "d5"), "0x1.555556p-2");
    EXPECT_EQ(StaticValue(yaml, "f1"), "0x1.555556p-2");
    EXPECT_EQ(StaticValue(yaml, "e1"), "1");
    EXPECT_EQ(StaticValue(yaml, "e2"), "1");
    EXPECT_EQ(StaticValue(yaml, "e3"), "1");
}

// A double literal is rounded to binary32 once, by strtof: 1.00000005960464477539062501
// is just above the halfway point between 1 and 1 + 2^-23, so it rounds up; rounded first
// to binary64 it lands exactly on the halfway point, and then to even, down to 1.
TEST_F(TranslateTestAvr, DoubleLiteralRoundsOnce)
{
    std::string yaml = CompileToYaml("double d = 1.00000005960464477539062501;");
    EXPECT_EQ(StaticValue(yaml, "d"), "0x1.000002p+0");
}

// Folded double arithmetic in a function is rounded too.
TEST_F(TranslateTestAvr, DoubleFoldIsSingle)
{
    std::string yaml = CompileToYaml("double f(void) { return 1.0 / 3.0; }");
    EXPECT_NE(yaml.find("value: 0x1.555556p-2"), std::string::npos) << yaml;
}

// Elsewhere double stays binary64.
TEST_F(TranslateTestX86, DoubleIsBinary64)
{
    std::string yaml = CompileToYaml("double d = 0.1; double e = 16777217L;");
    EXPECT_EQ(StaticValue(yaml, "d"), "0x1.999999999999ap-4");
    EXPECT_EQ(StaticValue(yaml, "e"), "0x1.000001p+24");
}
