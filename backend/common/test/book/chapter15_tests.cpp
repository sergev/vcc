//
// Chapter 15 — Arrays and pointer arithmetic: valid programs compiled and run
// on BESM-6.  Imported from "Writing a C Compiler" (tests/chapter_15/valid:
// allocation + casts + declarators + extra_credit + initialization +
// pointer_arithmetic + subscripting + libraries).  Each program defines
// int main(void); b6sim --status prints its return value, and we compare program
// output against the value computed by host cc.  The book's host-only
// "#if defined SUPPRESS_WARNINGS / #pragma" blocks are dropped (our scanner has
// no preprocessor); two-file "libraries" cases are merged into one source,
// client first.
//
// Multi-dimensional arrays, nested subscripts, pointer-to-array arithmetic and
// pointer difference all lower and run on BESM-6, so most of the chapter is in
// range.  The split is driven by BESM-6 facts the x86-oriented corpus assumes
// away:
//
//   * A BESM-6 pointer is a *word* address, not a byte address, so x86
//     "16-byte-aligned, ptr % 16 == 0" assumptions do not hold.
//   * There is NO block-scope static-local storage (a `static` inside a function
//     emits no toplevel, so the body references an undefined name).
//   * Integer types are narrow: signed int/long is 41-bit (max ~1.1e12),
//     unsigned is 48-bit (max ~2.8e14) — no 32-bit int wrap and no 64-bit
//     long/unsigned-long wrap.
//   * No identifier shadowing (permanent design decision): a program that reuses
//     a name across nested scopes is rejected with "Duplicate variable
//     declaration".
//   * Array->pointer parameter adjustment is not performed in declaration-
//     compatibility checking, so `int a[2][3]` and `int (*a)[3]` parameter forms
//     of the same function read as conflicting declarations.
//
// Programs that stay within these limits are enabled run tests below; the rest
// are DISABLED_ (grouped at the bottom with a one-line reason each).  Most are
// not compiler bugs — they exercise x86 semantics BESM-6 lacks.  Like chapters
// 11-14 the programs self-check and return an error code on mismatch, so a
// BESM-6-valued expectation would just encode a meaningless failure code;
// DISABLED_ is the honest call.
//
// Programs whose results depend on integer widths or sizes are in their generic
// LP64 form; BESM-6 runs its own versions of them (see README.md).
//
#include "book_test.h"

// --- casts -------------------------------------------------------------------

// casts/cast_array_of_pointers: round-trip cast between pointer-to-array types.
TEST_F(BookTest, Chapter15_CastArrayOfPointers)
{
    EXPECT_EQ(
        "1\n",
        CompileAndRunBook(
            R"(/* Test that we can convert between different pointer types, including pointers to arrays */

int main(void) {

    int simple_array[2] = {1, 2};
    // Array of pointers to arrays
    int(*ptr_arr[3])[2] = {&simple_array, 0, &simple_array};
    // Cast from one pointer type to another
    // Note #1: dereferencing other_ptr would violate strict aliasing, but casting/comparing it is okay.
    // Note #2: casting between pointer types is undefined if the pointer is misaligned for the new type;
    // this specific case is safe because we're casting from pointer-to-pointer to pointer-to-long,
    // and pointers and longs are both eight-byte aligned
    long *other_ptr = (long *)ptr_arr;

    // After round-trip cast from int(**)[2] to long * and back,
    // this must compare equal to its original value.
    return (int(**)[2])other_ptr == ptr_arr;
})"));
}

// casts/multi_dim_casts: cast to pointers of different dimensions.
TEST_F(BookTest, Chapter15_MultiDimCasts)
{
    EXPECT_EQ(
        "0\n",
        CompileAndRunBook(
            R"(/* Test that we can cast to pointers to different dimensions in a multi-dimensional array */

int main(void) {
    int multi_dim[2][3] = {{0, 1, 2}, {3, 4, 5}};

    // get pointer to whole array
    int (*array_pointer)[2][3] = &multi_dim;

    // get pointer to one row
    int (*row_pointer)[3] = (int (*)[3]) array_pointer;

    if (row_pointer != multi_dim) {
        return 1;
    }

    // now make it point to second row
    row_pointer = row_pointer + 1;
    if (row_pointer[0][1] != 4) {
        return 2;
    }

    // get pointer to one element
    int *elem_ptr = (int *) row_pointer;

    if (*elem_ptr != 3 ){
        return 3;
    }

    elem_ptr = elem_ptr + 2;
    if (*elem_ptr != 5) {
        return 4;
    }

    // now set row_pointer back to the beginning, cast it back to an array,
    // and make sure it round-tripped
    row_pointer = row_pointer - 1;
    if ((int (*)[2][3]) row_pointer != array_pointer) {
        return 5;
    }

    return 0;
})"));
}

// --- declarators -------------------------------------------------------------

// declarators/big_array: parse an array declarator with size > UINT_MAX (extern, never allocated).
TEST_F(BookTest, Chapter15_BigArray)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test that we can parse an array declarator with a size greater than UINT_MAX
 * Note that we don't actually allocate space for this array!
 */

extern int x[4294967297L][100000000];

int main(void) {
    return 0;
})"));
}

// declarators/for_loop_array: array declared and used in a for loop.
TEST_F(BookTest, Chapter15_ForLoopArray)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(/* Test that we can declare arrays in for loop initializers */
int main(void) {
    int counter = 0;

    // declare an array in for loop header, then check its values
    for (int i[3] = {1, 2, 3}; counter < 3; counter = counter + 1){
        if (i[counter] != counter + 1) {
            return 1;
        }
    }

    return 0;
})"));
}

// --- extra_credit ------------------------------------------------------------

// extra_credit/bitwise_subscript: bitwise ops on subscripted values.
TEST_F(BookTest, Chapter15_BitwiseSubscript)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test bitwise operations on array elements
int main(void) {
    int arr[6] = {-10, 10, -11, 11, -12, 12};
    if ((arr[0] & arr[5]) != 4) {
        return 1; // fail
    }

    if ((arr[1] | arr[4]) != -2) {
        return 2;
    }

    if ((arr[2] ^ arr[3]) != -2) {
        return 3;
    }

    arr[0] = 2041302511;
    if ((arr[0] >> arr[1]) != 1993459) {
        return 4;
    }

    if ((arr[5] << 3 ) != 96) {
        return 5;
    }

    return 0;
})"));
}

// extra_credit/compound_assign_and_increment: compound assignment + ++/-- on array elements.
TEST_F(BookTest, Chapter15_CompoundAssignAndIncrement)
{
    EXPECT_EQ(
        "0\n",
        CompileAndRunBook(
            R"(// Combination of compound assignment and increment/decrement with subscript expressions
int main(void) {
    int arr[4] = {-1, -2, -3, -4};
    int *ptr = arr;
    int idx = 2;

    // arr[2] *= -3;
    // after expression, ptr points to arr[1] and idx is 3
    if ((ptr++[idx++] *= 3) != -9) {
        return 1; // fail
    }
    if (*ptr != -2) {
        return 2; // fail
    }
    if (idx != 3) {
        return 3; // fail
    }
    idx--;
    // arr[3] += 4 results in 4
    if ((--ptr)[3] += 4) {
        return 4; // fail
    }

    // validate all array elements
    if (arr[0] != -1 || arr[1] != -2 || arr[2] != -9 || arr[3] != 0) {
        return 5; // fail
    }
    return 0; // success
})"));
}

// extra_credit/compound_assign_to_nested_subscript: compound assign through a 2D subscript.
TEST_F(BookTest, Chapter15_CompoundAssignToNestedSubscript)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         R"(// test compound assignment where LHS is nested subscripted expression

long long_nested_arr[2][3] = {{1, 2, 3}, {4, 5, 6}};
double dbl_nested_arr[3][2] = {{100.0, 101.0}, {102.0, 103.0}, {104.0, 105.0}};
unsigned unsigned_index = 10;

int main(void) {
    // nested long array
    if ((long_nested_arr[1][unsigned_index - 8] *= -1) != -6) {
        return 1;  // fail
    }
    if (long_nested_arr[1][2] != -6) {
        return 2;  // fail
    }

    // make sure other five elements are unchanged
    for (int i = 0; i < 2; i += 1) {
        for (int j = 0; j < 3; j += 1) {
            if (i == 1 && j == 2) {
                // this is the one we just checked
                break;
            }
            long expected = i * 3 + j + 1;
            if (long_nested_arr[i][j] != expected) {
                return 3;  // fail
            }
        }
    }

    // another nested array
    if ((dbl_nested_arr[1][1] += 100.0) != 203.0) {
        return 4;  // fail
    }

    // make sure the other elements of dbl_nested_arr are unchanged
    for (int i = 0; i < 3; i += 1) {
        for (int j = 0; j < 2; j += 1) {
            if (i == 1 && j == 1) {
                // we already validated this one
                continue;
            }
            int expected = 100 + i * 2 + j;
            if (dbl_nested_arr[i][j] != expected) {
                return 5;  // fail
            }
        }
    }

    return 0;  // success
})"));
}

// extra_credit/compound_nested_pointer_assignment: compound assign through pointers into a
// file-scope nested array.
TEST_F(BookTest, Chapter15_CompoundNestedPointerAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Nested pointer assignment with +=/-=

// partially initialized
static long nested_arr[3][4][5] = {{{10, 9, 8}, {1, 2}}, {{100, 99, 98}}};

int main(void) {
    // pointer arithmetic at outermost level
    long(*outer_ptr)[4][5] = nested_arr;
    outer_ptr += 1;
    if (outer_ptr != nested_arr + 1) {
        return 1;  // fail
    }
    if (outer_ptr[0][0][0] != 100) {
        return 2;
    }

    long(*inner_ptr)[5] =
        nested_arr[0] + 4;  // pointer to one past the end of nested_arr[0]
    inner_ptr -= 3;
    if (inner_ptr[0][1] != 2) {
        return 3;
    }

    // example with non-constant rval
    unsigned long idx = nested_arr[0][0][0] - 9;  // 1
    if ((inner_ptr += idx) != &nested_arr[0][2]) {
        return 4;
    }

    if ((inner_ptr[-2][1] != 9)) {
        return 5;
    }

    return 0;
})"));
}

// extra_credit/incr_and_decr_nested_pointers: ++/-- on pointers into a 3D array.
TEST_F(BookTest, Chapter15_IncrAndDecrNestedPointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Array arithmetic with prefix and postfix ++/--
int main(void) {
    long arr[2][3][4] = {
        {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}},
        {{13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}};

    // pointer to outermost level
    long (*outer_ptr)[3][4] = arr + 1;
    if (outer_ptr-- != &arr[1]) {
        return 1; // fail
    }
    if (outer_ptr[0][1] != arr[0][1]) {
        return 2; // fail
    }

    if ((++outer_ptr)[0][2][3] != 24) {
        return 3; // fail
    }
    if (outer_ptr[0][2][3] != 24) {
        return 4; // fail
    }

    // pointer to next level in
    long (*inner_ptr)[4] = arr[0] + 1;
    if (inner_ptr++[0][2] != 7) {
        return 5; // fail
    }

    if (inner_ptr[0][2] != 11) {
        return 6; // fail
    }

    if ((--inner_ptr)[0][1] != 6) {
        return 7; // fail
    }

    // pointer to scalar elements
    long *scalar_ptr = arr[1][2];
    if (scalar_ptr--[2] != 23) {
        return 8; // fail
    }
    if (scalar_ptr[2] != 22) {
        return 9; // fail
    }

    return 0;  // success
})"));
}

// extra_credit/incr_and_decr_pointers: ++/-- on pointers into a 1D array.
TEST_F(BookTest, Chapter15_IncrAndDecrPointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Array arithmetic with prefix and postfix ++/--
int main(void) {
    double x[3] = {0.0, 1.0, 2.0};
    double *ptr = x;
    // prefix ++
    if (++ptr != x + 1) {
        return 1;  // fail
    }
    if (*ptr != 1.0) {
        return 2;  // fail
    }

    // postfix ++
    if (ptr++ != x + 1) {
        return 3;  // fail
    }
    if (ptr != x + 2) {
        return 4;
    }
    if (*ptr != 2.0) {
        return 5;  // fail
    }

    // prefix --
    if (--ptr != x + 1) {
        return 6;  // fail
    }
    if (*ptr != 1.0) {
        return 7;  // fail
    }

    // postfix--
    if (ptr-- != x + 1) {
        return 8;  // fail
    }
    if (*ptr != 0.0) {
        return 9;  // fail
    }
    if (ptr != x) {
        return 10;  // fail
    }

    return 0;  // success
})"));
}

// extra_credit/incr_decr_subscripted_vals: ++/-- on subscripted values.
TEST_F(BookTest, Chapter15_IncrDecrSubscriptedVals)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(// Apply ++ and -- to subscript expressions, which are lvalues

// indices (static to prevent copy prop)
int i = 2;
int j = 1;
int k = 0;

int main(void) {
    int arr[3][2][2] = {
        {{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}, {{9, 10}, {11, 12}}};

    if (arr[i][j][k]++ != 11) {
        return 1;  // fail
    }
    if (arr[i][j][k] != 12) {
        return 2;  // fail
    }

    // also apply ++/-- to indices
    if (++arr[--i][j--][++k] /* arr[1][1][1] */ != 9) {
        return 3;  // fail
    }

    // check side effect of updating j
    if (arr[i][j][k] /* arr[1][0][1]*/ != 6) {
        return 4;  // fail
    }
    if (--arr[i][j][k] != 5) {
        return 5;  // fail
    }
    return 0;  // success
})"));
}

// extra_credit/postfix_prefix_precedence: precedence of postfix/prefix with subscripts.
TEST_F(BookTest, Chapter15_PostfixPrefixPrecedence)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         R"(// Postfix ++/-- and subscript have higher precedence than prefix ++/--
int idx = 3;
int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    int *ptr = arr + 1;
    // 1. evaluate ptr--; this yields a pointer to arr[1], makes ptr point to arr[0]
    // 2. evaluate subscript operation, yielding lval at arr[4]
    // 3. increment lval at arr[4] (to 6) and return incremented value
    int result = ++ptr--[idx];

    // check result
    if (result != 6) {
        return 1; // fail
    }

    // check side effect of decrementing pointer
    if (*ptr != 1) {
        return 2; // fail
    }

    // check side effect of decrementing pointer (a different way)
    if (ptr != arr) {
        return 3; // fail
    }

    // make sure postfix ++ is higher precedence than dereference (*) operator
    if (*ptr++ != 1) {
        return 4; // fail
    }

    // check side effect of decrementing pointer
    if (*ptr != 2) {
        return 5;
    }

    // first four elements of arr have value as before same
    for (int i = 0; i < 4; i++) {
        if (arr[i] != i + 1) {
            return 6; // fail
        }
    }

    // check side effect of incrementing last element
    if (arr[4] != 6) {
        return 7; // fail
    }

    return 0;
})"));
}

// --- initialization ----------------------------------------------------------

// initialization/trailing_comma_initializer: array initializer with a trailing comma.
TEST_F(BookTest, Chapter15_TrailingCommaInitializer)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int foo(int a, int b, int c);
int main(void) {
    int arr[3] = {
        1,
        2,
        3, // last element in a compound initializer may have a trailing comma
    };
    return arr[2];
})"));
}

// --- pointer_arithmetic ------------------------------------------------------

// pointer_arithmetic/add_dereference_and_assign: assign through dereferenced pointer arithmetic.
TEST_F(BookTest, Chapter15_AddDereferenceAndAssign)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that you can assign to any dereferenced pointer,
 * including pointers resulting from pointer arithmetic */
int main(void) {
    int arr[2] = {1, 2};
    // dereferenced expressions, including dereferenced results of
    // pointer arithmetic, are valid lvalues
    *arr = 3;
    *(arr + 1) = 4;
    if (arr[0] != 3) {
        return 1;
    }

    if (arr[1] != 4) {
        return 2;
    }
    return 0;
})"));
}

// pointer_arithmetic/compare: compare pointers to elements of the same (nested) array.
TEST_F(BookTest, Chapter15_Compare)
{
    EXPECT_EQ(
        "0\n",
        CompileAndRunBook(
            R"(/* Test comparison of elements of the same array, including multi-dimensional arrays */

// pointer comparisons
unsigned long gt(unsigned long *a, unsigned long *b) {
    return a > b;
}


unsigned long lt(unsigned long *a, unsigned long *b) {
    return a < b;
}

unsigned long ge(unsigned long *a, unsigned long *b) {
    return a >= b;
}

unsigned long le(unsigned long *a, unsigned long *b) {
    return a <= b;
}

// comparing pointers to nested arrays
unsigned long gt_nested(unsigned long (*a)[5], unsigned long (*b)[5]) {
    return a > b;
}

unsigned long ge_nested(unsigned long (*a)[5], unsigned long (*b)[5]) {
    return a >= b;
}


int main(void)
{
    // compare elements of a 1D array

    // we don't need to initialize this because we're only comparing pointers to array elements,
    // not dereferencing them
    unsigned long arr[5];
    unsigned long *elem_1 = arr + 1;
    unsigned long *elem_4 = arr + 4;
    if (gt(elem_1, elem_4)) {
        return 1;
    }
    if (!(lt(elem_1, elem_4))) {
        return 2;
    }
    if (!(ge(elem_1, elem_1))) {
        return 3;
    }
    if (le(elem_4, elem_1)) {
        return 4;
    }

    // can also compare to pointer to one past the end of the array
    unsigned long *one_past_the_end = arr + 5;
    if (!(gt(one_past_the_end, elem_4))) {
        return 5;
    }
    if (one_past_the_end != elem_4 + 1) {
        return 6;
    }

    // do the same for nested array elements. start w/ pointers to scalar elements within array
    unsigned long nested_arr[4][5];

    unsigned long *elem_3_2 = *(nested_arr + 3) + 2;
    unsigned long *elem_3_3 = *(nested_arr + 3) + 3;

    if (lt(elem_3_3, elem_3_2)) {
        return 7;
    }

    if (!ge(elem_3_3, elem_3_2)) {
        return 8;
    }

    // now look at pointers to whole sub-arrays
    unsigned long (*subarray_0)[5] = nested_arr;
    unsigned long (*subarray_3)[5] = nested_arr + 3;
    unsigned long (*subarray_one_past_the_end)[5] = nested_arr + 4;

    if (ge_nested(subarray_0, subarray_3)){
        return 9;
    }

    if (!(gt_nested(subarray_one_past_the_end, subarray_3))) {
        return 10;
    }

    if (subarray_3 != subarray_one_past_the_end - 1) {
        return 11;
    }

    return 0;
})"));
}

// --- subscripting ------------------------------------------------------------

// subscripting/addition_subscript_equivalence: x[i] equals *(x+i) for a 2D array.
TEST_F(BookTest, Chapter15_AdditionSubscriptEquivalence)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we treat x[i] and *(x + i) as equivalent

int main(void)
{
    unsigned long x[300][5];
    for (int i = 0; i < 300; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            x[i][j] = i * 5 + j;
        }
    }

    // check for equivalent values using explicit pointer dereference vs subscript
    if (*(*(x + 20) + 3) != x[20][3]) {
        return 1;
    }

    // same idea but taking address
    if (&(*(*(x + 290) + 3)) != &x[290][3]) {
        return 2;
    }

    // do this exhaustively
    for (int i = 0; i < 300; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            if (*(*(x + i) + j) != x[i][j]) {
                return 3;
            }
        }
    }


    // assign, then read
    *(*(x + 275) + 4) = 22000ul;
    if (x[275][4] != 22000ul) {
        return 4;
    }
    return 0;
})"));
}

// subscripting/array_of_pointers_to_arrays: subscripts mixing pointers and decayed arrays.
TEST_F(BookTest, Chapter15_ArrayOfPointersToArrays)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         R"(/* Test that we can correcty handle subscript expressions that involve
 * a mix of pointers and arrays that decay to pointers
 */
int main(void) {
    int x = 0;
    int y = 1;
    int z = 2;

    // define two arrays of pointers
    int *arr[3] = { &x, &y, &z };
    int *arr2[3] = {&z, &y, &x};

    // an array of pointers to arrays of pointers
    int *(*array_of_pointers[3])[3] = {&arr, &arr2, &arr};
    if (array_of_pointers[0] != (int *(*)[3]) arr) {
        return 1;
    }

    if (array_of_pointers[1] != (int *(*)[3]) arr2) {
        return 2;
    }

    if (array_of_pointers[2] != (int *(*)[3]) arr) {
        return 3;
    }


    if (array_of_pointers[1][0][0] != &z) {
        return 4;
    }

    if (array_of_pointers[1][0][1] != &y) {
        return 5;
    }

    if (array_of_pointers[2][0][2][0] != 2) {
        return 6;
    }

    return 0;
})"));
}

// subscripting/simple: return arr[2] of a 1D array.
TEST_F(BookTest, Chapter15_Simple)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(/* A very simple subscripting test case */

int main(void) {
    int arr[3] = {1, 2, 3};
    return arr[2];
})"));
}

// subscripting/subscript_pointer: subscript a pointer.
TEST_F(BookTest, Chapter15_SubscriptPointer)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(/* Test that we can apply subscript expressions to all pointers,
 * not just pointers that decayed from arrays */


int subscript_pointer_to_pointer(int **x) {
    return x[0][0];
}

int main(void) {
    int a = 3;
    int *ptr = &a;

    // subscript a pointer
    if (ptr[0] != 3) {
        return 1;
    }

    // subscript a pointer to a pointer
    int **ptr_ptr = &ptr;
    if (ptr_ptr[0][0] != 3) {
        return 2;
    }

    // pass pointer to pointer as a function argument, which will be subscripted
    // note that this NOT equivalent to pointer to array!
    int dereferenced = subscript_pointer_to_pointer(ptr_ptr);
    if (dereferenced != 3) {
        return 3;
    }
    return 0;
})"));
}

// subscripting/subscript_precedence: subscript operator precedence.
TEST_F(BookTest, Chapter15_SubscriptPrecedence)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    return (-arr[2] == -3);
})"));
}

// --- libraries (two files merged, client first) ------------------------------

// libraries/global_array: access an array defined in another translation unit.
TEST_F(BookTest, Chapter15_GlobalArray)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Make sure we can access an array declared in another translation unit */
extern long arr[4];
int double_each_element(void);

int main(void) {
    // check value of each array element
    for (int i = 0; i < 4; i = i + 1) {
        if (arr[i] != i + 1) {
            return i + 1;
        }
    }

    // update each element
    double_each_element();

    // check new values
    for (int i = 0; i < 4; i = i + 1) {
        if (arr[i] != (i + 1) * 2) {
            return i + 5;
        }
    }

    return 0;
}

long arr[4] = {1, 2, 3, 4};

int double_each_element(void) {
    for (int i = 0; i < 4; i = i + 1) {
        arr[i] = arr[i] * 2;
    }

    return 0;
})"));
}

// libraries/return_pointer_to_array: define/call functions returning pointers to arrays.
TEST_F(BookTest, Chapter15_ReturnPointerToArray)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Make sure we can define/call functions that return pointers to arrays */
long (*return_row(long (*arr)[3][4], int idx))[4];

int main(void) {
    long nested_array[2][3][4] = {
        {{0}},
        {{-12, -13, -14, -15}, {-16}}
    };

    long (*row_pointer)[4] = return_row(nested_array, 1);

    // make sure values are correctly
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 4; j = j + 1) {
            if (row_pointer[i][j] != nested_array[1][i][j]) {
                return 1;
            }
        }
    }

    // make sure that when we update the array through one pointer,
    // it's visible in the other

    row_pointer[2][1] = 100;
    if (nested_array[1][2][1] != 100) {
        return 2;
    }

    return 0;
}

// given a nested array of longs, return a pointer to one row in the array
long (*return_row(long (*arr)[3][4], int idx))[4] {
    return arr[idx];
})"));
}

// libraries/set_array_val: pass pointers to (nested) array elements as arguments.
TEST_F(BookTest, Chapter15_SetArrayVal)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we can pass pointers to array elements,
 * including nested array elements, as function arguments. */

int set_nth_element(double *arr, int idx);
int set_nested_element(int (*arr)[2], int i, int j);

int main(void) {

    // pass a 1D array as a function argument
    double arr[5] = {0.0, 0.0, 0.0, 0.0, 0.0};

    // if this is non-zero, value of arr passed to set_nth_element was wrong
    int check = set_nth_element(arr, 4);
    if (check) {
        return check;
    }

    // make sure updated values are correct
    for (int i = 0; i < 4; i = i + 1) {
        if (arr[i] != 0) {
            return 2;
        }
    }
    if (arr[4] != 8)
        return 3;

    // now try a 2D array
    int nested_arr[3][2] = {{-10, -9}, {-8, -7}, {-6, -5}};

    // if this is non-zero, value of arr passed to set_nested_element was wrong
    check = set_nested_element(nested_arr, 2, 1);
    if (check) {
        return check;
    }

    // make sure updated values are correct
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) {

            if (i == 2 && j == 1) {
                // this is the element we just updated
                if (nested_arr[i][j] != 10) {
                    return 5;
                }
            } else {
                // value shoudl be the same as before
                int expected = -10 + 2 * i + j;
                if (nested_arr[i][j] != expected) {
                    return 6;
                }
            }
        }
    }

    return 0;
}

int set_nth_element(double *arr, int idx) {
    /* Validate current values */
    for (int i = 0; i < 5; i = i + 1) {
        if (arr[i]) {
            return 1;
        }
    }
    arr[idx] = 8;
    return 0;
}

int set_nested_element(int (*arr)[2], int i, int j) {
    for (int x = 0; x < 3; x = x + 1) {
        for (int y = 0; y < 2; y = y + 1) {
            int expected = -10 + 2*x + y;
            if (arr[x][y] != expected) {
                return 4;
            }
        }
    }
    arr[i][j] = 10;
    return 0;
})"));
}

// ===========================================================================
// DISABLED_ — programs BESM-6 cannot reproduce, grouped by reason.
// ===========================================================================

// --- Static locals (now supported) + remaining besm6 gaps -------------------
// Block-scope statics work now; tests still DISABLED_ here have a separate blocker
// (multi-dimensional array sub-word scaling, pointer representation, etc.).

// declarators/equivalent_declarators: re-enabled once task #19 landed (commit 016593e) — the
// redundant tentative re-declaration `int long arr[4ul];` after the initialized
// `long int(arr)[4] = {1,2,3,4};` no longer re-emits a zero-init that clobbers the
// initializer.  The `test_array_of_pointers` helper was shortened to `test_aop` so it stays
// distinct from `test_arr` within the Madlen 8-char label limit (both truncate to `test*arr`).
TEST_F(BookTest, Chapter15_EquivalentDeclarators)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Declare the same global array multiple times w/ equivalent declarators */

// an array of four longs
long int(arr)[4] = {1, 2, 3, 4};

int long arr[4ul];

// a pointer to a two-dimensional array
int (*ptr_to_arr)[3][6l];

int((*(ptr_to_arr))[3l])[6u] = 0;

// an array of pointers
int *array_of_pointers[3] = {0, 0, 0};

// helper function to make sure arr has the values we just initialized
int test_arr(void) {
    for (int i = 0; i < 4; i = i + 1) {
        if (arr[i] != i + 1) {
            return 1;
        }
    }
    return 0; // success
}

int test_ptr_to_arr(void) {
    // at first ptr_to_arr should be null
    if (ptr_to_arr) {
        return 2;
    }

    static int nested_arr[3][6];
    ptr_to_arr = &nested_arr;
    ptr_to_arr[0][2][4] = 100;
    if (nested_arr[2][4] != 100) {
        return 3;
    }
    return 0; // success
}

int test_aop(int *ptr) {

    extern int *((array_of_pointers)[3]); // make sure we can redeclare this locally

    // make sure every array element is null
    // then assign ptr to each of them
    for (int i = 0; i < 3; i = i + 1) {
        if (array_of_pointers[i])
            return 4;
        array_of_pointers[i] = ptr;
    }

    // update value through pointer
    array_of_pointers[2][0] = 11;

    if (*ptr != 11) {
        return 5;
    }

    for (int i = 0; i < 3; i = i + 1) {
        if (array_of_pointers[i][0] != 11) {
            return 6;
        }
    }
    return 0;

}

int main(void)
{
    // make sure arr has the right type/initial values;
    int check = test_arr();
    if (check) {
        return check;
    }

    // make sure ptr_to_arr has right type
    check = test_ptr_to_arr();
    if (check) {
        return check;
    }

    // make sure array_of_pointers has the right type
    int x = 0;
    check = test_aop(&x);
    if (check) {
        return check;
    }

    return 0;
})"));
}

// extra_credit/compound_assign_array_of_pointers: uses a `static` array of pointers local.
TEST_F(BookTest, Chapter15_CompoundAssignArrayOfPointers)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(// Compound assignment where lval is a subscript expression with pointer type
int main(void) {
    // array of 3 pointers to arrays of 4 ints
    static int (*array_of_pointers[3])[4] = {0, 0, 0};
    int array1[4] = {100, 101, 102, 103};
    int nested_array[2][4] = {
        {200, 201, 202, 203},
        {300, 301, 302, 303}
    };
    array_of_pointers[0] = &array1;
    array_of_pointers[1] = &nested_array[0];
    array_of_pointers[2] = &nested_array[1];

    array_of_pointers[0] += 1; // points one past the end of array1
    if (array_of_pointers[0][-1][3] != 103) {
        return 1; // fail
    }

    // swap these so they point to last and first elements of nested_array, respectively
    array_of_pointers[1] += 1;
    array_of_pointers[2] -= 1;
    if (array_of_pointers[1][0][3] != 303) {
        return 2; // fail
    }
    if (array_of_pointers[2][0][3] != 203) {
        return 3; // fail
    }

    return 0;
})"));
}

// extra_credit/compound_lval_evaluated_once: uses a `static int count` local.
TEST_F(BookTest, Chapter15_CompoundLvalEvaluatedOnce)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(// Make sure the left side of a compound expression is evaluated only once

int get_call_count(void) {
    // a function that returns the number of times it's been called
    // throughout the program
    static int count = 0;
    count += 1;
    return count;
}

int main(void) {
    int arr[4] = {10, 11, 12, 13};
    if (arr[get_call_count()] != 11) { // arr[0]
        return 1; // fail
    }
    int *end_ptr = arr + 4;
    if ((end_ptr - 1)[-get_call_count()] != 11) { // arr[2]
        return 2; // fail
    }

    if (get_call_count() != 3) {
        return 3; // fail
    }

    return 0; // success
})"));
}

// initialization/automatic_nested: uses a `static int x` local.
TEST_F(BookTest, Chapter15_AutomaticNested)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         R"(/* Test initializing nested arrays with automatic storage duration */

/* A fully initialized array of constants */
int test_simple(void) {
    int arr[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

    // check the value of each element
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            if (arr[i][j] != i * 3 + j + 1) {
                return 0;
            }
        }
    }

    return 1;  // success
}

/* A partially initialized array of constants.
 * Elements that aren't explicitly initialized
 * (including nested arrays) should be zeroed out.
 * */
int test_partial(void) {
    // explicitly initialize only the first half of each array,
    // at each dimension
    int first_half_only[4][2][6] = {
        {{1, 2, 3}},  // first_half_only[0][0][0-2]
        {{4, 5, 6}}   // first_half_only[1][0][0-2]
    };

    int expected = 1;
    for (int i = 0; i < 4; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) {
            for (int k = 0; k < 6; k = k + 1) {
                int val = first_half_only[i][j][k];
                if (i > 1 || j > 0 || k > 2) {
                    // this wasn't explicitly initialized, should be zero
                    if (val) {
                        return 0;
                    }
                } else {
                    if (val != expected) {
                        return 0;
                    }
                    expected = expected + 1;
                }
            }
        }
    }

    return 1;  // success
}

/* elements in a compound initializer may include non-constant expressions
 * and expressions of other types, which are converted to the right type
 * as if by assignment */
int test_non_constant_and_type_conversion(void) {
    // first let's define some value (that can't be copy propagated
    // or constant-folded away in Part III)
    extern unsigned int three(void);
    static int x = 2000;
    int negative_four = -4;
    int *ptr = &negative_four;

    double arr[3][2] = {
        {x, x / *ptr},
        {three()},
    };

    if (arr[0][0] != 2000.0 || arr[0][1] != -500.0 || arr[1][0] != 3.0) {
        return 0;
    }

    if (arr[1][1] || arr[2][0] || arr[2][1]) {
        return 0;
    }

    return 1;  // success
}

// helper function for previous test
unsigned int three(void) {
    return 3u;
}

/* Initializing an array must not corrupt other objects on the stack. */
long one = 1l;
int test_preserve_stack(void) {
    int i = -1;

    /* Initialize with expressions of long type - make sure they're truncated
     * before being copied into the array.
     * Also use an array of < 16 bytes so it's not 16-byte aligned, so there are
     * quadwords that include both array elements and other values.
     * Also leave last element uninitialized; in assembly, we should set it to
     * zero without overwriting what follows
     */
    int arr[3][1] = {{one * 2l}, {one + three()}};
    unsigned int u = 2684366905;

    if (i != -1) {
        return 0;
    }

    if (u != 2684366905) {
        return 0;
    }

    if (arr[0][0] != 2 || arr[1][0] != 4 || arr[2][0] != 0) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    if (!test_simple()) {
        return 1;
    }

    if (!test_partial()) {
        return 2;
    }

    if (!test_non_constant_and_type_conversion()) {
        return 3;
    }

    if (!test_preserve_stack()) {
        return 4;
    }

    return 0;  // success
})"));
}

// initialization/static: validates static-storage-duration local arrays.  The book's
// 1000-element `long` arrays were shrunk to 100 so the program fits in BESM-6 memory
// (the originals overflowed the short address field — "ДЛИHHЫЙ AДPEC"); the construct
// under test (static-duration init + zero-fill) is unchanged.
TEST_F(BookTest, Chapter15_Static)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test initializing one-dimensional arrays with static storage duration */

// fully initialized
double double_arr[3] = {1.0, 2.0, 3.0};

int check_double_arr(double *arr) {
    if (arr[0] != 1.0) {
        return 1;
    }

    if (arr[1] != 2.0) {
        return 2;
    }

    if (arr[2] != 3.0) {
        return 3;
    }

    return 0;
}

// partly initialized
unsigned uint_arr[5] = {
    1u,
    0u,
    2147497230u,
};

int check_uint_arr(unsigned *arr) {
    if (arr[0] != 1u) {
        return 4;
    }

    if (arr[1]) {
        return 5;
    }
    if (arr[2] != 2147497230u) {
        return 6;
    }

    if (arr[3] || arr[4]) {
        return 7;
    }

    return 0;
}

// uninitialized; should be all zeros
long long_arr[100];

int check_long_arr(long *arr) {
    for (int i = 0; i < 100; i = i + 1) {
        if (arr[i]) {
            return 8;
        }
    }
    return 0;
}

// initialized w/ values of different types
unsigned long ulong_arr[4] = {
    100.0, 11, 12345l, 4294967295U
};

int check_ulong_arr(unsigned long *arr) {
    if (arr[0] != 100ul) {
        return 9;
    }

    if (arr[1] != 11ul) {
        return 10;
    }

    if (arr[2] != 12345ul) {
        return 11;
    }

    if (arr[3] != 4294967295Ul) {
        return 12;
    }
    return 0;
}

int test_global(void) {
    int check = check_double_arr(double_arr);
    if (check) {
        return check;
    }

    check = check_uint_arr(uint_arr);
    if (check) {
        return check;
    }
    check = check_long_arr(long_arr);
    if (check) {
        return check;
    }
    check = check_ulong_arr(ulong_arr);
    if (check) {
        return check;
    }
    return 0;
}

// equivalent static local arrays
int test_local(void) {

    // fully initialized
    double local_double_arr[3] = {1.0, 2.0, 3.0};
    // partly initialized
    static unsigned local_uint_arr[5] = {
        1u,
        0u, // truncated to 0
        2147497230u,
    };

    // uninitialized
    static long local_long_arr[100];

    // initialized w/ values of different types
    static unsigned long local_ulong_arr[4] = {
        100.0, 11, 12345l, 4294967295U
    };

    // validate
    int check = check_double_arr(local_double_arr);
    if (check) {
        return 100 + check;
    }

    check = check_uint_arr(local_uint_arr);
    if (check) {
        return 100 + check;
    }
    check = check_long_arr(local_long_arr);
    if (check) {
        return 100 + check;
    }
    check = check_ulong_arr(local_ulong_arr);
    if (check) {
        return 100 + check;
    }
    return 0;
}

int main(void) {
    int check = test_global();
    if (check) {
        return check;
    }
    return test_local();
})"));
}

// initialization/static_nested: validates static-storage-duration multi-dim local arrays.
// The book's `long[30][50][40]` (60000 words) was shrunk to `[3][5][4]` so the program fits
// in BESM-6 memory; the partially-initialized `unsigned long[4][6][2]` exercises the static-
// local zero-fill fixed in backend/besm6/static.c (explicit `,log, 0` words, not `,bss,`).
TEST_F(BookTest, Chapter15_StaticNested)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test initializing multi-dimensional arrays with static storage duration */


// fully initialized
double double_arr[2][2] = {{1.1, 2.2}, {3.3, 4.4}};

int check_double_arr(double (*arr)[2]) {
    if (arr[0][0] != 1.1) {
        return 1;
    }

    if (arr[0][1] != 2.2) {
        return 2;
    }

    if (arr[1][0] != 3.3) {
        return 3;
    }

    if (arr[1][1] != 4.4) {
        return 4;
    }

    return 0;
}

// uninitialized; should be all zeros
long long_arr[3][5][4];

int check_long_arr(long (*arr)[5][4]) {
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            for (int k = 0; k < 4; k = k + 1) {
                if (arr[i][j][k]) {
                    return 5;
                }
            }
        }
    }

    return 0;
}

// partially initialized using values of different types

unsigned long ulong_arr[4][6][2] = {
    {{
         1000.3,
     }, // truncated to 1000
     {12u}},
    {{2}}};

int check_ulong_arr(unsigned long (*arr)[6][2]) {
    for (int i = 0; i < 4; i = i + 1) {
        for (int j = 0; j < 6; j = j + 1) {
            for (int k = 0; k < 2; k = k + 1) {
                int val = arr[i][j][k];
                if (i == 0 && j == 0 && k == 0) {
                    if (val != 1000ul) {
                        return 6;
                    }
                } else if (i == 0 && j == 1 && k == 0) {
                    if (val != 12ul) {
                        return 7;
                    }
                } else if (i == 1 && j == 0 && k == 0) {
                    if (val != 2ul) {
                        return 8;
                    }
                } else {
                    // not explicitly initialized, should be 0
                    if (val) {
                        return 9;
                    }
                }
            }
        }
    }

    return 0;
}

// validate all the global arrays
int test_global(void) {
    int check = check_double_arr(double_arr);
    if (check) {
        return check;
    }

    check = check_long_arr(long_arr);
    if (check) {
        return check;
    }
    check = check_ulong_arr(ulong_arr);
    if (check) {
        return check;
    }
    return 0;
}

// equivalent static local arrays
int test_local(void) {

    static double local_double_arr[2][2] = {{1.1, 2.2}, {3.3, 4.4}};

    int check = check_double_arr(local_double_arr);
    if (check) {
        return 100 + check;
    }

    static long local_long_arr[3][5][4];
    check = check_long_arr(local_long_arr);
    if (check) {
        return 100 + check;
    }

    static unsigned long local_ulong_arr[4][6][2] = {
        {{
            1000.3,
        }, // truncated to 1000
        {12u}},
        {{2}}};
    check = check_ulong_arr(local_ulong_arr);
    if (check) {
        return 100 + check;
    }
    return 0;
}
int main(void) {
    int check = test_global();
    if (check) {
        return check;
    }
    return test_local();
})"));
}

// pointer_arithmetic/pointer_add: many `static` locals (also `static int flag;` zero-init).
TEST_F(BookTest, Chapter15_PointerAdd)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(/* Test pointer addition and subtraction to specify array indices
 * (but not subtracting two pointers to get the distance between them)
 * */

/* Addition */

/* basic pointer addition */
int test_add_constant_to_pointer(void) {
    long long_arr[12] = {0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 13};
    long *ptr = long_arr + 10;
    return *ptr == 13;
}

/* add negative index to pointer */
int test_add_negative_index(void) {
    unsigned unsigned_arr[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 42};
    unsigned *end_ptr = unsigned_arr + 12;

    unsigned *ptr = end_ptr + -10;
    return *ptr == 2;
}

/* it doesn't matter whether we add pointer to int or vice versa */
int test_add_pointer_to_int(void) {
    int int_arr[5] = {0, 98, 99};
    int *ptr1 = int_arr + 2;
    int *ptr2 = 2 + int_arr;

    return (ptr1 == ptr2 && *ptr2 == 99);
}

/* array index can be any integer type, not just int */
int test_add_different_index_types(void) {
    double double_arr[11] = {0, 0, 0, 0, 0, 6.0};

    // four equivalent expresssions that should produce the same pointer
    double *ptr1 = double_arr + 5;
    double *ptr2 = double_arr + 5l;
    double *ptr3 = double_arr + 5u;
    double *ptr4 = double_arr + 5ul;

    return (ptr1 == ptr2 && ptr1 == ptr3 && ptr1 == ptr4 && *ptr4 == 6.0);
}

/* pointer addition where pointer and index are both complex expressions */
int test_add_complex_expressions(void) {
    // use some static variables and function calls so operands
    // won't be constant-folded away in Part III
    static int flag;  // 0
    int i = -2;
    int *small_int_ptr = &i;
    extern int return_one(void);
    extern int *get_elem1_ptr(int *arr);
    extern int *get_elem2_ptr(int *arr);
    static int arr[4] = {1, 2, 3, 4};
    // ptr = 1 + -2 + (0 ? (arr + 1) : (arr + 2))
    //  => -1 + (arr + 2)
    //  => arr + 1
    int *ptr = return_one() + (*small_int_ptr) +
               (flag ? get_elem1_ptr(arr) : get_elem2_ptr(arr));
    return (ptr == arr + 1 && *ptr == 2);
}

// define our helper functions for the test case above
int return_one(void) {
    return 1;
}

int *get_elem1_ptr(int *arr) {
    return arr + 1;
}

int *get_elem2_ptr(int *arr) {
    return arr + 2;
}

/* add pointers to rows in a multi-dimensional array */
int test_add_multi_dimensional(void) {
    static int index = 2;
    int nested_arr[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    int(*row_pointer)[3] = nested_arr + index;
    return **row_pointer == 7;
}

/* add pointers to scalar elements in a multi-dimensional array */
int test_add_to_subarray_pointer(void) {
    static int index = 2;
    int nested_arr[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    // pointer to nested_arr[1]
    int *row1 = *(nested_arr + 1);

    // pointer to nested_arr[1][2]
    int *elem_ptr = row1 + index;
    return *elem_ptr == 6;
}

/* Subtraction */

/* Subtract a variable from a pointer */
int test_subtract_from_pointer(void) {
    long long_arr[5] = {10, 9, 8, 7, 6};
    long *one_past_the_end = long_arr + 5;
    static int index = 3;
    long *subtraction_result = one_past_the_end - index;
    return *subtraction_result == 8;
}

/* Subtract negative index from pointer */
int test_subtract_negative_index(void) {
    unsigned arr[5] = {100, 101, 102, 103, 104};
    unsigned *ptr = arr - (-3);
    return *ptr == 103;
}

/* array index can be any integer type, not just int */
int test_subtract_different_index_types(void) {
    double double_arr[11] = {0, 0, 0, 0, 0, 0, 6.0};
    double *end_ptr = double_arr + 11;

    // four equivalent expresssions that should produce the same pointer
    double *ptr1 = end_ptr - 5;
    double *ptr2 = end_ptr - 5l;
    double *ptr3 = end_ptr - 5u;
    double *ptr4 = end_ptr - 5ul;
    return (ptr1 == ptr2 && ptr1 == ptr3 && ptr1 == ptr4 && *ptr4 == 6.0);
}

/* index and pointer can both be arbitrary expressions, not just constants and
 * variables */
int test_subtract_complex_expressions(void) {
    static int flag = 1;
    static int four = 4;
    static int arr[4] = {1, 2, 3, 4};
    // reuse get_elem1_ptr and get_elem2_ptr funcionts we defined earlier
    // ptr = (1 ? (arr + 1) : (arr + 2)) - (4/-2)
    //  => (arr + 1) - -2
    //  => arr + 3
    int *ptr = (flag ? get_elem1_ptr(arr) : get_elem2_ptr(arr)) - (four / -2);
    return (*ptr == 4);
}

/* subtract pointers to rows in a multi-dimensional array */
int test_subtract_multi_dimensional(void) {
    static int index = 1;
    int nested_arr[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    int(*last_row_pointer)[3] = nested_arr + 2;
    int(*row_pointer)[3] = last_row_pointer - index;
    return (**row_pointer == 4);
}

int main(void) {
    /* Addition */
    if (!test_add_constant_to_pointer()) {
        return 1;
    }

    if (!test_add_negative_index()) {
        return 2;
    }

    if (!test_add_pointer_to_int()) {
        return 3;
    }

    if (!test_add_different_index_types()) {
        return 4;
    }

    if (!test_add_complex_expressions()) {
        return 5;
    }

    if (!test_add_multi_dimensional()) {
        return 6;
    }

    if (!test_add_to_subarray_pointer()) {
        return 7;
    }

    /* Subtraction */
    if (!test_subtract_from_pointer()) {
        return 8;
    }

    if (!test_subtract_negative_index()) {
        return 9;
    }

    if (!test_subtract_different_index_types()) {
        return 10;
    }

    if (!test_subtract_complex_expressions()) {
        return 11;
    }

    return 0;
})"));
}

// pointer_arithmetic/pointer_diff: uses a `static double multidim[6][7][3][5]` local.
// Exercises the task #11 wide word-pointer difference (double(*)[3][5], double(*)[5]).
// The book's helper names (`get_multidim_ptr_diff` / `..._2`) are shortened here because
// Madlen identifiers truncate to 8 chars: the originals both collapse to `get*mult` and
// would alias each other.  `pdiff_m` / `pdiff_m2` stay distinct after truncation.
TEST_F(BookTest, Chapter15_PointerDiff)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test subtracting two pointers to find the number of elements between them */


/* subtract two pointers into a 1D array of ints */
int pdiff_i(int *ptr1, int *ptr2) {
    return (ptr2 - ptr1);
}

/* subtract two pointers into array of longs */
int pdiff_l(long *ptr1, long *ptr2) {
    return (ptr2 - ptr1);
}

/* subtract pointers to two elements in a multi-dimensional array */
int pdiff_m(double (*ptr1)[3][5], double (*ptr2)[3][5]) {
    return (ptr2 - ptr1);
}

/* subtract pointers into a multi-dimensional array again, but at different levels of nesting */
int pdiff_m2(double (*ptr1)[5], double (*ptr2)[5]) {
    return (ptr2 - ptr1);
}

int main(void) {
    int arr[5] = {5, 4, 3, 2, 1};
    int *end_of_array = arr + 5;

    if (pdiff_i(arr, end_of_array) != 5) {
        return 1;
    }

    long long_arr[8];

    if (pdiff_l(long_arr + 3, long_arr) != -3) {
        return 2;
    }

    // test subtracting multi-dimensional pointers;
    // also make sure we can handle pointers into array with static storage duration
    static double multidim[6][7][3][5];

    if (pdiff_m(multidim[2] + 1, multidim[2] + 4) != 3) {
        return 3;
    }

    if (pdiff_m2(multidim[2][2] + 2, multidim[2][2]) != -2) {
        return 4;
    }

    return 0;
})"));
}

// subscripting/simple_subscripts: uses a `static int arr[4]` local.
TEST_F(BookTest, Chapter15_SimpleSubscripts)
{
    EXPECT_EQ(
        "0\n",
        CompileAndRunBook(
            R"(/* Test out simple cases involving constant indices and one-dimensional arrays */


int integer_types(unsigned *arr, unsigned expected) {
    // make sure our index can be any integer type
    unsigned val1 = arr[5];
    unsigned val2 = arr[5u];
    unsigned val3 = arr[5l];
    unsigned val4 = arr[5ul];
    if (val1 != expected) {
        return 1;
    }

    if (val2 != expected) {
        return 2;
    }

    if (val3 != expected) {
        return 3;
    }

    if (val4 != expected) {
        return 4;
    }
    return 0;
}

// x[i] == i[x] - doesn't matter which is the index
int reverse_subscript(long *arr, long expected)  {
    if (arr[3] != expected) {
        return 5;
    }

    if (3[arr] != expected) {
        return 6;
    }

    // taking address of both expression should yield same address
    if (&3[arr] != &arr[3]) {
        return 7;
    }

    return 0;
}

// subscript a static array
static double static_array[3] = {0.1, 0.2, 0.3};

int subscript_static(void) {
    if (static_array[0] != 0.1) {
        return 8;
    }
    if (static_array[1] != 0.2) {
        return 9;
    }
    if (static_array[2] != 0.3) {
        return 10;
    }
    return 0;
}

// update an array element using subscripting
// expected is new value of arr[10] after update
int update_element(int *arr, int expected) {
    arr[10] = arr[10] * 2;

    if (arr[10] != expected) {
        return 11;
    }

    return 0;
}

// update an array element with static storage duration using subscripting
int *increment_static_element(void) {
    static int arr[4];
    arr[3] = arr[3] + 1;
    return arr;
}

int check_increment_static_element(void) {
    // increment static arr and get a pointer to it
    int *arr1 = increment_static_element();

    // last element should be 1, all others should be 0
    if (arr1[3] != 1) {
        return 12;
    }

    if (arr1[0] || arr1[1] || arr1[2]) {
        return 13;
    }

    // call function again to increment last element again
    int *arr2 = increment_static_element();

    if (arr1 != arr2) {
        return 14;
    }

    if (arr1[3] != 2) {
        return 15;
    }

    return 0;
}

int main(void) {
    unsigned int unsigned_arr[6] = {0, 0, 0, 0, 0, 7u};
    // unsigned_arr[5] == 7
    int check = integer_types(unsigned_arr, 7u);
    if (check) {
        return check;
    }

    long int long_arr[4] = {100, 102, 104, 106};
    // long_arr[3] == 106
    check = reverse_subscript(long_arr, 106);
    if (check) {
        return check;
    }

    check = subscript_static();
    if (check) {
        return check;
    }

    int int_arr[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15};
    check = update_element(int_arr, 30);
    if (check) {
        return check;
    }

    check = check_increment_static_element();
    if (check) {
        return check;
    }

    return 0;

})"));
}

// --- No identifier shadowing (permanent design decision) ---------------------

// declarators/return_nested_array: local `arr` shadows file-scope `arr`.
TEST_F(BookTest, Chapter15_ReturnNestedArray)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(/* Declare a function that returns a pointer to an array */

int g_arr[3] = {1, 1, 1};

int (*foo(int x, int y))[3] {
    g_arr[1] = x;
    g_arr[2] = y;
    return &g_arr;
}

int main(void) {
    int (*arr)[3] = foo(2, 3);
    if (arr[0][0] != 1) {
        return 1;
    }
    if (arr[0][1] != 2) {
        return 2;
    }
    if (arr[0][2] != 3) {
        return 3;
    }
    return 0;
})"));
}

// subscripting/subscript_nested: parameter `nested_arr` shadowed file-scope `nested_arr`
// (renamed to `s_nested`); `read_nested`/`read_nested_negated` and
// `write_nested`/`write_nested_complex` collided in the first 8 Madlen chars (renamed).
TEST_F(BookTest, Chapter15_SubscriptNested)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test subscripting multi-dimensional arrays */

// read an element through a nested subscript
int read_elem(int nested_arr[2][3], int i, int j, int expected) {
    return (nested_arr[i][j] == expected);
}

// write through a nested subscript
int write_elem(int nested_arr[2][3], int i, int j, int new_val) {
    nested_arr[i][j] = new_val;
    return 0;
}

// read through a more complex index
int read_neg(int (*nested_arr)[3], int i, int j, int expected) {
    return (nested_arr[-i][j] == expected);
}


// get address of nested subscript
int get_nested_addr(int nested_arr[2][3], int i, int j, int *expected) {
    return &nested_arr[i][j] == expected;
}

// nested access to a static array
static int s_nested[4][3][5] = {
    {{1, 2}, {3}},
    {{4}, {5}}
};

int read_static_nested(int i, int j, int k, int expected) {
    return s_nested[i][j][k] == expected;
}

// write a nested element using more complex expression to get array
int (*get_array(void))[3][5] {
    return s_nested;
}

int write_cplx(int i, int j, int k, int val) {
    get_array()[i][j][k] = val;
    return 0;
}

// only subscript first dimension to return pointer to sub-array
int *get_subarray(int nested[2][3], int i) {
    return nested[i];
}

int main(void) {
    int nested_arr[2][3] = {{1, 2, 3}, {4, 5, 6}};
    if (!read_elem(nested_arr, 1, 2, 6)) {
        return 1;
    }

    write_elem(nested_arr, 1, 2, -1);
    if (nested_arr[1][2] != -1) {
        return 2;
    }

    if (!read_neg(nested_arr + 2, 2, 0, 1)) {
        return 3;
    }

    int *ptr = (nested_arr[0]) + 1;
    if (!get_nested_addr(nested_arr, 0, 1, ptr)) {
        return 4;
    }

    if (!read_static_nested(1, 1, 0, 5)) {
        return 5;
    }

    write_cplx(0, 2, 3, 111);
    if (get_array()[0][2][3] != 111) {
        return 6;
    }

    int *row_1 = get_subarray(nested_arr, 1);
    if (row_1 + 1 != &nested_arr[1][1]) {
        return 7;
    }

    return 0;
})"));
}

// subscripting/complex_operands: `subscript_inception`/`subscript_function_result`
// collided in the first 8 Madlen chars (`subscrip`); renamed to `sub_incept`/`sub_funcres`.
TEST_F(BookTest, Chapter15_ComplexOperands)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test subscript expressions where both operands are complex sub-expressions,
 * not just variables and constants. This test program only includes 1D arrays. */

// use a side-effecting statement as an index
int assign_in_index(int idx) {
    int arr[3] = {1, 2, 3};
    int val = arr[idx = idx + 2];
    if (idx != 1) {
        return 1;
    }

    if (val != 2) {
        return 2;
    }

    return 0;
}

// helper function for funcall_in_index
int static_index(void) {
    static int index = 0;
    int retval = index;
    index = index + 1;
    return retval;
}

// use a side-effecting function call as an index
int funcall_in_index(void) {
    int arr[3] = {1, 2, 3};
    int v1 = arr[static_index()];
    int v2 = arr[static_index()];
    if (v1 != 1) {
        return 3;
    }
    if (v2 != 2) {
        return 4;
    }

    return 0;
}

// use result of another subscript expression as index
int sub_incept(long *arr, int *a, int b){
    return arr[a[b]];
}

int check_subscript_inception(void) {
    long arr[4] = {4, 3, 2, 1};
    int indices[2] = {1, 2};
    if (sub_incept(arr, indices, 1) != 2) {
        return 5;
    }

    if (sub_incept(arr, indices, 0) != 3) {
        return 6;
    }

    return 0;
}

// use result of function call as pointer
int *get_array(void) {
    static int arr[3];
    return arr;
}

int sub_funcres(void){
    get_array()[2] = 1;
    if (get_array()[2] != 1) {
        return 7;
    }

    return 0;
}

int negate_subscript(int *arr, int idx, int expected) {
    if (arr[-idx] != expected) {
        return 8;
    }

    return 0;
}

int main(void) {
    int check = assign_in_index(-1);
    if (check) {
        return check;
    }

    check = funcall_in_index();
    if (check) {
        return check;
    }

    check = check_subscript_inception();
    if (check) {
        return check;
    }

    check = sub_funcres();
    if (check) {
        return check;
    }

    int arr[3] = {0, 1, 2};
    check = negate_subscript(arr + 2, 2, 0);
    if (check) {
        return check;
    }
    return 0;
})"));
}

// --- Array->pointer parameter adjustment not performed -----------------------

// declarators/array_as_argument: `int a[2][3]` vs `int (*a)[3]` param forms read as conflicting
// declarations.
TEST_F(BookTest, Chapter15_ArrayAsArgument)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         R"(/* Test that array types in parameters are converted to pointer types */


/* The type of 'a' will be adjusted to (int *) */
int array_param(int a[5]) {
    a[4] = 0;
    return 0;
}

/* Now try a multi-dimensional array; the type of 'a' will be adjusted to int (*)[3] */
int nested_array_param(int a[2][3]) {
    a[1][1] = 1;
    return 0;
}

/* It's okay to redeclare a function with a different outermost array dimension,
 * because that dimension is ignored
 */
int array_param(int a[2]);

int nested_array_param(int (*a)[3]);

int main(void) {

    // Make sure we adjust parameters in local function declarations too
    int array_param(int a[6]);
    int nested_array_param(int a[5][3]);


    // call array_param and make sure it works as expected
    int arr[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    array_param(arr);
    if (arr[4]) {
        return 1;
    }

    // check the other elements too
    for (int i = 0; i < 8; i = i + 1) {
        if (i != 4 && arr[i] != 8 - i)
            return 2;
    }

    // call nested_array_param and make sure it works as expected
    int nested_arr[4][3] = { {-1, -1, -1}, {-2, -2, -2}, {-3, -3, -3}, {-4, -4, -4}};

    nested_array_param(nested_arr);
    if (nested_arr[1][1] != 1) {
        return 3;
    }

    // check other elements
    for (int i = 0; i < 4; i = i + 1) {
        int expected = -1 - i;
        for (int j = 0; j < 3; j = j + 1) {
            if ((i != 1 || j != 1) &&
                (nested_arr[i][j] != expected)) {
                    return 4;
            }
        }
    }

    return 0;
}

int array_param(int *a);)"));
}

// --- Value exceeds the BESM-6 integer range (41-bit signed / 48-bit unsigned) --

// casts/implicit_and_explicit_conversions: reading the long elements -1 and -4
// through an unsigned long lvalue yields 2^64-1 and 2^64-4.
TEST_F(BookTest, Chapter15_ImplicitAndExplicitConversions)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test that we correctly track both implicit type conversions via array decay
 * and explicit casts
 */


int main(void) {
    long arr[4] = {-1,-2,-3,-4};

    // (long *) cast here is a no-op, since arr already decays to a pointer to its first element
    if (arr != (long *) arr) {
        return 1;
    }

    // taking address with & and explicitly converting to pointer to array
    // both result in address of arr with same type
    if ((long (*)[4]) arr != &arr) {
        return 2;
    }

    // reinterpret arr as an array of unsigned longs
    // NOTE: effective type rules usually don't let you read an object
    // with an lvalue of different type, but reading signed integer thru
    // corresponding unsigned type, and vice versa, is okay.
    unsigned long *unsigned_arr = (unsigned long *)arr;
    if (unsigned_arr[0] != 18446744073709551615UL) {
        return 3;
    }

    if (unsigned_arr[3] != 18446744073709551612UL) {
        return 4;
    }

    return 0;
})"));
}

// initialization/automatic: arrays with automatic storage duration.
TEST_F(BookTest, Chapter15_Automatic)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test initialzing one-dimensional arrays with automatic storage duration */

/* Initialize array with three constants */
int test_simple(void) {
    unsigned long arr[3] = {18446744073709551615UL, 9223372036854775807UL,
                            100ul};

    return (arr[0] == 18446744073709551615UL &&
            arr[1] == 9223372036854775807UL && arr[2] == 100ul);
}

/* if an array is partially initialized, any elements that aren't
 * explicitly initialized should be zero.
 */
int test_partial(void) {
    double arr[5] = {1.0, 123e4};

    // make sure first two elements have values from initializer and last three
    // are zero
    return (arr[0] == 1.0 && arr[1] == 123e4 && !arr[2] && !arr[3] && !arr[4]);
}

/* An initializer can include non-constant expressions, including function
 * parameters */
int test_non_constant(long negative_7billion, int *ptr) {
    *ptr = 1;
    extern int three(void);
    long var = negative_7billion * three();  // -21 billion
    long arr[5] = {
        negative_7billion,
        three() * 7l,                      // 21
        -(long)*ptr,                       // -1
        var + (negative_7billion ? 2 : 3)  // -21 billion  + 2
    };  // fifth element  not initialized, should be 0

    return (arr[0] == -7000000000 && arr[1] == 21l && arr[2] == -1l &&
            arr[3] == -20999999998l && arr[4] == 0l);
}

// helper function for test case above
int three(void) {
    return 3;
}

long global_one = 1l;
/* elements in a compound initializer are converted to the right type as if by
 * assignment */
int test_type_conversion(int *ptr) {
    *ptr = -100;

    unsigned long arr[4] = {
        3458764513821589504.0,  // convert double to ulong
        *ptr,  // dereference to get int (-100), convert to ulong = 2^64 - 100
        (unsigned int)18446744073709551615UL,  // truncates to 2^32 - 1
        -global_one                 // (unsigned long)(-1) = 2^64 - 1
    };

    return (arr[0] == 3458764513821589504ul &&
            arr[1] == 18446744073709551516ul && arr[2] == 4294967295U &&
            arr[3] == 18446744073709551615UL);
}

/* Initializing an array must not corrupt other objects on the stack. */
int test_preserve_stack(void) {
    int i = -1;

    /* Initialize with expressions of long type - make sure they're truncated
     * before being copied into the array.
     * Also use an array of < 16 bytes so it's not 16-byte aligned, so there are
     * eightbytes that include both array elements and other values.
     * Also leave last element uninitialized; in assembly, we should set it to
     * zero without overwriting what follows
     */
    int arr[3] = {global_one * 2l, global_one + three()};
    unsigned int u = 2684366905;

    // check surrounding objects
    if (i != -1) {
        return 0;
    }
    if (u != 2684366905) {
        return 0;
    }

    // check arr itself
    return (arr[0] == 2 && arr[1] == 4 && !arr[2]);
}

int main(void) {
    if (!test_simple()) {
        return 1;
    }

    if (!test_partial()) {
        return 2;
    }

    long negative_seven_billion = -7000000000l;
    int i = 0;  // value of i doesn't matter, functions will always overwrite it
    if (!test_non_constant(negative_seven_billion, &i)) {
        return 3;
    }

    if (!test_type_conversion(&i)) {
        return 4;
    }

    if (!test_preserve_stack()) {
        return 5;
    }

    return 0;  // success
})"));
}

// extra_credit/compound_bitwise_subscript: compound bitwise operators on subscript
// expressions.
TEST_F(BookTest, Chapter15_CompoundBitwiseSubscript)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// compound bitwise assignment on subscript expressions
int main(void) {
    unsigned long arr[4] = {
        2147483648l,                // 2^31
        18446744069414584320ul,     // 0xffff_ffff_0000_0000
        9223372036854775808ul,      // 2^63
        1085102592571150095l        // 0x0f0f_0f0f_0f0f_0f0f
    };

    // &=
    arr[1] &= arr[3];
    if (arr[1] != 1085102592318504960 /* 0x0f0f_0f0f_0000_0000 */) {
        return 1;
    }

    // |=
    arr[0] |= arr[1];
    if (arr[0] != 1085102594465988608ul) {
        return 2;
    }

    // ^=
    arr[2] ^= arr[3];
    if (arr[2] != 10308474629425925903ul) {
        return 3;
    }

    // >>=
    arr[3] >>= 25;
    if (arr[3] != 32338577287l) {
        return 4;
    }

    // <<=
    arr[1] <<= 12;
    if (arr[1] != 17361640446303928320ul) {
        return 5;
    }

    return 0; // success
})"));
}

// extra_credit/compound_pointer_assignment: pointer += and -= with integer operands
// of different types; `4294967295U + i` wraps to 3.
TEST_F(BookTest, Chapter15_CompoundPointerAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Pointer arithmetic with +=/-=

int i = 4;

int int_array(void) {
    int arr[6] = {1, 2, 3, 4, 5, 6};
    int *ptr = arr;

    // basic +=
    if (*(ptr += 5) != 6) {
        return 1; // fail
    }
    if (ptr[0] != 6) {
         return 2; // fail
    }

    if (ptr != arr + 5) {
        return 3;
    }

    // basic -=
    if (*(ptr -=3) != 3) {
        return 4; // fail
    }
    if (ptr[0] != 3) {
        return 5;
    }
    if (ptr != arr + 2) {
        return 6;
    }

    // += w/ more complex rval
    if ((ptr += i - 1) != arr + 5) {
        return 7;
    }

    if (*ptr != 6) {
        return 8;
    }

    // with rval of different types
    // here, rval is unsigned and wraps around
    if ((ptr -= (4294967295U + i)) != arr + 2) {
        return 9;
    }

    if (*ptr != 3) {
        return 10;
    }

    long l = 9223372036854775807l;
    if ((ptr += l - 9223372036854775806l) != arr + 3) {
        return 11;
    }

    if (*ptr != 4) {
        return 12;
    }

    return 0; // success
}

int double_array(void) {
    // identical to int_array but with static double array instead
    static double arr[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double *ptr = arr;

    // basic +=
    if (*(ptr += 5) != 6) {
        return 1; // fail
    }
    if (ptr[0] != 6) {
         return 2; // fail
    }

    if (ptr != arr + 5) {
        return 3;
    }

    // basic -=
    if (*(ptr -=3) != 3) {
        return 4; // fail
    }
    if (ptr[0] != 3) {
        return 5;
    }
    if (ptr != arr + 2) {
        return 6;
    }

    // += w/ more complex rval
    if ((ptr += i - 1) != arr + 5) {
        return 7;
    }

    if (*ptr != 6) {
        return 8;
    }

    // with rval of different types
    // here, rval is unsigned and wraps around
    if ((ptr -= (4294967295U + i)) != arr + 2) {
        return 9;
    }

    if (*ptr != 3) {
        return 10;
    }

    long l = 9223372036854775807l;
    if ((ptr += l - 9223372036854775806l) != arr + 3) {
        return 11;
    }

    if (*ptr != 4) {
        return 12;
    }

    return 0;
}

int main(void) {
    int result;

    if ((result = int_array())) {
        return result; // int_array returned non-zero result - fail
    }
    if ((result = double_array())) {
        return result + 12; // double_array returned non-zero result - fail
    }
    return 0; // success
})"));
}

// --- Relies on x86 32-bit unsigned wraparound --------------------------------

// extra_credit/compound_assign_to_subscripted_val: unsigned += and *= wrap mod 2^32.
TEST_F(BookTest, Chapter15_CompoundAssignToSubscriptedVal)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(R"(// Test compound assignment where LHS is a subscript expression

unsigned unsigned_arr[4] = {4294967295U, 4294967294U, 4294967293U, 4294967292U};

int idx = 2;
long long_idx = 1;

int main(void) {
    long_idx = -long_idx; // -1
    // flat array
    unsigned_arr[1] += 2;  // should wrap around to 0
    if (unsigned_arr[1]) {
        return 1;  // fail
    }
    unsigned_arr[idx] -= 10.0;
    if (unsigned_arr[idx] != 4294967283U) {
        return 2;  // fail
    }

    unsigned *unsigned_ptr = unsigned_arr + 4;  // pointer one past end
    unsigned_ptr[long_idx] /= 10;  // pointer to last element, unsigned_arr[3]
    if (unsigned_arr[3] != 429496729U) {
        return 3;  // fail
    }

    // unsigned_arr[2]; 4294967283 * 4294967295 (wraps mod 2^32)
    unsigned_ptr[long_idx *= 2] *= unsigned_arr[0];
    if (unsigned_arr[2] != 13) {
        return 4;  // fail
    }

    // unsigned_arr[2 + -2] --> unsigned_arr[0]
    if ((unsigned_arr[idx + long_idx] %= 10) != 5) {
        return 5;  // fail
    }

    // validate other three four elements; make sure updating one didn't
    // accidentally clobber its neighbors
    if (unsigned_arr[0] != 5u) {
        return 6;  // fail
    }

    if (unsigned_arr[1]) {  // should still be 0
        return 7;           // fail
    }

    if (unsigned_arr[2] != 13) {
        return 8;  // fail
    }

    if (unsigned_arr[3] != 429496729U) {
        return 9;  // fail
    }

    return 0;
})"));
}
