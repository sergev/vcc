#include "translate_test.h"

// Typed TAC: every parameter, local and temporary carries its type, functions and
// calls carry their function types, and a unit lists the names it uses but does not
// define.  x86_64 widths make int, long and char distinguishable.

static bool Has(const std::string &yaml, const char *text)
{
    return yaml.find(text) != std::string::npos;
}

TEST_F(TranslateTestX86, TypedSymbols)
{
    std::string yaml =
        CompileUnitToTypedYaml("long f(int a, char *p) { long x = a; return x + p[1]; }");
    EXPECT_TRUE(Has(yaml, R"(
  type: fn(int, *schar) -> long
  params:
    - param: %a
      type: int
    - param: %p
      type: *schar
  locals:
    - local: %x
      type: long
    - local: %0
      type: long
    - local: %1
      type: long
    - local: %2
      type: *schar
    - local: %3
      type: schar
    - local: %4
      type: long
    - local: %5
      type: long
  body:
)")) << yaml;
}

TEST_F(TranslateTestX86, CallCarriesCalleeType)
{
    std::string yaml = CompileUnitToTypedYaml(R"(
        int printf(const char *fmt, ...);
        double (*fp)(float);
        void g(void) { printf("x", 1.0); fp(2.0f); }
    )");
    EXPECT_TRUE(Has(yaml, "      fun_type: fn(*schar, ...) -> int\n")) << yaml;
    EXPECT_TRUE(Has(yaml, "      indirect: true\n")) << yaml;
    EXPECT_TRUE(Has(yaml, "      fun_type: fn(float) -> double\n")) << yaml;
    EXPECT_TRUE(Has(yaml, R"(
  kind: extern
  name: printf
  type:
    kind: fun_type
    param_types:
      - type:
        kind: pointer
        target:
          kind: schar
    ret_type:
      kind: int
    variadic: true
)")) << yaml;
}

// Referenced but undefined: listed, in name order, with the declared type (an
// incomplete struct has size 0).  Unreferenced or defined later in the unit: not.
TEST_F(TranslateTestX86, ExternListsUsedUndefinedNames)
{
    std::string yaml = CompileUnitToTypedYaml(R"(
        extern int used, unused;
        int defined_later(void);
        int helper(int);
        struct S;
        extern struct S opaque;
        int f(void) { return used + helper(1) + defined_later() + (&opaque != 0); }
        int defined_later(void) { return 2; }
    )");
    std::string externs = yaml.substr(yaml.find("  kind: extern"));
    EXPECT_EQ(externs, R"(  kind: extern
  name: helper
  type:
    kind: fun_type
    param_types:
      - type:
        kind: int
    ret_type:
      kind: int
- toplevel:
  kind: extern
  name: opaque
  type:
    kind: structure
    tag: S
    size: 0
- toplevel:
  kind: extern
  name: used
  type:
    kind: int
)");
}
