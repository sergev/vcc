//
// Programs whose results depend on integer widths or sizes are in their generic
// LP64 form; BESM-6 runs its own versions of them (see README.md).
//
#include "book_test.h"

// BESM-6: the arrow case uses static objects instead of calloc (no heap dependency); all
// member reads here are same-type, so no punning-value changes are needed.
TEST_F(BookTest, Chapter18_CopyNonScalarMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
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
// Read and assign to non-scalar union members

int test_dot(void) {
    // Test reading/writing whole nested unions/structs w/ . operator
    // first, assign a union to a struct member
    struct struct_with_union my_struct = { {0}, 100000l };
    union simple my_simple_union;
    my_simple_union.l = -1;
    my_struct.u = my_simple_union;

    // now assign to a union mebmer of struct type
    static union complex_union my_union;
    my_union.s = my_struct;

    // validate what we have so far
    if (my_struct.ul != 100000l || my_struct.u.l != -1) {
        return 0; // fail
    }

    if (my_union.s.ul != 100000l) {
        return 0; // fail
    }

    // now copy whole structs/unions from members
    my_union.s.u.i = 45;
    // copy simple_union sub-object from my_union into my_simple_union
    my_simple_union = my_union.s.u;
    if (my_simple_union.i != 45) {
        return 0; // fail
    }

    // copy struct sub-object from my_union into another (static) variable
    static struct struct_with_union another_struct;
    another_struct = my_union.s;
    if (another_struct.ul != 100000l || another_struct.u.i != 45) {
        return 0; // fail
    }

    return 1; // success
}

int test_arrow(void) {
    // static objects in place of heap allocations
    static union complex_union cu_store;
    static union has_union hu_store;
    static union simple su_store;
    union complex_union* my_union_ptr = &cu_store;
    my_union_ptr->u_ptr = &hu_store;
    my_union_ptr->u_ptr->u_ptr = &su_store;
    my_union_ptr->u_ptr->u_ptr->i = 987654321;

    // read thru arrow to assign
    union has_union another_union = *my_union_ptr->u_ptr;

    // compare pointers & pointers' dereferenced values
    if (another_union.u_ptr != my_union_ptr->u_ptr->u_ptr || another_union.u_ptr->c != my_union_ptr->u_ptr->u_ptr->c) {
        return 0; // fail
    }

    // define another object to assign through arrow
    union simple small_union = { -9999 };
    my_union_ptr->u_ptr->u = small_union;
    if (my_union_ptr->u_ptr->u.i != -9999) {
        return 0; // fail
    }

    return 1; // success
}

int main(void) {
    if (!test_dot()) {
        return 1;
    }

    if (!test_arrow()) {
        return 2;
    }

    return 0; // success
}
)PROG"));
}

// extra_credit/union_copy/copy_thru_pointer, from the book.
TEST_F(BookTest, Chapter18_CopyThruPointer)
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

    // validate
    if (x->l != -20 || x->i != -20 || x->uc_arr[0] != 236 || x->uc_arr[1] != 255 || x->uc_arr[2] != 255) {
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

// BESM-6: the x86 SysV register-classification distinction (XMM vs general-purpose vs memory)
// has no analogue here — the backend marshals every aggregate the same multi-word by-value
// way — so this keeps a representative subset of the original union/struct shapes and turns
// each into a same-member round-trip through a by-value call. All cross-member IEEE type-puns
// are dropped, the out-of-range FP literals and the negative unsigned char were brought into
// range, helper names are shortened to stay distinct within Madlen's 8-char limit, and the
// strcmp strings are UPPERCASE so the automatic (ASCII) char data matches the KOI-7-repacked
// constants. No heap: every value is an initializer or stack object.
TEST_F(BookTest, Chapter18_ClassifyUnions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
int strcmp(char* s1, char* s2);

// union of doubles: an array member overlays a scalar member
union two_doubles {
    double arr[2];
    double single;
};

// union whose first member is an integer, second a char
union int_char {
    int i;
    char c;
};

// nested unions
union one_dbl {
    double d1;
    double d2;
};
union nest {
    union one_dbl od;
    double x;
};

// union containing a struct (which itself contains a union)
struct has_u {
    unsigned int i;
    union int_char u;
};
union us {
    double d;
    struct has_u s;
    unsigned long ul;
};

// struct containing a union
struct dbl_struct {
    union one_dbl m1;
    double m2;
};

// union containing an array of unions
union uarr {
    union int_char a[2];
};

// union containing an array of structs
struct small {
    char arr[3];
    signed char sc;
};
union has_sa {
    struct small a[3];
};

// two-word union: an 11-char array overlays an int
union char_arr {
    char arr[11];
    int i;
};

int t_twodbl(union two_doubles u);
int t_intchr(union int_char u);
int t_nest(union nest u);
int t_ustrct(union us u);
int t_dstrct(struct dbl_struct s);
int t_uarr(union uarr u);
int t_sarr(union has_sa u);
int t_carr(union char_arr u);

int main(void) {
    // union of doubles passed by value
    union two_doubles td = { {10.0, 11.0} };
    if (!t_twodbl(td)) {
        return 1;
    }

    // int/char union, written and read through its int member
    union int_char ic = { 1000000 };
    if (!t_intchr(ic)) {
        return 2;
    }

    // nested unions
    union nest nst;
    nst.od.d1 = -2.345e6;
    if (!t_nest(nst)) {
        return 3;
    }

    // union containing a struct
    union us u;
    u.s.i = 4294954951u;
    u.s.u.c = 60;
    if (!t_ustrct(u)) {
        return 4;
    }

    // struct containing a union
    struct dbl_struct ds;
    ds.m1.d1 = -2.345e6;
    ds.m2 = 123.45;
    if (!t_dstrct(ds)) {
        return 5;
    }

    // array of unions
    union uarr ua;
    ua.a[0].i = -20;
    ua.a[1].i = -30;
    if (!t_uarr(ua)) {
        return 6;
    }

    // array of structs
    union has_sa hsa = { {
        {"AS", 10}, {"DF", 11}, {"GH", 12}
    } };
    if (!t_sarr(hsa)) {
        return 7;
    }

    // two-word union
    union char_arr ca = { "CHARS" };
    if (!t_carr(ca)) {
        return 8;
    }

    return 0; // success
}

int t_twodbl(union two_doubles u) {
    return u.arr[0] == 10.0 && u.arr[1] == 11.0 && u.single == 10.0;
}
int t_intchr(union int_char u) {
    return u.i == 1000000;
}
int t_nest(union nest u) {
    return u.od.d1 == -2.345e6 && u.od.d2 == -2.345e6;
}
int t_ustrct(union us u) {
    return u.s.i == 4294954951u && u.s.u.c == 60;
}
int t_dstrct(struct dbl_struct s) {
    return s.m1.d1 == -2.345e6 && s.m1.d2 == -2.345e6 && s.m2 == 123.45;
}
int t_uarr(union uarr u) {
    return u.a[0].i == -20 && u.a[1].i == -30;
}
int t_sarr(union has_sa u) {
    return strcmp(u.a[0].arr, "AS") == 0 && u.a[0].sc == 10
        && strcmp(u.a[1].arr, "DF") == 0 && u.a[1].sc == 11
        && strcmp(u.a[2].arr, "GH") == 0 && u.a[2].sc == 12;
}
int t_carr(union char_arr u) {
    return strcmp(u.arr, "CHARS") == 0;
}
)PROG"));
}

// BESM-6: the x86 SysV register-class argument-passing rules (one/two XMM regs, one/two
// general-purpose regs, a mix, or spilled to memory) have no analogue here — the backend
// marshals every aggregate the same multi-word by-value way, pushing it into the param area
// regardless of how x86 would classify it. This keeps a representative subset of the original
// calls: unions and a struct-with-union interleaved with scalar args, and aggregates placed
// after several scalar args (x86's "passed in memory" case). Cross-member IEEE type-puns are
// dropped (same-member reads only), FP literals are in range, the strcmp string is UPPERCASE
// so the automatic (ASCII) char data matches the KOI-7-repacked constant, validator names are
// shortened to stay distinct within Madlen's 8-char limit, and there is no heap — every value
// is an initializer or stack object.
TEST_F(BookTest, Chapter18_ParamPassing)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
int strcmp(char* s1, char* s2);

// two-word union: an array member overlays a scalar member
union two_doubles {
    double arr[2];
    double single;
};

// one-word union that can hold either a double or a char
union one_int {
    double d;
    char c;
};

// one-word union of int and char
union char_int {
    char c;
    int i;
};

// struct containing a union
struct has_union {
    unsigned int i;
    union char_int u;
};

// two-word union: an 11-char array overlays an int
union char_arr {
    char arr[11];
    int i;
};

int p_mix(int i1, int i2, struct has_union s, double d1, union two_doubles td,
    union one_int oi, int i3, int i4, int i5);
int p_gpmem(union two_doubles td, struct has_union s, int i1, int i2, int i3,
    int i4, int i5, int i6, union one_int oi);
int p_xmmem(double d1, double d2, union two_doubles td, double d3, double d4,
    union two_doubles td2);
int p_strct(int i1, int i2, int i3, int i4, int i5, union char_arr ca);

int main(void) {
    // a mix of unions, a struct-with-union, and scalars, all passed by value
    union two_doubles td = { {10.0, 11.0} };
    union one_int oi = { 13.0 };
    struct has_union s = { 12345u, {0} };
    s.u.i = 123456789;

    if (!p_mix(1, 2, s, 4.0, td, oi, 100, 120, 130)) {
        return 1;
    }

    // aggregates placed after several scalar args (BESM-6 marshals them the same
    // by-value way x86 would spill to memory)
    if (!p_gpmem(td, s, 1, 2, 3, 4, 5, 6, oi)) {
        return 2;
    }

    // two two-word double unions among double scalars
    union two_doubles td2 = { {33e4, 55e6} };
    if (!p_xmmem(1.0, 2.0, td, 3.0, 4.0, td2)) {
        return 3;
    }

    // a two-word char-array union passed after scalar args
    union char_arr ca = { "CHARS" };
    if (!p_strct(1, 2, 3, 4, 5, ca)) {
        return 4;
    }

    return 0; // success
}

int p_mix(int i1, int i2, struct has_union s, double d1, union two_doubles td,
    union one_int oi, int i3, int i4, int i5) {
    if (!(i1 == 1 && i2 == 2 && d1 == 4.0 && i3 == 100 && i4 == 120 && i5 == 130)) {
        return 0; // fail
    }
    if (!(s.i == 12345u && s.u.i == 123456789)) {
        return 0; // fail
    }
    if (!(td.arr[0] == 10.0 && td.arr[1] == 11.0)) {
        return 0; // fail
    }
    if (!(oi.d == 13.0)) {
        return 0; // fail
    }
    return 1; // success
}

int p_gpmem(union two_doubles td, struct has_union s, int i1, int i2, int i3,
    int i4, int i5, int i6, union one_int oi) {
    if (!(i1 == 1 && i2 == 2 && i3 == 3 && i4 == 4 && i5 == 5 && i6 == 6)) {
        return 0; // fail
    }
    if (!(td.arr[0] == 10.0 && td.arr[1] == 11.0)) {
        return 0; // fail
    }
    if (!(s.i == 12345u && s.u.i == 123456789)) {
        return 0; // fail
    }
    if (!(oi.d == 13.0)) {
        return 0; // fail
    }
    return 1; // success
}

int p_xmmem(double d1, double d2, union two_doubles td, double d3, double d4,
    union two_doubles td2) {
    if (!(d1 == 1.0 && d2 == 2.0 && d3 == 3.0 && d4 == 4.0)) {
        return 0; // fail
    }
    if (!(td.arr[0] == 10.0 && td.arr[1] == 11.0)) {
        return 0; // fail
    }
    if (!(td2.arr[0] == 33e4 && td2.arr[1] == 55e6)) {
        return 0; // fail
    }
    return 1; // success
}

int p_strct(int i1, int i2, int i3, int i4, int i5, union char_arr ca) {
    if (!(i1 == 1 && i2 == 2 && i3 == 3 && i4 == 4 && i5 == 5)) {
        return 0; // fail
    }
    if (strcmp(ca.arr, "CHARS") != 0) {
        return 0; // fail
    }
    return 1; // success
}
)PROG"));
}

// extra_credit/libraries/static_union_inits, from the book;
// a negative-valued char member is `signed char`, since plain char may be unsigned.
TEST_F(BookTest, Chapter18_StaticUnionInits)
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

int validate_simple(void) {
    return (s.c == -39 && s.i == 217);
}

int validate_has_union(void) {
    return (h.u.c == 77 && h.c == 77 && h.u.i == 77);
}

int validate_has_union_array(void) {

    // validate array of unions
    // first validate elements 0-2
    for (int i = 0; i < 3; i = i + 1) {
        int expected = 'a' + i;
        if (my_struct.union_array[i].u.c != expected
            || my_struct.union_array[i].c != expected
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

    if (my_struct.s.c != '!' || my_struct.s.i != '!') {
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

// BESM-6: helper names shortened to stay distinct within 8 chars; the converted-init value
// is brought into range, the negative char-array init reads back as unsigned (255-i), the
// 64-bit long is reduced to the 41-bit range, and the strcmp string is UPPERCASE.
TEST_F(BookTest, Chapter18_UnionInits)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// library functions
int strcmp(char *s1, char *s2);

union simple {
    double d;
    char c;
    int *ptr;
};

union inner {
    char arr[9];
};

struct my_struct {
    long l;
    union inner u;
    int i;
};

union nested {
    struct my_struct str;
    union simple s;
    long l;
};

int vsimp(union simple *ptr);
int vsimpcv(union simple *ptr);
int vnest(union nested *ptr);
int vnestp(union nested *ptr);
// Test initialization of unions with automatic storage duration



int tsimp(void) {
    // initialize simple union w/ only scalar members
    union simple x = { 123.45 };
    return vsimp(&x);
}

int tsimpcv(void) {
    // initialize simple union where the unsigned value is implicitly converted to the
    // double member (2^32 is exactly representable in the BESM-6 native FP format)
    union simple x = { 4294967296UL };
    return vsimpcv(&x);
}


int tnest(void) {
    // initalize nested union where first member is a structure
    union nested x = { {4294967395l, {{-1, -2, -3, -4, -5, -6, -7, -8, -9}}} };
    return vnest(&x);
}

int tnestp(void) {
    // initialize union where inner subobject is a partly initialized struct
    union nested x = { {900037203685l, {"STRING"}} };
    return vnestp(&x);
}

int main(void) {
    if (!tsimp()) {
        return 1;
    }

    if (!tsimpcv()) {
        return 2;
    }

    if (!tnest()) {
        return 3;
    }

    if (!tnestp()) {
        return 4;
    }

    return 0;
}
// Test initialization of unions with both automatic and static storage duration


int vsimp(union simple* ptr) {
    return (ptr->d == 123.45);
}

int vsimpcv(union simple* ptr) {
    return (ptr->d == 4294967296.);
}

int vnest(union nested* ptr) {
    if (ptr->str.l != 4294967395l) {
        return 0; // fail
    }

    for (int i = 0; i < 9; i = i + 1) {
        // -1-i stored into an unsigned char reads back as 255-i
        if (ptr->str.u.arr[i] != 255 - i) {
            return 0;  // fail
        }
    }

    return 1; // success
}
int vnestp(union nested* ptr) {
    if (ptr->str.l != 900037203685l) {
        return 0; // fail
    }

    if (strcmp(ptr->str.u.arr, "STRING")) {
        return 0; // fail
    }

    return 1; // success
}
)PROG"));
}

// BESM-6: the x86 SysV ABI eightbyte-classification rules that decide which register(s) a
// union return value lands in (one/two XMM regs, one/two general-purpose regs, a GP+XMM mix,
// or memory) have no analogue here — the backend returns every multi-word aggregate the same
// way via the hidden-pointer (sret) ABI, and a one-word aggregate in the accumulator. This
// keeps a representative subset of the original return shapes: a one-word union, a nested
// one-word union, several two-word unions (including ones that overlay a struct), a four-word
// union returned "in memory", a multi-word aggregate returned while also passing aggregates by
// value, and a struct that contains a union. Each is validated by same-member round-trip reads
// (the cross-member IEEE type-puns are dropped); FP literals are in range, the negative char
// is brought into the unsigned-char range, strcmp strings are UPPERCASE so the automatic
// (ASCII) char data matches the KOI-7-repacked constant, the return-function names are
// shortened to stay distinct within Madlen's 8-char limit, and there is no heap.
TEST_F(BookTest, Chapter18_UnionRetvals)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
int strcmp(char* s1, char* s2);
void exit(int status);

// one-word union with only double members
union one_dbl {
    double d1;
    double d2;
};

// one-word union that can hold either a double or a char
union one_int {
    double d;
    char c;
};

// nested one-word unions
union one_int_nested {
    union one_int oi;
    union one_dbl od;
};

// two-word: struct containing a union, plus a trailing double
struct dbl_struct {
    union one_dbl m1;
    double m2;
};
union has_dbl_struct {
    struct dbl_struct m1;
};

// two-word: two parallel arrays overlaid
union two_arrs {
    double dbl_arr[2];
    long long_arr[2];
};

// two-word: a pointer overlaid with a struct (char + double)
struct char_first {
    char c;
    double d;
};
union scalar_and_struct {
    long *ptr;
    struct char_first cfe;
};

// two-word: a double overlaid with a struct (double + int)
struct int_second {
    double d;
    int i;
};
union xmm_and_gp {
    double d;
    struct int_second ise;
};

// four-word ("in memory"): array of two-word unions
union gp_and_xmm {
    double d_arr[2];
    char c;
};
union contains_union_array {
    union gp_and_xmm arr[2];
};

// multi-word char union, returned in memory
union lotsa_chars {
    char more_chars[18];
    char fewer_chars[5];
};

// struct containing a union, returned by value
union uneven {
    char arr[5];
    unsigned char uc;
};
struct has_uneven_union {
    int i;
    union uneven u;
};

union one_dbl r_onedbl(void);
union one_int_nested r_oin(void);
union has_dbl_struct r_hds(void);
union two_arrs r_arrs(void);
union scalar_and_struct r_scst(void);
union xmm_and_gp r_xgp(void);
union contains_union_array r_cua(void);
union lotsa_chars r_pmem(int i1, union scalar_and_struct int_and_dbl,
    union two_arrs ta, int i2, union contains_union_array big_union,
    union one_int_nested oin);
struct has_uneven_union r_swu(void);

int main(void) {
    // return a one-word union (in the accumulator)
    union one_dbl od = r_onedbl();
    if (!(od.d1 == 245.5 && od.d2 == 245.5)) {
        return 1;
    }

    // return a nested one-word union
    union one_int_nested oin = r_oin();
    if (oin.oi.d != -9876.5) {
        return 2;
    }

    // return a two-word union (sret)
    union has_dbl_struct two_xmm = r_hds();
    if (!(two_xmm.m1.m1.d1 == 1234.5 && two_xmm.m1.m2 == 6789.)) {
        return 3;
    }

    // return a two-word union, write/read distinct array slots
    union two_arrs ta = r_arrs();
    if (ta.dbl_arr[0] != 66.75 || ta.long_arr[1] != -4294967300l) {
        return 4;
    }

    // return a union that overlays a struct (char + double)
    union scalar_and_struct int_and_dbl = r_scst();
    if (int_and_dbl.cfe.c != 115 || int_and_dbl.cfe.d != 222222.25) {
        return 5;
    }

    // return a union that overlays a struct (double + int)
    union xmm_and_gp dbl_and_int = r_xgp();
    if (dbl_and_int.ise.d != -50000.125 || dbl_and_int.ise.i != -3000) {
        return 6;
    }

    // return a four-word union ("in memory")
    union contains_union_array big_union = r_cua();
    if (!(big_union.arr[0].d_arr[0] == -2000e-4 && big_union.arr[0].d_arr[1] == -3000e-4
        && big_union.arr[1].d_arr[0] == 20000e10 && big_union.arr[1].d_arr[1] == 5000e11)) {
        return 7;
    }

    // pass aggregates by value AND return one in memory; make sure returning in
    // memory doesn't disturb param passing
    union lotsa_chars chars_union = r_pmem(1, int_and_dbl, ta, 25, big_union, oin);
    if (strcmp(chars_union.more_chars, "ABCDEFGHIJKLMNOPQ") != 0) {
        return 8;
    }

    // return a struct that contains a union
    struct has_uneven_union s = r_swu();
    if (s.i != -8765 || strcmp(s.u.arr, "DONE") != 0) {
        return 9;
    }

    return 0; // success
}

union one_dbl r_onedbl(void) {
    union one_dbl result = { 245.5 };
    return result;
}

union one_int_nested r_oin(void) {
    union one_int_nested result = { {-9876.5} };
    return result;
}

union has_dbl_struct r_hds(void) {
    union has_dbl_struct result = { { {1234.5}, 6789. } };
    return result;
}

union two_arrs r_arrs(void) {
    union two_arrs result;
    result.dbl_arr[0] = 66.75;
    result.long_arr[1] = -4294967300l;
    return result;
}

union scalar_and_struct r_scst(void) {
    union scalar_and_struct result;
    result.cfe.c = 115;
    result.cfe.d = 222222.25;
    return result;
}

union xmm_and_gp r_xgp(void) {
    union xmm_and_gp result;
    result.ise.d = -50000.125;
    result.ise.i = -3000;
    return result;
}

union contains_union_array r_cua(void) {
    union contains_union_array result = {
        {
            {{-2000e-4, -3000e-4}}, {{20000e10, 5000e11}}
        }
    };
    return result;
}

union lotsa_chars r_pmem(int i1, union scalar_and_struct int_and_dbl,
    union two_arrs ta, int i2, union contains_union_array big_union,
    union one_int_nested oin) {

    // validate scalar params
    if (i1 != 1 || i2 != 25) {
        exit(-1);
    }

    // validate non-scalar params
    if (int_and_dbl.cfe.c != 115 || int_and_dbl.cfe.d != 222222.25) {
        exit(-2);
    }
    if (ta.dbl_arr[0] != 66.75 || ta.long_arr[1] != -4294967300l) {
        exit(-3);
    }
    if (!(big_union.arr[0].d_arr[0] == -2000e-4 && big_union.arr[0].d_arr[1] == -3000e-4
        && big_union.arr[1].d_arr[0] == 20000e10 && big_union.arr[1].d_arr[1] == 5000e11)) {
        exit(-4);
    }
    if (oin.oi.d != -9876.5) {
        exit(-5);
    }

    // construct the result
    union lotsa_chars result = { "ABCDEFGHIJKLMNOPQ" };
    return result;
}

struct has_uneven_union r_swu(void) {
    struct has_uneven_union result = {
        -8765, {"DONE"}
    };
    return result;
}
)PROG"));
}
// BESM-6: the accept_params parameter `l` was renamed to `lval` (no identifier
// shadowing of the file-scope static `l`); block-scope static storage is supported.
TEST_F(BookTest, Chapter18_ScalarMemberAccessDot)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test the . operator.
 * Relatively simple tests without nested accesses or members of aggregate
 * types.
 */

struct four_members {
    double d;
    char c;
    long l;
    char *ptr;
};

// helper functions/variables

// get_double and l are used to initialize members
double get_double(void) {
    return 2e12;
}

static long l = 34359738378l;

// validate members (and values derived from members) that are passed as
// parameters
int accept_params(int d_divided, int c_doubled, double l_cast,
                  int dereferenced_ptr, double d, int c, long lval, char *ptr) {
    if (d != 4e12 || c != 127 || lval != 8589934594l || *ptr != 100 ||
        d_divided != 100.0 || c_doubled != 254 || l_cast != 8589934594.0 ||
        dereferenced_ptr != 100) {
        return 0;
    }

    return 1;  // success
}

int test_auto(void) {
    // test reading, writing, and getting address of members
    // in struct with automatic storage duration
    struct four_members autom;

    // write to all members - assign results of complex expression to members
    autom.d = get_double() * 2.0;  // 4e12
    autom.c = 127;
    autom.l = l / 4;  // 8589934594l

    char chr = 100;
    autom.ptr = &chr;

    // read all members
    if (autom.d != 4e12 || autom.c != 127 || autom.l != 8589934594l ||
        autom.ptr != &chr) {
        return 0;
    }

    // take address of members
    double *d_ptr = &autom.d;
    char *c_ptr = &autom.c;
    if (*d_ptr != 4e12 || *c_ptr != 127) {
        return 0;
    }

    // dereference member
    if (*autom.ptr != 100) {
        return 0;
    }

    // read members and use them in complex expressions (e.g. function calls)
    if (!accept_params(autom.d / 4e10, autom.c * 2, (double)autom.l, *autom.ptr,
                       autom.d, autom.c, autom.l, autom.ptr)) {
        return 0;
    }

    return 1;
}

int test_static(void) {
    // test reading, writing, and getting address of members
    // in struct with static storage duration
    static struct four_members stat;
    static char chr = 100;

    // same test as test_auto above

    // write to all members - assign results of complex expression to members
    stat.d = get_double() * 2.0;  // 4e12
    stat.c = 127;
    stat.l = l / 4;  // 8589934594l

    stat.ptr = &chr;

    // read all members
    if (stat.d != 4e12 || stat.c != 127 || stat.l != 8589934594l ||
        stat.ptr != &chr) {
        return 0;
    }

    // take address of members
    double *d_ptr = &stat.d;
    char *c_ptr = &stat.c;
    if (*d_ptr != 4e12 || *c_ptr != 127) {
        return 0;
    }

    // dereference member
    if (*stat.ptr != 100) {
        return 0;
    }

    // read members and use them in complex expressions (e.g. function calls)
    if (!accept_params(stat.d / 4e10, stat.c * 2, (double)stat.l, *stat.ptr,
                       stat.d, stat.c, stat.l, stat.ptr)) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    // accessing struct w/ automatic storage duration
    if (!test_auto()) {
        return 1;
    }

    // accessing struct w/ static storage duration
    if (!test_static()) {
        return 2;
    }

    return 0;
}
)PROG"));
}
