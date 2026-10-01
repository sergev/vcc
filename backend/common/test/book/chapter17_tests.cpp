//
// Chapter 17 — void / sizeof / dynamic allocation: valid programs compiled and
// run on BESM-6.  Imported from "Writing a C Compiler" (tests/chapter_17/valid:
// void + void_pointer + sizeof + extra_credit + libraries).  Each program
// defines int main(void); b6sim --status prints its return value, and we compare
// program output against the expected value.  The book's host-only
// "#ifdef SUPPRESS_WARNINGS / #pragma" blocks are dropped (our scanner has no
// preprocessor); two-file "libraries" cases are merged into one source, client
// first.
//
// Two facts specific to this chapter drive the split:
//
//   * sizeof is evaluated in the frontend with BESM-6 byte sizes (CodegenTest
//     sets target=besm6): char == 1, but short/int/long/long long/float/double/
//     long double/pointer are all 6 (one 48-bit word).  The book's sizeof
//     self-checks assert x86 sizes (4 and 8); each such literal is rewritten to
//     the BESM-6 value so the program is a passing run test (returns 0).  Two
//     incidental obstacles are worked around: block-scope `static` on locals
//     used only as sizeof operands is dropped (the backend has no static-local
//     storage), and the single sizeof(int[4294967297L][100000000]) check whose
//     result exceeds the BESM-6 integer range is removed.
//
//   * libc has no malloc/calloc/realloc/aligned_alloc/free (no runtime heap), so
//     every dynamic-allocation program is rewritten to use static storage
//     instead (memset/memcmp/memcpy themselves ARE in libc).  Two programs that
//     still have no BESM-6 analogue are grouped at the bottom with one-line
//     reasons: one global array too large for BESM-6 core and one loop that
//     exceeds the ctest timeout.
//
#include "book_test.h"



// --- void --------------------------------------------------------------------

// void/cast_to_void: cast expressions (variable, call, void call) to void.
TEST_F(BookTest, Chapter17_CastToVoid)
{
    EXPECT_EQ("12\n", CompileAndRunBook(R"(/* Test that we can cast expressions to void */

int x;

int set_x(int i) {
    x = i;
    return 0;
}

void do_nothing(void) {
    ;
}

int main(void) {
    (void) x; // cast a variable to void; this expression has no effect

    // cast to void discards this expression's value but we still need its side effect.
    (void) set_x(12);

    // you can cast an expression to void that's already void
    (void) do_nothing();
    return x;
})"));
}


// void/ternary: ternary expressions where both sides are void.
TEST_F(BookTest, Chapter17_VoidTernary)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test ternary expressions where both sides are void */

int i = 4;
int j = 5;
int flag_1 = 1;
int flag_0 = 0;
void incr_i(void) {
    i = i + 1;
}
void incr_j(void) {
    j = j + 1;
}
int main(void) {
    flag_1 ? incr_i() : incr_j();  // increment i
    flag_0 ? incr_i() : incr_j();  // increment j
    if (i != 5) {
        return 1;
    }
    if (j != 6) {
        return 2;
    }

    // try a nested void expression

    flag_0 ? incr_j() : flag_1 ? incr_i() : incr_j();

    if (i != 6) {
        return 3;
    }

    if (j != 6) {
        return 4;
    }

    return 0;
})"));
}


// void/void_function: functions with void return values, incl. early return.
TEST_F(BookTest, Chapter17_VoidFunction)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test our support for functions with void return values */
int foo = 0;

void set_foo_to_positive_num(int a) {
    if (a < 0) {
        // even if we don't return a value,
        /// make sure return statement exits the function!
        return;
    }
    foo = a;
    return;
}
void do_nothing(void) {
    // no return statement
}

int main(void) {
    set_foo_to_positive_num(-2);
    if (foo) { // value of foo should still be 0
        return 1;
    }
    set_foo_to_positive_num(12);

    if (foo != 12) {
        return 2;
    }
    do_nothing();
    return 0;
})"));
}


// void/void_for_loop: void expressions in a for-loop header.  putchar -> libc
// putch; prints the uppercase alphabet Z..A, A..Z, Z..A (uppercase Latin renders
// as ASCII), then b6sim --status prints main()'s 0.
TEST_F(BookTest, Chapter17_VoidForLoop)
{
    EXPECT_EQ("ZYXWVUTSRQPONMLKJIHGFEDCBAABCDEFGHIJKLMNOPQRSTUVWXYZZYXWVUTSRQPONMLKJIHGFEDCBA0\n",
              CompileAndRunBook(R"(/* Test for void expressions in for loop header */

int putch(int c);  // libc (book uses putchar)

int letter;
void initialize_letter(void) {
    letter = 'Z';
}

void decrement_letter(void) {
    letter = letter - 1;
}

int main(void) {
    // void expression in initial condition: print the alphabet backwards
    for (initialize_letter(); letter >= 'A';
         letter = letter - 1) {
        putch(letter);
    }

    // void expression in post condition: print the alphabet forwards
    for (letter = 'A'; letter <= 90; (void)(letter = letter + 1)) {
        putch(letter);
    }

    // void expressions in both conditions: print the alphabet backwards again
    for (initialize_letter(); letter >= 65; decrement_letter()) {
        putch(letter);
    }
    return 0;
})"));
}


// --- sizeof (x86 size literals rewritten to BESM-6 sizes) --------------------

// sizeof/simple: two forms of sizeof (type names and expressions).
TEST_F(BookTest, Chapter17_SizeofSimple)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Basic test of two forms of sizeof: referring to type names and expressions */

int main(void) {
    if (sizeof (int) != 6) {
        return 1;
    }

    if (sizeof 3.0 != 6) {
        return 2;
    }

    return 0;
})"));
}


// sizeof/sizeof_basic_types: size of all basic types (char==1, word types==6).
TEST_F(BookTest, Chapter17_SizeofBasicTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we can get the size of all basic type */

int main(void) {
    if (sizeof(char) != 1) {
        return 1;
    }

    if (sizeof(signed char) != 1) {
        return 2;
    }

    if (sizeof(unsigned char) != 1) {
        return 3;
    }

    if (sizeof(int) != 6) {
        return 4;
    }
    if (sizeof(unsigned int) != 6) {
        return 5;
    }

    if (sizeof(long) != 6) {
        return 6;
    }
    if (sizeof(unsigned long) != 6) {
        return 7;
    }

    if (sizeof(double) != 6) {
        return 8;
    }

    return 0;
})"));
}


// sizeof/sizeof_consts: the type, and size, of all constants (char const has
// int type; word types are 6 bytes).
TEST_F(BookTest, Chapter17_SizeofConsts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly determine the type, and size, of all constants */

int main(void) {
    // test that character constants have integer type, not character type;
    // we couldn't test this in the previous chapter
    if (sizeof 'a' != 6) {
        return 1;
    }

    // int
    if (sizeof 2147483647 != 6) {
        return 2;
    }

    // unsigned int
    if (sizeof 4294967295U != 6) {
        return 3;
    }

    // long
    if (sizeof 2l != 6) {
        return 4;
    }

    // unsigned long
    if (sizeof 0ul != 6) {
        return 5;
    }

    // double
    if (sizeof 1.0 != 6) {
        return 6;
    }
    return 0;
})"));
}


// sizeof/sizeof_result_is_ulong: sizeof yields an unsigned long (size 6 here);
// second check exercises its unsignedness, independent of the size value.
TEST_F(BookTest, Chapter17_SizeofResultIsUlong)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that sizeof expression results in an unsigned long */

int main(void) {

    // sizeof result is a ulong, so _its_ size is 6 (one word)
    if (sizeof sizeof (char) != 6) {
        return 1;
    }

    // make sure sizeof result is unsigned
    // since the common type of ulong and int is ulong,
    // the result of subtraction here will be positive unsigned int
    // (and 0 in comparison will also be converted to 0u)
    if (sizeof 4 - sizeof 4 - 1 < 0) {
        return 2;
    }

    return 0;
})"));
}


// sizeof/sizeof_array: arrays keep their type under sizeof (no decay), array
// parameters are adjusted to pointers, and sizeof of a string literal is its
// decoded byte length incl. NUL (sizeof "Hello, World!" == 14).
TEST_F(BookTest, Chapter17_SizeofArray)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that arrays don't decay to pointers
 * when they're the operands of sizeof expression */

unsigned long sizeof_adjusted_param(int arr[3]) {
    // this should return the size of arr's _adjusted_ type,
    // so it should return the size of a pointer (6) instead of 18
    return sizeof arr;
}

int main(void) {
    // flat array
    int arr[3];
    if (sizeof arr != 18) {
        return 1;
    }

    long nested_arr[4][5];

    // arr[2] has type long[5], so its size is 6 * 5 = 30
    if (sizeof nested_arr[2] != 30) {
        return 2;
    }

    // string literals also don't decay to pointers in sizeof expressions
    if (sizeof "Hello, World!" != 14) {
        return 3;
    }

    // parameters declared with array type are adjusted to pointers,
    // and sizeof reflects this
    if (sizeof_adjusted_param(arr) != 6) {
        return 4;
    }

    return 0;
})"));
}


// sizeof/sizeof_derived_types: sizes of derived (pointer and array) types,
// including the nested abstract declarator double(*([3][4]))[2].
TEST_F(BookTest, Chapter17_SizeofDerivedTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we accurately calculate the size of derived (pointer and array)
 * types */

int main(void) {
    // start with a simple array type
    // 2 * 6 = 12; sizeof int is 6
    if (sizeof(int[2]) != 12) {
        return 1;
    }

    // try a nested array type
    // 3 * 6 * 17 * 9 == 2754; sizeof char is 1
    if (sizeof(char[3][6][17][9]) != 2754) {
        return 2;
    }

    // now try some pointer types; these are always 6 bytes (one word) no matter
    // what they point to
    if (sizeof(int *) != 6) {
        return 4;
    }

    if (sizeof(int(*)[2][4][6]) !=
        6) {  // pointer to a big array is still a pointer
        return 5;
    }

    if (sizeof(char *) != 6) {
        return 6;
    }

    // array of pointers
    // this is an array of three arrays of four pointers; 3 * 4 * 6 = 72
    // each pointer points to element type "array of four doubles"
    // but that doesn't impact the size of this type
    if (sizeof(double(*([3][4]))[2]) != 72) {
        return 7;
    }

    return 0;
})"));
}


// sizeof/sizeof_not_evaluated: sizeof does not evaluate its operand (foo, which
// would call exit, is never run).  sizeof(int) == 6 on BESM-6.
TEST_F(BookTest, Chapter17_SizeofNotEvaluated)
{
    EXPECT_EQ("6\n", CompileAndRunBook(R"(#include <stdlib.h>
int foo(void) { exit(10); }

int main(void) {
  // make sure foo isn't actually called
  return sizeof(foo());
})"));
}


// --- sizeof extra_credit (`static` dropped on test locals; sizes rewritten) --

// extra_credit/sizeof_bitwise: size of bitwise/bitshift expressions (common
// type / promoted left operand; all word types are 6 here).
TEST_F(BookTest, Chapter17_SizeofBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of bitwise and bitshift expression
int main(void) {
    long l = 0;
    int i = 0;
    char c = 0;

    // result type for &, |, ^ is common type
    if (sizeof (c & i) != 6) {
        return 1;  // fail
    }

    if (sizeof (i | l) != 6) {
        return 2;  // fail
    }

    // character operands are promoted
    if (sizeof (c ^ c) != 6) {
        return 3;  // fail
    }

    // result type for <<, >> is type of left operand
    if (sizeof (i << l) != 6) {
        return 4; // fail
    }

    // character operands are promoted
    if (sizeof (c << i) != 6) {
        return 5; // fail
    }

    if (sizeof (l >> c) != 6) {
        return 6; // fail
    }

    return 0;
})"));
}


// extra_credit/sizeof_compound: size of compound-assignment expressions, which
// are not evaluated (the type of the left operand; uc %= 2 stays char size 1).
TEST_F(BookTest, Chapter17_SizeofCompound)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get size of compound expressions (and don't evaluate
// them)

int main(void) {
    long long_arr[2] = {1, 2};
    int i = 3;
    unsigned char uc = 4;
    double d = 5.0;
    long *ptr = long_arr;

    if (sizeof(long_arr[1] *= 10) != 6) {
        return 1;  // fail
    }
    if (sizeof(i /= 10ul) != 6) {
        return 2;  // fail
    }
    if (sizeof(uc %= 2) != 1) {
        return 3;  // fail
    }
    if (sizeof(d -= 11) != 6) {
        return 4;  // fail
    }
    if (sizeof(ptr += 1) != 6) {
        return 5;  // fail
    }

    // make sure we didn't actually evaluate any sizeof operands
    if (long_arr[0] != 1) {
        return 6;  // fail
    }
    if (long_arr[1] != 2) {
        return 7;  // fail
    }
    if (i != 3) {
        return 8;  // fail
    }
    if (uc != 4) {
        return 9;  // fail
    }
    if (d != 5.0) {
        return 10;  // fail
    }
    if (ptr != long_arr) {
        return 11;  // fail
    }

    return 0;  // success
})"));
}


// extra_credit/sizeof_compound_bitwise: size of compound bitwise expressions
// (not evaluated; left-operand type, signed-char results stay 1).
TEST_F(BookTest, Chapter17_SizeofCompoundBitwise)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of compound bitwise operations
// (and don't evaluate them)

int main(void) {
    signed char sc = 10;
    unsigned int u = 10000u;
    long l = -99999;

    if (sizeof(sc &= l) != 1) {
        return 1;  // fail
    }

    if (sizeof(l |= u) != 6) {
        return 2;  // fail
    }

    if (sizeof(u ^= l) != 6) {
        return 3;  // fail
    }
    if (sizeof(l >>= sc) != 6) {
        return 4;
    }
    if (sizeof(sc <<= sc) != 1) {
        return 5;
    }

    // make sure we didn't perform updates
    if (sc != 10) {
        return 6;  // fail
    }
    if (u != 10000u) {
        return 7;  // fail
    }
    if (l != -99999) {
        return 8;  // fail
    }

    return 0;
})"));
}


// extra_credit/sizeof_incr: size of ++/-- expressions (not evaluated; operand
// type, char results stay 1).  `static` dropped on arr.
TEST_F(BookTest, Chapter17_SizeofIncr)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we correctly get the size of ++ and -- expressions (and don't evaluate them)

int main(void) {
    int i = 0;
    long l = 0;
    char arr[3] = {0, 0, 0};
    char *ptr = arr;
    if (sizeof (i++) != 6) {
        return 1; // fail
    }

    if (sizeof (arr[0]--) != 1) {
        return 2; // fail
    }


    if (sizeof (++l) != 6) {
        return 3; // fail
    }

    if (sizeof (--arr[1]) != 1) {
        return 4; // fail
    }

    if (sizeof (ptr--) != 6) {
        return 5;
    }

    // make sure we didn't actually increment/decrement anything

    if (i) {
        return 6; // fail
    }

    if (l) {
        return 7; // fail
    }

    if (arr[0] || arr[1] || arr[2]) {
        return 8; // fail
    }

    if (ptr != arr) {
        return 9; // fail
    }

    return 0; // success
})"));
}


// =============================================================================
// Dynamic-allocation programs rewritten for the BESM-6 (no heap): malloc/calloc
// replaced with static storage; free dropped.  memset/memcmp/memcpy ARE in libc.
// =============================================================================

// BESM-6: static array instead of malloc (no heap).
TEST_F(BookTest, Chapter17_VoidPointerSimple)
{
    EXPECT_EQ("100\n", CompileAndRunBook(R"(/* A simple test of using statically allocated memory */

int main(void) {
    static int array[10];
    array[2] = 100;
    int result = array[2];
    return result;
})"));
}


// BESM-6: static zeroed array instead of calloc.
TEST_F(BookTest, Chapter17_ArrayOfPointersToVoid)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test using complex types derived from void
 * arrays of void are illegal, but arrays of pointer to void are allowed */

int main(void) {
    int i = 10;

    static int s2[2];  // zero-initialized static storage (replaces calloc)

    // declare an array of 4 pointers to void;
    // we can implicitly convert elements in this compound initializer to void
    void *arr[4] = {
        s2,                      // pointer to zeroed static memory
        &i,                      // implicitly convert int * to void *
        0,                       // convert null pointer constant to void *
        arr  // pointer to arr itself - implicitly convert (void *[4]) to void *
    };

    // first element points to 8 bytes, all initialized to 0
    // cast this to a long
    long *l = arr[0];
    if (*l) // l should point to value 0
        return 1;

    // second element points to i
    int elem_1_val = *(int *)arr[1];
    if (elem_1_val != 10)
        return 2;

    // 3rd element is a null pointer
    if (arr[2])
        return 3;

    // 4th element points to arr itself! trippy!
    if (arr[3] != arr)
        return 4;
    return 0;
})"));
}


// BESM-6: static buffer instead of calloc.
TEST_F(BookTest, Chapter17_CommonPointerType)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test finding the common type of void * and other pointer types (it's always
 * void *) */

int main(void) {
    // get a pointer to void (zeroed static storage replaces calloc)
    static unsigned int buf3[3];
    void *void_ptr = buf3;

    // we'll use 'array' a a pointer to a complete object
    unsigned int array[3] = {1, 2, 3};

    // like other pointers, void * can be compared to null pointer constant
    if (void_ptr == 0)
        return 1;

    // compare with ==
    if (void_ptr == array)
        return 2;

    // compare with !=
    if (!(void_ptr != array))
        return 3;

    static void *null_ptr = 0;
    int *my_array = null_ptr ? void_ptr : array;

    int array_element = my_array[1];

    if (array_element != 2) {
        return 4;
    }

    return 0;
})"));
}


// BESM-6: static storage replaces malloc; pointer-byte aliasing and the memcmp
// sign check hold under the big-endian byte-#0 layout (memcmp returns *a-*b).
TEST_F(BookTest, Chapter17_ConversionByAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* In conversion as if by assignment, we can implicitly convert between void *
 * and other pointer types. */

int memcmp(void *s1, void *s2, unsigned long n);

void *return_ptr(char *i) {
    return i + 3;
}

int check_char_ptr_argument(char *pointer, char expected_val) {
    return *pointer == expected_val;
}

int *return_void_ptr_as_int_ptr(void *pointer) {
    return pointer;
}

static double dbl5[5];  // static backing for get_dbl_array (replaces malloc)

double *get_dbl_array(unsigned long n) {
    return dbl5;
}

void set_doubles(double *array, unsigned long n, double d) {
    for (unsigned long i = 0; i < n; i = i + 1) {
        array[i] = d;
    }
    return;
}

void *return_dbl_ptr_as_void_ptr(double *ptr) {
    return ptr;
}

int main(void) {
    static long fourw;  // one word of static storage (replaces malloc(4))
    void *four_bytes = &fourw;

    int *int_ptr = four_bytes;

    // BESM-6: write through the char view and read it back (the original wrote an
    // int -1 and read byte #0 through char *, which depends on x86 byte layout).
    char *char_ptr = four_bytes;
    *char_ptr = 100;

    if (!check_char_ptr_argument(four_bytes, 100)) {
        return 1;
    }

    if (return_void_ptr_as_int_ptr(four_bytes) != int_ptr) {
        return 2;
    }

    double *dbl_ptr = four_bytes;
    int (*complicated_ptr)[3][2][5] = four_bytes;
    long *long_ptr = four_bytes;
    if (dbl_ptr != four_bytes || complicated_ptr != four_bytes || long_ptr != four_bytes) {
        return 3;
    }

    double *dbl_array = get_dbl_array(5);

    void *void_array = dbl_array;

    set_doubles(void_array, 5, 4.0);
    if (dbl_array[3] != 4.0) {
        return 4;
    }

    if (return_dbl_ptr_as_void_ptr(dbl_array) != void_array) {
        return 5;
    }

    void *some_other_ptr = 0;

    some_other_ptr = dbl_array;
    if (some_other_ptr != void_array) {
        return 6;
    }

    some_other_ptr = &some_other_ptr;
    if (some_other_ptr == void_array) {
        return 7;
    }

    complicated_ptr = 0;
    some_other_ptr = complicated_ptr;
    if (some_other_ptr) {
        return 8;
    }

    static long lpa0, lpa1, lpa2;  // static storage replaces three malloc(sizeof(long))
    long *long_ptr_array[3] = { &lpa0, &lpa1, &lpa2 };

    *long_ptr_array[0] = 100l;
    *long_ptr_array[1] = 200l;
    *long_ptr_array[2] = 300l;
    long sum = (*long_ptr_array[0] + *long_ptr_array[1] + *long_ptr_array[2]);
    if (sum != 600l) {
        return 9;
    }

    long arr1[3] = {1, 2, 3};
    long arr2[3] = {1, 2, 3};
    long arr3[3] = {1, 2, 4};
    if (memcmp(arr1, arr2, sizeof arr1) != 0) {
        return 10;
    }
    if (memcmp(arr1, arr3, sizeof arr2) >= 0) {
        return 11;
    }
    return 0;
})"));
}


// BESM-6: static double[4] replaces malloc; the x86 `% 8` alignment check is
// removed (pointers are word addresses, not byte addresses).
TEST_F(BookTest, Chapter17_VoidPointerExplicitCast)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* test explicit casts between void * and other pointer types,
 * and between void * and integer types
 */

int main(void) {
    static double dbuf[4];
    void *ptr = dbuf;
    double *double_ptr = (double *)ptr;
    double_ptr[2] = 10.0;
    if ((void *)double_ptr != ptr) {
        return 1;
    }
    double result = double_ptr[2];

    if (result != 10.0) {
        return 2;
    }

    long zero = 0;
    ptr = (void *) zero;
    if (ptr) {
        return 4;
    }
    zero = (long) ptr;
    if (zero) {
        return 5;
    }
    return 0;
})"));
}


// BESM-6: static buffers replace malloc/realloc/calloc; the realloc "grow" is a
// no-op since the static buffer is already the larger size, and aligned_alloc +
// its `% 256` check are removed (no BESM-6 analogue — pointers are word addresses).
TEST_F(BookTest, Chapter17_MemoryManagementFunctions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can write, grow, and read back statically allocated buffers */

int main(void) {
    static char char_buffer[100];  // already the post-"realloc" size
    for (int i = 0; i < 50; i = i + 1) {
        char_buffer[i] = i;
    }

    char *char_buffer2 = char_buffer;  // "realloc" reuses the same storage
    char_buffer2[75] = 11;

    for (int i = 0; i < 50; i = i + 1) {
        if ( char_buffer2[i] != i) {
            return 1;
        }
    }

    if (char_buffer2[75] != 11) {
        return 2;
    }

    static double double_buffer[10];  // zero-initialized (replaces calloc)
    for (int i = 0; i < 10; i = i + 1) {
        if (double_buffer[i]) {
            return 3;
        }
    }
    return 0;
})"));
}


// BESM-6: static buffer instead of malloc; sizeof checks use BESM-6 word sizes.
TEST_F(BookTest, Chapter17_SizeofExpressions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly get the size of a range of expressions */

int main(void) {
    double d;

    if (sizeof d != 6) {
        return 2;
    }

    unsigned char c;

    if (sizeof c != 1) {
        return 3;
    }

    static char sbuf[100];
    void *buffer = sbuf;

    if (sizeof(buffer) != 6) {
        return 4;
    }

    if (sizeof ((int)d) != 6) {
        return 5;
    }

    if (sizeof (d ? c : 10l) != 6) {
        return 6;
    }

    if (sizeof (c = 10.0) != 1) {
        return 7;
    }

    return 0;
})"));
}


// BESM-6: static zeroed buffer instead of calloc; memset is in libc.
TEST_F(BookTest, Chapter17_PassAllocedMemory)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(void *memset(void *s, int c, unsigned long n);

static char zeroed_bytes[100];  // zero-initialized static storage (replaces calloc)

void *get_100_zeroed_bytes(void) {
    return zeroed_bytes;
}

void fill_100_bytes(void *pointer, int byte) {
    memset(pointer, byte, 100);
}

int main(void) {

    void *mem = get_100_zeroed_bytes();
    for (int i = 0; i < 100; i = i + 1) {
        if (((char *) mem + i)[0]) {
            return 1;
        }
    }

    fill_100_bytes(mem, 99);

    for (int i = 0; i < 100; i = i + 1) {
        if (((char *) mem + i)[0] != 99) {
            return 2;
        }
    }

    return 0;
})"));
}


// --- (B) sizeof a multi-dimensional global array ----------------------------

// libraries/sizeof_extern, shrunk to fit BESM-6 core: the book's double[1000][2000]
// is 12M words (core is only 32K), so use double[10][20] == 200 words; sizeof is
// 200 elements * 6 bytes/word == 1200.
TEST_F(BookTest, Chapter17_SizeofExtern)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(double large_array[10][20];

int main(void) {
    return sizeof large_array == 1200;
})"));
}


// --- (C) loop over ctest timeout --------------------------------------------

// libraries/test_for_memory_leaks: the textbook 10M-iteration loop is over the
// 10s timeout (void function call exercises stack-frame restore).  Shrunk to 100
// iterations: sum accumulates i over 0..99, so sum == 100*99/2 == 4950.
TEST_F(BookTest, Chapter17_TestForMemoryLeaks)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(#include <stdlib.h>

long sum = 0;
void lots_of_args(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int l, int m, int n, int o) {
    if (a != 1) { exit(1); }
    if (b != 2) { exit(2); }
    if (c != 3) { exit(3); }
    if (d != 4) { exit(4); }
    if (e != 5) { exit(5); }
    if (f != 6) { exit(6); }
    if (g != 7) { exit(7); }
    if (h != 8) { exit(8); }
    if (i != 9) { exit(9); }
    if (j != 10) { exit(10); }
    if (k != 11) { exit(11); }
    if (l != 12) { exit(12); }
    if (m != 13) { exit(13); }
    if (n != 14) { exit(14); }
    sum = sum + o;
    return;
}

int main(void) {
    for (int i = 0; i < 100; i = i + 1) {
        lots_of_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, i);
    }
    if (sum != 4950) {
        return 15;
    }
    return 0;
})"));
}
