//
// The MMIX versions of the "Writing a C Compiler" programs that read the bytes of an
// int, a double or a union in little-endian order: MMIX is big-endian.  The shared
// suite (backend/common/test/book/) holds their generic versions, which MMIX skips.
// Each is compared with GCC's build, as the suite is.
//
#include "book_test.h"

TEST_F(BookTest, Chapter16_AccessThroughCharPointerBigEndian)
{
    EXPECT_EQ("0\n",
              CompileAndRunBook(
                  R"(/* Test that we can read an object through a pointer to a character type */

int main(void) {

    // inspect the four bytes of an int, the lowest last
    int x = 100;
    signed char *byte_ptr = (signed char *) &x;

    if (byte_ptr[3] != 100) {
        return 1;
    }

    if (byte_ptr[0] || byte_ptr[1] || byte_ptr[2]) {
        return 2;
    }

    // now inspect a double -- only upper bit should be set, in the first byte
    double d = -0.0; // 0x8000_0000_0000_0000
    byte_ptr = (signed char *) &d;
    if (byte_ptr[0] != -128) {
        return 3;
    }

    for (int i = 1; i < 8; i = i + 1) {
        if (byte_ptr[i]) {
            return 4;
        }
    }

    // finally, let's look at an array
    unsigned int array[3][2][1] = {
        {{-1}, {-1}},
        {{-1}, {-1}},
        {{4294901760u}} // 0xffff_0000
    };
    byte_ptr = (signed char *) array;
    byte_ptr = byte_ptr + 16; // each row is 8 bytes since it has 2 ints
    if (byte_ptr[2] || byte_ptr[3]) {
        return 5;
    }

    if (byte_ptr[0] != -1) {
        return 6;
    }

    if (byte_ptr[1] != -1) {
        return 7;
    }

    return 0;
})"));
}

TEST_F(BookTest, Chapter18_CopyThruPointerBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test copying whole structs/unions through pointers (incl. to/from array members)

void *calloc(unsigned long nmemb, unsigned long size);
void *malloc(unsigned long size);

union simple {
    int i;
    long l;
    char c;
    unsigned char uc_arr[3];
};

union has_union {
    double d;
    union simple u;
    union simple *u_ptr;
};

struct simple_struct {
    long l;
    double d;
    unsigned int u;
};

union has_struct {
    long l;
    struct simple_struct s;
};

struct struct_with_union {
    union simple u;
    unsigned long ul;
};

union complex_union {
    double d_arr[2];
    struct struct_with_union s;
    union has_union *u_ptr;
};

int strcmp(char* s1, char* s2);

// case 1: *x = y
int test_copy_to_pointer(void) {
    union simple y;
    y.l = -20;
    union simple* x = malloc(sizeof(union simple));
    *x = y;

    // validate: i and uc_arr overlay the high-order bytes of l
    if (x->l != -20 || x->i != -1 || x->uc_arr[0] != 255 || x->uc_arr[1] != 255 || x->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1;  // success
}

// case 2: x = *y
int test_copy_from_pointer(void) {
    // define/initialize a union object containing a struct
    struct simple_struct my_struct = { 8223372036854775807l, 20e3, 2147483650u };
    static union has_struct my_union;
    my_union.s = my_struct;

    // get a pointer to that union
    union has_struct* union_ptr;
    union_ptr = &my_union;

    // copy from pointer to another union
    union has_struct another_union = *union_ptr;

    // validate
    if (another_union.s.l != 8223372036854775807l || another_union.s.d != 20e3 || another_union.s.u != 2147483650u) {
        return 0; // fail
    }

    return 1;
}

// case 3: copies to and from array members (using a union w/ trailing padding)

// size is 12 bytes; take largest member (10 bytes)
// and pad to 4-byte alignment (b/c ui is 4-byte aligned)
union with_padding {
    char arr[10];
    unsigned int ui;
};

int test_copy_array_members(void) {

    // define/initialize an array of unions
    union with_padding union_array[3] = { {"foobar"}, {"hello"}, {"itsaunion"} };

    // copy element out of array
    union with_padding another_union = union_array[0];
    union with_padding yet_another_union = { "blahblah" };

    // copy an element into the array
    union_array[2] = yet_another_union;

    // validate
    if (strcmp(union_array[0].arr, "foobar") || strcmp(union_array[1].arr, "hello") || strcmp(union_array[2].arr, "blahblah")) {
        return 0; // fail
    }

    if (strcmp(another_union.arr, "foobar")) {
        return 0; // fail
    }

    // check yet_another_union too, even though we didn't update it
    if (strcmp(yet_another_union.arr, "blahblah")) {
        return 0; // fail
    }

    return 1; // success

}

int main(void) {
    if (!test_copy_to_pointer()){
        return 1;
    }

    if (!test_copy_from_pointer()) {
        return 2;
    }

    if (!test_copy_array_members()) {
        return 3;
    }

    return 0; // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_NestedUnionAccessBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test access to nested union members through dot, arrow, and subscript operators */

void *calloc(unsigned long nmemb, unsigned long size);
void *malloc(unsigned long size);

union simple {
    int i;
    long l;
    signed char c;
    unsigned char uc_arr[3];
};

union has_union {
    double d;
    union simple u;
    union simple *u_ptr;
};

struct simple_struct {
    long l;
    double d;
    unsigned int u;
};

union has_struct {
    long l;
    struct simple_struct s;
};

struct struct_with_union {
    union simple u;
    unsigned long ul;
};

union complex_union {
    double d_arr[2];
    struct struct_with_union s;
    union has_union *u_ptr;
};

int test_auto_dot(void) {
    // Test nested access with . in unions/structs containing unions
    // with automatic storage duration

    // access union in union
    union has_union x;
    x.u.l = 200000u;
    if (x.u.i != 0) { // the high-order half of l
        return 0; // fail
    }

    // access struct in union
    union has_struct y;
    y.s.l = -5555l;
    y.s.d = 10.0;
    y.s.u = 100;

    if (y.l != -5555l) {
        return 0; // fail
    }

    // access union in struct in union
    union complex_union z;
    z.s.u.i = 12345;
    z.s.ul = 0;

    if (z.s.u.c != 0) { // highest byte of 12345
        return 0; // fail
    }

    if (z.d_arr[1]) { // bytes 8-15 of  union; same spot as z.s.ul
        return 0; // fail
    }

    // get/derefrence address of various members
    unsigned int *some_int_ptr = &y.s.u;
    union simple *some_union_ptr = &z.s.u;

    if (*some_int_ptr != 100 || (*some_union_ptr).i != 12345) {
        return 0; // fail
    }

    return 1; // success
}

int test_static_dot(void) {
    // identical to test_auto_dot but using objects
    // with static storage duration

    // access union in union
    static union has_union x;
    x.u.l = 200000u;
    if (x.u.i != 0) { // the high-order half of l
        return 0; // fail
    }

    // access struct in union
    static union has_struct y;
    y.s.l = -5555l;
    y.s.d = 10.0;
    y.s.u = 100;

    if (y.l != -5555l) {
        return 0; // fail
    }

    // access union in struct in union
    static union complex_union z;
    z.s.u.i = 12345;
    z.s.ul = 0;

    if (z.s.u.c != 0) { // highest byte of 12345
        return 0; // fail
    }

    if (z.d_arr[1]) { // bytes 8-15 of  union; same spot as z.s.ul
        return 0; // fail
    }

    return 1; // success
}

int test_auto_arrow(void) {
    // Test nested access in unions w/ automatic storage duration,
    // using only -> operator
    union simple inner = {100};
    union has_union outer;
    union has_union *outer_ptr = &outer;
    outer_ptr->u_ptr = &inner;
    if (outer_ptr->u_ptr->i != 100) {
        return 0; // fail
    }

    // write through nested access
    outer_ptr->u_ptr->l = -10;

    // read through other members, which overlay its high-order bytes
    if (outer_ptr->u_ptr->c != -1 || outer_ptr->u_ptr->i != -1 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr
    if (outer_ptr->u_ptr->uc_arr[0] != 255 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1; // success
}

int test_static_arrow(void) {
    // identical to test_auto_arrow but with objects of static storage duration
    static union simple inner = {100};
    static union has_union outer;
    static union has_union *outer_ptr;
    outer_ptr = &outer;
    outer_ptr->u_ptr = &inner;
    if (outer_ptr->u_ptr->i != 100) {
        return 0; // fail
    }

    // write through nested access
    outer_ptr->u_ptr->l = -10;

    // read through other members, which overlay its high-order bytes
    if (outer_ptr->u_ptr->c != -1 || outer_ptr->u_ptr->i != -1 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr
    if (outer_ptr->u_ptr->uc_arr[0] != 255 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
        return 0; // fail
    }

    return 1; // success
}

int test_array_of_unions(void) {
    // test access to array of unions
    union has_union arr[3];
    arr[0].u.l = -10000;
    arr[1].u.i = 200;
    arr[2].u.c = -120;

    if (arr[0].u.l != -10000 || arr[1].u.c != 0 || arr[2].u.uc_arr[0] != 136) {
        return 0; // fail
    }

    return 1; // success
}

int test_array_of_union_pointers(void) {
    // test access to array of union pointers
    union has_union *ptr_arr[3];
    for (int i = 0; i < 3; i = i + 1) {
        ptr_arr[i] = calloc(1, sizeof(union has_union));
        ptr_arr[i]->u_ptr = calloc(1, sizeof (union simple));
        ptr_arr[i]->u_ptr->l = i;
    }

    if (ptr_arr[0]->u_ptr->l != 0 || ptr_arr[1]->u_ptr->l != 1 || ptr_arr[2]->u_ptr->l != 2) {
        return 0; // fail
    }

    return 1;
}

int main(void) {
    if (!test_auto_dot()) {
        return 1;
    }

    if (!test_static_dot()) {
        return 2;
    }

    if (!test_auto_arrow()) {
        return 3;
    }

    if (!test_static_arrow()) {
        return 4;
    }

    if (!test_array_of_unions()) {
        return 5;
    }

    if (!test_array_of_union_pointers()) {
        return 6;
    }

    return 0;
}
)PROG"));
}

TEST_F(BookTest, Chapter18_StaticUnionAccessBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test access to static union members with . and ->
union u {
    unsigned long l;
    double d;
    signed char arr[8];
};

static union u my_union = { 18446744073709551615UL };
static union u* union_ptr = 0;

int main(void) {
    union_ptr = &my_union;
    if (my_union.l != 18446744073709551615UL) {
        return 1; // fail
    }

    for (int i = 0; i < 8; i = i + 1) {
        if (my_union.arr[i] != -1) {
            return 2; // fail
        }
    }

    union_ptr->d = -1.0;

    if (union_ptr->l != 13830554455654793216ul) {
        return 3; // fail
    }

    for (int i = 2; i < 8; i = i + 1) {
        // lower 6 bytes, the last ones, are 0
        if (my_union.arr[i]) {
            return 4; // fail
        }
    }
    if (union_ptr->arr[1] != -16) {
        return 5; // fail
    }

    if (union_ptr->arr[0] != -65) {
        return 6; // fail
    }

    return 0; // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_StaticUnionInitsBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test initialization of static unions; make sure uninitialized unions are initialized to zero

int strcmp(char* s1, char* s2);

// Test case 1 - simple union w/ scalar elements (and padding)
union simple {
    int i;
    signed char c;
    double d;
};

extern union simple s;
int validate_simple(void);

// Test case 2 - union w/ another union as first element
union has_union {
    union simple u;
    char c;
};

extern union has_union h;
int validate_has_union(void);

// Test case 3 - struct containing partially initialized array of unions
// (make sure we initialize padding to 0 for each of them)
struct has_union_array {
    union has_union union_array[4];
    char c;
    union simple s;
};

extern struct has_union_array my_struct;
int validate_has_union_array(void);

// Test case 4 - an uninitialized static union (make sure we initialize the
// whole thing, including padding, to zeroes)

extern union has_union all_zeros;
int validate_uninitialized(void);

// Test case 5 - an array of unions with trailing padding. Make sure padding
// is included
union with_padding {
    char arr[13];
    long l;
}; // extra 3 bytes of padding to make it 8-byte aligned

extern union with_padding padded_union_array[3];
int validate_padded_union_array(void);

// c overlays the high-order byte of i, which is 0
int validate_simple(void) {
    return (s.c == 0 && s.i == 217);
}

int validate_has_union(void) {
    return (h.u.c == 0 && h.c == 0 && h.u.i == 77);
}

int validate_has_union_array(void) {

    // validate array of unions
    // first validate elements 0-2
    for (int i = 0; i < 3; i = i + 1) {
        int expected = 'a' + i;
        if (my_struct.union_array[i].u.c != 0
            || my_struct.union_array[i].c != 0
            || my_struct.union_array[i].u.i != expected) {
            return 0;
        }
    }

    // last array element should be all 0s (including bytes that
    // aren't part of first member) b/c it's uninitialized
    if (my_struct.union_array[3].u.d != 0.0) {
        return 0;
    }

    // validate other elements of struct
    if (my_struct.c != '#') {
        return 0; // fail
    }

    if (my_struct.s.c != 0 || my_struct.s.i != '!') {
        return 0; // fail
    }

    return 1;
}

int validate_uninitialized(void) {
    if (all_zeros.u.d != 0.0) {
        return 0; // fail
    }
    return 1;
}

int validate_padded_union_array(void) {
    if (strcmp(padded_union_array[0].arr, "first string") != 0) {
        return 0; // fail
    }

    if (strcmp(padded_union_array[1].arr, "string #2") != 0) {
        return 0; // fail
    }

    if (strcmp(padded_union_array[2].arr, "string #3") != 0) {
        return 0; // fail
    }

    return 1;
}

// Test initialization of static unions; make sure uninitialized
// unions/sub-objects are initialized to zero

// Test case 1 - simple union w/ scalar elements

union simple s = {217};

// Test case 2 - union w/ another union as first element

union has_union h = {{77}};

// Test case 3 - struct containing partially initialized array of unions
// (make sure we initialize uninitialized values to zero)

struct has_union_array my_struct = {
    {{{'a'}}, {{'b'}}, {{'c'}}}, '#', {'!'}
};

// Test case 4 - uninitialized union (make sure whole thing is initialized to
// 0, not just first element)

union has_union all_zeros;

// Test case 5 - an array of unions with trailing padding. Make sure padding
// is included
union with_padding padded_union_array[3] = {
    {"first string"}, {"string #2"}, {
        "string #3"
    }
};

int main(void) {
    if (!validate_simple()) {
        return 1;
    }

    if (!validate_has_union()){
        return 2;
    }

    if (!validate_has_union_array()) {
        return 3;
    }

    if (!validate_uninitialized()) {
        return 4;
    }

    if (!validate_padded_union_array()) {
        return 5;
    }

    return 0;
}
)PROG"));
}

TEST_F(BookTest, Chapter18_UnionTempLifetimeBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// We can implicitly get the address of a union with temporary lifetime
// (and subscript it)

struct has_char_array {
    signed char arr[8];
};

union has_array {
    long l;
    struct has_char_array s;
};

int get_flag(void) {
    static int flag = 0;
    flag = !flag;
    return flag;
}

int main(void) {
    union has_array union1 = {9876543210l};
    union has_array union2 = {1234567890l};

    // first access member in union1: the lowest byte, the last
    if ((get_flag() ? union1 : union2).s.arr[7] != -22) {
        return 1; // fail
    }

    // then access member in union2
    if ((get_flag() ? union1 : union2).s.arr[7] != -46) {
        return 2; // fail
    }

    return 0; // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_UnionsInConditionalsBigEndian)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Like structures, unions can appear in conditional expression

union u {
    long l;
    int i;
    signed char c;
};
int choose_union(int flag) {
    union u one;
    union u two;
    one.l = -1;
    two.i = 100 << 24; // c overlays its high-order byte

    return (flag ? one : two).c;
}

int main(void) {
    if (choose_union(1) != -1) {
        return 1; // fail
    }

    if (choose_union(0) != 100) {
        return 2; // fail
    }

    return 0; // success
}
)PROG"));
}
