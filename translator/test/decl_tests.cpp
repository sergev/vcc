#include "translate_test.h"

// ---------------------------------------------------------------------------
// Local variable declarations — task #1
// ---------------------------------------------------------------------------

// A variable with no initializer emits no COPY; only the return is generated.
TEST_F(TranslateTest, LocalVarNoInit)
{
    std::string yaml = CompileToYaml("int f(void) { int x; return 0; }");
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
          value: 0
)");
}

// A variable with a constant initializer emits COPY dst ← constant.
TEST_F(TranslateTest, LocalVarWithIntInit)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 42; return x; }");
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

// Two local variables each emit their own COPY.
TEST_F(TranslateTest, TwoLocalVars)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 1; int y = 2; return x; }");
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
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst:
        kind: var
        name: %y
    - instruction:
      kind: return
      src:
        kind: var
        name: %x
)");
}

// Initialized variable used in an expression produces COPY then BINARY.
TEST_F(TranslateTest, LocalVarUsedInBinaryExpr)
{
    std::string yaml = CompileToYaml("int f(void) { int x = 10; return x + 1; }");
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
          value: 1
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

// ---------------------------------------------------------------------------
// Char literals — task #1
// ---------------------------------------------------------------------------

TEST_F(TranslateTest, CharLiteralReturnedDirectly)
{
    std::string yaml = CompileToYaml("int f(void) { return 'A'; }");
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
          value: 65
)");
}

// ---------------------------------------------------------------------------
// String literals — task #1
// ---------------------------------------------------------------------------

// A string literal emits a static_constant toplevel node followed by a
// get_address instruction that yields the pointer result.
TEST_F(TranslateTest, StringLiteralReturned)
{
    std::string yaml = CompileToYaml(R"(char *f(void) { return "hi"; })");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_constant
  name: _str0
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 3
  init:
    kind: string
    value: hi
    null_terminated: true
- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: _str0
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

// Two functions with distinct string literals each get their own
// static_constant node with unique names.
TEST_F(TranslateTest, TwoFunctionsDistinctStringLiterals)
{
    std::string yaml = CompileToYaml(
        "char *f(void) { return \"yes\"; }\n"
        "char *g(void) { return \"no\"; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_constant
  name: _str0
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 4
  init:
    kind: string
    value: yes
    null_terminated: true
- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: _str0
      dst:
        kind: var
        name: %0
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
- toplevel:
  kind: static_constant
  name: _str1
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 3
  init:
    kind: string
    value: no
    null_terminated: true
- toplevel:
  kind: function
  name: g
  global: true
  body:
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: _str1
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

// ---------------------------------------------------------------------------
// Enum constant literals — task #1
// ---------------------------------------------------------------------------

// Enumerators without explicit values start at 0 and auto-increment.
TEST_F(TranslateTest, EnumConstDefaultValues)
{
    std::string yaml = CompileToYaml("enum Color { RED, GREEN }; int f(void) { return GREEN; }");
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
          value: 1
)");
}

// An explicit initializer overrides the auto-increment.
TEST_F(TranslateTest, EnumConstExplicitValue)
{
    std::string yaml = CompileToYaml("enum Color { RED = 5, GREEN }; int f(void) { return RED; }");
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
          value: 5
)");
}

// Enum declared inside a function body is scoped to that body.
TEST_F(TranslateTest, EnumConstLocalDecl)
{
    std::string yaml = CompileToYaml("int f(void) { enum Dir { UP = 10, DOWN }; return DOWN; }");
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
          value: 11
)");
}

// A static initializer bypasses typecheck_init, so an enumerator reaches
// build_static_init still spelled as a LITERAL_ENUM (an identifier, not a value).
// It must fold through try_eval_const_int rather than being handed to
// new_static_init_from_literal, which has no symbol table and would abort with
// "literal_to_int64: Cannot convert enum".  Every element of an aggregate
// initializer recurses into that same scalar leaf, so one bad branch broke them all.
TEST_F(TranslateTest, EnumConstInStaticArrayInit)
{
    std::string yaml = CompileToYaml("enum { X, Y }; static const int a[] = { X, Y };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: a
  global: false
  type:
    kind: array
    elem_type:
      kind: int
    size: 2
  init_list:
    - init:
      kind: i64
      value: 0
    - init:
      kind: i64
      value: 1
)");
}

// Same for a non-static file-scope array: storage duration is static either way.
TEST_F(TranslateTest, EnumConstInGlobalArrayInit)
{
    std::string yaml = CompileToYaml("enum { X, Y }; int b[] = { X, Y };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: b
  global: true
  type:
    kind: array
    elem_type:
      kind: int
    size: 2
  init_list:
    - init:
      kind: i64
      value: 0
    - init:
      kind: i64
      value: 1
)");
}

// The scalar case is the same leaf, so it was equally broken.  An explicit
// enumerator value also exercises the auto-increment that follows it.
TEST_F(TranslateTest, EnumConstInScalarStaticInit)
{
    std::string yaml = CompileToYaml("enum Color { RED = 5, GREEN }; static int s = GREEN;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: s
  global: false
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 6
)");
}

// A narrower element type takes the same fallthrough: is_integer covers char.
TEST_F(TranslateTest, EnumConstInCharArrayInit)
{
    std::string yaml =
        CompileToYaml("enum Ch { LETTER_A = 65, LETTER_B }; char c[] = { LETTER_A, LETTER_B };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: c
  global: true
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 2
  init_list:
    - init:
      kind: i8
      value: 65
    - init:
      kind: i8
      value: 66
)");
}

// A variable of enumerated type is int-sized/int-aligned/signed everywhere else,
// so new_static_init_from_literal must give TYPE_ENUM int's representation too —
// otherwise even a plain "enum Color ev = 7;" died with "Unsupported constant type".
TEST_F(TranslateTest, EnumTypedGlobalInit)
{
    std::string yaml = CompileToYaml("enum Color { RED = 5, GREEN }; enum Color ev = GREEN;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: ev
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 6
)");
}

// for-init declaration emits COPY before the loop test label.
TEST_F(TranslateTest, ForLoopInitDecl)
{
    std::string yaml = CompileToYaml("int f(void) { for (int i = 5; ; ) { return i; } return 0; }");
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
          value: 5
      dst:
        kind: var
        name: %i
    - instruction:
      kind: label
      name: %2
    - instruction:
      kind: return
      src:
        kind: var
        name: %i
    - instruction:
      kind: label
      name: %L1
    - instruction:
      kind: jump
      target: %2
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
// Global declarations
// ---------------------------------------------------------------------------

// Tentative global variable (no initializer) emits static_variable with no init_list.
TEST_F(TranslateTest, GlobalVarTentative)
{
    std::string yaml = CompileToYaml("int x;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: int
)");
}

// Initialized global variable emits static_variable with i64 init (BESM-6 int
// is 48-bit, carried in the 64-bit static-init slot).
TEST_F(TranslateTest, GlobalVarInitialized)
{
    std::string yaml = CompileToYaml("int x = 42;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 42
)");
}

// Initialized global short variable emits static_variable with i16 init.
TEST_F(TranslateTest, GlobalVarShortInitialized)
{
    std::string yaml = CompileToYaml("short x = 42;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: short
  init_list:
    - init:
      kind: i16
      value: 42
)");
}

// Initialized global unsigned short variable emits static_variable with u16 init.
TEST_F(TranslateTest, GlobalVarUshortInitialized)
{
    std::string yaml = CompileToYaml("unsigned short x = 1000;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: ushort
  init_list:
    - init:
      kind: u16
      value: 1000
)");
}

// Static global variable sets global=false.
TEST_F(TranslateTest, GlobalVarStatic)
{
    std::string yaml = CompileToYaml("static int x = 5;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: false
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 5
)");
}

// extern declaration emits no TAC — no storage is allocated.
TEST_F(TranslateTest, GlobalVarExtern)
{
    std::string yaml = CompileToYaml("extern int x;");
    EXPECT_EQ(yaml, "");
}

// An extern array declaration allocates no storage and emits no TAC — a later
// reference decays the array to its address (GET_ADDRESS), which self-declares
// the external name, so no array-ness record is needed here.
TEST_F(TranslateTest, ExternArrayOfInlineStruct)
{
    std::string yaml = CompileToYaml("extern struct S { int x; } arr[];");
    EXPECT_EQ(yaml, "");
}

// Incomplete extern array declaration allocates no storage and emits no TAC.
TEST_F(TranslateTest, GlobalVarExternIncompleteArray)
{
    std::string yaml = CompileToYaml("extern int icode[];");
    EXPECT_EQ(yaml, "");
}

// Multi-declarator global declaration emits one static_variable per declarator.
TEST_F(TranslateTest, GlobalVarMultiDeclarator)
{
    std::string yaml = CompileToYaml("int x = 1, y = 2;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 1
- toplevel:
  kind: static_variable
  name: y
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 2
)");
}

// Struct/enum/typedef-only declarations produce no TAC output.
TEST_F(TranslateTest, StructDeclOnly)
{
    EXPECT_EQ(CompileToYaml("struct Foo { int x; };"), "");
}

TEST_F(TranslateTest, EnumDeclOnly)
{
    EXPECT_EQ(CompileToYaml("enum Color { RED, GREEN, BLUE };"), "");
}

TEST_F(TranslateTest, TypedefOnly)
{
    EXPECT_EQ(CompileToYaml("typedef int MyInt;"), "");
}

TEST_F(TranslateTest, TypedefLocalVar)
{
    std::string yaml = CompileToYaml(
        "typedef int myint;"
        "int f(void) { myint x = 42; return x; }");
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

// A file-scope typedef of an array of unspecified size: each initializer completes the
// array type of its own object (C11 §6.7.9p22), not the typedef the others share.
TEST_F(TranslateTest, TypedefUnsizedArray)
{
    std::string yaml = CompileToYaml(R"(
        typedef int A[];
        A x = {1, 2};
        int f(void)
        {
            A y = {1, 2, 3};
            return sizeof x + sizeof y;
        }
    )");
    // x has two elements.
    EXPECT_NE(yaml.find(R"(  name: x
  global: true
  type:
    kind: array
    elem_type:
      kind: int
    size: 2
)"),
              std::string::npos)
        << yaml;
    // y has three: sizeof x + sizeof y is 2 + 3 words of 6 bytes.
    EXPECT_NE(yaml.find(R"(
          kind: ulong
          value: 30
)"),
              std::string::npos)
        << yaml;
}

// ---------------------------------------------------------------------------
// Compound local-variable initializers — task #2
// ---------------------------------------------------------------------------

// struct Foo s = {1, 2}; emits two COPY_TO_OFFSET instructions.
TEST_F(TranslateTest, LocalStructCompoundInit)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "void f(void) { struct Foo s = {1, 2}; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %s
      size: 12
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %s
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst: %s
      offset: 6
)");
}

// int arr[3] = {10, 20, 30}; emits three COPY_TO_OFFSET instructions.
TEST_F(TranslateTest, LocalArrayCompoundInit)
{
    std::string yaml = CompileToYaml("void f(void) { int arr[3] = {10, 20, 30}; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %arr
      size: 18
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 10
      dst: %arr
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 20
      dst: %arr
      offset: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 30
      dst: %arr
      offset: 12
)");
}

// struct Outer o = {{1, 2}, 3}; — nested struct — emits COPY_TO_OFFSET at offsets 0, 4, 8.
TEST_F(TranslateTest, LocalNestedStructInit)
{
    std::string yaml = CompileToYaml(
        "struct Inner { int a; int b; };"
        "struct Outer { struct Inner in; int c; };"
        "void f(void) { struct Outer o = {{1, 2}, 3}; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %o
      size: 18
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %o
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst: %o
      offset: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 3
      dst: %o
      offset: 12
)");
}

// An uninitialized local aggregate still emits AllocateLocal so the backend reserves
// its full frame slot (task #23). Size/alignment are in target bytes (besm6 default
// here): int a[10] -> 60 bytes; struct Foo {int;int} -> 12 bytes; both 6-byte aligned.
TEST_F(TranslateTest, LocalAggregateAllocateLocal)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "void f(void) { int a[10]; struct Foo s; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %a
      size: 60
      alignment: 6
    - instruction:
      kind: allocate_local
      name: %s
      size: 12
      alignment: 6
)");
}

// A _Bool object with static storage duration used to abort the translator
// ("ast_type_to_tac_type: unsupported type kind 1"): unlike a struct member, which is
// emitted as a sized blob, the object's own type has to be lowered.  _Bool carries
// int's representation (one word here), and its initializer normalizes to 0/1.
TEST_F(TranslateTest, BoolStaticVariable)
{
    std::string yaml = CompileToYaml("_Bool g = 5;");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: g
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 1
)");
}

// The array and pointer cases recurse into the element/pointee type, so they failed
// the same way; a _Bool array is a plain word-per-element array, not the packed byte
// blob a char array lowers to.
TEST_F(TranslateTest, BoolStaticArrayAndPointer)
{
    std::string yaml = CompileToYaml("_Bool a[3] = { 0, 5, 0 };");
    EXPECT_NE(yaml.find("kind: array"), std::string::npos);
    EXPECT_NE(yaml.find("size: 3"), std::string::npos);
    EXPECT_NE(yaml.find("value: 1"), std::string::npos);
    EXPECT_EQ(yaml.find("kind: string"), std::string::npos);
}

// Brace elision in a static 2-D array: row by row, the missing element is a ZERO run.
TEST_F(TranslateTest, StaticMatrixBraceElision)
{
    std::string yaml = CompileToYaml("int g[2][2] = { 1, 2, 3 };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: g
  global: true
  type:
    kind: array
    elem_type:
      kind: array
      elem_type:
        kind: int
      size: 2
    size: 2
  init_list:
    - init:
      kind: i64
      value: 1
    - init:
      kind: i64
      value: 2
    - init:
      kind: i64
      value: 3
    - init:
      kind: zero
      bytes: 6
)");
}

// A braced scalar initializer, static and automatic (C11 §6.7.9p11).
TEST_F(TranslateTest, BracedScalarInit)
{
    std::string yaml = CompileToYaml(
        "int x = { 5 };"
        "int f(void) { int y = { 6 }; return y; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: x
  global: true
  type:
    kind: int
  init_list:
    - init:
      kind: i64
      value: 5
- toplevel:
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
        name: %y
    - instruction:
      kind: return
      src:
        kind: var
        name: %y
)");
}

// Brace elision in an automatic array of structs: "AB", 1 fill tab[0], "CD" starts
// tab[1], whose v is zero.
TEST_F(TranslateTest, AutoStructArrayBraceElision)
{
    std::string yaml = CompileToYaml(
        "struct s { char *name; int v; };"
        "void f(void) { struct s tab[2] = { \"AB\", 1, \"CD\" }; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_constant
  name: _str1
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 3
  init:
    kind: string
    value: CD
    null_terminated: true
- toplevel:
  kind: static_constant
  name: _str0
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 3
  init:
    kind: string
    value: AB
    null_terminated: true
- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %tab
      size: 24
      alignment: 6
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: _str0
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %0
      dst: %tab
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %tab
      offset: 6
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: _str1
      dst:
        kind: var
        name: %1
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %1
      dst: %tab
      offset: 12
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst: %tab
      offset: 18
)");
}

// Field designators in an automatic struct: each value is stored at its member's offset,
// and the undesignated member b is zeroed.
TEST_F(TranslateTest, AutoStructFieldDesignators)
{
    std::string yaml = CompileToYaml(
        "struct s { int a, b, c; };"
        "void f(void) { struct s x = { .c = 3, .a = 1 }; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %x
      size: 18
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %x
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst: %x
      offset: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 3
      dst: %x
      offset: 12
)");
}

// A file-scope compound literal is an anonymous static object, emitted ahead of the
// variable that points at it (C11 6.5.2.5p5).
TEST_F(TranslateTest, FileScopeCompoundLiteral)
{
    std::string yaml = CompileToYaml("int *p = (int[]){ 1, 2 };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: _cl0
  global: false
  type:
    kind: array
    elem_type:
      kind: int
    size: 2
  init_list:
    - init:
      kind: i64
      value: 1
    - init:
      kind: i64
      value: 2
- toplevel:
  kind: static_variable
  name: p
  global: true
  type:
    kind: pointer
    target:
      kind: int
  init_list:
    - init:
      kind: pointer
      name: _cl0
)");
}

// A member address, a char literal (byte #5 of its word, like a named char) and a literal
// nested in another; each object follows the one that references it.
TEST_F(TranslateTest, FileScopeCompoundLiteralNested)
{
    std::string yaml = CompileToYaml(
        "struct s { int a, b; };"
        "int *qb = &(struct s){ 1, 2 }.b;"
        "char *c = &(char){ 67 };"
        "int **pp = (int *[]){ (int[]){ 3 } + 1, 0 };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: _cl0
  global: false
  type:
    kind: structure
    tag: s
    size: 12
  init_list:
    - init:
      kind: i64
      value: 1
    - init:
      kind: i64
      value: 2
- toplevel:
  kind: static_variable
  name: qb
  global: true
  type:
    kind: pointer
    target:
      kind: int
  init_list:
    - init:
      kind: pointer
      name: _cl0
      byte_offset: 6
- toplevel:
  kind: static_variable
  name: _cl1
  global: false
  type:
    kind: uchar
  init_list:
    - init:
      kind: i8
      value: 67
- toplevel:
  kind: static_variable
  name: c
  global: true
  type:
    kind: pointer
    target:
      kind: uchar
  init_list:
    - init:
      kind: fat_pointer
      name: _cl1
      byte_offset: 5
- toplevel:
  kind: static_variable
  name: _cl3
  global: false
  type:
    kind: array
    elem_type:
      kind: pointer
      target:
        kind: int
    size: 2
  init_list:
    - init:
      kind: pointer
      name: _cl2
      byte_offset: 6
    - init:
      kind: zero
      bytes: 6
- toplevel:
  kind: static_variable
  name: _cl2
  global: false
  type:
    kind: array
    elem_type:
      kind: int
    size: 1
  init_list:
    - init:
      kind: i64
      value: 3
- toplevel:
  kind: static_variable
  name: pp
  global: true
  type:
    kind: pointer
    target:
      kind: pointer
      target:
        kind: int
  init_list:
    - init:
      kind: pointer
      name: _cl3
)");
}

// An automatic aggregate with at least 8 zero words is zeroed by a loop, and only the
// non-zero leaves are stored.
TEST_F(TranslateTest, BulkZeroFill)
{
    std::string yaml = CompileToYaml(
        "void g(int *p);"
        "void f(void) { int a[9] = { [8] = 1 }; g(a); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %a
      size: 54
      alignment: 6
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %a
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 9
      dst:
        kind: var
        name: %1
    - instruction:
      kind: label
      name: %2
    - instruction:
      kind: store
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst_ptr:
        kind: var
        name: %0
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %0
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 6
      dst:
        kind: var
        name: %0
    - instruction:
      kind: binary
      op: subtract
      src1:
        kind: var
        name: %1
      src2:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %1
    - instruction:
      kind: jump_if_not_zero
      condition:
        kind: var
        name: %1
      target: %2
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %a
      offset: 48
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %a
      dst:
        kind: var
        name: %3
    - instruction:
      kind: fun_call
      fun_name: g
      args:
        - val:
          kind: var
          name: %3
)");
}

// Below the threshold every zero is stored, as before.
TEST_F(TranslateTest, BulkZeroFillBelowThreshold)
{
    std::string yaml = CompileToYaml(
        "void g(int *p);"
        "void f(void) { int a[8] = { [7] = 1 }; g(a); }");
    EXPECT_EQ(yaml.find("jump_if_not_zero"), std::string::npos);
    size_t stores = 0;
    for (size_t pos = 0; (pos = yaml.find("kind: copy_to_offset", pos)) != std::string::npos; pos++)
        stores++;
    EXPECT_EQ(stores, 8u);
}

// A short string in a large char array: the array is zeroed by a loop, and only the
// string's bytes are stored (the NUL comes from the zeroing).
TEST_F(TranslateTest, BulkZeroFillString)
{
    std::string yaml = CompileToYaml(
        "void g(char *p);"
        "void f(void) { char b[60] = \"AB\"; g(b); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %b
      size: 60
      alignment: 1
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %b
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 10
      dst:
        kind: var
        name: %1
    - instruction:
      kind: label
      name: %2
    - instruction:
      kind: store
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst_ptr:
        kind: var
        name: %0
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %0
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 6
      dst:
        kind: var
        name: %0
    - instruction:
      kind: binary
      op: subtract
      src1:
        kind: var
        name: %1
      src2:
        kind: constant
        const:
          kind: int
          value: 1
      dst:
        kind: var
        name: %1
    - instruction:
      kind: jump_if_not_zero
      condition:
        kind: var
        name: %1
      target: %2
    - instruction:
      kind: copy_byte_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 65
      dst: %b
      offset: 0
    - instruction:
      kind: copy_byte_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 66
      dst: %b
      offset: 1
    - instruction:
      kind: get_address_decay
      src:
        kind: var
        name: %b
      dst:
        kind: var
        name: %3
    - instruction:
      kind: fun_call
      fun_name: g
      args:
        - val:
          kind: var
          name: %3
)");
}

// A string that fills its array stores every byte, with no loop.
TEST_F(TranslateTest, BulkZeroFillStringExact)
{
    std::string yaml = CompileToYaml(
        "void g(char *p);"
        "void f(void) { char b[4] = \"CDE\"; g(b); }");
    EXPECT_EQ(yaml.find("jump_if_not_zero"), std::string::npos);
    size_t stores = 0;
    for (size_t pos = 0; (pos = yaml.find("kind: copy_byte_to_offset", pos)) != std::string::npos;
         pos++)
        stores++;
    EXPECT_EQ(stores, 4u);
}

// Adjacent ZERO runs merge: a zero member, the padding after a char and a trailing
// uninitialized member become one run.
TEST_F(TranslateTest, StaticZeroRunsMerge)
{
    std::string yaml = CompileToYaml("struct m { char c; int x; int y[3]; } v = { 1, 0 };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: v
  global: true
  type:
    kind: structure
    tag: m
    size: 30
  init_list:
    - init:
      kind: i8
      value: 1
    - init:
      kind: zero
      bytes: 29
)");
}

// Zero tails of array elements merge with the zero elements that follow them.
TEST_F(TranslateTest, StaticZeroRunsMergeAcrossElements)
{
    std::string yaml = CompileToYaml(
        "struct p { int a, b, c; };"
        "struct p t[4] = { { 1 }, { 0 }, { 0, 0, 3 } };");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: t
  global: true
  type:
    kind: array
    elem_type:
      kind: structure
      tag: p
      size: 18
    size: 4
  init_list:
    - init:
      kind: i64
      value: 1
    - init:
      kind: zero
      bytes: 42
    - init:
      kind: i64
      value: 3
    - init:
      kind: zero
      bytes: 18
)");
}
