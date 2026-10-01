#include "pipeline_test_fixture.h"

// Structural assertion (instruction-kind histogram) — exact TAC
// YAML too large to pin verbatim (110 instructions).
TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_RecognizeAllUses)
{
    EXPECT_EQ(KindHistogram(OptimizeYaml(R"SRC(
/* Make sure we recognize all the different ways a variable
 * can be used/generated (in Unary, Binary, JumpIfZero, etc.) */

int test_jz(int flag, int arg) {
    if (flag) {
        arg = 0;  // this store is not dead b/c arg is used later;
                  // put it in an if statement so we don't propagate 0 into
                  // return statement
    }
    return arg ? 1 : 2;
}

int test_jnz(int flag, int arg) {
    if (flag) {
        arg = 0;
    }
    return arg || 0;
}

int test_binary(int flag, int arg1, int arg2) {
    if (flag == 0) {
        arg1 = 4;  // this store is not dead b/c arg is used later;
                   // put it in an if statement so we don't propagate 4 into
                   // return statement
    } else if (flag == 1) {
        arg2 = 3;  // also not a dead store
    }
    return arg1 * arg2;  // generates arg1 and arg2
}

int test_unary(int flag, int arg) {
    if (flag) {
        arg = 5;  // this store is not dead b/c arg is used later;
                  // put it in an if statement so we don't propagate 5 into
                  // return statement
    }
    return -arg;  // generates arg
}

int f(int arg) {
    return arg + 1;
}

int test_funcall(int flag, int arg) {
    if (flag) {
        arg = 7;  // this store is not dead b/c arg is used later;
                  // put it in an if statement so we don't propagate 7 into
                  // return statement
    }
    return f(arg);
}

int main(void) {
    if (test_jz(1, 1) != 2) {  // 0 ? 1 : 2
        return 1; // fail
    }
    if (test_jz(0, 1) != 1) {  // 1 ? 1 : 2
        return 2; // fail
    }
    if (test_jnz(1, 1) != 0) {  // 0 || 0
        return 3; // fail
    }
    if (test_jnz(0, 1) != 1) {  // 1 || 1
        return 4; // fail
    }
    if (test_binary(0, 8, 9) != 36) {  // 4 * 9
        return 5; // fail
    }
    if (test_binary(1, 8, 9) != 24) {  // 8 * 3
        return 6; // fail
    }
    if (test_binary(2, 8, 9) != 72) {  // 8 * 9
        return 7; // fail
    }
    if (test_unary(0, 8) != -8) {
        return 8;  // fail
    }
    if (test_unary(1, 8) != -5) {
        return 9;  // fail
    }
    if (test_funcall(1, 5) != 8) {  // f(7) => 7 + 1
        return 10; // fail
    }
    if (test_funcall(0, 9) != 10) {  // f(9) ==> 9 + 1
        return 11; // fail
    }
    return 0;
}
)SRC")),
              "binary=15 copy=10 fun_call=12 jump=8 jump_if_not_zero=1 jump_if_zero=18 label=27 return=18 unary=1");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_SelfCopy)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that updating and using a value in the same instruction generates it
 * rather than killing it. */


int target(int flag) {
    int i = 2;
    // make sure value of i isn't known at compile time,
    // so we can't propagate it
    if (flag) {
        i = 3;
    }
    i = i;  // this is a no-op, but it doesn't kill i
            // if we treat this as a kill instead of a gen,
            // we'll incorrectly eliminate both earlier copies to i
            // as dead stores
    return i;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %i
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
      value: 3
  dst:
    kind: var
    name: %i
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
    name: %i
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_StaticVarsAtExit)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we reocgnize that static local variables may be live at exit */
int f(void) {
    static int i = 10;
    if (i == 5)
        return 0;
    i = 5;  // not a dead store! i is live at exit
    return 1;
}

int main(void) {

    if (f() != 1) {
        return 1; // fail
    }
    if (f() != 0) {
        return 2; // fail
    }
    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: equal
  src1:
    kind: var
    name: i
  src2:
    kind: constant
    const:
      kind: int
      value: 5
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
      value: 5
  dst:
    kind: var
    name: i
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: fun_call
  fun_name: f
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
      value: 1
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
      value: 0
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

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_StaticVarsFun)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we recognize that function calls generate all static variables */
int x = 100;

int get_x(void) {
    return x;
}

int main(void) {
    x = 5;  // don't eliminate this!
    int result = get_x();
    x = 10;
    return result;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: var
    name: x
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 5
  dst:
    kind: var
    name: x
- instruction:
  kind: fun_call
  fun_name: get_x
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
    name: x
- instruction:
  kind: return
  src:
    kind: var
    name: %0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_UsedOnePath)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* A variable is live if it's used later on one path but not others.
 * Loosely based on figure 19-10
 * */

int f(int arg, int flag) {
    int x = arg * 2;  // not dead, b/c x is live on one path
    if (flag)
        return x;
    return 0;
}

int main(void) {
    if (f(20, 1) != 40) {
        return 1;  // fail
    }
    if (f(3, 0) != 0) {
        return 2;  // fail
    }
    return 0;  // success
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
  kind: jump_if_zero
  condition:
    kind: var
    name: %flag
  target: %1
- instruction:
  kind: return
  src:
    kind: var
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
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 20
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
      value: 40
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
        value: 3
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
      value: 0
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

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DeadCompoundAssignment)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we delete compound assignment to dead variables */

int glob = 0;

int target(void) {
    int x = glob;
    x *= 20; // dead
    x = 10;
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DeadIncrDecr)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we delete ++/-- with dead variables */


static int glob;

int target(void) {
    // initialize these so they can't be constant-folded
    int a = glob;
    int b = glob;
    int c = glob;
    int d = glob;
    // these operations are all dead stores so we'll eliminate them
    a++;
    b--;
    ++c;
    --d;
    return 10;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 10
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_IntOnly_DontElim_IncrAndDeadStore)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
static int x = 10;

int main(void) {
    // incrementing x is not a dead store, although assigning to y is
    int y = ++x;
    return x;
}
)SRC"),
              R"OPT(- instruction:
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
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: %0
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_AliasedDeadAtExit)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we recognize aliased non-static variables are live
 * just after function calls but dead at function exit
 * */

int b = 0;

void callee(int *ptr) {
    b = *ptr;
    *ptr = 100;
}

int target(void) {
    int x = 10;
    callee(&x);  // generates all aliased variables (i.e. x)
    int y = x;
    x = 50;  // this is dead
    return y;
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
  kind: copy
  src:
    kind: var
    name: %0
  dst:
    kind: var
    name: b
- instruction:
  kind: store
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst_ptr:
    kind: var
    name: %ptr
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
  kind: get_address
  src:
    kind: var
    name: %x
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
- instruction:
  kind: copy
  src:
    kind: var
    name: %x
  dst:
    kind: var
    name: %y
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 50
  dst:
    kind: var
    name: %x
- instruction:
  kind: return
  src:
    kind: var
    name: %y
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_CopyToDeadStruct)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Dead store elimination can also eliminate copies to struct members. */
struct s {
    int i;
};

int f(struct s arg) {
    return arg.i;
}

int target(void) {
    struct s my_struct = {4};
    int x = f(my_struct);
    my_struct.i = 10;  // dead!
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: copy_from_offset
  src: %arg
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
- instruction:
  kind: allocate_local
  name: %my_struct
  size: 4
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst: %my_struct
  offset: 0
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: var
      name: %my_struct
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 10
  dst: %my_struct
  offset: 0
- instruction:
  kind: return
  src:
    kind: var
    name: %0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DeleteDeadPtIiInstructions)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we can delete type conversions, load, and other instructions
 * from Part II when they're dead stores
 * */


long l = 1l;
int i = 2;
unsigned int u = 30u;

struct s {
    int a;
    int b;
    int c;
};

int target(void) {
    // everything except the return instruction should be deleted
    long x = (long) i; // dead sign extend
    unsigned long y = (unsigned long) u; // dead zero extend
    double d = (double) y + (double) i; // dead IntToDouble and UIntToDouble
    x = (long) d; // dead DoubleToInt
    y = (unsigned long) d; // dead DoubleToUInt
    int arr[3] = {1, 2, 3}; // dead CopyToOffset
    int j = arr[2]; // dead AddPtr and Load
    int *ptr = &i; // dead GetAddress
    char c = (char)l; // dead truncate
    struct s my_struct = {0, 0, 0};
    j = my_struct.b; // dead CopyFromOffset
    d = -d * 5.0; // dead Binary/Unary instructions w/ non-int operands
    return 5;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %arr
  size: 12
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 1
  dst: %arr
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 2
  dst: %arr
  offset: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 3
  dst: %arr
  offset: 8
- instruction:
  kind: allocate_local
  name: %my_struct
  size: 12
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst: %my_struct
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst: %my_struct
  offset: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 0
  dst: %my_struct
  offset: 8
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 5
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_GetaddrDoesntGen)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that getting the address of a variable does _not_ make that variable
 * live.
 * */

int target(void) {
    int x = 4;  // initialization is a dead store because we never use the value
                // of x
    int *ptr = &x;
    return ptr == 0;
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
    name: %x
- instruction:
  kind: get_address
  src:
    kind: var
    name: %x
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
      kind: long
      value: 0
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_CopytooffsetDoesntKill)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* CopyToOffset does not kill src struct */

struct s {
    int a;
    int b;
    int c;
};

struct s glob = {1, 2, 3};

int main(void) {
    struct s my_struct = glob;  // not a dead store
    my_struct.c = 100;          // this doesn't make my_struct dead
    if (my_struct.c != 100 ) {
        return 1; // fail
    }
    if (my_struct.a != 1) {
        return 2; // fail
    }
    if (glob.c != 3) {
        return 3; // fail
    }
    return 0; // success
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_struct
  size: 12
  alignment: 4
- instruction:
  kind: copy_from_offset
  src: glob
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %0
  dst: %my_struct
  offset: 0
- instruction:
  kind: copy_from_offset
  src: glob
  offset: 4
  dst:
    kind: var
    name: %1
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %1
  dst: %my_struct
  offset: 4
- instruction:
  kind: copy_from_offset
  src: glob
  offset: 8
  dst:
    kind: var
    name: %2
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %2
  dst: %my_struct
  offset: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 100
  dst: %my_struct
  offset: 8
- instruction:
  kind: copy_from_offset
  src: %my_struct
  offset: 8
  dst:
    kind: var
    name: %3
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %3
  src2:
    kind: constant
    const:
      kind: int
      value: 100
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
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %5
- instruction:
  kind: copy_from_offset
  src: %my_struct
  offset: 0
  dst:
    kind: var
    name: %7
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %7
  src2:
    kind: constant
    const:
      kind: int
      value: 1
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
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
- instruction:
  kind: label
  name: %9
- instruction:
  kind: copy_from_offset
  src: glob
  offset: 8
  dst:
    kind: var
    name: %11
- instruction:
  kind: binary
  op: not_equal
  src1:
    kind: var
    name: %11
  src2:
    kind: constant
    const:
      kind: int
      value: 3
  dst:
    kind: var
    name: %12
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %12
  target: %13
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 3
- instruction:
  kind: label
  name: %13
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_FuncallGeneratesAliased)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* A function call generates every aliased variable,
 * even if that variable isn't passed as a function argument
 * */
static int *i;

void set_ptr(int *arg) {
    i = arg;
}

int get_ptr_val(void) {
    return *i;
}

int main(void) {
    int x = 1;
    set_ptr(&x);
    x = 4;  // not dead b/c x is aliased, and funcall generates it
    return get_ptr_val(); // generates x
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
  kind: load
  src_ptr:
    kind: var
    name: i
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
    kind: constant
    const:
      kind: int
      value: 1
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
    name: %0
- instruction:
  kind: fun_call
  fun_name: set_ptr
  args:
    - val:
      kind: var
      name: %0
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
  kind: fun_call
  fun_name: get_ptr_val
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_LoadGeneratesAliased)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Load instruction generates all aliased variables */
long *pass_and_return(long *ptr) {
    return ptr;
}

int main(void) {
    long l;
    long *ptr = &l;
    long *other_ptr = pass_and_return(ptr);  // now other_ptr points to l
    l = 10;  // not a dead store b/c l is aliased and this is followed by load
             // from memory
    return *other_ptr;  // this makes all aliased vars (i.e. l) live
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: var
    name: %ptr
- instruction:
  kind: get_address
  src:
    kind: var
    name: %l
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: pass_and_return
  args:
    - val:
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
      kind: long
      value: 10
  dst:
    kind: var
    name: %l
- instruction:
  kind: load
  src_ptr:
    kind: var
    name: %1
  dst:
    kind: var
    name: %3
- instruction:
  kind: truncate
  src:
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_NeverKillStore)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Dead store elimination should never eliminate Store instructions
 * */
void f(int *ptr) {
    *ptr = 4;  // not a dead store!
    return;
}

int main(void) {
    int x = 0;
    f(&x);
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: store
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst_ptr:
    kind: var
    name: %ptr
- instruction:
  kind: return
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
  kind: get_address
  src:
    kind: var
    name: %x
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: var
      name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %x
)OPT");
}

// DISABLED: static local 'arr' collides with main's 'arr' (no-shadowing / static naming)
TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_RecognizeAllUses)
{
    OptimizeYaml(R"SRC(
/* Make sure we recognize all the different ways a variable
 * can be used/generated by the instructions introduced in Part II
 * (type conversions, AddPtr, etc) */

long test_sign_extend(int flag, int arg) {
    if (flag) {
        arg = -1;  // not a dead store b/c arg is used later;
                   // put it in an if statement so we don't propagate -1
                   // into the return statement
    }
    return (long)arg;
}

unsigned long test_zero_extend(int flag, unsigned int arg) {
    if (flag) {
        arg = 4294967295U;  // not a dead store b/c arg is used later;
                            // put this in an if statement so we don't propagate
                            // constant into the return statement
    }
    return (unsigned long)arg;
}

int test_double_to_int(int flag, double arg) {
    if (flag) {
        arg = 225.5;  // not a dead store b/c arg is used later;
                      // put this in an if statement so we don't propagate
                      // constant into the return statement
    }
    return (int)arg;
}

double test_int_to_double(int flag, long arg) {
    if (flag) {
        arg = 500000l;  // not a dead store b/c arg is used later;
                        // put this in an if statement so we don't propagate
                        // constant into the return statement
    }
    return (double)arg;
}

unsigned long test_double_to_uint(int flag, double arg) {
    if (flag) {
        arg = 1844674407370955264.;  // not a dead store
    }
    return (unsigned long)arg;
}

double test_uint_to_double(int flag, unsigned int arg) {
    if (flag) {
        arg = 2147483650u;  // not a dead store
    }
    return (double)arg;
}

char test_truncate(int flag, long arg) {
    if (flag) {
        arg = 300;  // not a dead store b/c arg is used later;
                    // put this in an if statement so we don't propagate
                    // constant into the return statement
    }
    return (char)arg;
}

double *test_add_ptr(int flag, double *ptr, long index) {
    static double arr[3] = {1.0, 2.0, 3.0};
    if (flag == 0) {
        ptr = arr;  // not dead b/c ptr is used later in AddPtr
    } else if (flag == 1) {
        index = 2l;  // not dead b/c index is used later in AddPtr
    }
    return &ptr[index];
}

struct s {
    int a;
    int b;
    int c;
};

int test_copyfromoffset(int flag, struct s arg) {
    struct s other_struct = {10, 9, 8};
    if (flag) {
        arg = other_struct;  // not a dead store; we use arg below
    }
    return arg.b;
}

struct s test_copytooffset(int flag, int arg) {
    struct s my_struct = {101, 102, 103};
    if (flag) {
        arg = -1;  // not a dead store; used in CopyToOffset
    }
    my_struct.b = arg;
    return my_struct;
}

// Store(val, dst_ptr) generates both val and dst_ptr
void test_store(int flag, long *ptr1, long *ptr2, long val) {
    if (flag == 1) {
        ptr1 = ptr2;  // not a dead store b/c we store through ptr1 below
    }
    if (flag == 2) {
        val = 77l;  // not a dead store
    }
    *ptr1 = val;
}

// dst = Load(src_ptr) generates src_ptr
// NOTE: Load also generates all aliased variables but we
// validate that in a separate test program (load_generates_aliased)
int test_load(int flag, int *ptr1, int *ptr2) {
    if (flag) {
        ptr1 = ptr2;  // not a dead store b/c used as src_ptr in Load below
    }
    return *ptr1;
}

int main(void) {
    if (test_sign_extend(0, -5) != -5l) {
        return 1;  // fail
    }
    if (test_sign_extend(1, -5) != -1l) {
        return 2;  // fail
    }
    if (test_zero_extend(0, 100000u) != 100000ul) {
        return 3;  // fail
    }
    if (test_zero_extend(1, 100000u) != 4294967295UL) {
        return 4;  // fail
    }
    if (test_double_to_int(0, 1000.5) != 1000) {
        return 5;  // fail
    }
    if (test_double_to_int(1, 1000.5) != 225) {
        return 6;  // fail
    }
    if (test_int_to_double(0, 100) != 100.0) {
        return 7;  // fail
    }
    if (test_int_to_double(1, 100) != 500000.0) {
        return 8;  // fail
    }
    if (test_double_to_uint(0, 1234567.8) != 1234567u) {
        return 9; // fail
    }
    if (test_double_to_uint(1, 1234567.8) != 1844674407370955264u) {
        return 10; // fail
    }
    if (test_uint_to_double(0, 4294967000U) != 4294967000.) {
        return 11; // fail
    }
    if (test_uint_to_double(1, 4294967000U) != 2147483650u) {
        return 12; // fail
    }
    if (test_truncate(0, 500) != -12) {
        return 13;  // fail
    }
    if (test_truncate(1, 500) != 44) {
        return 14;  // fail
    }
    double arr[3] = {4.0, 5.0, 6.0};
    if (*test_add_ptr(0, arr, 1) != 2.0) {
        return 15;  // fail
    }
    if (*test_add_ptr(1, arr, 1) != 6.0) {
        return 16;  // fail
    }
    if (*test_add_ptr(2, arr, 1) != 5.0) {
        return 17;  // fail
    }
    struct s strct = {20, 21, 22};

    if (test_copyfromoffset(0, strct) != 21) {
        return 18;  // fail
    }
    if (test_copyfromoffset(1, strct) != 9) {
        return 19;  // fail
    }
    // BESM-6 adaptation: store the struct return in a local before reading .b.
    // Field access directly on a call temporary (gen_lval of EXPR_CALL) is task #46.
    struct s ct0 = test_copytooffset(0, -10);
    if (ct0.b != -10) {
        return 20;  // fail
    }
    struct s ct1 = test_copytooffset(1, -10);
    if (ct1.b != -1) {
        return 21;  // fail
    }
    long l1 = 0l;
    long l2 = 0l;

    test_store(0, &l1, &l2, 5l);
    if (l1 != 5l || l2 != 0l) {
        return 22;  // fail
    }
    test_store(1, &l1, &l2, 6l);
    if (l1 != 5l || l2 != 6l) {
        return 23;  // fail
    }
    test_store(2, &l1, &l2, 5l);
    if (l1 != 77l || l2 != 6l) {
        return 24;  // fail
    }

    int i1 = 2;
    int i2 = 3;

    if (test_load(0, &i1, &i2) != 2) {
        return 25;  // fail
    }
    if (test_load(1, &i1, &i2) != 3) {
        return 26;  // fail
    }

    return 0;  // success
}
)SRC");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_UseAndUpdate)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that updating and using a value in the same AddPtr instruction
 * generates it rather than killing it.
 * Most instructions won't use and update the same variable,
 * since their destinations are temporary variables that are used updated
 * only once, but implement &x.member  as:
 *   tmp = &x
 *   tmp = AddPtr(tmp, <offset of member>, scale=1)
 * So we can validate that AddPtr generates tmp rather than killing it.
 * */

struct s {
    int a;
    int b;
    int c;
};

struct s global_struct = {1, 2, 3};

int *target(void) {
    return &global_struct.b;
}
)SRC"),
              R"OPT(- instruction:
  kind: get_address
  src:
    kind: var
    name: global_struct
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
      value: 4
  scale: 1
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_CompoundAssignToDeadStructMember)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* compound assignment to struct members can be a dead store */

struct s {
    int i;
};

struct s glob_struct = { 15 };
int target(void) {
    struct s my_struct = { 4 }; // dead (because compound assign below is dead too)
    my_struct.i /= 2; // dead!
    my_struct = glob_struct;
    return my_struct.i;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_struct
  size: 4
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst: %my_struct
  offset: 0
- instruction:
  kind: get_address
  src:
    kind: var
    name: %my_struct
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
  scale: 8
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
  kind: binary
  op: divide
  src1:
    kind: var
    name: %2
  src2:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %3
- instruction:
  kind: store
  src:
    kind: var
    name: %3
  dst_ptr:
    kind: var
    name: %1
- instruction:
  kind: copy_from_offset
  src: glob_struct
  offset: 0
  dst:
    kind: var
    name: %4
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %4
  dst: %my_struct
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %my_struct
  offset: 0
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_CopyToDeadUnion)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we detect dead stores to union members */

union u {
    long l;
    int i;
};

union u global_union = {10};

int target(void) {
    union u my_union = {4};
    my_union.i = 123; // dead!
    my_union = global_union;
    return my_union.i;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_union
  size: 8
  alignment: 8
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: 4
  dst: %my_union
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 123
  dst: %my_union
  offset: 0
- instruction:
  kind: copy_from_offset
  src: global_union
  offset: 0
  dst:
    kind: var
    name: %1
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %1
  dst: %my_union
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %my_union
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DecrStructMember)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* ++/-- applied to struct members can be a dead store */

struct s {
    int i;
};

int target(void) {
    struct s my_struct = {4};
    int x = 15;
    my_struct.i--; // dead!
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_struct
  size: 4
  alignment: 4
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: int
      value: 4
  dst: %my_struct
  offset: 0
- instruction:
  kind: get_address
  src:
    kind: var
    name: %my_struct
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
  scale: 8
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
  kind: binary
  op: subtract
  src1:
    kind: var
    name: %2
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %3
- instruction:
  kind: store
  src:
    kind: var
    name: %3
  dst_ptr:
    kind: var
    name: %1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 15
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_CopyGeneratesUnion)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Reading a sub-object within a union makes the whole union live */

struct s {
    int a;
    int b;
};

union u {
    struct s str;
    long l;
};

union u glob = {{1, 2}};

int main(void) {
    union u my_union;
    my_union = glob; // not a dead store b/c we access a member
    return my_union.str.a;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_union
  size: 8
  alignment: 8
- instruction:
  kind: copy_from_offset
  src: glob
  offset: 0
  dst:
    kind: var
    name: %0
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %0
  dst: %my_union
  offset: 0
- instruction:
  kind: get_address
  src:
    kind: var
    name: %my_union
  dst:
    kind: var
    name: %2
- instruction:
  kind: add_ptr
  ptr:
    kind: var
    name: %2
  index:
    kind: constant
    const:
      kind: int
      value: 0
  scale: 8
  dst:
    kind: var
    name: %3
- instruction:
  kind: add_ptr
  ptr:
    kind: var
    name: %3
  index:
    kind: constant
    const:
      kind: int
      value: 0
  scale: 8
  dst:
    kind: var
    name: %4
- instruction:
  kind: load
  src_ptr:
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

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_IncrThroughPointer)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Dead store elimination should never eliminate Store instructions
 * */
void f(int *ptr) {
    ++*ptr;  // not a dead store!
    return;
}

int main(void) {
    int x = 0;
    f(&x);
    return x;
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
    name: %ptr
- instruction:
  kind: return
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
  kind: get_address
  src:
    kind: var
    name: %x
  dst:
    kind: var
    name: %0
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: var
      name: %0
- instruction:
  kind: return
  src:
    kind: var
    name: %x
)OPT");
}

TEST_F(PipelineTest, Chapter19_DSE_AllTypes_DontElim_TypePunning)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* It's not safe to eliminate a store to one union member if we read another
 * union member later
 */

union u {
    long l;
    int i;
};

int main(void) {
    union u my_union = { -1 };
    // not a dead store; we'll read lower bytes of this through my_union.i
    my_union.l = 180;
    return my_union.i;
}
)SRC"),
              R"OPT(- instruction:
  kind: allocate_local
  name: %my_union
  size: 8
  alignment: 8
- instruction:
  kind: sign_extend
  src:
    kind: constant
    const:
      kind: int
      value: -1
  dst:
    kind: var
    name: %1
- instruction:
  kind: copy_to_offset
  src:
    kind: var
    name: %1
  dst: %my_union
  offset: 0
- instruction:
  kind: copy_to_offset
  src:
    kind: constant
    const:
      kind: long
      value: 180
  dst: %my_union
  offset: 0
- instruction:
  kind: copy_from_offset
  src: %my_union
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

TEST_F(PipelineTest, Chapter19_UCE_AndClause)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate the second clause in 0 && y */
int putch(int c);

int target(void) {
    return 0 && putch(97);
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_ConstantIfElse)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate an unreachable 'if' statement body.
 * This also tests that we won't eliminate a block if some, but not all,
 * of its precedessors are unreachable. The final 'return' statement's
 * predecessors include the 'if' branch (which is dead) and the 'else'
 * statement (which isn't).
 * */
int callee(void) {
    return 0;
}

int target(void) {
    int x;
    if (0)
        x = callee();
    else
        x = 40;
    return x + 5;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 45
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadAfterIfElse)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we recognize call to 'callee' is unreachable;
 * */

int callee(void) {
    return 100;
}

int target(int a) {
    if (a) {
        return 1;
    } else {
        return 2;
    }

    return callee();  // this should be optimized away
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 100
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %a
  target: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: label
  name: %0
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadAfterReturn)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate any code after a return statement. */
int callee(void) {
    return 1;
}

int target(void) {
    return 2;

    /* Everything past this point should be optimized away */
    int x = callee();

    if (x) {
        x = 10;
    }

    int y = callee();
    return x + y;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadBlocksWithPredecessors)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure we can eliminate unreachable code even if every unreachable
 * block has a predecessor; in other words, we're traversing the graph to find
 * reachable blocks, not just looking for blocks with no predecessor list.
 * */

int callee(void) {
    return 1 / 0;
}

int target(void) {
    int x = 5;

    return x;

    /* make sure we eliminate this loop even though every block in it has a
     * predecessor */
    for (; x < 10; x = x + 1) {
        x = x + callee();
    }
    return x;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: divide
  src1:
    kind: constant
    const:
      kind: int
      value: 1
  src2:
    kind: constant
    const:
      kind: int
      value: 0
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
      value: 5
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadBranchInsideLoop)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can eliminate dead code inside of a larger, non-dead
 * control structure
 * */

int callee(void) {
    return 1 / 0;
}

int target(void) {
    int result = 105;
    // loop is not optimized away but inner function call is
    for (int i = 0; i < 100; i = i + 1) {
        if (0) {  // this if statement and function call should be optimized
                  // away
            return callee();
        }
        result = result - i;
    }
    return result;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: divide
  src1:
    kind: constant
    const:
      kind: int
      value: 1
  src2:
    kind: constant
    const:
      kind: int
      value: 0
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
    kind: constant
    const:
      kind: int
      value: 105
  dst:
    kind: var
    name: %result
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
  op: less_than
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
    name: %3
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %3
  target: %L0
- instruction:
  kind: binary
  op: subtract
  src1:
    kind: var
    name: %result
  src2:
    kind: var
    name: %i
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
    name: %result
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
    name: %8
- instruction:
  kind: copy
  src:
    kind: var
    name: %8
  dst:
    kind: var
    name: %i
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: label
  name: %L0
- instruction:
  kind: return
  src:
    kind: var
    name: %result
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadForLoop)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we can optimize away a for loop that will never execute;
 * initial expression still runs but post expression and body don't.
 * */

int callee(void) {
    return 1 / 0;
}

int target(void) {
    int i = 0;
    for (i = 10; 0; i = callee()) callee();
    return i;
}
)SRC"),
              R"OPT(- instruction:
  kind: binary
  op: divide
  src1:
    kind: constant
    const:
      kind: int
      value: 1
  src2:
    kind: constant
    const:
      kind: int
      value: 0
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
      value: 10
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_Empty)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that an empty function doesn't crash this optimization pass.
 * We don't inspect the assembly for this program, so there's no 'target'
 * function
 * */

int main(void) {
}
)SRC"),
              // main() falls off the end, so the typechecker appends an implicit
              // `return 0;` (C11 §5.1.2.2.3); the body is no longer empty.
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_EmptyBlock)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that having empty blocks after optimization doesn't break anything;
 * after removing useless jumps and labels, 'target' will contain several
 * empty basic blocks.
 * */

int target(int x, int y) {
    if (x) {
        if (y) {
        }
    }
    return 1;
}
)SRC"),
              R"OPT(- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %x
  target: %0
- instruction:
  kind: jump_if_zero
  condition:
    kind: var
    name: %y
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
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_InfiniteLoop)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* make sure we don't choke on programs that never terminate
 * This program _does_ terminate because it indirectly calls exit()
 * but the compiler doesn't know that.
 * */

int exit_wrapper(int status); // defined in chapter_19/libraries/exit.c

int main(void) {
    int i = 0;
    do {
        i = i + 1;
        if (i > 10) {
            exit_wrapper(i);
        }
    } while(1);
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
    name: %i
- instruction:
  kind: label
  name: %2
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
    name: %3
- instruction:
  kind: copy
  src:
    kind: var
    name: %3
  dst:
    kind: var
    name: %i
- instruction:
  kind: binary
  op: greater_than
  src1:
    kind: var
    name: %3
  src2:
    kind: constant
    const:
      kind: int
      value: 10
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
  kind: fun_call
  fun_name: exit_wrapper
  args:
    - val:
      kind: var
      name: %3
  dst:
    kind: var
    name: %7
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
  kind: jump
  target: %2
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_KeepFinalJump)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we don't choke on a program where final instruction is a jump.
 * Note that last instruction will only be a jump on second iteration through
 * the pipeline, after we've removed the extra Return instruction.
 * We don't inspect the assembly for this program, we just make its behavior
 * is correct, so the function under test is 'f' instead of 'target'
 * */

int f(int a) {
    do {
        a = a - 1;
        if (a)
            return 17;
    } while (1);
}

int main(void) {
    return f(10);
}
)SRC"),
              R"OPT(- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: subtract
  src1:
    kind: var
    name: %a
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
    name: %a
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
      value: 17
- instruction:
  kind: label
  name: %4
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: fun_call
  fun_name: f
  args:
    - val:
      kind: constant
      const:
        kind: int
        value: 10
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

TEST_F(PipelineTest, Chapter19_UCE_OrClause)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate the second clause in 1 || x */
int putch(int c);

int target(void) {
    return 1 || putch(97);
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 1
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_RemoveConditionalJumps)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate useless JumpIfZero and JumpIfNotZero instructions. */

int target(int a) {
    // on second unreachable code elimination pass, this will include
    // a JumpIfNotZero to its default successor (where we assign result = 1)
    int x = a || 5;

    // on second unreachable code elimination pass, this will include
    // a JumpIfZero to its default successor (where we assign result = 0)
    int y = a && 0;
    return x + y;
}
)SRC"),
              R"OPT(- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %a
  target: %0
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
  kind: jump_if_zero
  condition:
    kind: var
    name: %a
  target: %3
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
  kind: binary
  op: add
  src1:
    kind: var
    name: %2
  src2:
    kind: constant
    const:
      kind: int
      value: 0
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

TEST_F(PipelineTest, Chapter19_UCE_RemoveJumpKeepLabel)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we remove one jump to a label without necessarily removing that
 * label, since other blocks may also jump to it.
 * We don't inspect the assembly for this function,
 * so the function under test is 'f' insetad of 'target'
 * */

int x = 0;
int callee(void) {
    x = x + 1;
    return 0;
}

int f(void) {
    for (int i = 0; i < 10; i = i + 1) {
        if (0) {
            // we'll optimize away this break, which jumps to this loop's
            // break label; however, we shouldn't optimize away the break label
            // because we still jump to it when we exit the loop normally
            break;
        }
        callee();
    }
    return 0;
}

int main(void) {
    f();
    return x;
}
)SRC"),
              R"OPT(- instruction:
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
    name: %0
- instruction:
  kind: copy
  src:
    kind: var
    name: %0
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
      value: 0
  dst:
    kind: var
    name: %i
- instruction:
  kind: label
  name: %2
- instruction:
  kind: binary
  op: less_than
  src1:
    kind: var
    name: %i
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
  target: %L0
- instruction:
  kind: fun_call
  fun_name: callee
  dst:
    kind: var
    name: %6
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
    name: %7
- instruction:
  kind: copy
  src:
    kind: var
    name: %7
  dst:
    kind: var
    name: %i
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
    name: x
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_RemoveUselessStartingLabel)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we remove useless labels */
int target(void) {
    // This empty do loop will start with several labels that we don't jump to;
    // make sure they're removed
    do {
    } while (0);

    return 99;
}
)SRC"),
              R"OPT(- instruction:
  kind: label
  name: %2
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 99
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadBeforeFirstSwitchCase)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Anything that appears in a switch statement before the body of the first
 * case is unreachable.
 */

int callee(void) {
    return 0;
}

int target(int x) {
    switch(x) {
        return callee(); // unreachable
        case 1: return 1;
        default: return 2;
    }

}
)SRC"),
              R"OPT(- instruction:
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
    name: %x
  src2:
    kind: constant
    const:
      kind: int
      value: 1
  dst:
    kind: var
    name: %4
- instruction:
  kind: jump_if_not_zero
  condition:
    kind: var
    name: %4
  target: %1
- instruction:
  kind: jump
  target: %2
- instruction:
  kind: label
  name: %1
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
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 2
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_DeadInSwitchBody)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Eliminate unreachable code witihn a switch statement body */

int callee(void) {
    return -1;
}

int target(int x) {
    int retval = 0;
    switch (x) {
    case 1:
        retval = 1; break;
    case 2: retval = 2; break;
        callee(); // unreachable - occurs after 'break' from previous case and before next one
    case 3: retval = 10; break;
    default: return -1;
        callee(); // unreachable
    }

    return retval;


}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -1
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
    name: %retval
- instruction:
  kind: jump
  target: %L0
- instruction:
  kind: label
  name: %2
- instruction:
  kind: copy
  src:
    kind: constant
    const:
      kind: int
      value: 2
  dst:
    kind: var
    name: %retval
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
      value: 10
  dst:
    kind: var
    name: %retval
- instruction:
  kind: jump
  target: %L0
- instruction:
  kind: label
  name: %4
- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: -1
- instruction:
  kind: label
  name: %L0
- instruction:
  kind: return
  src:
    kind: var
    name: %retval
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_GotoSkipsOverCode)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Test that we eliminate code that goto jumps over */
int callee(void) {
    return 1;
}

int target(void) {
    int x = 10;
    goto end;
    x = callee(); // eliminate this
    end:
    return x;
}
)SRC"),
              R"OPT(- instruction:
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
    name: %x
- instruction:
  kind: return
  src:
    kind: var
    name: %x
)OPT");
}

TEST_F(PipelineTest, Chapter19_UCE_RemoveUnusedLabel)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* Make sure this pass removes unused label instructions */


int target(void) {
    lbl:
    return 0;
}
)SRC"),
              R"OPT(- instruction:
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

TEST_F(PipelineTest, Chapter19_UCE_UnreachableSwitchBody)
{
    EXPECT_EQ(OptimizeYaml(R"SRC(
/* If a switch body contains no case or default statements, we'll eliminate the whole thing */

int target(int flag) {
    switch (flag) {
        // Eliminate all of this - it's unreachable b/c outer
        // switch statement has no case/default statements (even
        // though inner switch does)
        static int x = 0;
        for (int i = 0; i < flag; i = i + 1) {
            switch (i) {
            case 1: x = x + 1;
            case 2: x = x + 2;
            default: x = x * 3;
            }
        }
        return x;
    }

    return 0;
}
)SRC"),
              R"OPT(- instruction:
  kind: return
  src:
    kind: constant
    const:
      kind: int
      value: 0
)OPT");
}
