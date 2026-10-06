//
// The signed-char versions of the "Writing a C Compiler" programs that read a plain
// char back as unsigned, for the targets where plain char is signed: x86-64 (as clang
// has it) and MMIX (as GCC has it).  The suite's own versions, in this directory, are
// skipped there.  Each is compared with clang or GCC, as the suite is.
//
#include "book_test.h"

// Static char initializers: 2147483777u in a plain char is -127.
TEST_F(BookTest, Chapter16_StaticInitializersSignedChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
/* Test that initializers for static objects with character type are correctly
 * converted to the correct type */

char from_long = 1099511627520l;        // low byte 0
char from_double = 15.6;
char from_uint = 2147483777u;            // low byte 129
char from_ulong = 281474976710410ul;     // low byte 10
signed char sc_long = 1099511627523l; // low byte 3
signed char sc_uint = 2147483898u;
signed char sc_ulong = 281474976710410ul; // low byte 10
signed char sc_dbl = 1e-10;
unsigned char uc_int = 13526;
unsigned char uc_uint = 2147483898u;
unsigned char uc_long = 1099511627770l;    // low byte 250
unsigned char uc_ulong = 281474976710410ul; // low byte 10
unsigned char uc_dbl = 77.7;

int main(void) {
    if (from_long != 0) return 1;
    if (from_double != 15) return 2;
    if (from_uint != -127) return 3;
    if (from_ulong != 10) return 4;
    if (sc_uint != -6) return 5;
    if (sc_ulong != 10) return 6;
    if (sc_dbl != 0) return 7;
    if (uc_int != 214) return 8;
    if (uc_uint != 250) return 9;
    if (uc_ulong != 10) return 10;
    if (uc_dbl != 77) return 11;
    if (sc_long != 3) return 12;
    if (uc_long != 250) return 13;
    return 0;
})"));
}

// Struct parameters of every class; 255 in a plain char member reads back as -1.
TEST_F(BookTest, Chapter18_ClassifyParamsSignedChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"PROG(
/* Test that we classify structure parameters correctly,
 * by passing a variety of structures as arguments.
 * Each test function takes only one argument.
 */
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
 */
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
 */
int test_twelve_bytes(struct twelve_bytes s) {
    if (s.i != 0 || strcmp(s.arr, "LMNOPQR")) {
        return 0;
    }
    return 1;  // success
}
int t_nints(struct nested_ints s) {
    if (s.ch1 != 127 || s.nested.i != 2147483647 || s.nested.ch2 != -1) {
        return 0;
    }
    return 1;  // success
}
int test_flattened_ints(struct flattened_ints s) {
    if (s.c != 127 || s.i != 2147483647 || s.a != -1) {
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

// Union initializers; -1-i in a plain char array element reads back as itself.
TEST_F(BookTest, Chapter18_UnionInitsSignedChar)
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
        if (ptr->str.u.arr[i] != -1 - i) {
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
