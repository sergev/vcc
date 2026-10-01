//
// Programs whose results depend on integer widths or sizes are in their generic
// LP64 form; BESM-6 runs its own versions of them (see README.md).
//
#include "book_test.h"

// Passes structs of every classification by value as single parameters.  Out-of-range x86
// literals adapted to the BESM-6 ranges (doubles 1.7e308 -> 1.0e18; long -9223372036854775807
// -> -(2^40-1)) and strcmp strings uppercased so the ASCII char path matches the KOI-7 static
// path (see docs/KOI7_Encoding.md).
TEST_F(BookTest, Chapter18_ClassifyParams)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we classify structure parameters correctly,
 * by passing a variety of structures as arguments.
 * Each test function takes only one argument.
 * */


int strcmp(char *s1, char *s2);

// from Listing 18-39
struct twelve_bytes {
    int i;
    char arr[8];
};  // two INTEGER eightbytes

// from Listing 18-40
struct inner {
    int i;
    char ch2;
};

struct nested_ints {
    char ch1;
    struct inner nested;
};  // two INTEGER eightbytes

// from Listing 18-41
struct flattened_ints {
    char c;
    int i;
    char a;
};  // two INTEGER eightbytes

// From uncaptioned listing in "Classifying Eightbytes" section
struct large {
    int i;
    double d;
    char arr[10];
};  // four MEMORY eightbytes

// Three structure declarations from Listing 18-42
struct two_ints {
    int i;
    int i2;
};  // one INTEGER eightbyte

struct nested_double {
    double array[1];
};  // one SSE eightbyte

struct two_eightbytes {
    double d;
    char c;
};  // one SSE eightbyte, one INTEGER eightbyte

// From Listing 18-47
struct pass_in_memory {
    double w;
    double x;
    int y;
    long z;
};  // four MEMORY eightbytes

// validation functions defined in library
int test_twelve_bytes(struct twelve_bytes s);
int t_nints(struct nested_ints s);
int test_flattened_ints(struct flattened_ints s);
int test_large(struct large s);
int t_2ints(struct two_ints s);
int t_ndbl(struct nested_double s);
int t_2eb(struct two_eightbytes s);
int test_pass_in_memory(struct pass_in_memory s);
/* Test that we classify structure parameters correctly,
 * by passing a variety of structures as arguments.
 * Each test function takes only one argument.
 * */


int main(void) {
    struct twelve_bytes s1 = {0, "LMNOPQR"};
    if (!test_twelve_bytes(s1)) {
        return 1;
    }

    struct nested_ints s2 = {127, {2147483647, 255}};
    if (!t_nints(s2)) {
        return 2;
    }

    struct flattened_ints s3 = {127, 2147483647, 255};
    if (!test_flattened_ints(s3)) {
        return 3;
    }

    struct large s4 = {200000, 23.25, "ABCDEFGHI"};
    if (!test_large(s4)) {
        return 4;
    }

    struct two_ints s5 = {999, 888};
    if (!t_2ints(s5)) {
        return 5;
    }

    struct nested_double s6 = {{25.125e3}};
    if (!t_ndbl(s6)) {
        return 6;
    }

    struct two_eightbytes s7 = {1000., 'x'};
    if (!t_2eb(s7)) {
        return 7;
    }

    struct pass_in_memory s8 = {1.0e18, -1.0e18, -2147483647, -1099511627775l};
    if (!test_pass_in_memory(s8)) {
        return 8;
    }

    return 0; // success
}
/* Test that we classify structure parameters correctly,
 * by passing a variety of structures as arguments.
 * Each test function takes only one argument.
 * */


int test_twelve_bytes(struct twelve_bytes s) {
    if (s.i != 0 || strcmp(s.arr, "LMNOPQR")) {
        return 0;
    }
    return 1;  // success
}
int t_nints(struct nested_ints s) {
    if (s.ch1 != 127 || s.nested.i != 2147483647 || s.nested.ch2 != 255) {
        return 0;
    }
    return 1;  // success
}
int test_flattened_ints(struct flattened_ints s) {
    if (s.c != 127 || s.i != 2147483647 || s.a != 255) {
        return 0;
    }

    return 1;  // success
}
int test_large(struct large s) {
    if (s.i != 200000 || s.d != 23.25 || strcmp(s.arr, "ABCDEFGHI")) {
        return 0;
    }

    return 1;  // success
}
int t_2ints(struct two_ints s) {
    if (s.i != 999 || s.i2 != 888) {
        return 0;
    }

    return 1;  // success
}
int t_ndbl(struct nested_double s) {
    if (s.array[0] != 25.125e3) {
        return 0;
    }

    return 1;  // success
}
int t_2eb(struct two_eightbytes s) {
    if (s.d != 1000. || s.c != 'x') {
        return 0;
    }

    return 1;  // success
}
int test_pass_in_memory(struct pass_in_memory s) {
    if (s.w != 1.0e18 || s.x != -1.0e18 || s.y != -2147483647 ||
        s.z != -1099511627775l) {
        return 0;
    }

    return 1;  // success
}
)PROG"));
}

// Passes a mix of struct and scalar arguments by value.  strcmp strings uppercased (KOI-7),
// signed-char member values made positive (plain char is unsigned on BESM-6) and out-of-range
// long literals reduced to the BESM-6 ~2^40 range.
TEST_F(BookTest, Chapter18_ParamCallingConventions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we can pass a mix of struct and non-struct arguments according to
 * the ABI */


int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, unsigned long n);

// This type comes from Listing 18-45
struct two_longs {
    long a;
    long b;
};

struct one_int {
    int i;
    char c;
};

struct one_int_exactly {
    unsigned long l;
};

struct two_ints {
    char c;
    int arr[3];
};

struct two_ints_nested {
    struct one_int a;
    struct one_int b;
};

struct twelve_bytes {
    int i;
    char arr[8];
};

struct one_xmm {
    double d;
};

struct two_xmm {
    double d[2];
};

struct int_and_xmm {
    char c;
    double d;
};

struct xmm_and_int {
    struct one_xmm dbl;
    char c[3];
};

struct odd_size {
    char arr[5];
};

struct memory {
    double d;
    char c[3];
    long l;
    int i;
};

// passing structures as parameters

// all arguments fit in registers
int pass_small_structs(struct two_xmm two_xmm_struct, struct one_int int_struct,
                       struct one_xmm xmm_struct,
                       struct xmm_and_int mixed_struct,
                       struct twelve_bytes int_struct_2,
                       struct one_int_exactly another_int_struct);

// based on example in Listing 18-45
int a_bunch_of_arguments(int i0, int i1, int i2, int i3, int i4,
                         struct two_longs param, int i5);

// use remaining structure types, mix with scalars
int structs_and_scalars(long l, double d, struct odd_size os, struct memory mem,
                        struct one_xmm xmm_struct);

// pass fourth_struct in memory b/c we're out of XMM registers
int struct_in_mem(double a, double b, double c, struct xmm_and_int first_struct,
                  double d, struct two_xmm second_struct, long l,
                  struct int_and_xmm third_struct,
                  struct one_xmm fourth_struct);

// pass two_ints_nested in memory - we have one general-purpose reg left for
// parameter passing but it requires two
int pass_borderline_struct_in_memory(struct two_ints t_i, int c,
                                     struct int_and_xmm i_x, void *ptr,
                                     struct two_ints_nested t_i_n, double d);

// pass a struct in memory that isn't neatly divisible by 8
int pass_uneven_struct_in_mem(struct twelve_bytes struct1, long a, long b,
                              struct twelve_bytes struct2, struct odd_size os,
                              struct memory m);

// pass first struct in memory, later structs in registers
int pass_later_structs_in_regs(struct memory m, struct twelve_bytes struct1, struct one_xmm struct2);
/* Test that we can pass a mix of struct and non-struct arguments according to
 * the ABI */


int main(void) {
    // define a bunch of structures
    struct two_longs two_longs = {1234567l, 89101112l};
    struct one_int one_int = {54320, 'c'};
    struct one_int_exactly one_long = {567890l};
    struct two_ints two_ints = {'_', {5, 6, 7}};
    struct two_ints_nested two_ints_nested = {one_int, one_int};
    struct twelve_bytes xii = {123, "STRING!"};

    struct one_xmm one_xmm = {5.125};
    struct two_xmm two_xmm = {{55.5, 44.4}};
    struct int_and_xmm int_and_xmm = {'p', 4.56};
    struct xmm_and_int xmm_and_int = {{1.234}, "HI"};

    struct odd_size odd = {"LMNO"};
    struct memory mem = {15.75, "RS", 4444, 3333};

    // call validation functions

    if (!pass_small_structs(two_xmm, one_int, one_xmm, xmm_and_int, xii,
                            one_long)) {
        return 1;
    }

    // based on example in Listing 18-45
    if (!a_bunch_of_arguments(0, 1, 2, 3, 4, two_longs, 5)) {
        return 2;
    }

    if (!structs_and_scalars(10, 10.0, odd, mem, one_xmm)) {
        return 2;
    }

    if (!struct_in_mem(10.0, 11.125, 12.0, xmm_and_int, 13.0, two_xmm, 0,
                       int_and_xmm, one_xmm)) {
        return 3;
    }
    if (!pass_borderline_struct_in_memory(two_ints, '!', int_and_xmm, 0,
                                          two_ints_nested, 7.8)) {
        return 4;
    }

    // define some more structs to use in last two test cases
    struct twelve_bytes struct1 = {-1, {127, 126, 125}};
    struct twelve_bytes struct2 = {-5, {100, 101, 102}};
    struct odd_size os = {{100, 99, 98, 97, 96}};
    struct memory m = {5.345, {1, 2, 3}, 4294967300l, 10000};
    if (!pass_uneven_struct_in_mem(struct1, 1099511627775l,
                                   1099511627774l, struct2, os, m)) {
        return 5;
    }

    if (!pass_later_structs_in_regs(m, struct1, one_xmm)) {
        return 6;
    }

    // success!
    return 0;
}
/* Test that we can pass a mix of struct and non-struct arguments according to
 * the ABI */


// all arguments fit in registers
int pass_small_structs(struct two_xmm two_xmm_struct, struct one_int int_struct,
                       struct one_xmm xmm_struct,
                       struct xmm_and_int mixed_struct,
                       struct twelve_bytes int_struct_2,
                       struct one_int_exactly another_int_struct) {
    if (two_xmm_struct.d[0] != 55.5 || two_xmm_struct.d[1] != 44.4)
        return 0;

    if (int_struct.c != 'c' || int_struct.i != 54320)
        return 0;
    if (xmm_struct.d != 5.125)
        return 0;
    if (strcmp(mixed_struct.c, "HI") || mixed_struct.dbl.d != 1.234)
        return 0;
    if (strcmp(int_struct_2.arr, "STRING!") || int_struct_2.i != 123)
        return 0;

    if (another_int_struct.l != 567890)
        return 0;

    return 1;  // success
}

// based on example in Listing 18-45
int a_bunch_of_arguments(int i0, int i1, int i2, int i3, int i4,
                         struct two_longs param, int i5) {
    if (i0 != 0 || i1 != 1 || i2 != 2 || i3 != 3 || i4 != 4 || i5 != 5) {
        return 0;
    }

    if (param.a != 1234567l || param.b != 89101112l) {
        return 0;
    }

    return 1;  // success
}

// use remaining structure types, mix with scalars
int structs_and_scalars(long l, double d, struct odd_size os, struct memory mem,
                        struct one_xmm xmm_struct) {
    if (l != 10)
        return 0;
    if (d != 10.0)
        return 0;
    if (strcmp(os.arr, "LMNO"))
        return 0;
    if (strcmp(mem.c, "RS") || mem.d != 15.75 || mem.i != 3333 || mem.l != 4444)
        return 0;
    if (xmm_struct.d != 5.125)
        return 0;

    return 1;  // success
}

// pass fourth_struct in memory b/c we're out of XMM registers
int struct_in_mem(double a, double b, double c, struct xmm_and_int first_struct,
                  double d, struct two_xmm second_struct, long l,
                  struct int_and_xmm third_struct,
                  struct one_xmm fourth_struct) {
    if (a != 10.0 || b != 11.125 || c != 12.0)
        return 0;
    if (strcmp(first_struct.c, "HI") || first_struct.dbl.d != 1.234)
        return 0;
    if (d != 13.0)
        return 0;
    if (second_struct.d[0] != 55.5 || second_struct.d[1] != 44.4)
        return 0;
    if (l)
        return 0;
    if (third_struct.c != 'p' || third_struct.d != 4.56)
        return 0;
    if (fourth_struct.d != 5.125)
        return 0;

    return 1;  // success
}

// pass two_ints_nested in memory - we have one general-purpose reg left for
// parameter passing but it requires two
int pass_borderline_struct_in_memory(struct two_ints t_i, int c,
                                     struct int_and_xmm i_x, void *ptr,
                                     struct two_ints_nested t_i_n, double d) {
    if (t_i.c != '_' || t_i.arr[0] != 5 || t_i.arr[1] != 6 || t_i.arr[2] != 7)
        return 0;
    if (c != '!')
        return 0;
    if (i_x.c != 'p' || i_x.d != 4.56)
        return 0;

    if (ptr)
        return 0;

    if (t_i_n.a.c != 'c' || t_i_n.a.i != 54320)
        return 0;
    if (t_i_n.b.c != 'c' || t_i_n.b.i != 54320)
        return 0;
    if (d != 7.8)
        return 0;
    return 1;  // success
}

// pass a struct in memory that isn't neatly divisible by 8
int pass_uneven_struct_in_mem(struct twelve_bytes struct1, long a, long b,
                              struct twelve_bytes struct2, struct odd_size os,
                              struct memory m) {
    if (struct1.i != -1) {
        return 0;
    }
    if (struct1.arr[0] != 127 || struct1.arr[1] != 126 ||
        struct1.arr[2] != 125) {
        return 0;
    }
    if (a != 1099511627775l || b != 1099511627774l) {
        return 0;
    }
    if (struct2.i != -5) {
        return 0;
    }
    if (struct2.arr[0] != 100 || struct2.arr[1] != 101 ||
        struct2.arr[2] != 102) {
        return 0;
    }
    for (int i = 0; i < 5; i = i + 1) {
        if (os.arr[i] != 100 - i) {
            return 0;
        }
    }
    if (m.d != 5.345) {
        return 0;
    }
    if (m.c[0] != 1 || m.c[1] != 2 || m.c[2] != 3) {
        return 0;
    }
    if (m.l != 4294967300l) {
        return 0;
    }
    if (m.i != 10000) {
        return 0;
    }
    return 1;  // success
}

int pass_later_structs_in_regs(struct memory m, struct twelve_bytes struct1,
                               struct one_xmm struct2) {
    if (m.d != 5.345) {
        return 0;
    }

    if (m.c[0] != 1 || m.c[1] != 2 || m.c[2] != 3) {
        return 0;
    }

    if (m.l != 4294967300l) {
        return 0;
    }

    if (m.i != 10000) {
        return 0;
    }

    if (struct1.i != -1) {
        return 0;
    }
    if (struct1.arr[0] != 127 || struct1.arr[1] != 126 ||
        struct1.arr[2] != 125) {
        return 0;
    }

    if (struct2.d != 5.125) {
        return 0;
    }
    return 1;  // success
}
)PROG"));
}

TEST_F(BookTest, Chapter18_StructSizes1)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Pass structs of sizes 1..12 bytes by value, validated byte-exact with memcmp. Split from the book's StructSizes (passing all sizes 1..24 through one function exceeds the BESM-6 address range). */

int memcmp(void *s1, void *s2, unsigned long n);

struct bytesize1 { unsigned char arr[1]; };
extern struct bytesize1 gvar1;
struct bytesize2 { unsigned char arr[2]; };
extern struct bytesize2 gvar2;
struct bytesize3 { unsigned char arr[3]; };
extern struct bytesize3 gvar3;
struct bytesize4 { unsigned char arr[4]; };
extern struct bytesize4 gvar4;
struct bytesize5 { unsigned char arr[5]; };
extern struct bytesize5 gvar5;
struct bytesize6 { unsigned char arr[6]; };
extern struct bytesize6 gvar6;
struct bytesize7 { unsigned char arr[7]; };
extern struct bytesize7 gvar7;
struct bytesize8 { unsigned char arr[8]; };
extern struct bytesize8 gvar8;
struct bytesize9 { unsigned char arr[9]; };
extern struct bytesize9 gvar9;
struct bytesize10 { unsigned char arr[10]; };
extern struct bytesize10 gvar10;
struct bytesize11 { unsigned char arr[11]; };
extern struct bytesize11 gvar11;
struct bytesize12 { unsigned char arr[12]; };
extern struct bytesize12 gvar12;

int chk0(struct bytesize1 s1, struct bytesize2 s2, struct bytesize3 s3, struct bytesize4 s4, struct bytesize5 s5, struct bytesize6 s6, unsigned char *e1, unsigned char *e2, unsigned char *e3, unsigned char *e4, unsigned char *e5, unsigned char *e6);
int chk1(struct bytesize7 s7, struct bytesize8 s8, struct bytesize9 s9, struct bytesize10 s10, struct bytesize11 s11, struct bytesize12 s12, unsigned char *e7, unsigned char *e8, unsigned char *e9, unsigned char *e10, unsigned char *e11, unsigned char *e12);

int main(void) {
    if (!chk0(gvar1, gvar2, gvar3, gvar4, gvar5, gvar6, gvar1.arr, gvar2.arr, gvar3.arr, gvar4.arr, gvar5.arr, gvar6.arr)) return 1;
    if (!chk1(gvar7, gvar8, gvar9, gvar10, gvar11, gvar12, gvar7.arr, gvar8.arr, gvar9.arr, gvar10.arr, gvar11.arr, gvar12.arr)) return 2;

    struct bytesize1 loc1 = {{1}};
    struct bytesize2 loc2 = {{1, 2}};
    struct bytesize3 loc3 = {{1, 2, 3}};
    struct bytesize4 loc4 = {{1, 2, 3, 4}};
    struct bytesize5 loc5 = {{1, 2, 3, 4, 5}};
    struct bytesize6 loc6 = {{1, 2, 3, 4, 5, 6}};
    struct bytesize7 loc7 = {{1, 2, 3, 4, 5, 6, 7}};
    struct bytesize8 loc8 = {{1, 2, 3, 4, 5, 6, 7, 8}};
    struct bytesize9 loc9 = {{1, 2, 3, 4, 5, 6, 7, 8, 9}};
    struct bytesize10 loc10 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    struct bytesize11 loc11 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    struct bytesize12 loc12 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};

    if (!chk0(loc1, loc2, loc3, loc4, loc5, loc6, loc1.arr, loc2.arr, loc3.arr, loc4.arr, loc5.arr, loc6.arr)) return 3;
    if (!chk1(loc7, loc8, loc9, loc10, loc11, loc12, loc7.arr, loc8.arr, loc9.arr, loc10.arr, loc11.arr, loc12.arr)) return 4;
    return 0;
}

struct bytesize1 gvar1 = {{1}};
struct bytesize2 gvar2 = {{1, 2}};
struct bytesize3 gvar3 = {{1, 2, 3}};
struct bytesize4 gvar4 = {{1, 2, 3, 4}};
struct bytesize5 gvar5 = {{1, 2, 3, 4, 5}};
struct bytesize6 gvar6 = {{1, 2, 3, 4, 5, 6}};
struct bytesize7 gvar7 = {{1, 2, 3, 4, 5, 6, 7}};
struct bytesize8 gvar8 = {{1, 2, 3, 4, 5, 6, 7, 8}};
struct bytesize9 gvar9 = {{1, 2, 3, 4, 5, 6, 7, 8, 9}};
struct bytesize10 gvar10 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
struct bytesize11 gvar11 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
struct bytesize12 gvar12 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};

int chk0(struct bytesize1 s1, struct bytesize2 s2, struct bytesize3 s3, struct bytesize4 s4, struct bytesize5 s5, struct bytesize6 s6, unsigned char *e1, unsigned char *e2, unsigned char *e3, unsigned char *e4, unsigned char *e5, unsigned char *e6) {
    if (memcmp(&s1, e1, sizeof s1)) return 0;
    if (memcmp(&s2, e2, sizeof s2)) return 0;
    if (memcmp(&s3, e3, sizeof s3)) return 0;
    if (memcmp(&s4, e4, sizeof s4)) return 0;
    if (memcmp(&s5, e5, sizeof s5)) return 0;
    if (memcmp(&s6, e6, sizeof s6)) return 0;
    return 1;
}
int chk1(struct bytesize7 s7, struct bytesize8 s8, struct bytesize9 s9, struct bytesize10 s10, struct bytesize11 s11, struct bytesize12 s12, unsigned char *e7, unsigned char *e8, unsigned char *e9, unsigned char *e10, unsigned char *e11, unsigned char *e12) {
    if (memcmp(&s7, e7, sizeof s7)) return 0;
    if (memcmp(&s8, e8, sizeof s8)) return 0;
    if (memcmp(&s9, e9, sizeof s9)) return 0;
    if (memcmp(&s10, e10, sizeof s10)) return 0;
    if (memcmp(&s11, e11, sizeof s11)) return 0;
    if (memcmp(&s12, e12, sizeof s12)) return 0;
    return 1;
}
)PROG"));
}

TEST_F(BookTest, Chapter18_StructSizes2)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Pass structs of sizes 13..24 bytes by value, validated byte-exact with memcmp. Split from the book's StructSizes (see StructSizes1). */

int memcmp(void *s1, void *s2, unsigned long n);

struct bytesize13 { unsigned char arr[13]; };
extern struct bytesize13 gvar13;
struct bytesize14 { unsigned char arr[14]; };
extern struct bytesize14 gvar14;
struct bytesize15 { unsigned char arr[15]; };
extern struct bytesize15 gvar15;
struct bytesize16 { unsigned char arr[16]; };
extern struct bytesize16 gvar16;
struct bytesize17 { unsigned char arr[17]; };
extern struct bytesize17 gvar17;
struct bytesize18 { unsigned char arr[18]; };
extern struct bytesize18 gvar18;
struct bytesize19 { unsigned char arr[19]; };
extern struct bytesize19 gvar19;
struct bytesize20 { unsigned char arr[20]; };
extern struct bytesize20 gvar20;
struct bytesize21 { unsigned char arr[21]; };
extern struct bytesize21 gvar21;
struct bytesize22 { unsigned char arr[22]; };
extern struct bytesize22 gvar22;
struct bytesize23 { unsigned char arr[23]; };
extern struct bytesize23 gvar23;
struct bytesize24 { unsigned char arr[24]; };
extern struct bytesize24 gvar24;

int chk0(struct bytesize13 s13, struct bytesize14 s14, struct bytesize15 s15, struct bytesize16 s16, struct bytesize17 s17, struct bytesize18 s18, unsigned char *e13, unsigned char *e14, unsigned char *e15, unsigned char *e16, unsigned char *e17, unsigned char *e18);
int chk1(struct bytesize19 s19, struct bytesize20 s20, struct bytesize21 s21, struct bytesize22 s22, struct bytesize23 s23, struct bytesize24 s24, unsigned char *e19, unsigned char *e20, unsigned char *e21, unsigned char *e22, unsigned char *e23, unsigned char *e24);

int main(void) {
    if (!chk0(gvar13, gvar14, gvar15, gvar16, gvar17, gvar18, gvar13.arr, gvar14.arr, gvar15.arr, gvar16.arr, gvar17.arr, gvar18.arr)) return 1;
    if (!chk1(gvar19, gvar20, gvar21, gvar22, gvar23, gvar24, gvar19.arr, gvar20.arr, gvar21.arr, gvar22.arr, gvar23.arr, gvar24.arr)) return 2;

    struct bytesize13 loc13 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}};
    struct bytesize14 loc14 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}};
    struct bytesize15 loc15 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
    struct bytesize16 loc16 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    struct bytesize17 loc17 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}};
    struct bytesize18 loc18 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}};
    struct bytesize19 loc19 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}};
    struct bytesize20 loc20 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}};
    struct bytesize21 loc21 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}};
    struct bytesize22 loc22 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}};
    struct bytesize23 loc23 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23}};
    struct bytesize24 loc24 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}};

    if (!chk0(loc13, loc14, loc15, loc16, loc17, loc18, loc13.arr, loc14.arr, loc15.arr, loc16.arr, loc17.arr, loc18.arr)) return 3;
    if (!chk1(loc19, loc20, loc21, loc22, loc23, loc24, loc19.arr, loc20.arr, loc21.arr, loc22.arr, loc23.arr, loc24.arr)) return 4;
    return 0;
}

struct bytesize13 gvar13 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}};
struct bytesize14 gvar14 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}};
struct bytesize15 gvar15 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
struct bytesize16 gvar16 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
struct bytesize17 gvar17 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}};
struct bytesize18 gvar18 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}};
struct bytesize19 gvar19 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}};
struct bytesize20 gvar20 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}};
struct bytesize21 gvar21 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}};
struct bytesize22 gvar22 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}};
struct bytesize23 gvar23 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23}};
struct bytesize24 gvar24 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}};

int chk0(struct bytesize13 s13, struct bytesize14 s14, struct bytesize15 s15, struct bytesize16 s16, struct bytesize17 s17, struct bytesize18 s18, unsigned char *e13, unsigned char *e14, unsigned char *e15, unsigned char *e16, unsigned char *e17, unsigned char *e18) {
    if (memcmp(&s13, e13, sizeof s13)) return 0;
    if (memcmp(&s14, e14, sizeof s14)) return 0;
    if (memcmp(&s15, e15, sizeof s15)) return 0;
    if (memcmp(&s16, e16, sizeof s16)) return 0;
    if (memcmp(&s17, e17, sizeof s17)) return 0;
    if (memcmp(&s18, e18, sizeof s18)) return 0;
    return 1;
}
int chk1(struct bytesize19 s19, struct bytesize20 s20, struct bytesize21 s21, struct bytesize22 s22, struct bytesize23 s23, struct bytesize24 s24, unsigned char *e19, unsigned char *e20, unsigned char *e21, unsigned char *e22, unsigned char *e23, unsigned char *e24) {
    if (memcmp(&s19, e19, sizeof s19)) return 0;
    if (memcmp(&s20, e20, sizeof s20)) return 0;
    if (memcmp(&s21, e21, sizeof s21)) return 0;
    if (memcmp(&s22, e22, sizeof s22)) return 0;
    if (memcmp(&s23, e23, sizeof s23)) return 0;
    if (memcmp(&s24, e24, sizeof s24)) return 0;
    return 1;
}
)PROG"));
}


// BESM-6: static struct inner instead of calloc for the nested pointer member.
TEST_F(BookTest, Chapter18_AccessRetvalMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test for accessing the members in a return value of structure type */
struct inner {
    char x;
    long y;
};

struct outer {
    double d;
    struct inner *ptr;
    struct inner s;
};

struct inner return_small_struct(void);
struct outer return_nested_struct(void);
/* Test for accessing the members in a return value of structure type */

int main(void) {
    // get member in a non-nested struct
    if (return_small_struct().y != 102) {
        return 1;
    }

    // get members in nested struct
    if (return_nested_struct().d != 2.0 || return_nested_struct().s.x != 10 ||
        return_nested_struct().s.y != 11) {
        return 3;
    }

    // get members thru pointer in nested struct
    if (return_nested_struct().ptr->x != 12 ||
        return_nested_struct().ptr->y != 13) {
        return 4;
    }

    // update members through pointer in nested struct
    return_nested_struct().ptr->x = 70;
    return_nested_struct().ptr->y = 71;

    // validate updated values
    if (return_nested_struct().ptr->x != 70 ||
        return_nested_struct().ptr->y != 71) {
        return 5;
    }

    return 0;  // success
}
/* Test for accessing the members in a return value of structure type */

struct inner return_small_struct(void) {
    struct inner i = {101, 102};
    return i;
}

struct outer return_nested_struct(void) {
    static struct outer ret = {2.0, 0, {10, 11}};

    // on first call to this function, initialize ret.ptr (static storage replaces calloc)
    static struct inner ri;
    if (!ret.ptr) {
        ret.ptr = &ri;
        ret.ptr->x = 12;
        ret.ptr->y = 13;
    }

    return ret;
}
)PROG"));
}

// Returns a wide range of struct types by value (accumulator and sret classes) and mixes
// struct returns with scalar/struct params.  Out-of-range double 34e43 adapted to 34e16 and
// strcmp strings uppercased for the KOI-7 static path.
TEST_F(BookTest, Chapter18_ReturnCallingConventions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we return a wide range of struct types according to the ABI */

int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, unsigned long n);

struct one_int {
    int i;
    char c;
};

struct one_int_exactly {
    unsigned long l;
};

struct two_ints {
    char c;
    int arr[3];
};

struct two_ints_nested {
    struct one_int a;
    struct one_int b;
};

struct twelve_bytes {
    int i;
    char arr[8];
};

struct one_xmm {
    double d;
};

struct two_xmm {
    double d[2];
};

struct int_and_xmm {
    char c;
    double d;
};

struct xmm_and_int {
    struct one_xmm dbl;
    char c[3];
};

struct odd_size {
    char arr[5];
};

struct memory {
    double d;
    char c[3];
    long l;
    int i;
};

// returning structures

struct one_int return_int_struct(void);
struct twelve_bytes r_2int(void);
struct one_xmm return_double_struct(void);
struct two_xmm r_2dbl(void);
struct xmm_and_int r_mix(void);
struct int_and_xmm r_mix2(void);
struct memory return_on_stack(void);

// return on stack + pass other int params
struct memory pass_and_return_regs(int i, double d, struct int_and_xmm strct,
                                   int c, struct two_ints t_i, long l,
                                   struct one_int_exactly o_i_e, int c2);
/* Test that we return a wide range of struct types according to the ABI */

int main(void) {
    struct one_int_exactly one_long = {567890l};
    struct two_ints two_ints = {'_', {5, 6, 7}};
    struct int_and_xmm int_and_xmm = {'p', 4.56};

    // returning structures

    struct one_int s1 = return_int_struct();
    if (s1.i != 1 || s1.c != 2) {
        return 1;
    }

    struct twelve_bytes s2 = r_2int();
    if (s2.i != 10 || strncmp(s2.arr, "12345678", sizeof s2.arr))
        return 2;

    struct one_xmm s3 = return_double_struct();
    if (s3.d != 100.625)
        return 3;
    struct two_xmm s4 = r_2dbl();
    if (s4.d[0] != 8.8 || s4.d[1] != 7.8)
        return 4;

    struct xmm_and_int s5 = r_mix();
    if (s5.dbl.d != 10.0 || strcmp(s5.c, "AB"))
        return 5;

    struct int_and_xmm s6 = r_mix2();
    if (s6.c != 127 || s6.d != 34e16)
        return 6;

    struct memory s7 = return_on_stack();
    if (s7.d != 1.25 || strcmp(s7.c, "XY") || s7.l != 100l || s7.i != 44)
        return 7;

    s7 = pass_and_return_regs(6, 4.0, int_and_xmm, 5, two_ints, 77, one_long,
                              99);
    // something was clobbered or set incorrectly in retval
    if (s7.d || s7.c[0] || s7.c[1] || s7.c[2])
        return 8;

    // i was set to indicate problem w/ parameter passing
    if (s7.i)
        return 9;

    if (s7.l != 100)
        return 10;  // l field was clobbered or set incorrectly

    // success!
    return 0;
}
/* Test that we return a wide range of struct types according to the ABI */

struct one_int return_int_struct(void) {
    struct one_int retval = {1, 2};
    return retval;
}

struct twelve_bytes r_2int(void) {
    struct twelve_bytes retval = {10, "12345678"};
    return retval;
}

struct one_xmm return_double_struct(void) {
    struct one_xmm retval = {100.625};
    return retval;
}
struct two_xmm r_2dbl(void) {
    struct two_xmm retval = {{8.8, 7.8}};
    return retval;
}
struct xmm_and_int r_mix(void) {
    struct xmm_and_int retval = {{10.0}, "AB"};
    return retval;
}
struct int_and_xmm r_mix2(void) {
    struct int_and_xmm retval = {127, 34e16};
    return retval;
}
struct memory return_on_stack(void) {
    struct memory retval = {1.25, "XY", 100l, 44};
    return retval;
}

int leaf_call(struct two_ints t_i, int c, double d) {
    // validate t_i
    if (t_i.c != '_' || t_i.arr[0] != 5 || t_i.arr[1] != 6 || t_i.arr[2] != 7) {
        return 0;
    }

    // validate c1 and d1 (originally passed in a struct int_and_xmm)
    if (c != 'p' || d != 4.56) {
        return 0;
    }
    return 1;  // success
}

struct memory pass_and_return_regs(int i, double d, struct int_and_xmm strct,
                                   int c, struct two_ints t_i, long l,
                                   struct one_int_exactly o_i_e, int c2) {
    // include a stack variable to make sure it doen't overwrite return value
    // pointer or vice versa
    char stackbytes[8] = "ZYXWVUT";
    struct memory retval = {0, {0, 0, 0}, 0, 0};

    // make another function call to ensure that passing parameters
    // doesn't overwrite return address in RDI or other struct eightybtes
    // passed in registers; validate t_i and strct while we're at it
    if (!leaf_call(t_i, strct.c, strct.d)) {
        retval.i = 1;
        return retval;
    }
    // validate scalar params
    if (i != 6 || d != 4.0 || c != 5 || l != 77 || c2 != 99) {
        retval.i = 2;
        return retval;
    }
    // validate remainign struct
    if (o_i_e.l != 567890) {
        retval.i = 3;
        return retval;
    }

    // validate stackbytes
    if (strcmp(stackbytes, "ZYXWVUT")) {
        retval.i = 4;
        return retval;
    }
    retval.l = 100;
    return retval;  // success
}
)PROG"));
}

// Returns structs of every size 1..24 bytes by value: <=6-byte structs in the accumulator,
// larger ones via the hidden-pointer (sret) ABI.  Validated byte-exact with memcmp.
TEST_F(BookTest, Chapter18_RetvalStructSizes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we can return structs of every size between 1 and 24 bytes. */

struct bytesize1 {
    unsigned char arr[1];
};

extern struct bytesize1 gvar1;
struct bytesize1 fun1(void);

struct bytesize2 {
    unsigned char arr[2];
};

extern struct bytesize2 gvar2;
struct bytesize2 fun2(void);

struct bytesize3 {
    unsigned char arr[3];
};

extern struct bytesize3 gvar3;
struct bytesize3 fun3(void);

struct bytesize4 {
    unsigned char arr[4];
};

extern struct bytesize4 gvar4;
struct bytesize4 fun4(void);

struct bytesize5 {
    unsigned char arr[5];
};

extern struct bytesize5 gvar5;
struct bytesize5 fun5(void);

struct bytesize6 {
    unsigned char arr[6];
};

extern struct bytesize6 gvar6;
struct bytesize6 fun6(void);

struct bytesize7 {
    unsigned char arr[7];
};

extern struct bytesize7 gvar7;
struct bytesize7 fun7(void);

struct bytesize8 {
    unsigned char arr[8];
};

extern struct bytesize8 gvar8;
struct bytesize8 fun8(void);

struct bytesize9 {
    unsigned char arr[9];
};

extern struct bytesize9 gvar9;
struct bytesize9 fun9(void);

struct bytesize10 {
    unsigned char arr[10];
};

extern struct bytesize10 gvar10;
struct bytesize10 fun10(void);

struct bytesize11 {
    unsigned char arr[11];
};

extern struct bytesize11 gvar11;
struct bytesize11 fun11(void);

struct bytesize12 {
    unsigned char arr[12];
};

extern struct bytesize12 gvar12;
struct bytesize12 fun12(void);

struct bytesize13 {
    unsigned char arr[13];
};

extern struct bytesize13 gvar13;
struct bytesize13 fun13(void);

struct bytesize14 {
    unsigned char arr[14];
};

extern struct bytesize14 gvar14;
struct bytesize14 fun14(void);

struct bytesize15 {
    unsigned char arr[15];
};

extern struct bytesize15 gvar15;
struct bytesize15 fun15(void);

struct bytesize16 {
    unsigned char arr[16];
};

extern struct bytesize16 gvar16;
struct bytesize16 fun16(void);

struct bytesize17 {
    unsigned char arr[17];
};

extern struct bytesize17 gvar17;
struct bytesize17 fun17(void);

struct bytesize18 {
    unsigned char arr[18];
};

extern struct bytesize18 gvar18;
struct bytesize18 fun18(void);

struct bytesize19 {
    unsigned char arr[19];
};

extern struct bytesize19 gvar19;
struct bytesize19 fun19(void);

struct bytesize20 {
    unsigned char arr[20];
};

extern struct bytesize20 gvar20;
struct bytesize20 fun20(void);

struct bytesize21 {
    unsigned char arr[21];
};

extern struct bytesize21 gvar21;
struct bytesize21 fun21(void);

struct bytesize22 {
    unsigned char arr[22];
};

extern struct bytesize22 gvar22;
struct bytesize22 fun22(void);

struct bytesize23 {
    unsigned char arr[23];
};

extern struct bytesize23 gvar23;
struct bytesize23 fun23(void);

struct bytesize24 {
    unsigned char arr[24];
};

extern struct bytesize24 gvar24;
struct bytesize24 fun24(void);
/* Test that we can return structs of every size between 1 and 24 bytes. */
int memcmp(void *s1, void *s2, unsigned long n);

int main(void) {
    struct bytesize1 s1 = fun1();
    if (memcmp(&s1, &gvar1, sizeof s1)) {
        return 1;
    }

    struct bytesize2 s2 = fun2();
    if (memcmp(&s2, &gvar2, sizeof s2)) {
        return 2;
    }

    struct bytesize3 s3 = fun3();
    if (memcmp(&s3, &gvar3, sizeof s3)) {
        return 3;
    }

    struct bytesize4 s4 = fun4();
    if (memcmp(&s4, &gvar4, sizeof s4)) {
        return 4;
    }

    struct bytesize5 s5 = fun5();
    if (memcmp(&s5, &gvar5, sizeof s5)) {
        return 5;
    }

    struct bytesize6 s6 = fun6();
    if (memcmp(&s6, &gvar6, sizeof s6)) {
        return 6;
    }

    struct bytesize7 s7 = fun7();
    if (memcmp(&s7, &gvar7, sizeof s7)) {
        return 7;
    }

    struct bytesize8 s8 = fun8();
    if (memcmp(&s8, &gvar8, sizeof s8)) {
        return 8;
    }

    struct bytesize9 s9 = fun9();
    if (memcmp(&s9, &gvar9, sizeof s9)) {
        return 9;
    }

    struct bytesize10 s10 = fun10();
    if (memcmp(&s10, &gvar10, sizeof s10)) {
        return 10;
    }

    struct bytesize11 s11 = fun11();
    if (memcmp(&s11, &gvar11, sizeof s11)) {
        return 11;
    }

    struct bytesize12 s12 = fun12();
    if (memcmp(&s12, &gvar12, sizeof s12)) {
        return 12;
    }

    struct bytesize13 s13 = fun13();
    if (memcmp(&s13, &gvar13, sizeof s13)) {
        return 13;
    }

    struct bytesize14 s14 = fun14();
    if (memcmp(&s14, &gvar14, sizeof s14)) {
        return 14;
    }

    struct bytesize15 s15 = fun15();
    if (memcmp(&s15, &gvar15, sizeof s15)) {
        return 15;
    }

    struct bytesize16 s16 = fun16();
    if (memcmp(&s16, &gvar16, sizeof s16)) {
        return 16;
    }

    struct bytesize17 s17 = fun17();
    if (memcmp(&s17, &gvar17, sizeof s17)) {
        return 17;
    }

    struct bytesize18 s18 = fun18();
    if (memcmp(&s18, &gvar18, sizeof s18)) {
        return 18;
    }

    struct bytesize19 s19 = fun19();
    if (memcmp(&s19, &gvar19, sizeof s19)) {
        return 19;
    }

    struct bytesize20 s20 = fun20();
    if (memcmp(&s20, &gvar20, sizeof s20)) {
        return 20;
    }

    struct bytesize21 s21 = fun21();
    if (memcmp(&s21, &gvar21, sizeof s21)) {
        return 21;
    }

    struct bytesize22 s22 = fun22();
    if (memcmp(&s22, &gvar22, sizeof s22)) {
        return 22;
    }

    struct bytesize23 s23 = fun23();
    if (memcmp(&s23, &gvar23, sizeof s23)) {
        return 23;
    }

    struct bytesize24 s24 = fun24();
    if (memcmp(&s24, &gvar24, sizeof s24)) {
        return 24;
    }

    return 0;
}

struct bytesize1 gvar1 = {{0}};

struct bytesize2 gvar2 = {{1, 2}};

struct bytesize3 gvar3 = {{3, 4, 5}};

struct bytesize4 gvar4 = {{6, 7, 8, 9}};

struct bytesize5 gvar5 = {{10, 11, 12, 13, 14}};

struct bytesize6 gvar6 = {{15, 16, 17, 18, 19, 20}};

struct bytesize7 gvar7 = {{21, 22, 23, 24, 25, 26, 27}};

struct bytesize8 gvar8 = {{28, 29, 30, 31, 32, 33, 34, 35}};

struct bytesize9 gvar9 = {{36, 37, 38, 39, 40, 41, 42, 43, 44}};

struct bytesize10 gvar10 = {{45, 46, 47, 48, 49, 50, 51, 52, 53, 54}};

struct bytesize11 gvar11 = {{55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65}};

struct bytesize12 gvar12 = {
    {66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77}};

struct bytesize13 gvar13 = {
    {78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90}};

struct bytesize14 gvar14 = {
    {91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104}};

struct bytesize15 gvar15 = {{105, 106, 107, 108, 109, 110, 111, 112, 113,
                                 114, 115, 116, 117, 118, 119}};

struct bytesize16 gvar16 = {{120, 121, 122, 123, 124, 125, 126, 127, 128,
                                 129, 130, 131, 132, 133, 134, 135}};

struct bytesize17 gvar17 = {{136, 137, 138, 139, 140, 141, 142, 143, 144,
                                 145, 146, 147, 148, 149, 150, 151, 152}};

struct bytesize18 gvar18 = {{153, 154, 155, 156, 157, 158, 159, 160, 161,
                                 162, 163, 164, 165, 166, 167, 168, 169, 170}};

struct bytesize19 gvar19 = {{171, 172, 173, 174, 175, 176, 177, 178, 179,
                                 180, 181, 182, 183, 184, 185, 186, 187, 188,
                                 189}};

struct bytesize20 gvar20 = {{190, 191, 192, 193, 194, 195, 196,
                                 197, 198, 199, 200, 201, 202, 203,
                                 204, 205, 206, 207, 208, 209}};

struct bytesize21 gvar21 = {{210, 211, 212, 213, 214, 215, 216,
                                 217, 218, 219, 220, 221, 222, 223,
                                 224, 225, 226, 227, 228, 229, 230}};

struct bytesize22 gvar22 = {{231, 232, 233, 234, 235, 236, 237, 238,
                                 239, 240, 241, 242, 243, 244, 245, 246,
                                 247, 248, 249, 250, 251, 252}};

struct bytesize23 gvar23 = {{253, 254, 255, 0,  1,  2,  3,  4,
                                 5,   6,   7,   8,  9,  10, 11, 12,
                                 13,  14,  15,  16, 17, 18, 19}};

struct bytesize24 gvar24 = {{20, 21, 22, 23, 24, 25, 26, 27,
                                 28, 29, 30, 31, 32, 33, 34, 35,
                                 36, 37, 38, 39, 40, 41, 42, 43}};
/* Test that we can return structs of every size between 1 and 24 bytes. */

struct bytesize1 fun1(void) {
    return gvar1;
}
struct bytesize2 fun2(void) {
    return gvar2;
}
struct bytesize3 fun3(void) {
    return gvar3;
}
struct bytesize4 fun4(void) {
    return gvar4;
}
struct bytesize5 fun5(void) {
    return gvar5;
}
struct bytesize6 fun6(void) {
    return gvar6;
}
struct bytesize7 fun7(void) {
    return gvar7;
}
struct bytesize8 fun8(void) {
    return gvar8;
}
struct bytesize9 fun9(void) {
    return gvar9;
}
struct bytesize10 fun10(void) {
    return gvar10;
}
struct bytesize11 fun11(void) {
    return gvar11;
}
struct bytesize12 fun12(void) {
    return gvar12;
}
struct bytesize13 fun13(void) {
    return gvar13;
}
struct bytesize14 fun14(void) {
    return gvar14;
}
struct bytesize15 fun15(void) {
    return gvar15;
}
struct bytesize16 fun16(void) {
    return gvar16;
}
struct bytesize17 fun17(void) {
    return gvar17;
}
struct bytesize18 fun18(void) {
    return gvar18;
}
struct bytesize19 fun19(void) {
    return gvar19;
}
struct bytesize20 fun20(void) {
    return gvar20;
}
struct bytesize21 fun21(void) {
    return gvar21;
}
struct bytesize22 fun22(void) {
    return gvar22;
}
struct bytesize23 fun23(void) {
    return gvar23;
}
struct bytesize24 fun24(void) {
    return gvar24;
}
)PROG"));
}

// extra_credit/member_access/nested_union_access, from the book;
// a negative-valued char member is `signed char`, since plain char may be unsigned.
TEST_F(BookTest, Chapter18_NestedUnionAccess)
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
    if (x.u.i != 200000) {
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

    if (z.s.u.c != 57) { // lowest byte of 12345
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
    if (x.u.i != 200000) {
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

    if (z.s.u.c != 57) { // lowest byte of 12345
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

    // read through other members that should have same value
    if (outer_ptr->u_ptr->c != -10 || outer_ptr->u_ptr->i != -10 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr
    if (outer_ptr->u_ptr->uc_arr[0] != 246 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
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

    // read through other members that should have same value
    if (outer_ptr->u_ptr->c != -10 || outer_ptr->u_ptr->i != -10 || outer_ptr->u_ptr->l != -10) {
        return 0; // fail
    }

    // read through members of uc_arr
    if (outer_ptr->u_ptr->uc_arr[0] != 246 || outer_ptr->u_ptr->uc_arr[1] != 255 || outer_ptr->u_ptr->uc_arr[2] != 255) {
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

    if (arr[0].u.l != -10000 || arr[1].u.c != -56 || arr[2].u.uc_arr[0] != 136) {
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

// extra_credit/member_access/static_union_access, from the book;
// a negative-valued char member is `signed char`, since plain char may be unsigned.
TEST_F(BookTest, Chapter18_StaticUnionAccess)
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

    for (int i = 0; i < 6; i = i + 1) {
        // lower 6 bytes are 0
        if (my_union.arr[i]) {
            return 4; // fail
        }
    }
    if (union_ptr->arr[6] != -16) {
        return 5; // fail
    }

    if (union_ptr->arr[7] != -65) {
        return 6; // fail
    }

    return 0; // success
}
)PROG"));
}

// extra_credit/member_access/union_temp_lifetime, from the book;
// a negative-valued char member is `signed char`, since plain char may be unsigned.
TEST_F(BookTest, Chapter18_UnionTempLifetime)
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

    // first access member in union1
    if ((get_flag() ? union1 : union2).s.arr[0] != -22) {
        return 1; // fail
    }

    // then access member in union2
    if ((get_flag() ? union1 : union2).s.arr[0] != -46) {
        return 2; // fail
    }

    return 0; // success
}
)PROG"));
}

// Adapted for BESM-6: unsigned long is 48-bit, so the wide constants use the
// top word bit (2^47) instead of the x86 2^63 sign bit.
TEST_F(BookTest, Chapter18_BitwiseOpsStructMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Bitwise operations with structure members

struct inner {
    char b;
    unsigned int u;
};

struct outer {
    unsigned long l;
    struct inner *in_ptr;
    int bar;
    struct inner in;
};

int main(void) {
    struct inner i = {'a', 100000u};
    struct outer o = {140737488355330ul, &i, 100, {-80, 4294967295U}};

    if ((i.b | o.l) != 140737488355427ul) {
        return 1;  // fail
    }

    if ((o.bar ^ i.u) != 100036u) {
        return 2;  // fail
    }

    if ((o.in_ptr->b & o.in.b) != 32) {
        return 3;  // fail
    }

    if ((o.l >> 26) != 2097152ul) {
        return 4;  // fail
    }

    o.bar = 12;
    if ((i.b << o.bar) != 397312) {
        return 5;
    }

    return 0;
}
)PROG"));
}

// Adapted for BESM-6: unsigned long is 48-bit (so the wide modulo constant is
// 2^48-1), plain char is unsigned (so the -12 member is made positive), and the
// double members stay within the BESM-6 ~2^63 exponent range (80e10 not 80e20).
TEST_F(BookTest, Chapter18_CompoundAssignStructMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Compound assignment operations with structure members
struct inner {
    double a;
    char b;
    int *ptr;
};

struct outer {
    unsigned long l;
    struct inner *in_ptr;
    struct inner in_array[4];
    int bar;
};

int main(void) {
    int i = -1;
    int i2 = -2;
    struct inner si = {150., 12, &i};
    struct outer o = {// l
                      281474976710655UL,
                      // in_ptr
                      &si,
                      // in_array
                      {si, {-20e10, 120, 0}, {0, 0, 0}, {1, 1, &i2}},
                      // bar
                      2000};

    // +=
    si.a += 10;  // 150. + 10 = 160
    if (si.a != 160) {
        return 1;  // fail
    }

    // -=
    // no overflow b/c of integer promotion
    o.in_array[0].b -= 460;  //  12 - 460 = -448, reduces to 64
    if (o.in_array[0].b != 64) {
        return 2;  // fail
    }

    // *=
    o.in_array[1].a *= -4;  // -20e10 * -4 = 80e10
    if (o.in_array[1].a != 80e10) {
        return 4;  // fail
    }

    // /=
    o.in_ptr->a /= 5;  // 160. / 5 = 32
    // o.in_ptr points to si
    if (si.a != 32) {
        return 5;  // fail
    }

    // %=
    (&o)->l %= o.bar;  // 281474976710655 % 2000 = 655
    if (o.l != 655) {
        return 6;  // fail
    }

    // pointer +=
    o.in_ptr = o.in_array;
    if ((o.in_ptr += 3)->a != 1) {
        return 7;  // fail
    }
    if (*o.in_ptr->ptr != -2) {
        return 8;  // fail
    }

    // pointer -=
    o.in_ptr -= 1u;
    if (o.in_ptr->a || o.in_ptr->b || o.in_ptr->ptr) {
        return 9;  // fail
    }

    // validate everything! (make sure nothing was clobbered)
    if (si.a != 32 || si.b != 12 || si.ptr != &i) {
        return 10;  // fail
    }

    if (o.l != 655) {
        return 11;  // fail
    }

    if (o.in_ptr != &o.in_array[2]) {
        return 12;  // fail
    }

    if (o.in_array[0].a != 150. || o.in_array[0].b != 64 ||
        o.in_array[0].ptr != &i) {
        return 13;  // fail
    }

    if (o.in_array[1].a != 80e10 || o.in_array[1].b != 120 ||
        o.in_array[1].ptr) {
        return 14;  // fail
    }

    if (o.in_array[2].a || o.in_array[2].b || o.in_array[2].ptr) {
        return 15;  // fail
    }

    if (o.in_array[3].a != 1 || o.in_array[3].b != 1 ||
        o.in_array[3].ptr != &i2) {
        return 16;  // fail
    }

    if (o.bar != 2000) {
        return 17;
    }

    return 0;
}
)PROG"));
}

// extra_credit/other_features/incr_struct_members, from the book;
// a negative-valued char member is `signed char`, since plain char may be unsigned.
TEST_F(BookTest, Chapter18_IncrStructMembers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Test prefix and postfix ++ and -- with structure members

struct inner {
    signed char c;
    unsigned int u;
};

struct outer {
    unsigned long l;
    struct inner *in_ptr;
    int array[3];
};

void *calloc(unsigned long nmemb, unsigned long size);

int main(void) {
    struct outer my_struct = {
        // l
        9223372036854775900ul,
        // in_ptr
        calloc(3, sizeof (struct inner)),
        // array
        {-1000, -2000, -3000},
    };
    struct outer *my_struct_ptr = &my_struct;

    // prefix ++
    if (++my_struct.l != 9223372036854775901ul) {
        return 1; // fail
    }

    // prefix --
    if (--my_struct.in_ptr[0].u != 4294967295U) { // unsigned wraparound
        return 2; // fail
    }

    // postfix ++
    if (my_struct.in_ptr->c++) {
        return 3; // fail
    }

    // postfix --
    if (my_struct_ptr->array[1]-- != -2000) {
        return 4; // fail
    }

    // validate current state of my_struct - make sure we performed updates
    // and didn't clobber anything
    if (my_struct_ptr->l != 9223372036854775901ul) {
        return 5; // fail
    }

    if (my_struct.in_ptr->c != 1) {
        return 6; // fail
    }
    if (my_struct_ptr->in_ptr->u !=  4294967295U) {
        return 7; // fail
    }

    if (my_struct_ptr->array[1] != -2001) {
        return 8; // fail
    }

    if (my_struct_ptr->array[0] != -1000 || my_struct_ptr->array[2] != -3000) {
        return 9; // fail
    }

    // ++/-- w/ pointers to structs
    // first let's populate the struct array at my_struct_ptr->in_ptr
    my_struct_ptr->in_ptr[1].c = -1;
    my_struct_ptr->in_ptr[1].u = 1u;
    my_struct_ptr->in_ptr[2].c = 'X';
    my_struct_ptr->in_ptr[2].u = 100000u;

    (++my_struct_ptr->in_ptr)->c--; // decrement struct array[1].c
    my_struct_ptr->in_ptr++->u++; // decrement stuct_array[1].u, increment in_ptr

    // validate - in_ptr currently points to array member at index 2

    // element 0 (now at index -2) should have same values as last time we checked
    if (my_struct_ptr->in_ptr[-2].c != 1 || my_struct_ptr->in_ptr[-2].u != 4294967295U) {
        return 10;
    }

    // we decremented c in element 1 (now at index -1), didn't change u
    if (my_struct_ptr->in_ptr[-1].c != -2) {
        return 11; // fail
    }

    if (my_struct_ptr->in_ptr[-1].u != 2) {
        return 12; // fail
    }

    // didn't change any values in last array element (now at index 0)
    if (my_struct_ptr->in_ptr[0].c != 'X' || my_struct_ptr->in_ptr[0].u != 100000u) {
        return 13; // fail
    }

    return 0;
}
)PROG"));
}

// BESM-6: static int storage replaces calloc for the incomplete-union pointers;
// the block-scope union value is +100000000 so the big-endian char member reads
// 0; the puts("NULL POINTER") branch is dead (param is never null).
TEST_F(BookTest, Chapter18_IncompleteUnionTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that our typechecker can handle valid declarations and expressions
 * involving incomplete union types
 * */


int puts(char *s);

 // Test 1: you can declare a function that accepts/returns incomplete
 // union types
union never_used;
union never_used incomplete_fun(union never_used x);

// test 2: declare an incomplete union type, then complete it.
// BESM-6: block-scope tag *definitions* are unsupported, so this type is
// forward-declared and completed at file scope instead of inside the function.
union bsu;  // declare incomplete union type
union bsu {
    long x;
    char y;
};  // complete the type

int test_block_scope_forward_decl(void) {
    union bsu* u_ptr = 0;  // pointer to the (now complete) union type

    union bsu val = { 100000000l };
    u_ptr = &val;
    if (u_ptr->x != 100000000l || u_ptr->y != 0) {
        return 0; // fail
    }

    return 1;  // success
}

// test 3: you can pass and return pointers to incomplete union types
union opaque_union;

union opaque_union* use_union_pointers(union opaque_union* param) {
    if (param == 0) {
        puts("NULL POINTER");
    }

    return 0;
}

int test_use_incomplete_union_pointers(void) {
    // define a couple of pointers to this type
    // (distinct zeroed static ints back the incomplete-type pointers)
    static int ou1, ou2;
    union opaque_union* ptr1 = (union opaque_union*)&ou1;
    union opaque_union* ptr2 = (union opaque_union*)&ou2;

    // can cast to char * and inspect; this is well-defined
    // and all bits should be 0 since we used calloc
    char* ptr1_bytes = (char*)ptr1;
    if (ptr1_bytes[0] || ptr1_bytes[1]) {
        return 0;
    }

    // can compare to 0 or each other
    if (ptr1 == 0 || ptr2 == 0 || ptr1 == ptr2) {
        return 0;
    }

    // can use them in conditionals
    static int flse = 0;
    union opaque_union* ptr3 = flse ? ptr1 : ptr2;
    if (ptr3 != ptr2) {
        return 0;
    }

    // can pass them as parameters
    if (use_union_pointers(ptr3)) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    if (!test_block_scope_forward_decl()) {
        return 1; // fail
    }

    if (!test_use_incomplete_union_pointers()) {
        return 2; // fail
    }

    return 0; // success
}
)PROG"));
}

// Union punning through a pointer + nested struct member access.
// BESM-6: the original test (named StructShadowsUnion) used different tags and did
// not actually shadow; the heap (malloc) is replaced with a stack union object so the
// test runs without the heap.
TEST_F(BookTest, Chapter18_StructShadowsUnion)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
struct s {int a; int b;};
union u {int i; unsigned int u;};

int main(void) {
    struct s my_struct = {12, 13};
    {
        union u storage;
        union u *ptr = &storage;
        ptr->i = 10;
        if (ptr->u != 10) {
            return 1; // fail
        }
        if (my_struct.b != 13) {
            return 2; // fail
        }
    }

    return 0; // success
}
)PROG"));
}

// 64-bit (LONG_MIN) + union punning + block-scope union.
// BESM-6: helper names shortened to stay distinct within 8 chars; the type tags are
// hoisted to file scope (block-scope tag definitions are unsupported); the shared-member
// union initializes its double member directly (positive char values; no LONG_MIN→double
// punning, which has no portable BESM-6 result).
TEST_F(BookTest, Chapter18_UnionNamespace)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we treat union tags, function/variable names, and each
 * struct/union's member names as separate namespaces.
 */

// Different unions/structs can use same member names
union u1 {
    int a;
};
union u2 {
    double a;
    long l;
};
struct s {
    char a[2];
};

int sharemem(void) {
    union u1 var1 = {10};
    union u2 var2 = {2.5};
    struct s var3 = {{1, 2}};
    if (var1.a != 10 || var2.a != 2.5 || var3.a[0] != 1) {
        return 0;
    }

    return 1;  // success
}

// you can use the same identiifer as a struct tag, member name, and variable
// name
union u {
    int u;
};

int samevar(void) {
    union u u = {100};
    if (u.u != 100) {
        return 0;
    }

    return 1;  // success
}

// you can use the same identifier as a union tag and function name
int f(void) {
    return 10;
}

union f {
    int f;
};

int samefun(void) {
    union f x;
    x.f = f();
    if (x.f != 10) {
        return 0;  // fail
    }

    return 1;  // success
}

int main(void) {
    if (!sharemem()) {
        return 1;  // fail
    }

    if (!samevar()) {
        return 2;  // fail
    }

    if (!samefun()) {
        return 3;  // fail
    }

    return 0;  // success
}
)PROG"));
}

// word/byte pointer punning comparison.
TEST_F(BookTest, Chapter18_CompareUnionPointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
// Pointers to a union object and to its members all compare equal
struct s {
    int i;
};

union u {
    char arr[3];
    double d;
    struct s my_struct;
};

union u my_union; // don't initialize, so it will be initialized to zero

int main(void) {
    union u* u_ptr = &my_union;

    // compare pointer to whole union w/ pointers to members,
    // using both == and !=
    if ((void*)u_ptr != (void*)&(u_ptr->arr)) {
        return 1; // fail
    }

    if (!((void*)u_ptr == (void*)&(u_ptr->d))) {
        return 2; // fail
    }

    if ((void*)&(u_ptr->my_struct) != u_ptr) {
        return 3; // fail
    }

    // compare member pointers
    if (my_union.arr != (char*)&my_union.d) {
        return 4; // fail
    }

    if (!(&my_union.arr[0] >= (char *) &my_union.my_struct.i)) {
        return 5; // fail
    }

    if (! ((char *) (&u_ptr->d) <= (char *) &u_ptr->my_struct)) {
        return 6; // fail
    }

    return 0;
}
)PROG"));
}
