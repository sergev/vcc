#include "pipeline_test_fixture.h"

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (364 instructions).
TEST_F(PipelineTest, Chapter19_CP_IntOnly_NestedLoops)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* A test case that takes even longer than fig_19_8.c to converge;
 * some blocks need to be visited three times before the algorithm converges.
 * */

static int outer_flag = 0;
static int inner_flag = 1;

// functions to validate args and control number of loop iterations
int inner_loop1(int a, int b, int c, int d, int e, int f) {
    // this should be the second loop iteration, so b, c, and e should be
    // updated, but a, d, and f shouldn't
    if (a != 1 || b != 11 || c != 12 || d != 4 || e != 20 || f != 100) {
        return 0;  // fail
    }
    return 1;  // success
}

int inner_loop2(int a, int b, int c, int d, int e, int f) {
    if (outer_flag == 0) {
        // first call: no variables have been updated
        if (a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 100) {
            return 0;  // fail
        }
    } else {
        // second call: a, b, c, and e have been updated
        if (a != 10 || b != 11 || c != 12 || d != 4 || e != 20 || f != 100) {
            return 0;  // fail
        }
    }

    return 1;  // success
}

int inner_loop3(int a, int b, int c, int d, int e, int f) {
    if (outer_flag == 0) {
        if (inner_flag == 2) {
            // first call to this function: only b has been updated
            if (a != 1 || b != 11 || c != 3 || d != 4 || e != 5 || f != 100) {
                return 0;  // fail
            }
        } else {
            // second iteration through inner loop: b and c have been updated
            if (a != 1 || b != 11 || c != 12 || d != 4 || e != 5 || f != 100) {
                return 0;  // fail
            }
        }
    } else {
        // second time through outer loop: a, b, c, and e have been updated
        if (a != 10 || b != 11 || c != 12 || d != 4 || e != 20 || f != 100) {
            return 0;  // fail
        }
    }

    return 1;  // success
}

int inner_loop4(int a, int b, int c, int d, int e, int f) {
    // this never runs
    // use all parameters to silence compiler warnings
    return a + b + c + d + e + f;
}

int validate(int a, int b, int c, int d, int e, int f) {
    // a, b, c, and e have been updated
    if (a != 10 || b != 11 || c != 12 || d != 4 || e != 20 || f != 100) {
        return 0;  // fail
    }
    return 1;  // success
}

int target(void) {
    // we can propagate f throughout whole function, but nothing else
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 100;

    // go through outer loop twice
    while (outer_flag < 2) {
        // skip this loop on first outer iteration, run on second
        while (inner_flag < 1) {
            if (!inner_loop1(a, b, c, d, e, f)) {
                return 1;  // fail
            }
            a = 10;
            inner_flag = 1;
        }

        // do this loop once per outer iteration
        while (inner_flag < 2) {
            if (!inner_loop2(a, b, c, d, e, f)) {
                return 2;  // fail
            }
            b = 11;
            // set inner_flag to 2 so this loop doesn't run again but next loop
            // runs twice
            inner_flag = 2;
        }

        // do this loop twice per outer iteration
        while (inner_flag < 4) {
            if (!inner_loop3(a, b, c, d, e, f)) {
                return 3;  // fail
            }
            // increment inner_flag so this loop runs twice
            inner_flag = inner_flag + 1;
            c = 12;
        }

        // skip this loop both times
        while (inner_flag < 4) {
            inner_loop4(a, b, c, d, e, f);
            d = 13;
        }

        e = 20;
        f = 100;
        outer_flag = outer_flag + 1;
        // reset inner flag
        inner_flag = 0;
    }

    if (!validate(a, b, c, d, e, f)) {  // we can propagate f into this call
        return 4;
    }

    return 0;  // success
}
)SRC")),
              "binary=57 copy=15 fun_call=5 jump=8 jump_if_not_zero=39 jump_if_zero=15 label=34 return=17");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateIntoComplexExpressions)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate values from copies
 * into unary expressions, binary expressions,
 * and conditional jumps.
 * */
int target(void) {
    int x = 100;
    int y = -x * 3 + 300;
    return (y ? x % 3 : x / 4);
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 25
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateParams)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate copies both to and from function parameters;
 * similar to propagate_var, but with paramters instead of variables.
 * */
int callee(int a, int b) {
    return a * b;
}
int f(void) {
    return 3;
}
int globl = 0;
int set_globvar(void) {
    globl = 4;
    return 0;
}
int target(int a, int b) {
    b = a;  // propagate copy from a to b

    // call another function before callee so we can't coalesce a into EDI
    // or b into ESI; otherwise, once we implement register coalescing,
    // it will look like we've propagated the copy even if we haven't
    set_globvar();
    // look for: same value passed in ESI, EDI
    int product = callee(a, b);

    // now update b while a is live, so we can't coalesce them
    // into the same register; otherwise it will look like we've propagated
    // the copy even if we haven't
    b = f();
    return (product + a - b);  // return 5 * 5 + 5 - 3 ==> 27
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: globl
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: fun_call
  fun_name: set_globvar
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %a
    - val:
      kind: var
      name: %a
  dst:
    kind: var
    name: %1
- instruction:
  kind: fun_call
  fun_name: f
  dst:
    kind: var
    name: %2
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %1
  src2:
    kind: var
    name: %a
  dst:
    kind: var
    name: %3
- instruction:
  kind: binary
  op: subtract
  src1:
    kind: var
    name: %3
  src2:
    kind: var
    name: %2
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

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateStatic)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate copies to variables with static storage
 * duration */
int x = 0;

int target(void) {
    // we can propagate value of x, even though it has static storage duration,
    // b/c no intervening reads/writes
    x = 10;
    return x;  // should become "return 10"
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 10
  dst:
    kind: var
    name: x
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateStaticVar)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Propagate a copy where the source value is a variable with static storage
 * duration, in a function with no control flow strucures.
 * */
int callee(int a, int b) {
    return a + b;
}

int target(void) {
    static int x = 3;

    // y also needs to be static so we can't coalesce
    // it into ESI once we implement register coalescing;
    // otherwise it may look like we've propagated a copy
    // when we haven't
    static int y = 0;

    y = x;  // make sure we propagate this into function call

    // look for: same value passed in ESI, EDI
    int sum = callee(x, y);

    // increment x to make sure we're not just propagating
    // x's initial value. (If we are, we'll get the wrong result on
    // the second call to target
    x = x + 1;
    return sum;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: x
  dst:
    kind: var
    name: y
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: x
    - val:
      kind: var
      name: x
  dst:
    kind: var
    name: %0
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: x
  src2:
    kind: constant
    const:
      kind: int
      value: 1
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
    name: x
- instruction:
  kind: return
  src:
    kind: var
    name: %0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateVar)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate copies where the source value is
 * a variable, in a function with no control flow strucures.
 * */
int callee(int a, int b) {
    return a + b;
}
int f(void) {
    return 3;
}

int globl = 0;
int set_globvar(void) {
    globl = 4;
    return 0;
}

int target(void) {
    int x = f();
    int y = x;  // propagate this copy into function call

    // call another function before callee so we can't coalesce x into EDI
    // or y into ESI; otherwise it will look like we've propagated x as
    // a function argument even if we haven't
    set_globvar();

    // look for: same value passed in ESI, EDI
    int sum = callee(x, y);

    // now update y while x is live, so we can't coalesce them
    // into the same register; otherwise it will look like we've propagated x as
    // a function argument even if we haven't
    y = f();
    return (sum + x * y);  // return 6 + 9 ==> 15
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: globl
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
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: set_globvar
  dst:
    kind: var
    name: %1
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %0
    - val:
      kind: var
      name: %0
  dst:
    kind: var
    name: %2
- instruction:
  kind: fun_call
  fun_name: f
  dst:
    kind: var
    name: %3
- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: %0
  src2:
    kind: var
    name: %3
  dst:
    kind: var
    name: %4
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %2
  src2:
    kind: var
    name: %4
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

TEST_F(PipelineTest, Chapter19_CP_IntOnly_RedundantCopies)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate y = x and y = x if we can prove that x and y
 * already have the same values.
 * After copy propagation and cleanup unreachable code elimination,
 * target should contain no control-flow instructions
 * */

int target(int flag, int flag2, int y) {
    int x = y;

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
  op: add
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

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_AddAllBlocksToWorklist)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we add every block to the worklist
 * at the start of the iterative algoirthm
 * */

int global;

int flag = 1;

int f(void) {
    global = 100;
    return 0;
}

int main(void) {
    // Initially, we annotate every block with 'global = 0',
    // which is the only copy in 'main'.
    global = 0;

    // The copy 'global = 0' reaches this if statement. If we only add a block
    // to the worklist when its predecessor's outgoing copies change,
    // instead of adding every block to the initial worklist, we won't
    // visit this block at all, and we won't see the call to f(),
    // which kills this copy
    if (flag) {
        f();  // kill copy to global
    }

    // If we didn't visit that if statement, we'll incorrectly
    // rewrite this as 'return 0'.
    return global;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst:
    kind: var
    name: global
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst:
    kind: var
    name: global
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: flag
  target: %0
- instruction:
  kind: fun_call
  fun_name: f
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
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: global
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_DestKilled)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that updating a variable kills previous
 * copies to that variable
 * */
int foo(void) {
    return 4;
}

int main(void) {
    int x = 3;
    x = foo();  // this kills x = 3
    return x;   // don't propagate x = 3
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
  kind: fun_call
  fun_name: foo
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

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_Listing1914)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that function calls kill copies to static local variables;
 * example from Listing 19-14
 * */

int indirect_update(void);

int f(int new_total) {
    static int total = 0;
    total = new_total;  // generate copy total = new_total
    if (total > 100)
        return 0;
    total = 10;         // generate total = 10
    indirect_update();  // kill total = 10 (b/c total is static)
    return total;       // can't rewrite as 'return 10'
}

int indirect_update(void) {
    f(101);  // this will update 'total'
    return 0;
}

int main(void) {
    return f(1);  // expected return value: 101
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %new_total
  dst:
    kind: var
    name: total
- instruction:
  kind: binary
  op: greater_than
  src1:
    kind: var
    name: %new_total
  src2:
    kind: constant
    const:
      kind: int
      value: 100
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
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: label
  name: %1
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 10
  dst:
    kind: var
    name: total
- instruction:
  kind: fun_call
  fun_name: indirect_update
  dst:
    kind: var
    name: %3
- instruction:
  kind: return
  src:
    kind: var
    name: total
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 101
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
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_MultiValues)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test for meet operator: if different copies to a variable
 * reach the ends of a block's predecessors, no copies to that variable
 * reach that block
 * */

int multi_path(int flag) {
    int x = 3;  // generate x = 3
    if (flag)
        x = 4;  // kill x = 3, generate x = 4

    // One predecessor of our final block has outgoing copy x = 3,
    // the other has outgoing copy x = 4. Their intersection is the empty set,
    // so we can't propagate any value to this return statement.
    return x;
}

int main(void) {
    if (multi_path(1) != 4) {
        return 1;
    }

    if (multi_path(0) != 3) {
        return 2;
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
      value: 3
  dst:
    kind: var
    name: %x
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
      kind: int
      value: 4
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
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %x
- instruction:
  kind: fun_call
  fun_name: multi_path
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 1
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
      kind: int
      value: 4
  dst:
    kind: var
    name: %1
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %1
  target: %2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %2
- instruction:
  kind: fun_call
  fun_name: multi_path
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 0
  dst:
    kind: var
    name: %4
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %4
  src2:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %5
  target: %6
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: label
  name: %6
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_NoCopiesReachEntry)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we track that the set of reaching copies from ENTRY is empty */
int target(int a, int flag) {
    if (flag) {
        // if we initialized ENTRY with the set of all copies in target,
        // we'll think that a = 10 reaches this return statement,
        // and incorrectly rewrite it as 'return 10'
        return a;
    }

    a = 10;  // initialize ENTRY w/ empty set of copies, not including this one
    return a;
}
)SRC"),
              R"OPT(- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %a
- instruction:
  kind: label
  name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_OneReachingCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If a copy appears on one path to a block but not on all
 * paths to that block, it doesn't reach that block.
 * */

int three(void) {
    return 3;
}

int target(int flag) {
    int x;
    if (flag)
        x = 10;
    else
        x = three();
    // one predecessor contains copy x = 10, other predecessor contains no
    // copies to x, so no copies reach 'return x'
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
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
      kind: int
      value: 10
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
  fun_name: three
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
  kind: label
  name: %1
- instruction:
  kind: return
  src:
    kind: var
    name: %x
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_SourceKilled)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Basic test that updating the source of a copy kills that copy */

int x = 10;

int main(void) {
    int y = x;      // generate y = x
    x = 4;          // kill y = x
    if (y != 10) {  // can't replace y with x here
        return 1;
    }
    if (x != 4) {
        return 2;
    }
    return 0;  // success
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: x
  dst:
    kind: var
    name: %y
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: x
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %y
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
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
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
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_SourceKilledOnOnePath)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If a copy is generated on all paths to a block,
 * and its source is updated on one path,
 * it doesn't reach that block
 * */

int putch(int c);  // from standard library

int f(int src, int flag) {
    int x = src;  // generate x = src
    if (flag) {
        src = 65;  // kill x = src
    }
    putch(src);  // use src so assignment doesn't get optimized away entirely
    return x;      // make sure we don't rewrite this as 'return src'
}

int main(void) {
    // first call f with flag = 0;
    // validate return value, and make sure
    // src is not updated
    if (f(68, 0) != 68) {
        return 1;
    }

    // now call f with flag = 1;
    // validate return value and make sure
    // src is updated
    if (f(70, 1) != 70) {
        return 2;
    }

    return 0;  // success
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %src
  dst:
    kind: var
    name: %x
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
      kind: int
      value: 65
  dst:
    kind: var
    name: %src
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
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %src
  dst:
    kind: var
    name: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %x
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 68
    - val:
      kind: constant
      const:
        kind: int
        value: 0
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
      kind: int
      value: 68
  dst:
    kind: var
    name: %1
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %1
  target: %2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %2
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 70
    - val:
      kind: constant
      const:
        kind: int
        value: 1
  dst:
    kind: var
    name: %4
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %4
  src2:
    kind: constant
    const:
      kind: int
      value: 70
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %5
  target: %6
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: label
  name: %6
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_StaticDstKilled)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Function calls kill copies to variables with static storage duration */
int x;

int update_x(void) {
    x = 4;
    return 0;
}

int target(void) {
    x = 3;       // generate x = 3
    update_x();  // kill x = 3
    return x;    // can't propagte b/c it's static
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: x
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: x
- instruction:
  kind: fun_call
  fun_name: update_x
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: x
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_StaticSrcKilled)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Function calls kill copies where source value
 * is a variable with static storage duration
 * */

int x = 1;

int f(void) {
    x = 4;
    return 0;
}

int target(void) {
    int y = x;  // generate y = x
    f();        // kill y = x
    return y;   // don't
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: x
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: copy
  src:
    kind: var
    name: x
  dst:
    kind: var
    name: %y
- instruction:
  kind: fun_call
  fun_name: f
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %y
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_GotoDefine)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
int target(int flag) {
    int x = 10;
    goto def_x;
    if (flag) {
    def_x:
        x = 20;
    }
    return x; // return 20
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 20
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PrefixResult)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure copy propagation can track that the result of ++x and the updated
 * value of x have the same value
 */

int callee(int a, int b) {
    return a + b;
}
int f(void) {
    return 3;
}

int globl = 0;
int set_globvar(void) {
    globl = 4;
    return 0;
}

int target(void) {
    int x = f(); // x = 3
    // now x and y should have same (tmp) value
    int y = ++x; // x and y are 4

    // call another function before callee so we can't coalesce x into EDI
    // or y into ESI; otherwise it will look like we've propagated x as
    // a function argument even if we haven't
    set_globvar();

    // look for: same value passed in ESI, EDI
    int sum = callee(x, y); // sum = 8


    // now update y while x is live, so we can't coalesce them
    // into the same register; otherwise it will look like we've propagated x as
    // a function argument even if we haven't
    y = f(); // y  = 3
    return (sum + x * y);  // return 8 + 12 ==> 20
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %a
  src2:
    kind: var
    name: %b
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: globl
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
  kind: fun_call
  fun_name: set_globvar
  dst:
    kind: var
    name: %2
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %1
    - val:
      kind: var
      name: %1
  dst:
    kind: var
    name: %3
- instruction:
  kind: fun_call
  fun_name: f
  dst:
    kind: var
    name: %4
- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: %1
  src2:
    kind: var
    name: %4
  dst:
    kind: var
    name: %5
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: %3
  src2:
    kind: var
    name: %5
  dst:
    kind: var
    name: %6
- instruction:
  kind: return
  src:
    kind: var
    name: %6
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateFromDefault)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Propagate a value that's defined in a default statement that we always
 * reach
 */

int globvar = 0;

int target(int x) {
    int retval = 0;
    switch (x) {
        case 1: globvar = 1;
        case 2: globvar = globvar + 3;
        case 3: globvar = globvar * 2;
        default: retval = 3; // we always reach this no matter which case we take
    }

    return retval; // replace with "return 3"
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: equal
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
    name: %6
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %6
  target: %1
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %x
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %7
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %7
  target: %2
- instruction:
  kind: binary
  op: equal
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
    name: %8
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %8
  target: %3
- instruction:
  kind: jump
  target: %4
- instruction:
  kind: label
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
    name: globvar
- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: add
  src1:
    kind: var
    name: globvar
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
    name: globvar
- instruction:
  kind: label
  name: %3
- instruction:
  kind: binary
  op: multiply
  src1:
    kind: var
    name: globvar
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %10
- instruction:
  kind: copy
  src:
    kind: var
    name: %10
  dst:
    kind: var
    name: globvar
- instruction:
  kind: label
  name: %4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_PropagateIntoCase)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
int globvar = 0;

int callee(int arg) {
    globvar = arg;
    return 0;
}

int target(int flag) {
    int arg = 10;
    switch (flag) {
        case 1:
            arg = 20;
            break;
        case 2:
            // replace arg w/ 10 here - previous assignment
            // doesn't kill this b/c we never pass through it to get here
            callee(arg);
            break;
        default:
            globvar = -1;
    }
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %arg
  dst:
    kind: var
    name: globvar
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %flag
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %5
  target: %1
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %flag
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %6
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %6
  target: %2
- instruction:
  kind: jump
  target: %3
- instruction:
  kind: label
  name: %1
- instruction:
  kind: jump
  target: %L0
- instruction:
  kind: label
  name: %2
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 10
  dst:
    kind: var
    name: %7
- instruction:
  kind: jump
  target: %L0
- instruction:
  kind: label
  name: %3
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: -1
  dst:
    kind: var
    name: globvar
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
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (84 instructions).
TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_DecrKillsDest)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* A ++/-- operation kills its operand */

int target(int flag) {
    int w = 3;
    if (flag) {
        w++;
    }

    int x = 10;
    if (flag) {
        x--;
    }

    int y = -12;
    if (flag) {
        ++y;
    }

    int z = -100;
    if (flag) {
        --z;
    }

    if (flag) {
        if (w == 4 && x == 9 && y == -11 && z == -101) {
            // success
            return 0;
        }
        return 1;
    }
    else {
        if (w == 3 && x == 10 && y == -12 && z == -100) {
            // success
            return 0;
        }
        return 1; // fail

    }

}
)SRC")),
              "binary=8 copy=8 jump=4 jump_if_zero=13 label=11 return=4");
}

TEST_F(PipelineTest, Chapter19_CP_IntOnly_DontPropagate_SwitchFallthrough)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test case where we can't propagate a value into a case statement,
 * because it's reachable from either start of switch or fallthrough from
 * previous case statement, where the values differ.
 * */

int target(int flag) {
    int retval = 10;
    switch(flag) {
        case 1:
        retval = 0;
        case 2:
        // can't propagate - retval could be eitehr 10 or 0
        return retval;
        default: return -1;
    }
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 10
  dst:
    kind: var
    name: %retval
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %flag
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %5
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %5
  target: %1
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %flag
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %6
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %6
  target: %2
- instruction:
  kind: jump
  target: %3
- instruction:
  kind: label
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
    name: %retval
- instruction:
  kind: label
  name: %2
- instruction:
  kind: return
  src:
    kind: var
    name: %retval
- instruction:
  kind: label
  name: %3
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -1
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_AliasAnalysis)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that alias analysis allows us to propagate some copies
 * from variables whose address has been taken. */
int callee(int *ptr) {
    if (*ptr != 10) {
        return 0;  // failure
    }
    *ptr = -1;
    return 1;
}

int target(int *ptr1, int *ptr2) {
    int i = 10;          // generate i = 10
    int j = 20;          // generate j = 20
    *ptr1 = callee(&i);  // record i as a variable whose address is taken
                         // function call kills i = 10
    *ptr2 = i;

    i = 4;  // gen i = 4

    // This should be rewritten as 'return 24'.
    // We can propagate i b/c there are no stores
    // or function calls after i = 4.
    // We can propagate j b/c it's not aliased.
    return i + j;
}
)SRC"),
              R"OPT(- instruction:
  kind: load
  src_ptr:
    kind: var
    name: %ptr
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
      kind: int
      value: 10
  dst:
    kind: var
    name: %1
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %1
  target: %2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: label
  name: %2
- instruction:
  kind: store
  src:
    kind: constant
    const:
      kind: int
      value: -1
  dst_ptr:
    kind: var
    name: %ptr
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 10
  dst:
    kind: var
    name: %i
- instruction:
  kind: get_address
  src:
    kind: var
    name: %i
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %0
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
    name: %ptr1
- instruction:
  kind: store
  src:
    kind: var
    name: %i
  dst_ptr:
    kind: var
    name: %ptr2
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst:
    kind: var
    name: %i
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 24
)OPT");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_CharTypeConversion)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can propagate copies between char and signed char */

int putch(int c);  // from standard library

void print_some_chars(char a, char b, char c, char d) {
    putch(a);
    putch(b);
    putch(c);
    putch(d);
}

int callee(char c, signed char s) {
    return c == s;
}

int target(char c, signed char s) {
    // first, call another function, with these arguments
    // in different positions than in target or callee, so we can't
    // coalesce them with the param-passing registers or each other
    print_some_chars(67, 66, c, s);

    s = c;  // generate s = c - we can do this because for the purposes of copy
            // propagation, we consider char and signed char the same type

    // both arguments to callee should be the same
    return callee(s, c);
}
)SRC"),
              R"OPT(- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %a
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %0
  dst:
    kind: var
    name: %1
- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %b
  dst:
    kind: var
    name: %2
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %2
  dst:
    kind: var
    name: %3
- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %c
  dst:
    kind: var
    name: %4
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %4
  dst:
    kind: var
    name: %5
- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %d
  dst:
    kind: var
    name: %6
- instruction:
  kind: fun_call
  fun_name: putch
  args:
    - val:
      kind: var
      name: %6
  dst:
    kind: var
    name: %7
- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %c
  dst:
    kind: var
    name: %0
- instruction:
  kind: sign_extend
  src:
    kind: var
    name: %s
  dst:
    kind: var
    name: %1
- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: %0
  src2:
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
- instruction:
  kind: fun_call
  fun_name: print_some_chars
  args:
    - val:
      kind: constant
      const:
        kind: char
        value: 67
    - val:
      kind: constant
      const:
        kind: char
        value: 66
    - val:
      kind: var
      name: %c
    - val:
      kind: var
      name: %s
- instruction:
  kind: fun_call
  fun_name: callee
  args:
    - val:
      kind: var
      name: %c
    - val:
      kind: var
      name: %c
  dst:
    kind: var
    name: %6
- instruction:
  kind: return
  src:
    kind: var
    name: %6
)OPT");
}

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (31 instructions).
TEST_F(PipelineTest, Chapter19_CP_AllTypes_CopyStruct)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Test that we can propagate copies of aggregate values */
struct s {
    int x;
    int y;
};

int callee(struct s a, struct s b) {
    if (a.x != 3) {
        return 1; // fail
    }
    if (a.y != 4) {
        return 2; // fail
    }
    if (b.x != 3) {
        return 3; // fail
    }
    if (b.y != 4) {
        return 4; // fail
    }
    return 0; // success
}

int target(void) {
    struct s s1 = {1, 2};
    struct s s2 = {3, 4};
    s1 = s2;  // generate s1 = s2

    // Make sure we pass the same value for both arguments.
    // We don't need to worry that register coalescing
    // will interfere with this test,
    // because s1 and s2, as structures, won't be stored in registers.
    return callee(s1, s2);
}
)SRC")),
              "allocate_local=2 binary=4 copy_from_offset=6 copy_to_offset=6 fun_call=1 jump_if_zero=4 label=4 return=6");
}

TEST_F(PipelineTest, Chapter19_CP_AllTypes_FuncallKillsAliased)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that function calls kill all aliased variables, but not non-aliased
 * variables */

double *globl_ptr = 0;

void save_ptr(double *to_save) {
    globl_ptr = to_save;
}

void update_ptr(void) {
    *globl_ptr = 4.0;
}

// here, function call doesn't kill copy
int target(void) {
    int x = 10;    // gen x = 10
    update_ptr();  // doesn't kill x = 10

    return x;  // rewrite as 'return 10'
}

// here, function call does kill copy
int kill_aliased(void) {
    double d = 1.0;
    double *ptr = &d;
    save_ptr(ptr);

    if (*globl_ptr != 1.0) {
        return 0;  // fail
    }

    d = 2.0;  // gen d = 2.0

    if (*globl_ptr != 2.0) {
        return 0;  // fail
    }

    update_ptr();  // kill d = 2.0
    return d;      // make sure we don't rewrite this as 'return 2.0'
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: var
    name: %to_save
  dst:
    kind: var
    name: globl_ptr
- instruction:
  kind: store
  src:
    kind: constant
    const:
      kind: double
      value: 0x1p+2
  dst_ptr:
    kind: var
    name: globl_ptr
- instruction:
  kind: fun_call
  fun_name: update_ptr
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: double
      value: 0x1p+0
  dst:
    kind: var
    name: %d
- instruction:
  kind: get_address
  src:
    kind: var
    name: %d
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: save_ptr
  args:
    - val:
      kind: var
      name: %0
- instruction:
  kind: load
  src_ptr:
    kind: var
    name: globl_ptr
  dst:
    kind: var
    name: %1
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %1
  src2:
    kind: constant
    const:
      kind: double
      value: 0x1p+0
  dst:
    kind: var
    name: %2
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %2
  target: %3
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: label
  name: %3
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: double
      value: 0x1p+1
  dst:
    kind: var
    name: %d
- instruction:
  kind: load
  src_ptr:
    kind: var
    name: globl_ptr
  dst:
    kind: var
    name: %5
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %5
  src2:
    kind: constant
    const:
      kind: double
      value: 0x1p+1
  dst:
    kind: var
    name: %6
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %6
  target: %7
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: label
  name: %7
- instruction:
  kind: fun_call
  fun_name: update_ptr
- instruction:
  kind: double_to_int
  src:
    kind: var
    name: %d
  dst:
    kind: var
    name: %9
- instruction:
  kind: return
  src:
    kind: var
    name: %9
)OPT");
}
