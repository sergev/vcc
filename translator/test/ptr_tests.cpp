#include "translate_test.h"

// ---------------------------------------------------------------------------
// Pointer operations — tasks #1+
// ---------------------------------------------------------------------------

// &x on a local variable emits GET_ADDRESS into a fresh temp,
// then the initializer COPY stores the temp into p.
TEST_F(TranslateTest, AddressOfLocalVar)
{
    std::string yaml = CompileToYaml("void f(void) { int x; int *p = &x; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %x
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
        name: %p
)");
}

// *p in rvalue context emits LOAD src_ptr←p, yielding the loaded value.
TEST_F(TranslateTest, DerefPointer)
{
    std::string yaml = CompileToYaml("void f(void) { int *p; int y = *p; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %p
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
        name: %y
)");
}

// int **pp; int y = **pp; — two LOADs (load pointer, load through it) then COPY.
TEST_F(TranslateTest, DerefDerefLocalVar)
{
    std::string yaml = CompileToYaml("void f(void) { int **pp; int y = **pp; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %pp
      dst:
        kind: var
        name: %0
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %0
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
        name: %y
)");
}

// *p = 5 through a pointer parameter emits a single STORE.
TEST_F(TranslateTest, AssignThroughPointer)
{
    std::string yaml = CompileToYaml("void f(int *p) { *p = 5; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
  body:
    - instruction:
      kind: store
      src:
        kind: constant
        const:
          kind: int
          value: 5
      dst_ptr:
        kind: var
        name: %p
)");
}

// (*p)++ emits LOAD + BINARY(add 1) + STORE; returns the old value.
TEST_F(TranslateTest, PostIncThroughPointer)
{
    std::string yaml = CompileToYaml("void f(int *p) { (*p)++; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
  body:
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %p
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
      kind: store
      src:
        kind: var
        name: %1
      dst_ptr:
        kind: var
        name: %p
)");
}

// *p += 3 emits LOAD + BINARY(add 3) + STORE.
TEST_F(TranslateTest, CompoundAssignThroughPointer)
{
    std::string yaml = CompileToYaml("void f(int *p) { *p += 3; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
  body:
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %p
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
          value: 3
      dst:
        kind: var
        name: %1
    - instruction:
      kind: store
      src:
        kind: var
        name: %1
      dst_ptr:
        kind: var
        name: %p
)");
}

// **pp emits two LOADs, validating the mutual recursion between gen_lval and gen_expr.
TEST_F(TranslateTest, DerefDerefPointer)
{
    std::string yaml = CompileToYaml("void f(int **pp) { int y = **pp; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %pp
  body:
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %pp
      dst:
        kind: var
        name: %0
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %0
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
        name: %y
)");
}

// ---------------------------------------------------------------------------
// Array subscript — task #4
// ---------------------------------------------------------------------------

// a[i] in rvalue context: ADD_PTR + LOAD + COPY into the local.
TEST_F(TranslateTest, SubscriptRead)
{
    std::string yaml = CompileToYaml("void f(int *a, long i) { int y = a[i]; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %a
    - param: %i
  body:
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %a
      index:
        kind: var
        name: %i
      scale: 6
      dst:
        kind: var
        name: %0
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %0
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
        name: %y
)");
}

// a[i] = 5: ADD_PTR + STORE.
TEST_F(TranslateTest, SubscriptWrite)
{
    std::string yaml = CompileToYaml("void f(int *a, long i) { a[i] = 5; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %a
    - param: %i
  body:
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %a
      index:
        kind: var
        name: %i
      scale: 6
      dst:
        kind: var
        name: %0
    - instruction:
      kind: store
      src:
        kind: constant
        const:
          kind: int
          value: 5
      dst_ptr:
        kind: var
        name: %0
)");
}

// a[i]++: ADD_PTR + LOAD + BINARY(add 1) + STORE; returns old value (discarded).
TEST_F(TranslateTest, SubscriptPostInc)
{
    std::string yaml = CompileToYaml("void f(int *a, long i) { a[i]++; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %a
    - param: %i
  body:
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %a
      index:
        kind: var
        name: %i
      scale: 6
      dst:
        kind: var
        name: %0
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %0
      dst:
        kind: var
        name: %1
    - instruction:
      kind: binary
      op: add
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
        name: %2
    - instruction:
      kind: store
      src:
        kind: var
        name: %2
      dst_ptr:
        kind: var
        name: %0
)");
}

// ---------------------------------------------------------------------------
// Volatile accesses — the lowered memory ops carry the volatile flag so the
// optimizer preserves them. (DerefPointer above is the non-volatile control.)
// ---------------------------------------------------------------------------

// *p where p is `volatile int *` lowers to a volatile LOAD.
TEST_F(TranslateTest, VolatileDerefLoad)
{
    std::string yaml = CompileToYaml("void f(void) { volatile int *p; int y = *p; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: load
      volatile: true
      src_ptr:
        kind: var
        name: %p
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
        name: %y
)");
}

// *p = 1 where p is `volatile int *` lowers to a volatile STORE.
TEST_F(TranslateTest, VolatileDerefStore)
{
    std::string yaml = CompileToYaml("void f(void) { volatile int *p; *p = 1; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: store
      volatile: true
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst_ptr:
        kind: var
        name: %p
)");
}

// Writing a volatile scalar variable lowers to a volatile COPY.
TEST_F(TranslateTest, VolatileScalarWrite)
{
    std::string yaml = CompileToYaml("void f(void) { volatile int x; x = 7; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: copy
      volatile: true
      src:
        kind: constant
        const:
          kind: int
          value: 7
      dst:
        kind: var
        name: %x
)");
}

// Reading a volatile scalar variable is materialized into a volatile COPY so the
// read re-executes on every use (strict C11 read semantics).
TEST_F(TranslateTest, VolatileScalarReadMaterialized)
{
    std::string yaml = CompileToYaml("void f(void) { volatile int x; int y = x; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: copy
      volatile: true
      src:
        kind: var
        name: %x
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
        name: %y
)");
}

// (*p)++ through a volatile pointer flags BOTH the load and the store halves.
TEST_F(TranslateTest, VolatileDerefPostIncLoadAndStore)
{
    std::string yaml = CompileToYaml("void f(void) { volatile int *p; (*p)++; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: load
      volatile: true
      src_ptr:
        kind: var
        name: %p
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
      kind: store
      volatile: true
      src:
        kind: var
        name: %1
      dst_ptr:
        kind: var
        name: %p
)");
}

// char* - char*: a fat-pointer difference is a ptrdiff_t (long) byte count, lowered to the
// dedicated PTR_DIFF instruction (not ADD_PTR, which is pointer ± integer). Task #22b.
TEST_F(TranslateTest, CharPtrDifference)
{
    std::string yaml = CompileToYaml("long f(char *p, char *q) { return p - q; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
    - param: %q
  body:
    - instruction:
      kind: ptr_diff
      ptr_a:
        kind: var
        name: %p
      ptr_b:
        kind: var
        name: %q
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

// char(*)[N] - char(*)[N]: a fat-pointer difference to a multi-byte element divides the
// raw b/pdiff byte count by the row size (3) to yield an element (row) count. Task #11.
TEST_F(TranslateTest, CharRowPtrDifference)
{
    std::string yaml = CompileToYaml("long f(char (*p)[3], char (*q)[3]) { return p - q; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
    - param: %q
  body:
    - instruction:
      kind: ptr_diff
      ptr_a:
        kind: var
        name: %p
      ptr_b:
        kind: var
        name: %q
      dst:
        kind: var
        name: %0
    - instruction:
      kind: binary
      op: divide
      src1:
        kind: var
        name: %0
      src2:
        kind: constant
        const:
          kind: int
          value: 3
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

// int(*)[2] - int(*)[2]: a wide word-pointer difference is a raw word-address subtract
// divided by the element word size (2) to yield a C element count, not the
// ptr-minus-integer scale path. Task #11.
TEST_F(TranslateTest, WideWordPtrDifference)
{
    std::string yaml = CompileToYaml("long f(int (*p)[2], int (*q)[2]) { return p - q; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
    - param: %q
  body:
    - instruction:
      kind: binary
      op: subtract
      src1:
        kind: var
        name: %p
      src2:
        kind: var
        name: %q
      dst:
        kind: var
        name: %0
    - instruction:
      kind: binary
      op: divide
      src1:
        kind: var
        name: %0
      src2:
        kind: constant
        const:
          kind: int
          value: 2
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

// `*p = v' used as a VALUE yields the value stored (C11 6.5.16p3), so the branch tests %v.
// It used to return a fresh temp that no instruction ever defined, and the condition read
// whatever that frame slot happened to hold -- which hung v7's callout compaction loop,
// `while ((p2->c_func = p1->c_func))'.
TEST_F(TranslateTest, StoreThroughPointerAsValue)
{
    std::string yaml = CompileToYaml("int f(int *p, int v) { if ((*p = v)) return 1; return 0; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
    - param: %v
  body:
    - instruction:
      kind: store
      src:
        kind: var
        name: %v
      dst_ptr:
        kind: var
        name: %p
    - instruction:
      kind: jump_if_zero
      condition:
        kind: var
        name: %v
      target: %0
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 1
    - instruction:
      kind: jump
      target: %1
    - instruction:
      kind: label
      name: %0
    - instruction:
      kind: label
      name: %1
    - instruction:
      kind: return
      src:
        kind: constant
        const:
          kind: int
          value: 0
)");
}

// char* < char*: on a byte-addressed target the pointers themselves are compared,
// unsigned, so constants fold right too; a difference compared against 0 could
// overflow, as on rv32 with pointers 2 GB apart.  The BESM-6 compares its fat pointers' positions.
TEST_F(TranslateTestX86, CharPtrRelationalCompare)
{
    std::string yaml = CompileToYaml("int f(char *p, char *q) { return p > q; }");
    EXPECT_EQ(std::string::npos, yaml.find("ptr_diff")) << yaml;
    EXPECT_NE(std::string::npos, yaml.find(R"(op: greater_than_unsigned
      src1:
        kind: var
        name: %p
      src2:
        kind: var
        name: %q
)")) << yaml;
}

TEST_F(TranslateTest, CharPtrRelationalCompareBesm6)
{
    std::string yaml = CompileToYaml("int f(char *p, char *q) { return p > q; }");
    EXPECT_NE(std::string::npos, yaml.find("kind: ptr_diff")) << yaml;
}

// A pointer compare is unsigned, so constant pointers fold right: 0x8100 is above
// 0x7f00 on a 16-bit target, where a signed view would put it below.
TEST_F(TranslateTestMsp430, PointerCompareUnsigned)
{
    std::string yaml = CompileToYaml(
        "int f(void) { int *hi = (int *)0x8100, *lo = (int *)0x7f00; return hi > lo; }");
    EXPECT_NE(std::string::npos, yaml.find("op: greater_than_unsigned")) << yaml;
}
