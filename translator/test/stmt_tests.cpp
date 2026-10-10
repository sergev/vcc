#include <cstdarg>

#include "translate_test.h"

extern "C" {
#include "srcloc.h"

[[noreturn]] void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    diag_vreport(diag_loc, "error", message, ap);
    va_end(ap);
    exit(1);
}
};

// ---------------------------------------------------------------------------
// goto statement — task #4
// ---------------------------------------------------------------------------

TEST_F(TranslateTest, GotoEmitsJump)
{
    std::string yaml = CompileToYaml("int f(void) { goto end; end: return 0; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: jump
      target: %L0
    - instruction:
      kind: label
      name: %L0
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 0
)");
}

// ---------------------------------------------------------------------------
// Labeled statement — task #5
// ---------------------------------------------------------------------------

TEST_F(TranslateTest, LabeledStatementEmitsLabel)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 1; loop: x = 2; return x; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %x
    - instruction:
      kind: label
      name: %L0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst:
        kind: var
        name: %x
    - instruction:
      kind: return
      src:
        kind: var
        name: %x
)");
}

// ---------------------------------------------------------------------------
// Function calls — task #5
// ---------------------------------------------------------------------------

// Void call with no arguments emits fun_call with no args and no dst.
TEST_F(TranslateTest, CallVoidNoArgs)
{
    std::string yaml = CompileToYaml("void g(void); void f(void) { g(); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: g
)");
}

// Non-void call with no arguments emits fun_call with a dst temp, then return.
TEST_F(TranslateTest, CallReturningInt)
{
    std::string yaml = CompileToYaml("int g(void); int f(void) { return g(); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: g
      dst:
        kind: var
        name: %0
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
)");
}

// Call with two integer arguments emits an args list in the fun_call.
TEST_F(TranslateTest, CallWithArgs)
{
    std::string yaml = CompileToYaml("int add(int a, int b); int f(void) { return add(1, 2); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: add
      args:
        - val:
          kind: constant
          const:
            kind: int
            value: 1
        - val:
          kind: constant
          const:
            kind: int
            value: 2
      dst:
        kind: var
        name: %0
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
)");
}

// Call result used inside a binary expression: fun_call → t.0, binary → t.1.
TEST_F(TranslateTest, CallResultInExpression)
{
    std::string yaml = CompileToYaml("int g(void); int f(void) { return g() + 1; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: g
      dst:
        kind: var
        name: %0
    - instruction:
      kind: binary
      op: add
      src1:
        kind: var
        name: %0
      src2:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %1
    - instruction:
      kind: return
      src:
        kind: var
        name: %1
)");
}

// Indirect call through a function pointer parameter: fun_name is the pointer variable.
TEST_F(TranslateTest, IndirectCallViaFunctionPointer)
{
    std::string yaml = CompileToYaml("int f(int (*fp)(int)) { return fp(42); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %fp
  body:
    - instruction:
      kind: fun_call
      fun_name: %fp
      indirect: true
      args:
        - val:
          kind: constant
          const:
            kind: int
            value: 42
      dst:
        kind: var
        name: %0
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
)");
}

// (*fp)(42): dereferencing a function pointer yields a designator that decays back to the
// same pointer, so (*fp)(42) lowers identically to fp(42) — the DEREF is stripped and the
// call goes directly through the pointer variable (no LOAD).
TEST_F(TranslateTest, IndirectCallViaExplicitDeref)
{
    std::string yaml = CompileToYaml("int f(int (*fp)(int)) { return (*fp)(42); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %fp
  body:
    - instruction:
      kind: fun_call
      fun_name: %fp
      indirect: true
      args:
        - val:
          kind: constant
          const:
            kind: int
            value: 42
      dst:
        kind: var
        name: %0
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
)");
}

// A function name used as a value (here a call argument) decays to a pointer-to-function:
// its address is materialized with GET_ADDRESS rather than passing the bare name (which the
// backend would load mem[name] from).  The directly-called function keeps a plain fun_name.
TEST_F(TranslateTest, FunctionNameDecaysToAddress)
{
    std::string yaml = CompileToYaml("int g(int (*fp)(int)); int h(int x) { return g(h); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: h
  global: true
  params:
    - param: %x
  body:
    - instruction:
      kind: get_address
      src:
        kind: var
        name: h
      dst:
        kind: var
        name: %0
    - instruction:
      kind: fun_call
      fun_name: g
      args:
        - val:
          kind: var
          name: %0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: return
      src:
        kind: var
        name: %1
)");
}

// ---------------------------------------------------------------------------
// Global linkage — task #4
// ---------------------------------------------------------------------------

// Static function definition must emit global=false (regression: was hardcoded true).
TEST_F(TranslateTest, StaticFunctionGlobalFalse)
{
    std::string yaml = CompileToYaml("static int f(void) { return 0; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: false
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 0
)");
}

// ---------------------------------------------------------------------------
// && || ! in a condition: jumps, not a 0/1 value tested again; a loop whose
// condition is not simple entered by a jump to its test, not by a copy of it.
// BESM-6 keeps the value and the copy, its code unchanged.
// ---------------------------------------------------------------------------

static int count(const std::string &s, const std::string &what)
{
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
        n++;
    return n;
}

TEST_F(TranslateTestRiscv, IfAndOrAsJumps)
{
    std::string yaml = CompileToYaml("int f(int a, int b, int c) { if ((a && b) || !c) return 1; return 2; }");
    EXPECT_EQ(0, count(yaml, "kind: copy")) << yaml;
    EXPECT_EQ(0, count(yaml, "op: not_equal")) << yaml;
    EXPECT_EQ(0, count(yaml, "op: not\n")) << yaml;
    EXPECT_EQ(3, count(yaml, "kind: jump_if_zero") + count(yaml, "kind: jump_if_not_zero")) << yaml;
}

TEST_F(TranslateTestRiscv, LoopEnteredAtItsTest)
{
    rotate           = true;
    std::string yaml = CompileToYaml(R"(int g(int);
int f(int *p)
{
    int n = 0;
    while (*p && g(*p))
        n++, p++;
    return n;
}
)");
    // One copy of the test, at the bottom: a single call of g.
    EXPECT_EQ(1, count(yaml, "fun_name: g")) << yaml;
    EXPECT_NE(std::string::npos, yaml.find("kind: jump\n")) << yaml;
}

TEST_F(TranslateTestRiscv, SimpleLoopKeepsItsGuard)
{
    rotate           = true;
    std::string yaml = CompileToYaml("int f(int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }");
    EXPECT_EQ(2, count(yaml, "op: less_than")) << yaml;
    EXPECT_EQ(0, count(yaml, "kind: jump\n")) << yaml;
}

TEST_F(TranslateTest, Besm6KeepsLogicalValues)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { if (a && b) return 1; return 2; }");
    EXPECT_EQ(1, count(yaml, "kind: copy")) << yaml;
    EXPECT_EQ(1, count(yaml, "op: not_equal")) << yaml;
}
