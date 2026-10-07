//
// The frontend on wasm32's combination no earlier target had: ILP32 with a signed plain
// char and a binary128 long double, every struct result returned by the backend rather
// than through a hidden argument.  The bit-field layouts are in bitfield_tests.cpp.
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

// Plain char is signed (clang's wasm32), and converts to a 32-bit long by sign extension.
TEST_F(TranslateTestWasm32, PlainCharIsSigned)
{
    std::string yaml = CompileToYaml(R"(
        int c = '\xff';
        long l = (char)-1;
        unsigned long ul = (char)-1;
        int n = (char)-1 < 0;
        int sl = sizeof(long);
        int f(void) { return '\x80'; }
    )");
    EXPECT_EQ(StaticValue(yaml, "c"), "-1");
    EXPECT_EQ(StaticValue(yaml, "l"), "-1");
    EXPECT_EQ(StaticValue(yaml, "ul"), "4294967295");
    EXPECT_EQ(StaticValue(yaml, "n"), "1");
    EXPECT_EQ(StaticValue(yaml, "sl"), "4");
    EXPECT_NE(yaml.find("value: -128"), std::string::npos) << yaml;
}

// long double is binary128, 16 bytes aligned to 16, wider than double.
TEST_F(TranslateTestWasm32, LongDoubleIsBinary128)
{
    std::string yaml = CompileToYaml(R"(
        int s1 = sizeof(long double);
        int a1 = _Alignof(long double);
        int e1 = (long double)0.1 == 0.1;
        int e2 = 0.1L == 0.1;
    )");
    EXPECT_EQ(StaticValue(yaml, "s1"), "16");
    EXPECT_EQ(StaticValue(yaml, "a1"), "16");
    EXPECT_EQ(StaticValue(yaml, "e1"), "1");
    EXPECT_EQ(StaticValue(yaml, "e2"), "0");
}

// Every scalar aligned to its size; the offsets and sizes are clang's.
TEST_F(TranslateTestWasm32, StructLayout)
{
    Tac_TopLevel *tac = CompileUnit(R"(
        struct A { char c; int i; long l; double d; };
        struct B { char c; long long ll; char e; };
        struct D { char c; float f; short s; };
        union U { char c; long l; };
        struct C { char a, b, c; };
        struct E { char c; struct C in; long double ld; };
        int f(struct A a, struct B b, struct D d, union U u, struct E e) { return 0; }
    )");
    const Tac_Type *a = SymbolType(tac, "f", "%a");
    EXPECT_EQ(TypeStr(a), "struct A(24,8)");
    EXPECT_EQ(Members(a), "c@0:schar i@4:int l@8:long d@16:double");
    const Tac_Type *b = SymbolType(tac, "f", "%b");
    EXPECT_EQ(TypeStr(b), "struct B(24,8)");
    EXPECT_EQ(Members(b), "c@0:schar ll@8:long_long e@16:schar");
    const Tac_Type *d = SymbolType(tac, "f", "%d");
    EXPECT_EQ(TypeStr(d), "struct D(12,4)");
    EXPECT_EQ(Members(d), "c@0:schar f@4:float s@8:short");
    EXPECT_EQ(TypeStr(SymbolType(tac, "f", "%u")), "union U(4,4)");
    const Tac_Type *e = SymbolType(tac, "f", "%e");
    EXPECT_EQ(TypeStr(e), "struct E(32,16)");
    EXPECT_EQ(Members(e), "c@0:schar in@1:struct C(3,1) ld@16:long_double");
    tac_free_toplevel(tac);
}

// A struct result, returned by the backend, used where a value is expected: the call
// always has a destination and no hidden argument.
TEST_F(TranslateTestWasm32, StructResultInExpressions)
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
}
