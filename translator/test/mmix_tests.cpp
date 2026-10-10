//
// The frontend on MMIX's combinations no earlier target had: LP64 with an 8-byte long
// double, signed plain char with a 64-bit long, the first big-endian byte-addressed
// target, and every struct result returned by the backend rather than through a hidden
// argument.  The layouts are in type_tests.cpp.
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

// Plain char is signed: a one-byte character constant over 127 is negative (C11
// §6.4.4.4p10, as GCC has it), and a char converts to long by sign extension.
TEST_F(TranslateTestMmix, PlainCharIsSigned)
{
    std::string yaml = CompileToYaml(R"(
        int c = '\xff';
        int o = '\377' == -1;
        int a = 'A';
        long l = (char)-1;
        unsigned long ul = (char)-1;
        int n = (char)-1 < 0;
        int w = sizeof('a');
        int p = sizeof(+(char)0);
        int f(void) { return '\x80'; }
    )");
    EXPECT_EQ(StaticValue(yaml, "c"), "-1");
    EXPECT_EQ(StaticValue(yaml, "o"), "1");
    EXPECT_EQ(StaticValue(yaml, "a"), "65");
    EXPECT_EQ(StaticValue(yaml, "l"), "-1");
    EXPECT_EQ(StaticValue(yaml, "ul"), "18446744073709551615");
    EXPECT_EQ(StaticValue(yaml, "n"), "1");
    EXPECT_EQ(StaticValue(yaml, "w"), "4");
    EXPECT_EQ(StaticValue(yaml, "p"), "4");
    EXPECT_NE(yaml.find("value: -128"), std::string::npos) << yaml;
}

// A wide or multi-character constant is not a char: L'\xff' and '\xff\xff' stay positive.
TEST_F(TranslateTestMmix, WideAndMultiCharConstants)
{
    std::string yaml = CompileToYaml(R"(
        int wc = L'\xff';
        int mc = '\xff\xff';
        int ab = 'ab';
        long abcd = 'abcd';
    )");
    EXPECT_EQ(StaticValue(yaml, "wc"), "255");
    EXPECT_EQ(StaticValue(yaml, "mc"), "65535");
    EXPECT_EQ(StaticValue(yaml, "ab"), "24930");
    EXPECT_EQ(StaticValue(yaml, "abcd"), "1633837924");
}

// A plain char widens by sign extension, to int and to long.
TEST_F(TranslateTestMmix, PlainCharSignExtends)
{
    std::string yaml = CompileToYaml("long f(char c) { return c; }");
    EXPECT_NE(yaml.find("kind: sign_extend"), std::string::npos) << yaml;
    EXPECT_EQ(yaml.find("kind: zero_extend"), std::string::npos) << yaml;
}

TEST_F(TranslateTestMmix, SwitchOnPlainChar)
{
    std::string yaml = CompileToYaml(R"(
        int f(char c) { switch (c) { case '\xff': return 1; case 'a': return 2; } return 0; }
    )");
    EXPECT_NE(yaml.find("value: -1"), std::string::npos) << yaml;
    EXPECT_NE(yaml.find("value: 97"), std::string::npos) << yaml;
}

// long double is binary64 beside a 64-bit long: 8 bytes, aligned to 8, folded and
// converted exactly as double.  A constant that is not folded keeps its exact binary128
// value in TAC, as on ARM32, and the backend rounds it once when it emits it.
TEST_F(TranslateTestMmix, LongDoubleIsBinary64)
{
    std::string yaml = CompileToYaml(R"(
        int s1 = sizeof(long double);
        int a1 = _Alignof(long double);
        int e1 = (long double)0.1 == 0.1;
        int e2 = 0.1L == 0.1;
        long double ld3 = 1.0L / 3;
        double d1 = 9007199254740993L;
        long double x1 = 9007199254740993L;
        long g1 = (long)1e18L;
        unsigned long u1 = (unsigned long)18446744073709549568.0L;
        long g2 = (long)-9223372036854775808.0L;
    )");
    EXPECT_EQ(StaticValue(yaml, "s1"), "8");
    EXPECT_EQ(StaticValue(yaml, "a1"), "8");
    EXPECT_EQ(StaticValue(yaml, "e1"), "1");
    EXPECT_EQ(StaticValue(yaml, "e2"), "1");
    EXPECT_EQ(StaticValue(yaml, "ld3"), "0x1.5555555555555p-2");
    EXPECT_EQ(StaticValue(yaml, "d1"), "0x1p+53");
    EXPECT_EQ(StaticValue(yaml, "x1"), "0x1.00000000000008p+53");
    EXPECT_EQ(StaticValue(yaml, "g1"), "1000000000000000000");
    EXPECT_EQ(StaticValue(yaml, "u1"), "18446744073709549568");
    EXPECT_EQ(StaticValue(yaml, "g2"), "-9223372036854775808");
}

// A static initializer of mixed widths carries one typed item per member, padding as
// zero runs: the byte order is the backend's directives', not the frontend's.
TEST_F(TranslateTestMmix, MixedWidthStaticInit)
{
    std::string yaml = CompileToYaml(R"(
        struct M { char c; short s; int i; long l; float f; double d; } m = { 1, 2, 3, 4, 5, 6 };
    )");
    size_t at        = yaml.find("  name: m\n");
    ASSERT_NE(at, std::string::npos) << yaml;
    std::string init = yaml.substr(yaml.find("init:", at));
    std::string kinds;
    for (size_t k = init.find("kind: "); k != std::string::npos; k = init.find("kind: ", k + 1))
        kinds += init.substr(k + 6, init.find('\n', k) - k - 6) + " ";
    EXPECT_EQ(kinds, "i8 zero i16 i32 i64 float zero double ") << yaml;
}

// A struct result, returned by the backend, used where a value is expected: the call
// always has a destination and no hidden argument.
TEST_F(TranslateTestMmix, StructResultInExpressions)
{
    std::string yaml = CompileToYaml(R"(
        struct P { long x, y, z; };
        struct P make(long x);
        long take(struct P p);
        long f(int c)
        {
            struct P a, b;
            a = b = make(1);
            b = c ? make(2) : a;
            return take(make(3)) + a.x + b.x + make(4).x;
        }
    )");
    EXPECT_EQ(yaml.find("%.ret"), std::string::npos) << yaml;
    size_t calls = 0;
    for (size_t at = yaml.find("name: make"); at != std::string::npos;
         at        = yaml.find("name: make", at + 1))
        calls++;
    EXPECT_EQ(calls, 4u) << yaml;
}
