#include "pipeline_test_fixture.h"

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (75 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_PointerArithmetic)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that we propagate copies into AddPtr instructions */
int target(void) {
    int nested[3][23] = {{0, 1}, {2}};

    // we'll initially generate something like this:
    // index0 = SignExtend(1)
    // tmp2 = AddPtr(ptr=tmp1, index=index0, scale=92)
    // index1 = SignExtend(0)
    // tmp3 = AddPtr(ptr=tmp2, index=index1, scale=4)
    // return tmp3
    // But after constant folding and copy propagation, both AddPtr
    // instruction should have constant indices, so we won't need
    // any imul instructions in the final assembly (like we normally would to
    // implement AddPtr with a non-standard scale)
    return nested[1][0];
}
)SRC")),
              "add_ptr=3 allocate_local=1 binary=1 copy=1 copy_byte_to_offset=4 copy_to_offset=2 "
              "get_address=2 jump_if_not_zero=1 label=1 load=1 return=1 store=1");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_PropagateAllTypes)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate all arithmetic types, including doubles,
 * long and unsigned integers, and characters.
 * */


int target(void) {
    // propagate doubles
    double d = 1500.0;
    double d2 = d;

    int sum = (int)(d + d2);  // 3000

    // propagate chars
    char c = 250;  // will be converted to -6
    char c2 = c;
    sum = sum + (c2 + c);  // 2988

    // propagate unsigned char
    unsigned char uc = -1;  // will be converted to 255
    unsigned char uc2 = uc;
    sum = sum + uc + uc2;  // 3498

    // propagate unsigned long
    unsigned long ul = 18446744073709551615UL;  // ULONG_MAX
    unsigned long ul2 = ul + 3ul;               // wraps around to 2
    sum = sum + ul2;                            // 3500

    return sum;  // rewrite as "return 3500"
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3500
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_PropagateIntoTypeConversions)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we correctly propagate copies into type conversion instructions */

int target(void) {
    unsigned char uc = 250;
    int i = uc * 2;              // 500 - tests ZeroExtend
    double d = i * 1000.;        // 500000.0 - tests IntToDouble
    unsigned long ul = d / 6.0;  // 83333 - tests DoubleToUInt
    d = ul + 5.0;                // 83338 - tests UIntToDouble
    long l = -i;                 // -500 - tests SignExtend
    char c = l;                  // 12 - tests Truncate
    return d + i - c;            // 83826 - tests DoubleToInt
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 83826
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_PropagateNullPointer)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate 0 between integer and
 * different pointer types
 * */
long *target(void) {
    int *ptr = 0;
    long *ptr2 = (long *)ptr;
    return ptr2;  // this should be rewritten as 'return 0'
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: long
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_RedundantDoubleCopies)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate y = x and y = x if we can prove that x and y
 * already have the same values.
 * After copy propagation and cleanup unreachable code elimination,
 * target should contain no control-flow instructions.
 * Similar to int_only/redundant_copies.c but with doubles
 * */

double target(int flag, int flag2, double y) {
    double x = y;

    if (flag) {
        y = x;  // we can remove this because x and y already have the same
                // value
    }
    if (flag2) {
        x = y;  // we can remove this because x and y already have the same
                // value
    }
    return x + y;
}
)SRC"),
              R"OPT(- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
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
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag2
  target: %2
- instruction:
  kind: jump
  target: %3
- instruction:
  kind: label
  name: %2
- instruction:
  kind: label
  name: %3
- instruction:
  kind: binary
  op: add_double
  src1:
    kind: var
    name: %y
  src2:
    kind: var
    name: %y
  dst:
    kind: var
    name: %4
- instruction:
  kind: return
  src:
    kind: var
    name: %4
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (31 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_RedundantStructCopies)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that we eliminate y = x and y = x if we can prove that x and y
 * already have the same values.
 * After copy propagation and cleanup unreachable code elimination,
 * target should contain no control-flow instructions.
 * Similar to int_only/redundant_copies.c but with structs
 * */

struct s {
    double d;
    int i;
};

double target(int flag, int flag2, struct s y) {
    struct s x = y;

    if (flag) {
        y = x;  // we can remove this because x and y already have the same
                // value
    }
    if (flag2) {
        x = y;  // we can remove this because x and y already have the same
                // value
    }
    return x.d + y.d + x.i + y.i;
}
)SRC")),
              "allocate_local=1 binary=3 copy_from_offset=10 copy_to_offset=6 int_to_double=2 jump=2 jump_if_zero=2 label=4 return=1");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (47 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_StoreDoesntKill)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that updating a value through a pointer does not kill that pointer */

void exit(int status);  // from standard library

void check_pointers(int a, int b, int *ptr1, int *ptr2) {
    if (a != 100 || b != 101) {
        exit(1);
    }

    if (*ptr1 != 60 || *ptr2 != 61) {
        exit(2);
    }
    return;
}

int callee(int *p1, int *p2) {
    if (p1 != p2) {
        exit(3);
    }
    if (*p2 != 10) {
        exit(4);
    }
    return 0;  // success
}

int target(int *ptr, int *ptr2) {
    // first, call another function, with these arguments
    // in different positions than in target or callee, so we can't
    // coalesce them with the param-passing registers or each other
    check_pointers(100, 101, ptr, ptr2);

    ptr2 = ptr;  // generate copy
    *ptr = 10;   // Store(10, ptr) does NOT kill copy

    // both arguments to callee should be the same
    return callee(ptr, ptr2);
}
)SRC")),
              "binary=6 fun_call=6 jump=4 jump_if_not_zero=2 jump_if_zero=4 label=10 load=3 return=3 store=1");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_CopyToOffset)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that CopyToOffset kills its destination */
struct s {
    int x;
    int y;
};

int main(void) {
    static struct s s1 = {1, 2};
    struct s s2 = {3, 4};
    s1 = s2;   // generate s1 = s2
    s2.x = 5;  // kill s1 = s2

    return s1.x;  // make sure we don't propagate s2 into this return statement
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %s2
  size: 8
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 3
  dst: %s2
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst: %s2
  offset: 4
- instruction:
  kind: copy_from_offset
  src: %s2
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %0
  dst: s1
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %s2
  offset: 4
  dst:
    kind: var
    name: %1
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %1
  dst: s1
  offset: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 5
  dst: %s2
  offset: 0
- instruction:
  kind: copy_from_offset
  src: s1
  offset: 0
  dst:
    kind: var
    name: %3
- instruction:
  kind: return
  src:
    kind: var
    name: %3
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_DontPropagateAddrOf)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we don't propagate copies into AddrOf instructions */
int main(void) {
    long x = 1;
    long y = 2;
    x = y;            // gen x = y
    return &x == &y;  // don't rewrite as &y == &y
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: long
      value: 2
  dst:
    kind: var
    name: %y
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: long
      value: 2
  dst:
    kind: var
    name: %x
- instruction:
  kind: get_address
  src:
    kind: var
    name: %x
  dst:
    kind: var
    name: %2
- instruction:
  kind: get_address
  src:
    kind: var
    name: %y
  dst:
    kind: var
    name: %3
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %2
  src2:
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
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_StaticAreAliased)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we consider all static variables aliased,
 * so store kills copies to/from these variables */
int stat;

int target(int *stat_ptr) {
    int a = 0;
    a = stat;       // gen a = stat
    *stat_ptr = 8;  // kill a = stat
    return a;       // make sure we don't rewrite as 'return stat'
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: stat
  dst:
    kind: var
    name: %a
- instruction:
  kind: store
  src:
    kind: constant
    const:
      kind: int
      value: 8
  dst_ptr:
    kind: var
    name: %stat_ptr
- instruction:
  kind: return
  src:
    kind: var
    name: %a
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (62 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_StoreKillsAliased)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that store instruction kills any copy
 * whose source or destination is aliased
 */

// Store kills copy w/ aliased src
int aliased_src(int flag, int x, int *ptr) {
    int y = x;  // gen y = x
    if (flag) {
        ptr = &x;  // x is aliased
    }
    *ptr = 100;  // kill y = x

    return y;  // make sure this isn't rewritten as 'return x'
}

// Store kills copy w/ aliased dst
int aliased_dst(int flag, int x, int *ptr) {
    int y = x;  // gen y = x
    if (flag) {
        ptr = &y;  // y is aliased
    }
    *ptr = 100;  // kill y = x
    return y;    // make sure this isn't rewritten as 'return x'
}

int main(void) {
    int i = 0;
    // first call aliased_src w/ flag = 0;
    // Store instruction will update i
    if (aliased_src(0, 1, &i) != 1) {
        return 1;  // fail
    }
    if (i != 100) {
        return 2;  // fail
    }

    // call again w/ flag = 1; won't update i or return value
    i = 0;
    if (aliased_src(1, 2, &i) != 2) {
        return 3;  // fail
    }
    if (i != 0) {
        return 4;  // fail
    }

    // call aliased_dst with flag = 0;
    // Store instruction will update i
    if (aliased_dst(0, 5, &i) != 5) {
        return 5;  // fail
    }

    if (i != 100) {
        return 6;  // fail
    }
    // call aliased_dst with flag = 1;
    // Store won't update i, will update return value
    i = 0;
    if (aliased_dst(1, 5, &i) != 100) {
        return 7;  // fail
    }

    if (i != 0) {
        return 8;  // fail
    }

    return 0;  // success
}
)SRC")),
              "binary=8 copy=7 fun_call=4 get_address=6 jump=2 jump_if_zero=10 label=12 return=11 store=2");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_TypeConversion)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we don't propagate copies between values of different types,
 * since that will lead to incorrect assembly generation (e.g. for comparisons
 * and division). (If we had separate TACKY operators for signed vs unsigned
 * comparisons/division/remainder operations instead of inferring signedness
 * from TACKY operand types, it would be save to propagate these copies.)
 */
int target(int i) {
    unsigned int j = i;
    return (j / 100);  // make sure we don't rewrite as i / 100
                       // correct answer is 42949670,
                       // but if we propagate this copy we'll return -2
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: divide_unsigned
  src1:
    kind: var
    name: %i
  src2:
    kind: constant
    const:
      kind: int
      value: 100
  dst:
    kind: var
    name: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %2
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_ZeroNegZeroDifferent)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure our copy propagation pass recognizes 0.0 and -0.0
 * as distinct values, even though they compare equal with standard
 * floating-point comparison operators
 * */
double copysign(double x, double y);

double target(int flag) {
    double result = 0.0;  // gen result = 0.0
    if (flag) {
        result = -0.0;  // gen result = -0.0
    }

    // can't propagate value of result because it has
    // different values on different paths
    return result;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: double
      value: 0x0p+0
  dst:
    kind: var
    name: %result
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: double
      value: -0x0p+0
  dst:
    kind: var
    name: %result
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
    kind: var
    name: %result
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_CopyUnion)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Propagate copies of whole unions */

union u {
    long l;
    int i;
};

int callee(union u a, union u b) {
    if (a.l != -100) {
        return 1; // fail
    }
    if (b.l != -100) {
        return 2; // fail
    }

    return 0; // success
}

int target(void) {
    union u u1 = {0};
    union u u2 = {-100};
    u1 = u2; // generates u1 = u2

    // Make sure we pass the same value for both arguments.
    // We don't need to worry that register coalescing
    // will interfere with this test, because unions
    // won't be stored in registers
    return callee(u1, u2);
}
)SRC"),
              R"OPT(- instruction:
  kind: copy_from_offset
  src: %a
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %0
  src2:
    kind: constant
    const:
      kind: long
      value: -100
  dst:
    kind: var
    name: %3
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %3
  target: %4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %4
- instruction:
  kind: copy_from_offset
  src: %b
  offset: 0
  dst:
    kind: var
    name: %6
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %6
  src2:
    kind: constant
    const:
      kind: long
      value: -100
  dst:
    kind: var
    name: %9
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %9
  target: %10
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: label
  name: %10
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: allocate_local
  name: %u1
  size: 8
  alignment: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: 0
  dst: %u1
  offset: 0
- instruction:
  kind: allocate_local
  name: %u2
  size: 8
  alignment: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: -100
  dst: %u2
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %u2
  offset: 0
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %3
  dst: %u1
  offset: 0
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %u1
    - val:
      kind: var
      name: %u2
  dst:
    kind: var
    name: %5
- instruction:
  kind: return
  src:
    kind: var
    name: %5
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (75 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_PointerCompoundAssignment)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* We can calculate constant offset for +=/-= with pointers into arrays;
 * similar to pointer_arithmetic.c
 */

int target(void) {
    int nested[3][23] = { {0, 1}, {2} };
    int (* ptr)[23] = nested;
    ptr += 2;
    return *ptr[0];
}
)SRC")),
              "add_ptr=2 allocate_local=1 binary=1 copy=1 copy_byte_to_offset=4 copy_to_offset=2 "
              "get_address=2 jump_if_not_zero=1 label=1 load=1 return=1 store=1");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (75 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_PointerIncr)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* We can calculate constant offset for ++/-- with pointers into arrays;
 * similar to pointer_arithmetic.c
 */
int target(void) {
    int nested[3][23] = { {0, 1}, {2} };
    int (* ptr)[23] = nested;
    ptr++;
    return *ptr[0];
}
)SRC")),
              "add_ptr=2 allocate_local=1 binary=1 copy=1 copy_byte_to_offset=4 copy_to_offset=2 "
              "get_address=2 jump_if_not_zero=1 label=1 load=1 return=1 store=1");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_RedundantNanCopy)
{
    OptimizeYaml(R"SRC(
/* Make sure we can eliminate redundant copies where source is NaN
 * (which requires us to compare NaN values appropriately and recognize when
 * they're the same even though NaN doesn't compare equal to itself).
 * We should be able to eliminate all control-flow instructions from target
 */

int double_isnan(double d); // defined in tests/chapter_13/helper_libs/nan.c

double na;

int target(int flag) {
    na = 0.0 / 0.0;
    double d = 0.0 / 0.0;
    if (flag) {
        na = d; // same value it already is; can delete this
    }
    return 0;
}
)SRC");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_RedundantUnionCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate y = x and y = x if we can prove that x and y
 * already have the same values.
 * After copy propagation and cleanup unreachable code elimination,
 * target should contain no control-flow instructions.
 * Similar to int_only/redundant_copies.c but with unions
 * */

union u {
    double d;
    int i;
};

double target(int flag, int flag2, union u y) {
    union u x = y;

    if (flag) {
        y = x;  // we can remove this because x and y already have the same
                // value
    }
    if (flag2) {
        x = y;  // we can remove this because x and y already have the same
                // value
    }
    return x.d + y.d;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %x
  size: 8
  alignment: 8
- instruction:
  kind: copy_from_offset
  src: %y
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %0
  dst: %x
  offset: 0
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %1
- instruction:
  kind: copy_from_offset
  src: %x
  offset: 0
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %3
  dst: %y
  offset: 0
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: label
  name: %1
- instruction:
  kind: label
  name: %2
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag2
  target: %5
- instruction:
  kind: copy_from_offset
  src: %y
  offset: 0
  dst:
    kind: var
    name: %7
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %7
  dst: %x
  offset: 0
- instruction:
  kind: jump
  target: %6
- instruction:
  kind: label
  name: %5
- instruction:
  kind: label
  name: %6
- instruction:
  kind: copy_from_offset
  src: %x
  offset: 0
  dst:
    kind: var
    name: %9
- instruction:
  kind: copy_from_offset
  src: %y
  offset: 0
  dst:
    kind: var
    name: %10
- instruction:
  kind: binary
  op: add_double
  src1:
    kind: var
    name: %9
  src2:
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
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_UpdateUnionMember)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Writing to union member kills previous copies to/from that union */

union u {
    long l;
    int i;
};

int main(void) {
    static union u u1 = {20};
    union u u2 = {3};
    u1 = u2; // generate u1 = u2
    u2.i = 0; // kill u1 = u2

    return u1.i;  // make sure we don't propagate u2 into this return statement
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %u2
  size: 8
  alignment: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: 3
  dst: %u2
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %u2
  offset: 0
  dst:
    kind: var
    name: %1
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %1
  dst: u1
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst: %u2
  offset: 0
- instruction:
  kind: copy_from_offset
  src: u1
  offset: 0
  dst:
    kind: var
    name: %3
- instruction:
  kind: return
  src:
    kind: var
    name: %3
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_DontPropagate_UpdateUnionMember2)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Copy to one member of a union kills previous copy to other member.
 * The easiest way to handle this, which is in line with how we handle structs,
 * is to not attempt to propagate copies to/from union members at all.
 * But if you do implement a more sophisticated copy propagation pass that
 * tracks copies to/from union members, you need to account for the fact
 * that they overlap.
 */

union u {
    long l;
    int i;
};

int main(void) {
    union u x;
    x.i = 100;
    x.l = 200; // clobber x.i;
    return x.i; // should be 200 (due to type punning), not 100
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %x
  size: 8
  alignment: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst: %x
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: 200
  dst: %x
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %x
  offset: 0
  dst:
    kind: var
    name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %1
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DeadStoreStaticVar)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate dead stores to static and global variables */

int i = 0;

int target(int arg) {
    i = 5;  // dead store
    i = arg;
    return i + 1;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %arg
  dst:
    kind: var
    name: i
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %arg
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
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DeleteArithmeticOps)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* In most of our test cases, the dead store we remove is a Copy.
 * This test case validates that we can remove dead
 * Binary and Unary instructions too.
 * */


int a = 1;
int b = 2;

int target(void) {
    // everything except the Return instruction should be optimized away.
    int unused = a * -b;
    return 5;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_ElimSecondCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* We can recognize that one store to a variable is a dead store,
 * but another store to that variable at a different point in the program
 * is not.
 * */
int callee(int arg) {
    return arg * 2;
}

int target(int arg, int flag) {
    int x = arg + 1;   // not a dead store
    if (flag) {
        // make sure x has more than one possible value,
        // so copy prop doesn't just replace it with a temporary
        // variable callee
        x = arg - 1;
    }
    int y = callee(x); // this generates x
    x = 100;  // dead store
    return y;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: %arg
  src2:
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
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %arg
  src2:
    kind: constant
    const:
      kind: int
      value: 1
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
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %1
- instruction:
  kind: binary
  op: subtract
  src1:
    kind: var
    name: %arg
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: %x
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: label
  name: %1
- instruction:
  kind: label
  name: %2
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %x
  dst:
    kind: var
    name: %4
- instruction:
  kind: return
  src:
    kind: var
    name: %4
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_Fig1911)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* We recognize an update to some variable as a dead store
 * when there are multiple paths from the store to some use of that
 * variable, and it's killed by different instructions
 * on those different paths.
 * This example is loosely based on Figure 19-11.
 * */
int callee(void) {
    return 4;
}

int callee2(void) {
    return 5;
}

int target(int flag) {
    int x = 10;  // this is a dead store; make sure its eliminated
    if (flag) {
        x = callee(); // this kills x; it's dead at earlier points
    } else {
        x = callee2(); // this kills x; it's dead at earlier points
    }
    return x; // this generates x; it's live at earlier points
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: fun_call
  fun_name: callee
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
    name: %x
- instruction:
  kind: jump
  target: %1
- instruction:
  kind: label
  name: %0
- instruction:
  kind: fun_call
  fun_name: callee2
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: %x
- instruction:
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %x
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_InitializeBlocksWithEmptySet)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we initialize each block in the CFG with an empty set of
 * live variables. Specifically, this test will fail if each block is
 * initialized with the set of all static variables.
 */


int j = 3;
int target(void) {
    static int i;
    i = 10;  // dead store, b/c i is killed on path to exit
             // but if we initially think i is live at the start of the
             // while loop, our analysis will never figure out that it's dead
             // here
    while (j > 0) {
        j = j - 1;
    }
    i = 0;
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: label
  name: %L1
- instruction:
  kind: binary
  op: greater_than
  src1:
    kind: var
    name: j
  src2:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: %2
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %2
  target: %L0
- instruction:
  kind: binary
  op: subtract
  src1:
    kind: var
    name: j
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: j
- instruction:
  kind: jump
  target: %L1
- instruction:
  kind: label
  name: %L0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: i
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_LoopDeadStore)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can detect dead stores in a function with a loop */
int putch(int c);  // from standard library

int target(void) {
    int x = 5;   // dead store
    int y = 65;  // not a dead store
    do {
        x = y + 2;  // kill x, gen y
        if (y > 70) {
            // make sure we assign to x on multiple paths
            // so copy prop doesn't replace it entirely
            x = y + 3;
        }
        y = putch(x) + 3;  // gen x and y
    } while (y < 90);
    if (x != 90) {
        return 1;  // fail
    }
    if (y != 93) {
        return 2;  // fail
    }
    return 0;  // success
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 65
  dst:
    kind: var
    name: %y
- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %y
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: %x
- instruction:
  kind: binary
  op: greater_than
  src1:
    kind: var
    name: %y
  src2:
    kind: constant
    const:
      kind: int
      value: 70
  dst:
    kind: var
    name: %4
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %4
  target: %5
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %y
  src2:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: %7
- instruction:
  kind: copy
  src:
    kind: var
    name: %7
  dst:
    kind: var
    name: %x
- instruction:
  kind: jump
  target: %6
- instruction:
  kind: label
  name: %5
- instruction:
  kind: label
  name: %6
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %x
  dst:
    kind: var
    name: %8
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %8
  src2:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: %9
- instruction:
  kind: copy
  src:
    kind: var
    name: %9
  dst:
    kind: var
    name: %y
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: %9
  src2:
    kind: constant
    const:
      kind: int
      value: 90
  dst:
    kind: var
    name: %10
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %10
  target: %2
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %x
  src2:
    kind: constant
    const:
      kind: int
      value: 90
  dst:
    kind: var
    name: %11
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %11
  target: %12
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %12
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %9
  src2:
    kind: constant
    const:
      kind: int
      value: 93
  dst:
    kind: var
    name: %14
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %14
  target: %15
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: label
  name: %15
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_Simple)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* A basic test case for eliminating a dead store */


int target(void) {
    int x = 10; // this is a dead store
    return 3;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_StaticNotAlwaysLive)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure the meet operator doesn't always assume static variables are live;
 * they're only generated by uses, function calls, and EXIT.
 * Test this using a program that never reaches EXIT (but does terminate
 * by caling the exit function indirectly)
 * */

int exit_wrapper(int status);  // defined in chapter_19/libraries/exit.c

int i;

int target(void) {
    i = 30;  // dead store!
    // i isn't killed in this block but it's killed on all paths to function
    // call
    int counter = 0;

    do {
        if (counter < 10) {
            i = counter + 1;
        } else {
            i = counter + 2;
        }
        if (counter > 20) {
            exit_wrapper(i);
        }
        counter = counter + 1;
    } while (1);
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: %counter
- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: %counter
  src2:
    kind: constant
    const:
      kind: int
      value: 10
  dst:
    kind: var
    name: %3
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %3
  target: %4
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %counter
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %6
- instruction:
  kind: copy
  src:
    kind: var
    name: %6
  dst:
    kind: var
    name: i
- instruction:
  kind: jump
  target: %5
- instruction:
  kind: label
  name: %4
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %counter
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %7
- instruction:
  kind: copy
  src:
    kind: var
    name: %7
  dst:
    kind: var
    name: i
- instruction:
  kind: label
  name: %5
- instruction:
  kind: binary
  op: greater_than
  src1:
    kind: var
    name: %counter
  src2:
    kind: constant
    const:
      kind: int
      value: 20
  dst:
    kind: var
    name: %8
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %8
  target: %9
- instruction:
  kind: fun_call
  fun_name: exit_wrapper
  args:
    - val:
      kind: var
      name: i
  dst:
    kind: var
    name: %11
- instruction:
  kind: jump
  target: %10
- instruction:
  kind: label
  name: %9
- instruction:
  kind: label
  name: %10
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %counter
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %12
- instruction:
  kind: copy
  src:
    kind: var
    name: %12
  dst:
    kind: var
    name: %counter
- instruction:
  kind: jump
  target: %2
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_AddAllToWorklist)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we add every basic block to the worklist
 * at the start of the iterative algorithm
 */
int putch(int c);

int f(int arg) {
    int x = 76;
    if (arg < 10) {
        // give x multiple values on different paths
        // so we can't propagate it
        x = 77;
    }
    // no live variables flow into this basic block from its successor,
    // bu we still need to process it to learn that x is live
    if (arg)
        putch(x);
    return 0;
}

int main(void) {
    f(0);
    f(1);
    f(11);
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 76
  dst:
    kind: var
    name: %x
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: %arg
  src2:
    kind: constant
    const:
      kind: int
      value: 10
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
    kind: constant
    const:
      kind: int
      value: 77
  dst:
    kind: var
    name: %x
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: label
  name: %1
- instruction:
  kind: label
  name: %2
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %arg
  target: %3
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %x
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump
  target: %4
- instruction:
  kind: label
  name: %3
- instruction:
  kind: label
  name: %4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 1
  dst:
    kind: var
    name: %1
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 11
  dst:
    kind: var
    name: %2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_DontRemoveFuncall)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we never optimize away function calls,
 * even if they're dead stores (i.e. update dead variables)
 * because they can have side effects */

int putch(int c);

int main(void) {
    // Make sure we don't optimize away this function call.
    // It would be safe to keep the function call, but optimize out
    // the store to x (i.e. get rid of movl %eax, %x), but our implementation
    // doesn't.
    int x = putch(67);
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 67
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_Loop)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test case where a block is its own predecessor
 * */

int putch(int c);

int fib(int count) {
    int n0 = 0;
    int n1 = 1;
    int i = 0;
    do {
        int n2 = n0 + n1;
        n0 = n1;  // not a dead store b/c n0 is used again in the next loop
                  // iteration, in n2 = n0 + n1
        n1 = n2;
        i = i + 1;
    } while (i < count);
    return n1;
}

int main(void) {
    return (fib(20) == 10946);
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: %n0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %n1
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: %i
- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %n0
  src2:
    kind: var
    name: %n1
  dst:
    kind: var
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %n1
  dst:
    kind: var
    name: %n0
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: %n1
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %i
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %4
- instruction:
  kind: copy
  src:
    kind: var
    name: %4
  dst:
    kind: var
    name: %i
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: %4
  src2:
    kind: var
    name: %count
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %5
  target: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %3
- instruction:
  kind: fun_call
  fun_name: fib
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 20
  dst:
    kind: var
    name: %0
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %0
  src2:
    kind: constant
    const:
      kind: int
      value: 10946
  dst:
    kind: var
    name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %1
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (32 instructions).
TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_NestedLoops)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that the algorithm runs until it converges;
 * some blocks need to be visited three times before the algorithm converges
 * */

int putch(int c);

int target(int a, int b, int c, int d) {
    while (a > 0) {
        while (c > 0) {
            putch(c + d);
            c = c - 1;
            if (d % 2) {
                c = c - 2;
            }
        }

        while (b > 0) {
            c = 10;  // this is not dead, b/c it's used in previous while
                     // loop, but it takes multiple passes for that
                     // information to propagate to this point
            b = b - 1;
        }

        a = a - 1;
    }
    return 0;
}
)SRC")),
              "binary=9 copy=5 fun_call=1 jump=4 jump_if_zero=4 label=8 return=1");
}
