#include "translate_test.h"

// ---------------------------------------------------------------------------
// Struct field read/write — task #6
// ---------------------------------------------------------------------------

// s.x in rvalue context emits COPY_FROM_OFFSET for the first field (offset 0).
TEST_F(TranslateTest, StructFieldReadFirst)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "int f(void) { struct Foo s; return s.x; }");
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
      kind: copy_from_offset
      src: %s
      offset: 0
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

// s.y in rvalue context emits COPY_FROM_OFFSET with the correct byte offset (4).
TEST_F(TranslateTest, StructFieldReadSecond)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "int f(void) { struct Foo s; return s.y; }");
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
      kind: copy_from_offset
      src: %s
      offset: 6
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

// s.x = val emits COPY_TO_OFFSET (direct-variable optimization).
TEST_F(TranslateTest, StructFieldWrite)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "void f(void) { struct Foo s; s.x = 5; }");
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
          value: 5
      dst: %s
      offset: 0
)");
}

// ---------------------------------------------------------------------------
// Pointer-to-struct field access — task #7
// ---------------------------------------------------------------------------

// p->x in rvalue context emits ADD_PTR + LOAD.
TEST_F(TranslateTest, PtrFieldRead)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "int f(struct Foo *p) { return p->x; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
  body:
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %p
      index:
        kind: constant
        const:
          kind: int
          value: 0
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
      kind: return
      src:
        kind: var
        name: %1
)");
}

// p->x = 5 emits ADD_PTR + STORE.
TEST_F(TranslateTest, PtrFieldWrite)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "void f(struct Foo *p) { p->x = 5; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %p
  body:
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %p
      index:
        kind: constant
        const:
          kind: int
          value: 0
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

// &s.x emits GET_ADDRESS then ADD_PTR (a word member uses a word offset: scale=6,
// index=byte_offset/6, keeping the result a plain word pointer).
TEST_F(TranslateTest, StructFieldAddressOf)
{
    std::string yaml = CompileToYaml(
        "struct Foo { int x; int y; };"
        "void f(void) { struct Foo s; int *p = &s.x; }");
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
      kind: get_address
      src:
        kind: var
        name: %s
      dst:
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
          value: 0
      scale: 6
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
        name: %p
)");
}

// A block-local tag reached through a pointer.  `bar` is a word member (a char *, not a
// char) at a word-aligned offset, so it is added as a plain word offset — index 0, scale 6,
// the same shape the identical program with a file-scope tag has always produced.  Before
// the member type was cached on the access node, the purged tag left field_member_type()
// NULL here and the unknown-member fallback emitted the byte form (index 0, scale 1);
// numerically the same address, since the index is 0.
TEST_F(TranslateTest, LocalStructCast)
{
    std::string yaml = CompileToYaml(R"(
        char *foo()
        {
            struct a {
                char *bar;
            } *quz;
            quz = (struct a *)0;
            return quz->bar;
        }
    )");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: foo
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
        name: %0
    - instruction:
      kind: copy
      src:
        kind: var
        name: %0
      dst:
        kind: var
        name: %quz
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %quz
      index:
        kind: constant
        const:
          kind: int
          value: 0
      scale: 6
      dst:
        kind: var
        name: %1
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %1
      dst:
        kind: var
        name: %2
    - instruction:
      kind: return
      src:
        kind: var
        name: %2
)");
}

// ---------------------------------------------------------------------------
// Struct-by-value return: hidden-pointer (sret) ABI, lowered in the translator.
// On the BESM-6 target a struct larger than one 6-byte word (here struct T = two ints =
// 12 bytes) is returned through a hidden pointer; a one-word struct stays on the
// accumulator path.
// ---------------------------------------------------------------------------

// Callee: a multi-word struct return prepends a hidden pointer param (%.ret), copies the
// result word by word into *%.ret via STORE, and returns the pointer itself.
TEST_F(TranslateTest, StructByValueReturnCallee)
{
    std::string yaml = CompileToYaml(
        "struct T { int a; int b; };"
        "struct T mk(void) { struct T t; t.a = 1; t.b = 2; return t; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: mk
  global: true
  params:
    - param: %.ret
  body:
    - instruction:
      kind: allocate_local
      name: %t
      size: 12
      alignment: 6
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 1
      dst: %t
      offset: 0
    - instruction:
      kind: copy_to_offset
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst: %t
      offset: 6
    - instruction:
      kind: copy_from_offset
      src: %t
      offset: 0
      dst:
        kind: var
        name: %0
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %.ret
      index:
        kind: constant
        const:
          kind: int
          value: 0
      scale: 6
      dst:
        kind: var
        name: %1
    - instruction:
      kind: store
      src:
        kind: var
        name: %0
      dst_ptr:
        kind: var
        name: %1
    - instruction:
      kind: copy_from_offset
      src: %t
      offset: 6
      dst:
        kind: var
        name: %2
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %.ret
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 6
      dst:
        kind: var
        name: %3
    - instruction:
      kind: store
      src:
        kind: var
        name: %2
      dst_ptr:
        kind: var
        name: %3
    - instruction:
      kind: return
      src:
        kind: var
        name: %.ret
)");
}

// Caller: a call returning a multi-word struct allocates a result slot, passes its
// address as the hidden first argument, and the call carries no scalar destination; the
// struct then lives in the slot and is copied into the destination word by word.
TEST_F(TranslateTest, StructByValueReturnCaller)
{
    std::string yaml = CompileToYaml(
        "struct T { int a; int b; };"
        "struct T mk(void);"
        "void use(void) { struct T r = mk(); }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: use
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %r
      size: 12
      alignment: 6
    - instruction:
      kind: allocate_local
      name: %0
      size: 12
      alignment: 6
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
      fun_name: mk
      args:
        - val:
          kind: var
          name: %1
    - instruction:
      kind: copy_from_offset
      src: %0
      offset: 0
      dst:
        kind: var
        name: %2
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %2
      dst: %r
      offset: 0
    - instruction:
      kind: copy_from_offset
      src: %0
      offset: 6
      dst:
        kind: var
        name: %3
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %3
      dst: %r
      offset: 6
)");
}

// A struct that fits in one word still returns through the accumulator (no hidden
// param, no STORE) — the sret rewrite must not trigger.
TEST_F(TranslateTest, StructByValueReturnOneWordUsesAccumulator)
{
    std::string yaml = CompileToYaml(
        "struct S { int a; };"
        "struct S mk(void) { struct S s; s.a = 7; return s; }");
    EXPECT_EQ(yaml.find("param: %.ret"), std::string::npos);
    EXPECT_EQ(yaml.find("kind: store"), std::string::npos);
    EXPECT_NE(yaml.find("name: %s"), std::string::npos); // returns the struct slot directly
}

// `s.f = v' used as a VALUE yields the value stored, not the aggregate it went into.
// Returning the base named field 0 whatever the offset was, so `if ((S.b = v))' branched
// on S.a.  The sibling of StoreThroughPointerAsValue in ptr_tests.cpp.
TEST_F(TranslateTest, StoreToFieldAsValue)
{
    std::string yaml = CompileToYaml(
        "struct s { int a, b; } S;"
        "int fv(int v) { if ((S.b = v)) return 1; return 0; }");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: static_variable
  name: S
  global: true
  type:
    kind: structure
    tag: s
    size: 12
- toplevel:
  kind: function
  name: fv
  global: true
  params:
    - param: %v
  body:
    - instruction:
      kind: copy_to_offset
      src:
        kind: var
        name: %v
      dst: S
      offset: 6
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

// ---------------------------------------------------------------------------
// Block-scope struct tags.  A tag declared inside a function body is purged from
// structtab when the block ends -- during typecheck -- while the translator runs
// afterwards and still needs the tag's layout.  Everything it needs is therefore
// resolved during typecheck and stashed on the AST: the size/alignment on the type
// node (carried through clone_type), the member offset and the member's declared
// type on the access node.  Each of these lowers to exactly the TAC the identical
// program with a file-scope tag produces.
// ---------------------------------------------------------------------------

// An array of an *anonymous* struct at block scope, subscripted.  Used to abort
// lower with "use of undeclared tag '__anon_1'": the ADD_PTR scale comes from
// get_size() of the decayed pointer's target, a clone that had lost the cache.
TEST_F(TranslateTest, BlockLocalAnonStructArray)
{
    std::string yaml = CompileToYaml(R"(
        char f(void)
        {
            struct { char t, r; } m[2];
            m[0].r = 98;
            return m[0].r;
        }
    )");
    // Element stride 6 (one word), then the byte member: the plain word address is
    // retyped to a fat byte pointer before the scale-1 member offset.
    EXPECT_NE(yaml.find("scale: 6"), std::string::npos);
    EXPECT_NE(yaml.find("kind: ptr_to_char_ptr"), std::string::npos);
    EXPECT_NE(yaml.find("kind: store_byte"), std::string::npos);
    EXPECT_NE(yaml.find("kind: load_byte"), std::string::npos);
}

// The same array with a *named* block-scope tag -- the failure was never about
// anonymity, and this one reported "use of undeclared tag 'S'".
TEST_F(TranslateTest, BlockLocalStructCharMemberArray)
{
    std::string yaml = CompileToYaml(R"(
        char f(void)
        {
            struct s { char t, r; } m[2];
            m[0].r = 98;
            return m[0].r;
        }
    )");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %m
      size: 12
      alignment: 6
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: uchar
          value: 98
      dst:
        kind: var
        name: %0
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %m
      dst:
        kind: var
        name: %1
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
      kind: ptr_to_char_ptr
      src:
        kind: var
        name: %3
      dst:
        kind: var
        name: %4
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %4
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 1
      dst:
        kind: var
        name: %5
    - instruction:
      kind: store_byte
      src:
        kind: var
        name: %0
      dst_ptr:
        kind: var
        name: %5
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %m
      dst:
        kind: var
        name: %6
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 0
      dst:
        kind: var
        name: %7
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %6
      index:
        kind: var
        name: %7
      scale: 6
      dst:
        kind: var
        name: %8
    - instruction:
      kind: ptr_to_char_ptr
      src:
        kind: var
        name: %8
      dst:
        kind: var
        name: %9
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %9
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 1
      dst:
        kind: var
        name: %10
    - instruction:
      kind: load_byte
      src_ptr:
        kind: var
        name: %10
      dst:
        kind: var
        name: %11
    - instruction:
      kind: return
      src:
        kind: var
        name: %11
)");
}

// The tag is declared on its own, so the variable's type node is a bare reference
// carrying no field list -- the member's declared type can only come from the
// annotation typecheck left on the access node.  Without it the byte member's base
// stayed a plain word address (read as byte #5 by the byte helpers), silently
// addressing the wrong byte instead of failing.
TEST_F(TranslateTest, BlockLocalStructSeparateTagDecl)
{
    std::string yaml = CompileToYaml(R"(
        char f(void)
        {
            struct s { char t, r; };
            struct s m;
            char *p = &m.r;
            return *p;
        }
    )");
    EXPECT_EQ(yaml, R"(- toplevel:
  kind: function
  name: f
  global: true
  body:
    - instruction:
      kind: allocate_local
      name: %m
      size: 6
      alignment: 6
    - instruction:
      kind: get_address
      src:
        kind: var
        name: %m
      dst:
        kind: var
        name: %0
    - instruction:
      kind: ptr_to_char_ptr
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
          value: 1
      scale: 1
      dst:
        kind: var
        name: %2
    - instruction:
      kind: copy
      src:
        kind: var
        name: %2
      dst:
        kind: var
        name: %p
    - instruction:
      kind: load_byte
      src_ptr:
        kind: var
        name: %p
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

// A char-array member of a block-scope struct decays to its address rather than
// being loaded.  That decision reads the member's *declared* type, which e->type no
// longer holds (typecheck decayed it to a pointer); an unresolved member used to
// fall through to a plain COPY_FROM_OFFSET, loading the first word as a value.
TEST_F(TranslateTest, BlockLocalStructCharArrayMemberDecays)
{
    std::string yaml = CompileToYaml(R"(
        char f(void)
        {
            struct s { int n; char b[4]; } m;
            char *p = m.b;
            return p[0];
        }
    )");
    EXPECT_EQ(yaml.find("copy_from_offset"), std::string::npos);
    EXPECT_NE(yaml.find("kind: get_address"), std::string::npos);
    EXPECT_NE(yaml.find("kind: ptr_to_char_ptr"), std::string::npos);
    EXPECT_NE(yaml.find("kind: load_byte"), std::string::npos);
}
