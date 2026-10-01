#include "book_test.h"

// BESM-6: rewritten to use a local struct instead of calloc (no heap dependency).
TEST_F(BookTest, Chapter18_MemberComparisons)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test comparisons between pointers to structures and structure members:
 * 1. Pointers to a structure and its first member compare equal
 * 2. Pointers to later structure members compare greater than pointers
 *    to earlier structure members
 */

struct three_ints {
    int a;
    int b;
    int c;
};

int main(void) {
    struct three_ints storage = {0, 0, 0};
    struct three_ints* my_struct = &storage;

    // compare struct pointer with pointer to first member
    if ((void*)my_struct != &my_struct->a) {
        return 1; // fail
    }

    // do the same with a relational operator
    if (!((int *)my_struct <= &my_struct->a)) {
        return 2; // fail
    }

    // compare earlier to later members using a few different relational operators
    if (&my_struct->c <= &my_struct->a) {
        return 3;  // fail
    }

    if (&my_struct->b > &my_struct->c) {
        return 4;  // fail
    }

    if (!(&my_struct->b > &my_struct->a)) {
        return 5;  // fail
    }

    return 0; // success
}
)PROG"));
}

// malloc + pointer-to-integer byte-address arithmetic.
TEST_F(BookTest, Chapter18_MemberOffsets)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// struct declarations for size/layout tests
//
// On BESM-6 a word is 6 bytes, every aggregate member is word-aligned, and a
// struct's sizeof is rounded up to a multiple of 6.

struct eight_bytes {
    int i;   // bytes 0-5 (one word)
    char c;  // byte 6
             // padded up to a word multiple -> sizeof 12
};

struct two_bytes {
    char arr[2];  // bytes 0-1
                  // padded up to a word -> sizeof 6
};

struct three_bytes {
    char arr[3];  // bytes 0-2
                  // padded up to a word -> sizeof 6
};

struct sixteen_bytes {
    struct eight_bytes eight;  // bytes 0-11
    struct two_bytes two;      // bytes 12-17
    struct three_bytes three;  // bytes 18-23
};                             // sizeof 24

struct wonky {
    char arr[19];
};  // sizeof 24 (19 data bytes + 5 bytes padding up to a word multiple)

struct internal_padding {
    char c;    // byte 0
    double d;  // byte 6 (word-aligned)
};             // sizeof 12

struct contains_struct_array {
    char c;                              // byte 0
    struct eight_bytes struct_array[3];  // bytes 6-41 (word-aligned)
};                                       // sizeof 42
/* Get the addresses of structure members to validate their offset and alignment
 * (including nested members accessed through chains of . and -> operations)
 * and addresses of one-past-the-end of structs to validate trailing padding.
 *
 * On BESM-6 a pointer-to-integer cast does not decode to a byte address, so we
 * compute byte offsets as char* - char* differences, which the backend decodes
 * via b/pdiff. */

// test 1: validate struct w/ scalar members (includes trailing padding)
// test member accesses of the form &x.y
int test_eightbytes(void) {
    struct eight_bytes s;
    char *start = (char *)&s;
    char *i_addr = (char *)&s.i;
    char *c_addr = (char *)&s.c;
    char *end = (char *)(&s + 1);

    // first element should always have same address as whole struct
    if (start != i_addr) {
        return 0;
    }

    // next element is one word in, at byte 6
    if (c_addr - start != 6) {
        return 0;
    }

    // end of struct is at byte 12 (padded up to a word multiple)
    if (end - start != 12) {
        return 0;
    }

    return 1;  // success
}

// test 2: validate struct w/ padding between members (accessing struct thru
// pointer) test member accesses of the form &x->y
int test_internal_padding(void) {
    struct internal_padding obj;
    struct internal_padding *s_ptr = &obj;
    char *start = (char *)s_ptr;
    char *c_addr = (char *)&s_ptr->c;
    char *d_addr = (char *)&s_ptr->d;
    char *end = (char *)(s_ptr + 1);

    // first element should always have same address as whole struct
    if (start != c_addr) {
        return 0;
    }

    // next element is word-aligned, at byte 6
    if (d_addr - c_addr != 6) {
        return 0;
    }

    // size of whole struct is 12 bytes
    if (end - start != 12) {
        return 0;
    }

    return 1;  // success
}

// test 3: validate struct that contains an array
// test member accesses of the form &x.y[i], x.y + i
int test_three_bytes(void) {
    // use static struct here to make sure that doesn't impact address
    // calculation
    static struct three_bytes s;

    char *start = (char *)&s;
    char *arr_addr = (char *)&s.arr;
    char *arr0_addr = (char *)&s.arr[0];
    char *arr1_addr = (char *)&s.arr[1];
    // different way to calculate same address as above
    char *arr1_addr_alt = (char *)(s.arr + 1);
    char *arr2_addr = (char *)&s.arr[2];
    char *arr_end = (char *)(s.arr + 3);
    char *struct_end = (char *)(&s + 1);

    // struct, array, and first array element should all have same address
    if (start != arr_addr) {
        return 0;
    }

    if (start != arr0_addr) {
        return 0;
    }

    // s.arr[1] and s.arr[2] should be at byte offsets 1 and 2
    if (arr1_addr - start != 1) {
        return 0;
    }

    if (arr1_addr != arr1_addr_alt) {
        return 0;
    }

    if (arr2_addr - start != 2) {
        return 0;
    }

    // arr_end is one past the 3-element char array, at byte offset 3
    if (arr_end - start != 3) {
        return 0;
    }

    // struct_end is at byte offset 6 (struct padded up to a word multiple)
    if (struct_end - start != 6) {
        return 0;
    }

    return 1;  // success
}

// test 4: validate struct containing nested structs
// test accesses of the form &x->y.z, &x->y.z[i],
// &x.y.z, &x.y.z[i]
int test_sixteen_bytes(void) {
    static struct sixteen_bytes s;
    struct sixteen_bytes *s_ptr = &s;

    // get addresses of various members through s_ptr
    char *start = (char *)s_ptr;
    char *eight_addr = (char *)&s_ptr->eight;
    char *eight_i_addr = (char *)&s_ptr->eight.i;
    char *eight_c_addr = (char *)&s_ptr->eight.c;
    char *two = (char *)&s_ptr->two;
    char *two_arr = (char *)s_ptr->two.arr;
    char *two_arr0 = (char *)&s_ptr->two.arr[0];
    char *two_arr1 = (char *)&s_ptr->two.arr[1];
    char *two_arr_end = (char *)(s_ptr->two.arr + 2);
    char *two_end = (char *)(&s_ptr->two + 1);
    char *three = (char *)&s_ptr->three;
    // not going to validate every individual element in three.arr
    // since we already did that for two.arr
    char *three_end = (char *)(&s_ptr->three + 1);
    char *struct_end = (char *)(s_ptr + 1);

    // struct, first member, first member's first member all have same address
    if (start != eight_addr) {
        return 0;
    }

    if (start != eight_i_addr) {
        return 0;
    }

    if (eight_c_addr - start != 6) {
        return 0;
    }

    // next member starts at byte 12
    if (two - start != 12) {
        return 0;
    }

    if (two_arr - start != 12) {
        return 0;
    }

    if (two_arr0 - start != 12) {
        return 0;
    }

    // validate next array element in s_ptr->two.arr
    if (two_arr1 - start != 13) {
        return 0;
    }

    // one past the 2-element char array, at byte 14
    if (two_arr_end - start != 14) {
        return 0;
    }

    // s_ptr->two is padded to a word, so its end is at byte 18
    if (two_end - start != 18) {
        return 0;
    }

    if (three - start != 18) {
        return 0;
    }

    if (three_end - start != 24) {
        return 0;
    }

    if (struct_end - start != 24) {
        return 0;
    }

    // now get addresses of a few members thru s directly and make sure they're
    // the same

    char *eight_i_addr_alt = (char *)&s.eight.i;
    char *eight_c_addr_alt = (char *)&s.eight.c;
    char *two_arr_alt = (char *)s.two.arr;
    char *two_arr1_alt = (char *)&s.two.arr[1];
    char *three_alt = (char *)&s.three;

    if (eight_i_addr_alt != eight_i_addr) {
        return 0;
    }

    if (eight_c_addr_alt != eight_c_addr) {
        return 0;
    }

    if (two_arr_alt != two_arr) {
        return 0;
    }

    if (two_arr1_alt != two_arr1) {
        return 0;
    }

    if (three_alt != three) {
        return 0;
    }

    return 1;  // success
}

// test 5: validate array of irregularly-sized structs; make sure there's no
// padding b/t array elements test access of the form x[i].y, &x[i].y[j]
int test_wonky_array(void) {
    struct wonky wonky_array[5];
    char *array_start = (char *)wonky_array;
    char *elem3 = (char *)(wonky_array + 3);
    char *elem3_arr = (char *)wonky_array[3].arr;
    char *elem2_arr2 = (char *)&wonky_array[2].arr[2];
    char *elem2_arr_end = (char *)(wonky_array[2].arr + 19);
    char *elem4_arr_end = (char *)(wonky_array[4].arr + 19);
    char *array_end = (char *)(wonky_array + 5);

    // each element is 24 bytes (19 data bytes + 5 bytes padding)
    if (elem3 - array_start != 24 * 3) {
        return 0;
    }

    if (elem3_arr != elem3) {
        return 0;
    }

    if (elem2_arr2 - array_start != 24 * 2 + 2) {
        return 0;
    }

    // 5 bytes of trailing padding b/t last data byte of elem2 and start of elem3
    if (elem3 - elem2_arr_end != 5) {
        return 0;
    }

    // 5 bytes of trailing padding b/t last data byte of elem4 and array end
    if (array_end - elem4_arr_end != 5) {
        return 0;
    }

    if (array_end - array_start != 24 * 5) {
        return 0;
    }

    return 1;  // success
}

// test 6: validate array of structs containing arrays of structs
// test access of the form x[i].y->z, x->y->z, where x and y are arrays that
// decay to pointers
int test_contains_struct_array_array(void) {
    struct contains_struct_array arr[3];
    char *array_start = (char *)arr;
    char *first_scalar_elem = (char *)(&arr[0].c);

    // arr[0].struct_array[0].i
    char *outer0_inner0_i = (char *)(&arr[0].struct_array->i);

    // arr[0].struct_array[0].c
    char *outer0_inner0_c = (char *)(&arr->struct_array->c);

    // one-past-the-end of arr[0].struct_array
    char *outer0_end = (char *)(arr->struct_array + 3);

    // start of arr[1] (should be the same as one-past-end of
    // arr[0].struct_array)
    char *outer1 = (char *)(&arr[1]);

    // struct_array of arr[1]
    char *outer1_arr = (char *)(arr[1].struct_array);

    // arr[1].struct_array[1].i
    char *outer1_inner1_i = (char *)&(((arr + 1)->struct_array + 1)->i);

    // arr[2].struct_array[0].c
    char *outer2_inner0_c = (char *)&((arr + 2)->struct_array->c);

    // validate pointers to start of struct
    if (first_scalar_elem != array_start) {
        return 0;
    }

    // 6 bytes into array (struct_array offset in contains_struct_array is 6,
    // i offset in eight_bytes is 0)
    if (outer0_inner0_i - array_start != 6) {
        return 0;
    }

    // 12 bytes into array (struct_array offset is 6,
    // c offset in eight_bytes is 6)
    if (outer0_inner0_c - array_start != 12) {
        return 0;
    }

    // no trailing padding in arr[0] (sizeof 42 is a word multiple)
    if (outer0_end != outer1) {
        return 0;
    }

    // check offsets in arr[1]
    if (outer1_arr - array_start != 48) {
        return 0;
    }

    if (outer1_arr - outer1 != 6) {
        return 0;
    }

    // arr[1] is 42 bytes into arr
    // arr[1].struct_array is 6 bytes into arr[1]
    // arr[1].struct_array[1] is 12 bytes into struct_array
    // arr[1].struct_array[1].i is 0 bytes into arr[1].struct_array[1]
    // total offset: 42+6+12 = 60
    if (outer1_inner1_i - array_start != 60) {
        return 0;
    }

    // arr[2] is 84 bytes into arr
    // arr[2].struct_array is 6 bytes into arr[2]
    // arr[2].struct_array[0] is 0 bytes into arr[2].struct_array
    // arr[2].struct_array[0].c is 6 bytes into eight_bytes
    // total offset: 84 + 6 + 6 = 96
    if (outer2_inner0_c - array_start != 96) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    if (!test_eightbytes()) {
        return 1;
    }

    if (!test_internal_padding()) {
        return 2;
    }

    if (!test_three_bytes()) {
        return 3;
    }

    if (!test_sixteen_bytes()) {
        return 4;
    }

    if (!test_wonky_array()) {
        return 5;
    }

    if (!test_contains_struct_array_array()) {
        return 6;
    }

    return 0;  // success
}
)PROG"));
}

// Passes structs by value as parameters and verifies the stack is not clobbered.  strcmp
// strings uppercased (KOI-7) and the irregular take_/pass_ helpers renamed to stay distinct
// within the Madlen 8-char identifier limit.
TEST_F(BookTest, Chapter18_ParametersStackClobber)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that passing structures as parameters doesn't clobber the stack.
 * To test this, we store some bytes to the stack, pass the struct, then
 * validate that those bytes haven't changed.
 * Our test functions don't store any values on the stack except the ones we
 * explicitly validate; e.g. they don't call functions that return values,
 * evaluate any expressions that undergo array decay (because the result of
 * GetAddr would be stored on the stack) or perform any other computations that
 * produce intermediate expressions. This ensures that if any value on the stack
 * is clobbered, we'll detect it.
 */


int strcmp(char *s1, char *s2);
#include <stdlib.h>

struct stack_bytes {
    char bytes[16];
};

// we copy bytes from the stack to here, then validate them
static struct stack_bytes to_validate;

// use this to validate to_validate after copying bytes from stack to it
void validate_stack_bytes(int code) {
    if (strcmp(to_validate.bytes, "EFGHIJKLMNOPQRS")) {
        exit(code);
    }
    return;
}

// test case 1: passing a struct holding four-byte int
struct one_longword {
    int i;
};

void take_longword(struct one_longword s, int code) {
    if (s.i != 10) {
        exit(code);
    }
    return;
}

int pass_longword(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};
    // make this static so it's not on the stack
    static struct one_longword my_var = {10};
    // this funcall doesn't require temporary values on the stack
    // b/c its args are just a variable and int (not more complex expressions)
    // and its return type is void
    take_longword(my_var, 1);

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // this funcall doesn't require temporary values on the stack
    // b/c its arg is just an int(not a more complex expression)
    // and its return type
    validate_stack_bytes(2);
    return 0;
}

// test case #2: passing a struct holding an eight-byte int
struct one_quadword {
    long l;
};

void take_quadword(struct one_quadword s, int code) {
    if (s.l != 10) {
        exit(code);
    }
    return;
}

int pass_quadword(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    static struct one_quadword my_var = {10};
    take_quadword(my_var, 3);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(4);
    return 0;
}

// test case #3: passing a struct holding a double
struct one_double {
    double d;
};

void take_double(struct one_double s, int code) {
    if (s.d != 10) {
        exit(code);
    }
    return;
}

int pass_double(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};
    static struct one_double my_var = {10};
    take_double(my_var, 5);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(6);
    return 0;
}

// test case #4: passing a struct holding twelve bytes
struct twelve_bytes {
    char arr[12];
};

void take_twelve_bytes(struct twelve_bytes s, int code) {
    if (strcmp(s.arr, "ABCDEFGHIJK")) {
        exit(code);
    }
    return;
}

int pass_twelve_bytes(void) {
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};
    static struct twelve_bytes my_var = {"ABCDEFGHIJK"};
    take_twelve_bytes(my_var, 7);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(8);
    return 0;
}

// test case #5: passing a struct in memory
// make sure this is an even number of quadwords so we don't need to add stack
// padding
struct memory {
    char arr[32];
};

void take_struct_in_mem(struct memory s, int code) {
    if (strcmp(s.arr, "HERE'S THE THING: I'M A STRING.")) {
        exit(code);
    }
    return;
}

int pass_struct_in_mem(void) {
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};
    static struct memory my_var = {"HERE'S THE THING: I'M A STRING."};
    take_struct_in_mem(my_var, 9);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(10);
    return 0;
}

// test case #6: passing a 3-byte struct
struct irregular {
    char arr[3];
};

void take3(struct irregular s, int code) {
    if (strcmp(s.arr, "12")) {
        exit(code);
    }
    return;
}

int pass3(void) {
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};
    static struct irregular my_var = {"12"};
    take3(my_var, 11);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(12);
    return 0;
}

// test case #7: passing an irregularly-sized struct in memory
// make sure this is an even number of quadwords so we don't need to add stack
// padding
struct irregular_memory {
    char arr[27];
};

void take27(struct irregular_memory s, int code) {
    if (strcmp(s.arr, "THE QUICK BROWN FOX JUMPED")) {
        exit(code);
    }
    return;
}

int pass27(void) {
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    static struct irregular_memory my_var = {"THE QUICK BROWN FOX JUMPED"};
    take27(my_var, 13);

    // validate stack
    to_validate = bytes;
    validate_stack_bytes(14);
    return 0;
}

int main(void) {
    pass_longword();
    pass_quadword();
    pass_double();
    pass_twelve_bytes();
    pass_struct_in_mem();
    pass3();
    pass27();
    return 0;
}
)PROG"));
}

// Passes and returns multi-word structs by value and verifies the stack is not clobbered.
// The validate_/return_/test_ helper families collided within the Madlen 8-char identifier
// limit, so they were renamed to short distinct names; the stack-bytes string was uppercased.
TEST_F(BookTest, Chapter18_ParamsAndReturnsStackClobber)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that returning a struct doesn't clobber the stack.
 * This is most likely when we're returning structs in memory, but test other
 * types of structs too.
 * To test this, we store some bytes on the stack, call a function that returns
 * the struct, then validate that those bytes haven't changed. In the functions
 * whose stacks we validate, we don't store any values on the stack except the
 * bytes to validate and the return value. This ensures that if the return
 * value clobbers any other bytes on the stack, we'll detect it. This is a
 * similar technique to
 * chapter_18/valid/no_structure_parameters/struct_copy/stack_clobber.c
 * This test assumes structures are allocated on the stack in the same order
 * they're declared/initialized (otherwise clobbers may overwrite stack padding
 * instead of data that we validate, and go undetected).
 */


int strcmp(char *s1, char *s2);
#include <stdlib.h>

struct stack_bytes {
    char bytes[16];
};

// we copy bytes from the stack to here, then validate them
static struct stack_bytes to_validate;

// use this to validate to_validate after copying bytes from stack to it
void vsb(int code) {
    if (strcmp(to_validate.bytes, "EFGHIJKLMNOPQRS")) {
        exit(code);
    }
    return;
}

// test case 1: return a struct in a general-purpose register
struct one_int_reg {
    char cs[7];
};

struct one_int_reg ret1(void) {
    struct one_int_reg retval = {{0, 0, 0, 0, 0, 0, 0}};
    return retval;
}

static struct one_int_reg one_int_struct;
void vck1(int code) {
    for (int i = 0; i < 7; i = i + 1) {
        if (one_int_struct.cs[i]) {
            exit(code);
        }
    }
}

int tc1(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    // call a function that returns a one-int struct
    // copy it to a static variable so we can validate it later
    // without putting more temporary variables on the satck
    one_int_struct = ret1();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // this funcall doesn't require temporary values on the stack
    // b/c its arg is just an int(not a more complex expression)
    // and its return type
    vsb(1);

    /// validate the static struct we copied the return val into earlier
    vck1(2);
    return 0;
}

// test case 2: return a struct in two general-purpose registers
struct two_int_regs {
    char cs[15];
};

struct two_int_regs ret2(void) {
    struct two_int_regs retval = {
        {20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34}};
    return retval;
}

static struct two_int_regs two_int_struct;
void vck2(int code) {
    for (int i = 0; i < 15; i = i + 1)
        if (two_int_struct.cs[i] != i + 20) {
            exit(code);
        }
}

int tc2(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    two_int_struct = ret2();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(3);

    /// validate returned struct
    vck2(4);
    return 0;
}

// test case 3: return a struct in one XMM register
struct one_xmm_reg {
    double d;
};

struct one_xmm_reg ret3(void) {
    struct one_xmm_reg retval = {234.5};
    return retval;
}

static struct one_xmm_reg one_double_struct;
void vck3(int code) {
    if (one_double_struct.d != 234.5) {
        exit(code);
    }
}

int tc3(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    one_double_struct = ret3();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(5);

    /// validate returned struct
    vck3(6);
    return 0;
}

// test case 4: return a struct in two XMM registers
struct two_xmm_regs {
    double d1;
    double d2;
};

struct two_xmm_regs ret4(void) {
    struct two_xmm_regs retval = {234.5, 678.25};
    return retval;
}

static struct two_xmm_regs two_doubles_struct;
void vck4(int code) {
    if (two_doubles_struct.d1 != 234.5 || two_doubles_struct.d2 != 678.25) {
        exit(code);
    }
}

int tc4(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    two_doubles_struct = ret4();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(7);

    /// validate returned struct
    vck4(8);
    return 0;
}

// test case 5: return a stuct in general-purpose and XMM registers

struct int_and_xmm {
    char c;
    double d;
};

struct int_and_xmm ret5(void) {
    struct int_and_xmm retval = {125, 678.25};
    return retval;
}

static struct int_and_xmm mixed_struct;
void vck5(int code) {
    if (mixed_struct.c != 125 || mixed_struct.d != 678.25) {
        exit(code);
    }
}

int tc5(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    mixed_struct = ret5();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(9);

    /// validate returned struct
    vck5(10);
    return 0;
}

// test case 6: return a struct on the stack
struct stack {
    char cs[28];
};

struct stack ret6(void) {
    struct stack retval = {{90,  91,  92,  93,  94,  95,  96,  97,  98,  99,
                            100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                            110, 111, 112, 113, 114, 115, 116, 117}};
    return retval;
}

static struct stack stack_struct;
void vck6(int code) {
    for (int i = 0; i < 28; i = i + 1) {
        if (stack_struct.cs[i] != i + 90) {
            exit(code);
        }
    }
}

int tc6(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    stack_struct = ret6();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(11);

    /// validate returned struct
    vck6(12);
    return 0;
}

// test case 7: return an irregularly-slized struct on the stack
struct stack_irregular {
    char cs[19];
};

struct stack_irregular ret7(void) {
    struct stack_irregular retval = {{70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                      80, 81, 82, 83, 84, 85, 86, 87, 88}};
    return retval;
}

static struct stack_irregular irregular_stack_struct;
void vck7(int code) {
    for (int i = 0; i < 19; i = i + 1) {
        if (irregular_stack_struct.cs[i] != i + 70) {
            exit(code);
        }
    }
}

int tc7(void) {
    // write some bytes to the stack
    struct stack_bytes bytes = {"EFGHIJKLMNOPQRS"};

    irregular_stack_struct = ret7();

    // assigning a variable doesn't produce any temporary values
    to_validate = bytes;

    // validate stack
    vsb(13);

    /// validate returned struct
    vck7(14);
    return 0;
}

int main(void) {
    tc1();
    tc2();
    tc3();
    tc4();
    tc5();
    tc6();
    tc7();
    return 0;
}
)PROG"));
}

// BESM-6 adaptation: there is no heap, so the `malloc(5)`,
// `calloc(1, sizeof(struct s))` and `calloc(1, sizeof(double))` allocations are
// replaced with zero-initialized block-scope statics (`struct s` is a file-scope
// tag, so task #62's block-local-static-struct limit does not apply; zeroed
// statics reproduce calloc's zero-init).  The four `validate_*` helpers all collide
// on the first 8 chars (`validate`) in Madlen's identifier space, so they are
// renamed to distinct `v*` names.  The message/strcmp string literals are
// UPPERCASED so the automatic (ASCII) and static-literal (KOI-7) paths compare
// equal.  The doubles (2e12, 2999.0, 150.0, 123.4) are each compared only against
// the compiler's own representation of the same constant, so equality is exact.
// FIXME: this test currently loops forever at run time — disabled until fixed.
TEST_F(BookTest, Chapter18_AutoStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of non-nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - implicit type conversions
 * - compound and single expressions as initializers
 * - string literals as pointer and array initializers
 * */


// library functions
int strcmp(char *s1, char *s2);

// struct type def
struct s {
    char *one_msg;
    unsigned char two_arr[3];
    struct s *three_self_ptr;
    double four_d;
    double *five_d_ptr;
};

// validation functions defined in library
int vfull(struct s *ptr);
int vpart(struct s *ptr, char *expected_msg);
int vconv(struct s *ptr);
int vtwo(struct s *ptr1, struct s *ptr2);
/* Test initialization of non-nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - implicit type conversions
 * - compound and single expressions as initializers
 * - string literals as pointer and array initializers
 * */



double get_double(void) {
    return 2e12;
}

// case 1: fully initialized struct
int test_full_initialization(void) {
    struct s full = {
        // use string literals to initialize both pointers and arrays
        "I'M A STRUCT!", "SUP",
        &full,          // initialize member with pointer to self
        get_double(),   // initialize member with result of function call
        &(full.four_d)  // initialize member with pointer to other member in
                        // self
    };

    return vfull(&full);
}

// case 2: partially initialized struct
int test_partial_initialization(void) {
    static char *msg = "ANOTHER STRING LITERAL";
    static struct s bk;  // zeroed static stands in for calloc(1, sizeof(struct s))
    struct s partial = {
        msg,         // initialize member from variable
        {'a', 'b'},  // partially initialize array
        &bk          // initialize ptr with zeroed static backing storage
        // don't initialize last element
    };

    return vpart(&partial, msg);
}

// case 3: implicit type conversions for struct members
int test_implicit_type_conversions(void) {
    static int i = 3000;
    static char mbuf[5];  // non-null static buffer stands in for malloc(5)
    static double zd;     // zeroed static stands in for calloc(1, sizeof(double))

    struct s converted = {
        mbuf,                   // non-null char *
        {i / 2, i / 3, i * 4},  // truncate ints to chars: 220, 232, and 224
        0l,                     // convert null pointer constant to null pointer
        i - 1,                  // convert int to double
        &zd                     // zeroed double *
    };

    return vconv(&converted);
}

// case 4: initialize with single expression instead of compound initiailizer
int test_single_exp_initializer(void) {
    double d = 123.4;
    struct s s1 = {"YET ANOTHER STRING", "XY", &s1, 150.0, &d};
    struct s s2 = s1;

    return vtwo(&s1, &s2);
}

int main(void) {
    if (!test_full_initialization()) {
        return 1;
    }

    if (!test_partial_initialization()) {
        return 2;
    }

    if (!test_implicit_type_conversions()) {
        return 3;
    }

    if (!test_single_exp_initializer()) {
        return 4;
    }

    return 0;  // success
}
/* Test initialization of non-nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - implicit type conversions
 * - compound and single expressions as initializers
 * - string literals as pointer and array initializers
 * */


int vfull(struct s *ptr) {
    if (strcmp(ptr->one_msg, "I'M A STRUCT!") || ptr->two_arr[0] != 'S' ||
        ptr->two_arr[1] != 'U' || ptr->two_arr[2] != 'P' ||
        ptr->three_self_ptr != ptr || ptr->four_d != 2e12 ||
        *ptr->five_d_ptr != 2e12) {
        return 0;
    }

    return 1;  // success
}

int vpart(struct s *ptr, char *expected_msg) {
    if (ptr->one_msg != expected_msg || ptr->two_arr[0] != 'a' ||
        ptr->two_arr[1] != 'b') {
        return 0;
    }

    // validate ptr->three_self_ptr by making sure one element in it is 0
    if (ptr->three_self_ptr->one_msg) {
        return 0;
    }

    // validate elements that weren't explicitly initialized are 0
    if (ptr->two_arr[2] || ptr->four_d || ptr->five_d_ptr) {
        return 0;
    }

    return 1;  // success
}
int vconv(struct s *ptr) {
    if (!ptr->one_msg ||  // just validate that this pointer isn't null
        ptr->two_arr[0] != 220 || ptr->two_arr[1] != 232 ||
        ptr->two_arr[2] != 224 || ptr->three_self_ptr ||
        ptr->four_d != 2999.0 || *ptr->five_d_ptr != 0.0) {
        return 0;
    }

    return 1;  // success
}

int vtwo(struct s *ptr1, struct s *ptr2) {
    // validate elements of ptr2
    if (strcmp(ptr2->one_msg, "YET ANOTHER STRING") ||
        ptr2->one_msg != ptr1->one_msg ||  // both one_msg members point to same
                                           // string literal
        // contents of two_arr copied from s1 to s2
        ptr2->two_arr[0] != 'X' || ptr2->two_arr[1] != 'Y' ||
        ptr2->three_self_ptr !=
            ptr1 ||  // ptr2->three_self_ptr is ptr1, not to itself
        ptr2->four_d != 150.0 ||
        *ptr1->five_d_ptr != 123.4) {
        return 0;
    }

    // ptr1->two_arr and ptr2->two_arr are distinct arrays with different
    // addresses, even though contents are the same
    if (ptr1->two_arr == ptr2->two_arr) {
        return 0;
    }
    return 1;  // success
}
)PROG"));
}

// Residual blocker (not libc; strcmp available, validation is by-pointer):
// nested-struct initialization with mixed long/double/unsigned-char-array
// members validates wrong (struct-init representation codegen), returns 1.
TEST_F(BookTest, Chapter18_NestedAutoStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - using mix of compound and single initializers to initialize nested structs
 * - arrays of structs, structs containing arrays
 * */


int strcmp(char *s1, char *s2);

// struct type defs
struct pair {
    int a;
    int b;
};

struct inner {
    int one_i;
    unsigned char two_arr[3];
    unsigned three_u;
};

struct outer {
    long one_l;
    struct inner two_struct;
    char *three_msg;
    double four_d;
    struct pair five_pair;
};

// validation functions defined in library
int check_full(struct outer *ptr);
int check_partial(struct outer *ptr);
int check_mixed(struct outer *ptr);
int check_array(struct outer *struct_array);
/* Test initialization of nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - using mix of compound and single initializers to initialize nested structs
 * - arrays of structs, structs containing arrays
 * */



// case 1: fully initialized struct (include some implicit conversions while
// we're at it)
int test_full_initialization(void) {
    struct outer full = {-200,
                         {-171l, {-56, -54, -53}, 40.5},
                         "Important message!",
                         -22,
                         {1, 2}};

    return check_full(&full);
}

// case 2: partially initialized struct
int test_partial_initialization(void) {
    struct outer partial = {1000,
                            {
                                1,
                                // leave two_arr and three_u ininitialized
                            },
                            "Partial"};  // leave four_d uninitialized

    return check_partial(&partial);
}

// case 3: initialize a nested struct with a single expression of struct type
// rather than a compound initializer
int test_mixed_initialization(void) {
    struct inner inner1 = {10};
    struct inner inner2 = {20, {21}, 22u};
    static int flag = 0;

    struct outer mixed = {
        200,
        flag ? inner1 : inner2,  // initialize to inner2
        "mixed",
        10.0,
        {99,
         100}  // still use compound init for second nexted struct, five_pair
    };

    return check_mixed(&mixed);
}

// case 4: initialize an array of structures
int test_array_of_structs(void) {
    struct outer s0 = {1, {2, {3, 4, 5}, 6}, "7", 8.0, {9, 10}};

    struct inner in1 = {102, {103, 104, 105}, 106};
    struct pair pair1 = {109, 110};
    struct pair pair2 = {209};

    struct outer s3 = {301};

    struct outer struct_array[4] = {
        // struct_array[0]: initialize whole array element w/ one struct
        s0,
        {101, in1, "107", 108.0, pair1},
        // struct_array[2]: partial initialization; compound initialize for one
        // subelement, single for other
        {201,
         // struct_array[2].two_struct
         {202, {203}},
         "207",
         208.0,
         pair2},
        // struct_array[3]: initialize whole array element from one partially
        // initialized struct
        s3};

    return check_array(struct_array);
}

int main(void) {
    if (!test_full_initialization()) {
        return 1;
    }

    if (!test_partial_initialization()) {
        return 2;
    }

    if (!test_mixed_initialization()) {
        return 3;
    }

    if (!test_array_of_structs()) {
        return 4;
    }

    return 0;  // success
}
/* Test initialization of nested structs with automatic storage duration,
 * including:
 * - partial initialization
 * - using mix of compound and single initializers to initialize nested structs
 * - arrays of structs, structs containing arrays
 * */


int check_full(struct outer *ptr) {
    if (ptr->one_l != -200l || ptr->two_struct.one_i != -171 ||
        ptr->two_struct.two_arr[0] != 200 ||
        ptr->two_struct.two_arr[1] != 202 ||
        ptr->two_struct.two_arr[2] != 203 || ptr->two_struct.three_u != 40u ||
        strcmp(ptr->three_msg, "Important message!") || ptr->four_d != -22. ||
        ptr->five_pair.a != 1 || ptr->five_pair.b != 2) {
        return 0;
    }

    return 1;  // success
}

int check_partial(struct outer *ptr) {
    // validate explicitly initialized members
    if (ptr->one_l != 1000 || ptr->two_struct.one_i != 1 ||
        strcmp(ptr->three_msg, "Partial")) {
        return 0;
    }

    // validate that uninitialized members are 0
    if (ptr->two_struct.two_arr[0] || ptr->two_struct.two_arr[1] ||
        ptr->two_struct.two_arr[2] || ptr->two_struct.three_u || ptr->four_d ||
        ptr->five_pair.a || ptr->five_pair.b) {
        return 0;
    }

    return 1;  // success
}

int check_mixed(struct outer *ptr) {
    // validate explicitly initialized elements
    if (ptr->one_l != 200 || ptr->two_struct.one_i != 20 ||
        ptr->two_struct.two_arr[0] != 21 || ptr->two_struct.three_u != 22u ||
        strcmp(ptr->three_msg, "mixed") || ptr->four_d != 10.0 ||
        ptr->five_pair.a != 99 || ptr->five_pair.b != 100) {
        return 0;
    }

    // validate elements that were not explicitly initialized in inner2
    if (ptr->two_struct.two_arr[1] || ptr->two_struct.two_arr[2]) {
        return 0;
    }

    return 1;  // success
}

int check_array(struct outer *struct_array) {
    // validate element 0
    if (struct_array[0].one_l != 1 || struct_array[0].two_struct.one_i != 2 ||
        struct_array[0].two_struct.two_arr[0] != 3 ||
        struct_array[0].two_struct.two_arr[1] != 4 ||
        struct_array[0].two_struct.two_arr[2] != 5 ||
        struct_array[0].two_struct.three_u != 6 ||
        strcmp(struct_array[0].three_msg, "7") ||
        struct_array[0].four_d != 8.0 || struct_array[0].five_pair.a != 9 ||
        struct_array[0].five_pair.b != 10) {
        return 0;
    }

    // validate element 1
    if (struct_array[1].one_l != 101 ||
        struct_array[1].two_struct.one_i != 102 ||
        struct_array[1].two_struct.two_arr[0] != 103 ||
        struct_array[1].two_struct.two_arr[1] != 104 ||
        struct_array[1].two_struct.two_arr[2] != 105 ||
        struct_array[1].two_struct.three_u != 106 ||
        strcmp(struct_array[1].three_msg, "107") ||
        struct_array[1].four_d != 108.0 || struct_array[1].five_pair.a != 109 ||
        struct_array[1].five_pair.b != 110) {
        return 0;
    }

    // validate element 2
    if (struct_array[2].one_l != 201 ||
        struct_array[2].two_struct.one_i != 202 ||
        struct_array[2].two_struct.two_arr[0] != 203 ||
        // remaining elements of two_struct should be 0 since they weren't
        // explicitly initialized
        struct_array[2].two_struct.two_arr[1] ||
        struct_array[2].two_struct.two_arr[2] ||
        struct_array[2].two_struct.three_u ||
        strcmp(struct_array[2].three_msg, "207") ||
        struct_array[2].four_d != 208.0 || struct_array[2].five_pair.a != 209 ||
        // five_pair.b should be 0 since it wasn't explicitly initialized
        struct_array[2].five_pair.b) {
        return 0;
    }

    // validate element 3: one_l is 301, everything else is 0
    if (struct_array[3].one_l != 301 || struct_array[3].two_struct.one_i ||
        struct_array[3].two_struct.two_arr[0] ||
        struct_array[3].two_struct.two_arr[1] ||
        struct_array[3].two_struct.two_arr[2] ||
        struct_array[3].two_struct.three_u || struct_array[3].three_msg ||
        struct_array[3].four_d || struct_array[3].five_pair.a ||
        struct_array[3].five_pair.b) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_NestedStaticStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */


// standard library function
int strcmp(char *s1, char *s2);

// structure type defs
struct inner {
    int one_i;
    signed char two_arr[3];
    unsigned three_u;
};

struct outer {
    long one_l;
    struct inner two_struct;
    char *three_msg;
    double four_d;
};

// declarations of global vars (defined in client)
extern struct outer all_zeros;
extern struct outer partial;
extern struct outer full;
extern struct outer converted;
extern struct outer struct_array[3];

// declarations of validation functions (defined in lib)
int test_uninitialized(void);
int test_partially_initialized(void);
int test_fully_intialized(void);
int test_implicit_conversions(void);
int test_array_of_structs(void);
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */




// structs defined here
// validation functions defined in library

// case 1: struct with no explicit initializer should be all zeros
struct outer all_zeros;

// case 2: partially initialized struct
struct outer partial = {
    100l,
    {10, {10}},  // leave arr[1], arr[2], and y uninitialized
    "Hello!"};   // leave d uninitialized

struct outer full = {
    1000000000000l,
    {1000, "OK",
     4292870144u},  // can initialized signed char array w/ static string
    "Another message",
    2e12};

struct outer converted = {
    10.5,  // 10l
    {
        2147483650u,  // 2147483650
        {
            15.6,             // 15
            17592186044419l,  // 3
            2147483777u       // -127
        },
        1152921506754330624ul  // 2147483648u
    },
    0ul,         // null pointer
    4292870144ul  // 4292870144.0
};

struct outer struct_array[3] = {{1, {2, "ab", 3}, 0, 5},
                                {6, {7, "cd", 8}, "Message", 9}};

int main(void) {
    if (!test_uninitialized()) {
        return 1;
    }

    if (!test_partially_initialized()) {
        return 2;
    }

    if (!test_fully_intialized()) {
        return 3;
    }

    if (!test_implicit_conversions()) {
        return 4;
    }

    if (!test_array_of_structs()) {
        return 5;
    }

    return 0;  // success
}
/* Test initialization of nested static structs, including:
 * - partial initialization
 * - arrays of structs, structs containing arrays
 * - implicit conversion of scalar elements, array decay of string literals
 */


// structs defined in client but visible here
// validation functions defined here

// case 1: struct with no explicit initializer should be all zeros
// struct outer all_zeros;
int test_uninitialized(void) {
    // validate elements in struct outer
    if (all_zeros.one_l || all_zeros.three_msg || all_zeros.four_d) {
        return 0;
    }

    // validate elements in struct inner
    if (all_zeros.two_struct.one_i || all_zeros.two_struct.two_arr[0] ||
        all_zeros.two_struct.two_arr[1] || all_zeros.two_struct.two_arr[2] ||
        all_zeros.two_struct.three_u) {
        return 0;
    }

    return 1;  // success
}

// case 2: partially initialized struct
/*
    struct outer partial = {
        100l,
        {10, {10}},  // leave arr[1], arr[2], and y uninitialized
        "Hello!"};   // leave d uninitialized
*/
int test_partially_initialized(void) {
    // validate elements in struct outer
    if (partial.one_l != 100l || strcmp(partial.three_msg, "Hello!")) {
        return 0;
    }

    if (partial.four_d) {  // this wasn't explicitly initialized, should be 0
        return 0;
    }

    // validate elements in struct inner
    if (partial.two_struct.one_i != 10 || partial.two_struct.two_arr[0] != 10) {
        return 0;
    }

    if (partial.two_struct.two_arr[1] || partial.two_struct.two_arr[2] ||
        partial.two_struct
            .three_u) {  // not explicitly initialized, should be 0
        return 0;
    }

    return 1;  // success
}

// case 3: fully initialized struct
/*
    struct outer full = {
        1000000000000l,
        {1000, "OK",
        4292870144u},  // can initialized signed char array w/ static string
        "Another message",
        2e12};
*/
int test_fully_intialized(void) {
    // validate elements in struct outer
    if (full.one_l != 1000000000000l ||
        strcmp(full.three_msg, "Another message") || full.four_d != 2e12) {
        return 0;
    }

    // validate elemetns in string inner
    if (full.two_struct.one_i != 1000 || full.two_struct.two_arr[0] != 'O' ||
        full.two_struct.two_arr[1] != 'K' || full.two_struct.two_arr[2] != 0 ||
        full.two_struct.three_u != 4292870144u) {
        return 0;
    }

    return 1;  // success
}

// case 4: implicit conversion of scalar elements
/*
    struct outer converted = {
        10.5,  // 10l
        {
            2147483650u,  // 2147483650
            {
                15.6,             // 15
                17592186044419l,  // 3
                2147483777u       // -127
            },
            1152921506754330624ul  // 2147483648u
        },
        0ul,         // null pointer
        4292870144ul  // 4292870144.0
    };
*/
int test_implicit_conversions(void) {
    // validate elements in struct outer
    if (converted.one_l != 10l || converted.three_msg != 0 ||
        converted.four_d != 4292870144.0) {
        return 0;
    }

    // validate elements in struct inner
    if (converted.two_struct.one_i != 2147483650 ||
        converted.two_struct.two_arr[0] != 15 ||
        converted.two_struct.two_arr[1] != 3 ||
        converted.two_struct.two_arr[2] != -127 ||
        converted.two_struct.three_u != 2147483648u) {
        return 0;
    }

    return 1;  // success
}

// case 5: array of structures
/*
    struct outer struct_array[3] = {{1, {2, "ab", 3}, 0, 5},
                                        {6, {7, "cd", 8}, "Message", 9}};
*/
int test_array_of_structs(void) {
    // leave last element uninitialized

    // validate outer members of array element 0
    if (struct_array[0].one_l != 1 || struct_array[0].three_msg != 0 ||
        struct_array[0].four_d != 5) {
        return 0;
    }

    // validate nested members of array element 0
    if (struct_array[0].two_struct.one_i != 2 ||
        strcmp((char *)struct_array[0].two_struct.two_arr, "ab") ||
        struct_array[0].two_struct.three_u != 3) {
        return 0;
    }

    // validate outer members of array element 1
    if (struct_array[1].one_l != 6 ||
        strcmp((char *)struct_array[1].three_msg, "Message") ||
        struct_array[1].four_d != 9) {
        return 0;
    }

    // validate nested members of array element 1
    if (struct_array[1].two_struct.one_i != 7 ||
        strcmp((char *)struct_array[1].two_struct.two_arr, "cd") ||
        struct_array[1].two_struct.three_u != 8) {
        return 0;
    }

    // validate array element 2 - should be all 0s
    if (struct_array[2].one_l || struct_array[2].three_msg ||
        struct_array[2].four_d) {
        return 0;
    }

    // validate nested members of array element 2
    if (struct_array[2].two_struct.one_i ||
        struct_array[2].two_struct.two_arr[0] ||
        struct_array[2].two_struct.two_arr[1] ||
        struct_array[2].two_struct.two_arr[2] ||
        struct_array[2].two_struct.three_u) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_StaticStructInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */

int strcmp(char *s1, char *s2);

struct s {
    double one_d;
    char *two_msg;
    unsigned char three_arr[3];
    int four_i;
};

// static structures defined in client
extern struct s uninitialized;
extern struct s partial;
extern struct s partial_with_array;
extern struct s converted;

// validation functions defined in library

// case 1: struct with no explicit initializer should be all zeros
int test_uninitialized(void);

// case 2: partially initialized struct
int test_partially_initialized(void);

// case 3: partially initialized array w/in struct
int test_partial_inner_init(void);

// case 4: implicit conversion of scalar elements
int test_implicit_conversion(void);
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */



// case 1: struct with no explicit initializer should be all zeros
struct s uninitialized;

// case 2: partially initialized struct
struct s partial = {1.0, "Hello"};

// case 3: partially initialized array w/in struct
struct s partial_with_array = {3.0, "!", {1}, 2};

// case 4: implicit conversion of scalar elements
struct s converted = {
    1099511627775l,  // 1099511627775.0
    0l,              // null ptr
    "ABC",           // {'A', 'B', 'C'}
    17179869189l     // 17179869189
};

int main(void) {
    if (!test_uninitialized()) {
        return 1;
    }

    if (!test_partially_initialized()) {
        return 2;
    }

    if (!test_partial_inner_init()) {
        return 3;
    }

    if (!test_implicit_conversion()) {
        return 4;
    }

    return 0;  // success
}
/* Test initialization of non-nested static structs, including:
 * - partial initialization
 * - implicit conversion of scalar elements
 * - array decay of string literals
 */


// structs defined in client but visible here
// validation functions defined here

// case 1: struct with no explicit initializer should be all zeros
// struct s uninitialized;
int test_uninitialized(void) {
    // make sure all elements are zero
    if (uninitialized.one_d || uninitialized.two_msg ||
        uninitialized.three_arr[0] || uninitialized.three_arr[1] ||
        uninitialized.three_arr[2] || uninitialized.four_i) {
        return 0;
    }
    return 1;  // success
}

// case 2: partially initialized struct
// struct s partial = {1.0, "Hello"};
int test_partially_initialized(void) {
    // validate first two elements
    if (partial.one_d != 1.0 || strcmp(partial.two_msg, "Hello")) {
        return 0;
    }

    // validate that remaining elements are zero
    if (partial.three_arr[0] || partial.three_arr[1] || partial.three_arr[2] ||
        partial.four_i) {
        return 0;
    }

    return 1;  // success
}

// case 3: partially initialized array w/in struct
// struct s partial with_array = {3.0, "!", {1}, 2};
int test_partial_inner_init(void) {
    // validate explicitly initialzed elements
    if (partial_with_array.one_d != 3.0 ||
        strcmp(partial_with_array.two_msg, "!") ||
        partial_with_array.three_arr[0] != 1 ||
        partial_with_array.four_i != 2) {
        return 0;
    }

    // validate that last two elements of arr are 0
    if (partial_with_array.three_arr[1] || partial_with_array.three_arr[2]) {
        return 0;
    }

    return 1;  // success
}

// case 4: implicit conversion of scalar elements
/*
    struct s converted = {
        1099511627775l,  // 1099511627775.0
        0l,              // null ptr
        "ABC",           // {'A', 'B', 'C'}
        17179869189l     // 17179869189
    };
*/
int test_implicit_conversion(void) {
    // validate elements
    if (converted.one_d != 1099511627775.0 || converted.two_msg ||
        converted.three_arr[0] != 'A' || converted.three_arr[1] != 'B' ||
        converted.three_arr[2] != 'C' || converted.four_i != 17179869189) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

// BESM-6: static struct instead of malloc; strcmp/puts strings uppercased for
// KOI-7; puts is kept (prints each struct's message) so expected includes them.
// FIXME: this test currently loops forever at run time — disabled until fixed.
TEST_F(BookTest, Chapter18_OpaqueStruct)
{
    EXPECT_EQ("NEW STRUCT\nSTATIC STRUCT\nGLOBAL STRUCT\n0\n", CompileAndRunBook(R"PROG(
/* Test working with a structure whose type is completed in the library but not
 * the client; this is a common idiom for hiding a library's implementation
 * details */

struct s;  // declare a type but don't define it

// declare some functions for getting/using pointers to this type
struct s *create_struct(int i, double d, char *s);
void increment_struct(struct s *src_ptr);

int check_struct(struct s *ptr, int expected_i, double expected_d,
                 char *expected_s);
void print_struct_msg(struct s *ptr);
struct s *get_internal_struct(void);

// declare a variable with this type
extern struct s incomplete_var;

int main(void) {
    struct s *new_struct = create_struct(101, 102.0, "NEW STRUCT");

    struct s *internal_struct = get_internal_struct();

    // print initial message from all three structs
    print_struct_msg(new_struct);
    print_struct_msg(internal_struct);
    print_struct_msg(&incomplete_var);

    // modify some structs
    increment_struct(new_struct);
    increment_struct(&incomplete_var);

    // check values
    if (!check_struct(new_struct, 102, 103.0, "NEW STRUCT")) {
        return 1;
    }

    if (!check_struct(&incomplete_var, 4, 5.0, "GLOBAL STRUCT")) {
        return 2;
    }

    return 0;  // success (assuming stdout is also correct)
}
/* Test working with a structure whose type is completed in the library but not
 * the client; this is a common idiom for hiding a library's implementation
 * details */


// library functions
int strcmp(char *s1, char *s2);
int puts(char *s);

struct s {
    int member1;
    double member2;
    char *member3;
};

// make a struct
struct s *create_struct(int i, double d, char *s) {
    static struct s cs_obj;  // static storage replaces malloc
    struct s *ptr = &cs_obj;
    ptr->member1 = i;
    ptr->member2 = d;
    ptr->member3 = s;
    return ptr;
}

// modify a struct
void increment_struct(struct s *ptr) {
    ptr->member1 = ptr->member1 + 1;
    ptr->member2 = ptr->member2 + 1;
    ptr->member3 = ptr->member3;
}

// read struct members
int check_struct(struct s *ptr, int expected_i, double expected_d,
                 char *expected_s) {
    if (ptr->member1 != expected_i) {
        return 0;
    }
    if (ptr->member2 != expected_d) {
        return 0;
    }
    if (strcmp(ptr->member3, expected_s)) {
        return 0;
    }

    return 1;  // success
}

void print_struct_msg(struct s *ptr) {
    puts(ptr->member3);
}

// define a struct s that isn't visible in the other translation unit
static struct s internal = {1, 2.0, "STATIC STRUCT"};

struct s *get_internal_struct(void) {
    return &internal;
}

// define struct that is visible in other translation unit
// (although its members aren't accessible)
struct s incomplete_var = {3, 4.0, "GLOBAL STRUCT"};
)PROG"));
}

// BESM-6: one static object per maker function instead of malloc; each result is
// consumed before the maker is re-called, so single statics never alias.  The
// maker/test helpers are renamed (mk_inner/mk_outer/mk_outmost, t_getptr/t_getmem,
// t_updmem/t_updnst) so they stay distinct within Madlen's 8-char identifier
// limit — make_struct_inner/outer/outermost all truncate to "make_str" and would
// otherwise alias into mutual recursion at runtime.
TEST_F(BookTest, Chapter18_ReturnStructPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test returning struct pointers from functions
 * and using struct pointers returned from functions
 * */

// define some struct types
struct inner {
    double d;
    int i;
};

struct outer {
    char a;
    char b;
    struct inner substruct;
};

struct outermost {
    int i;
    struct outer *nested_ptr;
    struct outer nested_struct;
};

// declare some functions that return pointers to structs
struct inner *mk_inner(int seed);
struct outer *mk_outer(int seed);
struct outermost *mk_outmost(int seed);
/* Test returning struct pointers from functions
 * and using struct pointers returned from functions
 * */


// case 1: use a struct pointer returned from a function call
int t_getptr(void) {
    struct inner *inner_ptr = mk_inner(11);

    if (inner_ptr->d != 11 || inner_ptr->i != 11) {
        return 0;
    }

    // assign struct pointer to member
    struct outermost o = {0, 0, {0, 0, {0, 0}}};
    o.nested_ptr = mk_outer(20);
    if (o.nested_ptr->a != 20 || o.nested_ptr->b != 21 ||
        o.nested_ptr->substruct.d != 22 || o.nested_ptr->substruct.i != 23) {
        return 0;
    }

    return 1;  // success
}

// case 2: apply member access operations to funcall expression
int t_getmem(void) {
    if (mk_inner(2)->d != 2) {
        return 0;
    }

    if (mk_outer(2)->substruct.d != 4) {
        return 0;
    }

    if (mk_outmost(0)->nested_ptr->a != 1) {
        return 0;
    }

    return 1;  // success
}

// case 3: update static structure member through pointer returned by funcall
// f()->member = val
struct outer *get_static_struct_ptr(void) {
    static struct outer s;
    return &s;
}

int t_updmem(void) {
    get_static_struct_ptr()->a = 10;
    get_static_struct_ptr()->substruct.d = 20.0;

    struct outer *ptr = get_static_struct_ptr();
    if (ptr->a != 10 || ptr->substruct.d != 20.0) {
        return 0;
    }

    return 1;  // success
}

// case 4: update whole structure member through pointer returned by funcall
int t_updnst(void) {
    struct inner small = {12.0, 13};
    get_static_struct_ptr()->substruct = small;
    if (get_static_struct_ptr()->substruct.d != 12.0) {
        return 0;
    }

    if (get_static_struct_ptr()->substruct.i != 13) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    if (!t_getptr()) {
        return 1;
    }

    if (!t_getmem()) {
        return 2;
    }

    if (!t_updmem()) {
        return 3;
    }

    if (!t_updnst()) {
        return 4;
    }

    return 0;
}
/* Test returning struct pointers from functions
 * and using struct pointers returned from functions
 * */


// define some functions that return pointers to structs
struct inner *mk_inner(int seed) {
    static struct inner msi;  // static storage replaces malloc
    struct inner *ptr = &msi;
    ptr->d = seed;
    ptr->i = seed;
    return ptr;
}

struct outer *mk_outer(int seed) {
    static struct outer mso;  // static storage replaces malloc
    struct outer *ptr = &mso;
    ptr->a = seed;
    ptr->b = seed + 1;
    ptr->substruct.d = seed + 2;
    ptr->substruct.i = seed + 3;
    return ptr;
}

struct outermost *mk_outmost(int seed) {
    static struct outermost msm;  // static storage replaces malloc
    struct outermost *ptr = &msm;
    ptr->i = seed;
    ptr->nested_ptr = mk_outer(seed + 1);
    ptr->nested_struct.a = seed + 5;
    ptr->nested_struct.b = seed + 6;
    ptr->nested_struct.substruct.d = seed + 7;
    ptr->nested_struct.substruct.i = seed + 8;
    return ptr;
}
)PROG"));
}
