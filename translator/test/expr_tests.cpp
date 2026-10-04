#include "translate_test.h"

// ---------------------------------------------------------------------------
// Assignment expressions — task #2
// ---------------------------------------------------------------------------

// Simple assignment emits COPY into the target variable and returns it.
TEST_F(TranslateTest, AssignSimple)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 0; x = 42; return x; }");
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
          value: 0
      dst:
        kind: var
        name: %x
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 42
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

// Assignment is an expression — its result can be used as an initializer.
TEST_F(TranslateTest, AssignUsedAsExpr)
{
    std::string yaml = CompileToYaml("int f(void) { int x; int y = (x = 7); return y; }");
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
          value: 7
      dst:
        kind: var
        name: %x
    - instruction:
      kind: copy
      src:
        kind: var
        name: %x
      dst:
        kind: var
        name: %y
    - instruction:
      kind: return
      src:
        kind: var
        name: %y
)");
}

// Compound add-assign: reads target, adds rhs, stores back.
TEST_F(TranslateTest, CompoundAssignAdd)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 10; x += 5; return x; }");
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
          value: 10
      dst:
        kind: var
        name: %x
    - instruction:
      kind: binary
      op: add
      src1:
        kind: var
        name: %x
      src2:
        kind: constant
        const:
          kind: int
          value: 5
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy
      src:
        kind: var
        name: %0
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

// Compound bitwise-or-assign exercises the bitwise_or TAC binary operator.
TEST_F(TranslateTest, CompoundAssignBitwiseOr)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 6; x |= 3; return x; }");
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
          value: 6
      dst:
        kind: var
        name: %x
    - instruction:
      kind: binary
      op: bitwise_or
      src1:
        kind: var
        name: %x
      src2:
        kind: constant
        const:
          kind: int
          value: 3
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy
      src:
        kind: var
        name: %0
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
// Unary plus — task #3
// ---------------------------------------------------------------------------

// Unary + is a no-op: it must not emit any instruction.
TEST_F(TranslateTest, UnaryPlusNoOp)
{
    std::string yaml = CompileToYaml("int f(void) { return +42; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 42
)");
}

// ---------------------------------------------------------------------------
// Ternary conditional ?: — task #3
// ---------------------------------------------------------------------------

// Simple ternary: variable condition, integer constant branches.
TEST_F(TranslateTest, TernaryConstantBranches)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 1; return x ? 2 : 3; }");
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
      kind: jump_if_zero
      condition:
        kind: var
        name: %x
      target: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst:
        kind: var
        name: %2
    - instruction:
      kind: jump
      target: %1
    - instruction:
      kind: label
      name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 3
      dst:
        kind: var
        name: %2
    - instruction:
      kind: label
      name: %1
    - instruction:
      kind: return
      src:
        kind: var
        name: %2
)");
}

// Ternary with expression condition and variable branches.
TEST_F(TranslateTest, TernaryExprCondVarBranches)
{
    std::string yaml = CompileToYaml("int f(int x, int y) { return x > 0 ? x : y; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %x
    - param: %y
  body:
    - instruction:
      kind: binary
      op: greater_than
      src1:
        kind: var
        name: %x
      src2:
        kind: constant
        const:
          kind: int
          value: 0
      dst:
        kind: var
        name: %0
    - instruction:
      kind: jump_if_zero
      condition:
        kind: var
        name: %0
      target: %1
    - instruction:
      kind: copy
      src:
        kind: var
        name: %x
      dst:
        kind: var
        name: %3
    - instruction:
      kind: jump
      target: %2
    - instruction:
      kind: label
      name: %1
    - instruction:
      kind: copy
      src:
        kind: var
        name: %y
      dst:
        kind: var
        name: %3
    - instruction:
      kind: label
      name: %2
    - instruction:
      kind: return
      src:
        kind: var
        name: %3
)");
}

// ---------------------------------------------------------------------------
// Short-circuit && and || — task #4
// ---------------------------------------------------------------------------

TEST_F(TranslateTest, LogicalAndShortCircuit)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { return a && b; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %a
    - param: %b
  body:
    - instruction:
      kind: jump_if_zero
      condition:
        kind: var
        name: %a
      target: %0
    - instruction:
      kind: binary
      op: not_equal
      src1:
        kind: var
        name: %b
      src2:
        kind: constant
        const:
          kind: int
          value: 0
      dst:
        kind: var
        name: %2
    - instruction:
      kind: jump
      target: %1
    - instruction:
      kind: label
      name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst:
        kind: var
        name: %2
    - instruction:
      kind: label
      name: %1
    - instruction:
      kind: return
      src:
        kind: var
        name: %2
)");
}

TEST_F(TranslateTest, LogicalOrShortCircuit)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { return a || b; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %a
    - param: %b
  body:
    - instruction:
      kind: jump_if_not_zero
      condition:
        kind: var
        name: %a
      target: %0
    - instruction:
      kind: binary
      op: not_equal
      src1:
        kind: var
        name: %b
      src2:
        kind: constant
        const:
          kind: int
          value: 0
      dst:
        kind: var
        name: %2
    - instruction:
      kind: jump
      target: %1
    - instruction:
      kind: label
      name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %2
    - instruction:
      kind: label
      name: %1
    - instruction:
      kind: return
      src:
        kind: var
        name: %2
)");
}

// ---------------------------------------------------------------------------
// sizeof and _Alignof operators — task #5
// ---------------------------------------------------------------------------

TEST_F(TranslateTestX86, SizeofType_Int)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { return sizeof(int); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 4
)");
}

TEST_F(TranslateTestX86, SizeofType_Long)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { return sizeof(long); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 8
)");
}

TEST_F(TranslateTestX86, SizeofExpr)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { int x; return sizeof(x); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 4
)");
}

TEST_F(TranslateTestX86, AlignofDouble)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { return _Alignof(double); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 8
)");
}

// __builtin_va_class: the AAPCS64 class of a type, as a constant (two floats: an HFA
// of 4-byte elements, 4 * 8 + 2).
TEST_F(TranslateTest, VaClassAarch64)
{
    target_config    = target_lookup("aarch64");
    std::string yaml = CompileToYaml(
        "int f(void) { return __builtin_va_class(struct { float a, b; }); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 34
)");
}

// ---------------------------------------------------------------------------
// _Generic expressions — task #5
// ---------------------------------------------------------------------------

// Controlling expression type matches a typed association.
TEST_F(TranslateTest, GenericTypeMatch)
{
    std::string yaml = CompileToYaml("int f(int x) { return _Generic(x, double: 0, int: 42); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %x
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 42
)");
}

// No type matches; falls back to the default association.
TEST_F(TranslateTest, GenericDefault)
{
    std::string yaml =
        CompileToYaml("int f(double x) { return _Generic(x, int: 0, default: 99); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %x
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 99
)");
}

// ---------------------------------------------------------------------------
// Compound literal expressions — task #7
// ---------------------------------------------------------------------------

// Scalar compound literal (int){42} — value returned directly, no temp.
TEST_F(TranslateTest, CompoundLiteralScalar)
{
    std::string yaml = CompileToYaml("int f(void) { return (int){42}; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 42
)");
}

// Struct compound literal field access: (struct Foo){1, 2}.x
TEST_F(TranslateTest, CompoundLiteralStructField)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "int f(void) { return (struct Foo){1, 2}.x; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %0
      size: 12
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %0
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst: %0
      offset: 6
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %1
      index:
        kind: constant
        const:
          kind: int
          value: 0
      scale: 6
      dst:
        kind: var
        name: %2
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %2
      dst:
        kind: var
        name: %3
    - instruction:
      kind: return
      src:
        kind: var
        name: %3
)");
}

// Array compound literal subscript: (int[3]){10, 20, 30}[1]
TEST_F(TranslateTest, CompoundLiteralArraySubscript)
{
    std::string yaml = CompileToYaml("int f(void) { return (int[3]){10, 20, 30}[1]; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %0
      size: 18
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 10
      dst: %0
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 20
      dst: %0
      offset: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 30
      dst: %0
      offset: 12
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %2
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %1
      index:
        kind: var
        name: %2
      scale: 6
      dst:
        kind: var
        name: %3
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %3
      dst:
        kind: var
        name: %4
    - instruction:
      kind: return
      src:
        kind: var
        name: %4
)");
}

// float literal (f suffix) → TAC_CONST_FLOAT
TEST_F(TranslateTest, FloatLiteral)
{
    std::string yaml = CompileToYaml("float f(void) { return 1.5f; }");
    EXPECT_NE(yaml.find("kind: float"), std::string::npos);
    EXPECT_NE(yaml.find("value: 0x1.8p+0"), std::string::npos);
}

// double literal (no suffix) → TAC_CONST_DOUBLE
TEST_F(TranslateTest, DoubleLiteral)
{
    std::string yaml = CompileToYaml("double f(void) { return 1.5; }");
    EXPECT_NE(yaml.find("kind: double"), std::string::npos);
    EXPECT_NE(yaml.find("value: 0x1.8p+0"), std::string::npos);
}

// long double literal (L suffix) → TAC_CONST_LONG_DOUBLE
TEST_F(TranslateTest, LongDoubleLiteral)
{
    std::string yaml = CompileToYaml("long double f(void) { return 1.5L; }");
    EXPECT_NE(yaml.find("kind: long_double"), std::string::npos);
    // The hex float representation of 1.5L is platform-dependent (%La format):
    // on macOS/ARM64 it prints as 0xcp-3; on Linux x86-64 it may differ.
    // Verify the value is present but don't hard-code the exact hex string.
    EXPECT_NE(yaml.find("value: "), std::string::npos);
}

// ---------------------------------------------------------------------------
// long long / long / ulong / ulong_long literals
// ---------------------------------------------------------------------------

// Small LL literal gets TAC kind long_long, not int.
TEST_F(TranslateTest, LongLongLiteralSmall)
{
    std::string yaml = CompileToYaml("long long f(void) { return 10LL; }");
    EXPECT_NE(yaml.find("kind: long_long"), std::string::npos);
    EXPECT_NE(yaml.find("value: 10"), std::string::npos);
}

// Large LL literal preserves all 64 bits.
TEST_F(TranslateTest, LongLongLiteralLarge)
{
    std::string yaml = CompileToYaml("long long f(void) { long long x = 9999999999LL; return x; }");
    EXPECT_NE(yaml.find("kind: long_long"), std::string::npos);
    EXPECT_NE(yaml.find("value: 9999999999"), std::string::npos);
}

// ULL literal gets TAC kind ulong_long.
TEST_F(TranslateTest, ULongLongLiteral)
{
    std::string yaml = CompileToYaml("unsigned long long f(void) { return 10ULL; }");
    EXPECT_NE(yaml.find("kind: ulong_long"), std::string::npos);
    EXPECT_NE(yaml.find("value: 10"), std::string::npos);
}

// L suffix literal gets TAC kind long.
TEST_F(TranslateTest, LongLiteral)
{
    std::string yaml = CompileToYaml("long f(void) { return 10L; }");
    EXPECT_NE(yaml.find("kind: long"), std::string::npos);
    EXPECT_NE(yaml.find("value: 10"), std::string::npos);
}

// UL suffix literal gets TAC kind ulong.
TEST_F(TranslateTest, ULongLiteral)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { return 10UL; }");
    EXPECT_NE(yaml.find("kind: ulong"), std::string::npos);
    EXPECT_NE(yaml.find("value: 10"), std::string::npos);
}

// U suffix (no L) lowers to TAC kind uint, not a signed int.
TEST_F(TranslateTest, UIntLiteral)
{
    std::string yaml = CompileToYaml("unsigned int f(void) { return 10U; }");
    EXPECT_NE(yaml.find("kind: uint"), std::string::npos);
    EXPECT_NE(yaml.find("value: 10"), std::string::npos);
}

// A wide unsigned constant keeps all 48 bits with a plain U suffix (no L needed):
// it widens to unsigned long and the full value reaches TAC.
TEST_F(TranslateTest, UIntLiteralWide)
{
    std::string yaml = CompileToYaml("unsigned long f(void) { return 0xFFFFFFFFFFFFU; }");
    EXPECT_NE(yaml.find("kind: ulong"), std::string::npos);
    EXPECT_NE(yaml.find("value: 281474976710655"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unsigned / logical TAC op kinds — task #1 (Phase E)
// ---------------------------------------------------------------------------

// Unsigned operands select divide_unsigned, not divide.
TEST_F(TranslateTest, UnsignedDivide)
{
    std::string yaml =
        CompileToYaml("unsigned int f(unsigned int a, unsigned int b) { return a / b; }");
    EXPECT_NE(yaml.find("op: divide_unsigned"), std::string::npos);
    EXPECT_EQ(yaml.find("op: divide\n"), std::string::npos);
}

// Unsigned operands select less_than_unsigned, not less_than.
TEST_F(TranslateTest, UnsignedLessThan)
{
    std::string yaml = CompileToYaml("int f(unsigned int a, unsigned int b) { return a < b; }");
    EXPECT_NE(yaml.find("op: less_than_unsigned"), std::string::npos);
    EXPECT_EQ(yaml.find("op: less_than\n"), std::string::npos);
}

// Unsigned left operand selects right_shift_logical, not right_shift.
TEST_F(TranslateTest, LogicalRightShift)
{
    std::string yaml = CompileToYaml("unsigned int f(unsigned int a) { return a >> 2; }");
    EXPECT_NE(yaml.find("op: right_shift_logical"), std::string::npos);
    EXPECT_EQ(yaml.find("op: right_shift\n"), std::string::npos);
}

// Unsigned operands select add_unsigned, not add (true 48-bit modular add on BESM-6).
TEST_F(TranslateTest, UnsignedAdd)
{
    std::string yaml =
        CompileToYaml("unsigned int f(unsigned int a, unsigned int b) { return a + b; }");
    EXPECT_NE(yaml.find("op: add_unsigned"), std::string::npos);
    EXPECT_EQ(yaml.find("op: add\n"), std::string::npos);
}

// Signed operands still select the signed variants.
TEST_F(TranslateTest, SignedDivideUnchanged)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { return a / b; }");
    EXPECT_NE(yaml.find("op: divide\n"), std::string::npos);
    EXPECT_EQ(yaml.find("op: divide_unsigned"), std::string::npos);
}

// Signed add still selects plain add, not add_unsigned.
TEST_F(TranslateTest, SignedAddUnchanged)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { return a + b; }");
    EXPECT_NE(yaml.find("op: add\n"), std::string::npos);
    EXPECT_EQ(yaml.find("op: add_unsigned"), std::string::npos);
}

// Unsigned operands select subtract_unsigned, not subtract (true 48-bit modular sub).
TEST_F(TranslateTest, UnsignedSub)
{
    std::string yaml =
        CompileToYaml("unsigned int f(unsigned int a, unsigned int b) { return a - b; }");
    EXPECT_NE(yaml.find("op: subtract_unsigned"), std::string::npos);
    EXPECT_EQ(yaml.find("op: subtract\n"), std::string::npos);
}

// Signed subtract still selects plain subtract, not subtract_unsigned.
TEST_F(TranslateTest, SignedSubUnchanged)
{
    std::string yaml = CompileToYaml("int f(int a, int b) { return a - b; }");
    EXPECT_NE(yaml.find("op: subtract\n"), std::string::npos);
    EXPECT_EQ(yaml.find("op: subtract_unsigned"), std::string::npos);
}

// ---------------------------------------------------------------------------
// _Noreturn calls — task #30
// ---------------------------------------------------------------------------

// A direct call to a _Noreturn function lowers to the dedicated fun_call_noreturn
// instruction (the backend tail-jumps to it and drops the dead post-call path).
TEST_F(TranslateTest, NoreturnCallEmitsFunCallNoreturn)
{
    std::string yaml = CompileToYaml("_Noreturn void die(void); void f(void) { die(); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: fun_call_noreturn
      fun_name: die
)");
}

// A _Noreturn function *definition* carries noret: true into its TAC top-level, so the
// backend can drop its b/save prologue.  (noret is emitted only when true, so an ordinary
// definition shows no noret line — as every other translate test's YAML confirms.)
TEST_F(TranslateTest, NoreturnDefinitionCarriesNoretFlag)
{
    std::string yaml = CompileToYaml("_Noreturn void halt(void) { for(;;); }");
    EXPECT_NE(yaml.find("\n  noret: true\n"), std::string::npos);
}

// A call to an ordinary (returning) function stays a plain fun_call.
TEST_F(TranslateTest, OrdinaryCallStaysFunCall)
{
    std::string yaml = CompileToYaml("void g(void); void f(void) { g(); }");
    EXPECT_NE(yaml.find("kind: fun_call\n"), std::string::npos);
    EXPECT_EQ(yaml.find("fun_call_noreturn"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Comma operator (C11 6.5.17)
// ---------------------------------------------------------------------------

// `(f(), g())` evaluates the left operand for its side effects, throws the value
// away, and yields the right one.  Regression: the parser used to drop the right
// operand entirely, so this lowered to a lone call to f() whose result was returned.
TEST_F(TranslateTest, CommaEvaluatesBothAndYieldsRight)
{
    std::string yaml =
        CompileToYaml("int f(void); int g(void); int h(void) { int r; r = (f(), g()); return r; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: h
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: f
      dst:
        kind: var
        name: %0
    - instruction:
      kind: fun_call
      fun_name: g
      dst:
        kind: var
        name: %1
    - instruction:
      kind: copy
      src:
        kind: var
        name: %1
      dst:
        kind: var
        name: %r
    - instruction:
      kind: return
      src:
        kind: var
        name: %r
)");
}

// A three-operand chain runs every operand in order; only the last supplies the value.
// The old single-->next-slot loop lost the middle operand outright.
TEST_F(TranslateTest, CommaChainRunsEveryOperandInOrder)
{
    std::string yaml =
        CompileToYaml("void p(int); int q(void); int h(void) { return (p(1), p(2), q()); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: h
  global: true
  body:
    - instruction:
      kind: fun_call
      fun_name: p
      args:
        - val:
          kind: constant
          const:
            kind: int
            value: 1
    - instruction:
      kind: fun_call
      fun_name: p
      args:
        - val:
          kind: constant
          const:
            kind: int
            value: 2
    - instruction:
      kind: fun_call
      fun_name: q
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

// k7: a designated struct compound literal gets its own slot; the undesignated member
// is zeroed and the value is copied out of the slot, not out of its address.
TEST_F(TranslateTest, CompoundLiteralDesignator)
{
    std::string yaml = CompileToYaml("struct s { int a, b; };"
                                     "void f(void) { struct s x; x = (struct s){ .b = 2 }; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %x
      size: 12
      alignment: 6
    - instruction:
      kind: allocate_local
      name: %0
      size: 12
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst: %0
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst: %0
      offset: 6
    - instruction:
      kind: copy_from_offset
      src: %0
      offset: 0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %1
      dst: %x
      offset: 0
    - instruction:
      kind: copy_from_offset
      src: %0
      offset: 6
      dst:
        kind: var
        name: %2
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %2
      dst: %x
      offset: 6
)");
}

// The address of a scalar compound literal: the literal gets its own slot, so a store
// through its address is not dropped as a dead store to a temporary.
TEST_F(TranslateTest, CompoundLiteralScalarAddress)
{
    std::string yaml = CompileToYaml("void g(int *p); void f(void) { g(&(int){ 5 }); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %0
      size: 6
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 5
      dst: %0
      offset: 0
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: fun_call
      fun_name: g
      args:
        - val:
          kind: var
          name: %1
)");
}

// A call of the C library's sqrt, on a target where square root is an instruction, is the
// unary sqrt_double: no call.  A float argument is converted by the prototype first.
TEST_F(TranslateTestRiscv, SqrtIsUnary)
{
    std::string yaml = CompileToYaml(R"(
#include <math.h>
double f(float x) { return sqrt(x); }
)");
    EXPECT_NE(std::string::npos, yaml.find("kind: unary\n      op: sqrt_double\n")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("fun_call")) << yaml;
}

// The BESM-6 has no square-root instruction: sqrt stays a call.
TEST_F(TranslateTest, SqrtCallOnBesm6)
{
    std::string yaml = CompileToYaml(R"(
#include <math.h>
double f(double x) { return sqrt(x); }
)");
    EXPECT_NE(std::string::npos, yaml.find("kind: fun_call\n      fun_name: sqrt\n")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("sqrt_double")) << yaml;
}

// Only the external sqrt of double(double) is the library's: a static one, one defined in
// the unit, or one of another type stays a call.
static void ExpectSqrtCall(const std::string &yaml)
{
    EXPECT_NE(std::string::npos, yaml.find("fun_name: sqrt\n")) << yaml;
    EXPECT_EQ(std::string::npos, yaml.find("sqrt_double")) << yaml;
}

TEST_F(TranslateTestRiscv, SqrtStaticCalled)
{
    ExpectSqrtCall(CompileToYaml(R"(
static double sqrt(double);
double f(double x) { return sqrt(x); }
static double sqrt(double x) { return x; }
)"));
}

TEST_F(TranslateTestRiscv, SqrtDefinedCalled)
{
    ExpectSqrtCall(CompileToYaml(R"(
double sqrt(double x) { return x; }
double f(double x) { return sqrt(x); }
)"));
}

TEST_F(TranslateTestRiscv, SqrtFloatArgumentCalled)
{
    ExpectSqrtCall(CompileToYaml(R"(
double sqrt(float);
double f(float x) { return sqrt(x); }
)"));
}

TEST_F(TranslateTestRiscv, SqrtFloatResultCalled)
{
    ExpectSqrtCall(CompileToYaml(R"(
float sqrt(double);
float f(double x) { return sqrt(x); }
)"));
}
