//
// The frontend on MSP430's combinations no earlier target had: unsigned plain char with
// a 16-bit int, a binary64 double with a 16-bit int, and every struct returned in memory.
// The 16-bit int itself is AVR's, covered in avr_tests.cpp.
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

// Plain char is unsigned and still promotes to int, which holds 0..255.
TEST_F(TranslateTestMsp430, PlainCharIsUnsigned)
{
    std::string yaml = CompileToYaml(R"(
        int c = '\xff';
        int i = (char)-1;
        int n = (char)-1 < 0;
        int s = (signed char)-1;
        int w = sizeof('a');
        int p = sizeof(+(char)0);
    )");
    EXPECT_EQ(StaticValue(yaml, "c"), "255");
    EXPECT_EQ(StaticValue(yaml, "i"), "255");
    EXPECT_EQ(StaticValue(yaml, "n"), "0");
    EXPECT_EQ(StaticValue(yaml, "s"), "-1");
    EXPECT_EQ(StaticValue(yaml, "w"), "2");
    EXPECT_EQ(StaticValue(yaml, "p"), "2");
}

// A plain char widens by zero extension, and compares as an int after promotion.
TEST_F(TranslateTestMsp430, PlainCharZeroExtends)
{
    std::string yaml = CompileToYaml("int f(char c) { return c; }");
    EXPECT_NE(yaml.find("kind: zero_extend"), std::string::npos) << yaml;
    EXPECT_EQ(yaml.find("kind: sign_extend"), std::string::npos) << yaml;
}

TEST_F(TranslateTestMsp430, SwitchOnPlainChar)
{
    std::string yaml = CompileToYaml(R"(
        int f(char c) { switch (c) { case '\xff': return 1; case 255 + 256: return 2; } return 0; }
    )");
    EXPECT_NE(yaml.find("value: 255"), std::string::npos) << yaml;
}

// double and long double are binary64, folded exactly as the host's double, beside a
// 16-bit int and a 32-bit long.
TEST_F(TranslateTestMsp430, DoubleIsBinary64)
{
    std::string yaml = CompileToYaml(R"(
        double d1 = 0.1;
        double d2 = 16777217L;
        double d3 = 2147483647L;
        double d4 = -32768;
        double d5 = 65535u;
        int e1 = (long double)0.1 == 0.1;
        int s1 = sizeof(double) + sizeof(long double);
        int i1 = (int)-32768.9;
        unsigned u1 = (unsigned)65535.5;
        long g1 = (long)2147483647.0;
        double h = 9007199254740993LL;
    )");
    EXPECT_EQ(StaticValue(yaml, "d1"), "0x1.999999999999ap-4");
    EXPECT_EQ(StaticValue(yaml, "d2"), "0x1.000001p+24");
    EXPECT_EQ(StaticValue(yaml, "d3"), "0x1.fffffffcp+30");
    EXPECT_EQ(StaticValue(yaml, "d4"), "-0x1p+15");
    EXPECT_EQ(StaticValue(yaml, "d5"), "0x1.fffep+15");
    EXPECT_EQ(StaticValue(yaml, "e1"), "1");
    EXPECT_EQ(StaticValue(yaml, "s1"), "16");
    EXPECT_EQ(StaticValue(yaml, "i1"), "-32768");
    EXPECT_EQ(StaticValue(yaml, "u1"), "65535");
    EXPECT_EQ(StaticValue(yaml, "g1"), "2147483647");
    EXPECT_EQ(StaticValue(yaml, "h"), "0x1p+53");
}

// A struct result, always through a hidden pointer, used where a value is expected.
TEST_F(TranslateTestMsp430, StructResultInExpressions)
{
    std::string yaml = CompileToYaml(R"(
        struct P { int x; };
        struct P make(int x);
        int take(struct P p);
        int f(int c)
        {
            struct P a, b;
            a = b = make(1);
            b = c ? make(2) : a;
            return take(make(3)) + a.x + b.x + make(4).x;
        }
    )");
    EXPECT_NE(yaml.find("name: make"), std::string::npos) << yaml;
}
