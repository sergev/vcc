//
// Chapter 16 — Characters and strings: valid programs compiled and run on
// BESM-6.  Imported from "Writing a C Compiler" (tests/chapter_16/valid:
// char_constants + chars + strings_as_initializers + strings_as_lvalues +
// extra_credit + libraries).  Each program defines int main(void); b6sim
// --status prints its return value, and we compare program output against the
// value computed by host cc.  The book's host-only "#if defined SUPPRESS_WARNINGS /
// #pragma" blocks are dropped (our scanner has no preprocessor); two-file
// "libraries" cases are merged into one source, client first.
//
// Char/string codegen (byte LOAD/STORE, fat char pointers, char-array init from
// string literals, char* arithmetic) is implemented, so most of the chapter is
// in range.  Two facts specific to this chapter drive the split, on top of the
// chapter 11-15 ones (narrow integers, no static-local storage, no shadowing):
//
//   * The static-data path repacks string-literal bytes to KOI-7, which folds
//     lowercase Latin onto uppercase and has no faithful glyph for '\' or '^'.
//     A byte read from a string literal therefore differs from the same lowercase
//     char constant ('c' is 67 from a string but 99 as a constant — char constants
//     keep their ASCII value), and printed lowercase renders as Cyrillic.  The
//     task-#16 "KOI-7-adapted" group below uses uppercase Latin (ASCII == KOI-7)
//     in its literals and expected output to sidestep this.
//   * libc provides printf/puts/strcmp/strlen but not atoi; programs needing atoi
//     stay DISABLED_ until it is added (task #22).
//
// Programs that stay within these limits are enabled run tests below; the rest
// are DISABLED_ (grouped at the bottom with a one-line reason each).  Most are
// not compiler bugs — they exercise x86 byte layout / 16-byte alignment / 64-bit
// or 32-bit-unsigned ranges / block-scope statics / lowercase output that BESM-6
// lacks.  Like chapters 11-15 the programs self-check and return an error code
// on mismatch, so a BESM-6-valued expectation would just encode a meaningless
// failure code; DISABLED_ is the honest call.
//
#include "book_test.h"



// --- char_constants ----------------------------------------------------------

// char_constants/return_char_constant: simplest character constant ('c' == 99).
TEST_F(BookTest, Chapter16_ReturnCharConstant)
{
    EXPECT_EQ("99\n", CompileAndRunBook(R"(/* Simplest possible test case for using a character constant */
int main(void) {
    return 'c'; // ASCII value 99
})"));
}


// char_constants/escape_sequences: parse escape sequences to the correct value.
TEST_F(BookTest, Chapter16_EscapeSequences)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we parse escape sequences to the correct value */
int main(void) {
    if ('\?' != 63) {
        return 1;
    }
    if ('\"' != 34) {
        return 2;
    }
    if ('\'' != 39) {
        return 3;
    }
    if ('\\' != 92) {
        return 4;
    }
    if ('\a' != 7) {
        return 5;
    }
    if ('\b' != 8) {
        return 6;
    }
    if ('\f' != 12) {
        return 7;
    }
    if ('\n' != 10) {
        return 8;
    }
    if ('\r' != 13) {
        return 9;
    }
    if ('\t' != 9) {
        return 10;
    }
    if ('\v' != 11) {
        return 11;
    }
    return 0;
})"));
}


// char_constants/control_characters: control chars in the source set.
// Raw VT/FF/TAB source bytes rewritten as \v/\f/\t escapes (same runtime value;
// raw-byte scanning is covered by scanner tests).
TEST_F(BookTest, Chapter16_ControlCharacters)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure we can handle control characters that are in the source character set */
int main(void)
{
    int tab = '\t';
    int vertical_tab = '\v';
    int form_feed = '\f';
    if (tab != '\t') {
        return 1;
    }
    if (vertical_tab != '\v') {
        return 2;
    }

    if (form_feed != '\f') {
        return 3;
    }

    return 0;
})"));
}


// --- chars -------------------------------------------------------------------

// chars/char_arguments: pass arguments of character type.
TEST_F(BookTest, Chapter16_CharArguments)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can pass arguments of character type */
int check_args(char a, signed char b, char c, unsigned char d, char e, char f, signed char g, char h) {
    char expected_a = 5;
    signed char expected_b = -12;
    char expected_c = 117;
    unsigned char expected_d = 254;
    char expected_e = 1;
    char expected_f = -20;
    signed char expected_g = 60;
    char expected_h = 100;

    if (expected_a != a) {
     return 1;
    }

    if (expected_b != b) {
     return 2;
    }

    if (expected_c != c) {
     return 3;
    }

    if (expected_d != d) {
     return 4;
    }

    if (expected_e != e) {
     return 5;
    }

    if (expected_f != f) {
     return 6;
    }

    if (expected_g != g) {
     return 7;
    }

    if (expected_h != h) {
     return 8;
    }

    return 0;
}

int main(void) {
    char a = 5;
    signed char b = -12;
    char c = 117;
    unsigned char d = 254;
    char e = 1;
    char f = -20;
    signed char g = 60;
    char h = 100;


    return check_args(a, b, c, d, e, f, g, h);
})"));
}


// chars/char_expressions: chars in arithmetic, comparison, pointer arith, logic.
TEST_F(BookTest, Chapter16_CharExpressions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can use chars in the same expressions as other integers */

int add_chars(char c1, char c2) {
    return c1 + c2;
}

int divide_chars(unsigned char c1, unsigned char c2) {
    return c1 / c2;
}

int le(char c1, char c2) {
    return c1 <= c2;
}

int subscript_char(int *ptr, char idx){
    return ptr[idx];
}

int *sub_char_from_pointer(int *ptr, signed char idx) {
    return ptr - idx;
}

int and_char(signed char c1, int i) {
    return c1 && i;
}

int or_char(signed char c1, unsigned char c2) {
    return c1 || c2;
}

int test_for_loop_char(void) {
    int counter = 0;
    for (signed char s = 127; s > 0; s = s - 1) {
        counter = counter + 1;
    }
    return (counter == 127);
}

int main(void) {

    char c1 = 8;
    char c2 = 4;
    if (add_chars(c1, c2) != 12)  {
        return 1;
    }

    unsigned char uc1 = 250;
    unsigned char uc2 = 25;
    if (divide_chars(uc1, uc2) != 10) {
        return 2;
    }

    if (le(c1, c2)) {
        return 3;
    }

    if (!le(c2, c2)) {
        return 4;
    }

    int arr[4] = {11, 12, 13, 14};
    char idx = 2;
    if (subscript_char(arr, idx) != 13) {
        return 5;
    }

    signed char offset = 1;
    if (sub_char_from_pointer(arr + 1, offset) != arr) {
        return 6;
    }

    signed char zero = 0;
    if (zero) {
        return 7;
    }

    if (and_char(zero, 12)) {
        return 8;
    }

    uc2 = 0;
    if (or_char(zero, uc2)) {
        return 9;
    }

    if (!test_for_loop_char()) {
        return 10;
    }

    return 0;
})"));
}


// chars/integer_promotion: character types promoted to int where required.
TEST_F(BookTest, Chapter16_IntegerPromotion)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we promote character types to integers when we're required to */

int add_chars(char c1, char c2, char c3) {
    return c1 + c2 + c3;
}

int negate(unsigned char uc) {
    return -uc;
}

int complement(unsigned char uc) {
    return ~uc;
}

int add_then_div(signed char a, signed char b, signed char c) {
    return (a + b) / c;
}

int mixed_multiply(signed char s, unsigned char u) {
    return s * u;
}

signed char decrement(signed char s) {
    s = s - 1;
    return s;
}

int main(void) {
    char a = 100;
    char b = 109;
    if (add_chars(a, a, b) != 309) {
        return 1;
    }

    unsigned char one = 1;
    if (negate(one) != -1) {
        return 2;
    }

    if (complement(one) != -2) {
        return 3;
    }

    signed char w = 127;
    signed char x = 3;
    signed char y = 2;
    if (add_then_div(w, x, y) != 65)
        return 4;

    signed char sc = -3;
    unsigned char uc = 250;
    if (mixed_multiply(sc, uc) != -750)
        return 5;

    sc = -128; // INT_MIN
    if (sc != -128) {
        return 6;
    }

    if (decrement(sc) != 127) {
        return 7;
    }

    return 0;
})"));
}


// chars/partial_initialization: unspecified char-array elements are zeroed.
TEST_F(BookTest, Chapter16_PartialInitialization)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that in arrays of character type, elements that aren't explicitly
 * initialized are zeroed out */

char static1[4] = {1, 2};
signed char static2[4] = {3, 4};
unsigned char static3[3] = {5};

int main(void)
{

    if (static1[0] != 1 || static1[1] != 2 || static1[2] || static1[3])
        return 1;

    if (static2[0] != 3 || static2[1] != 4 || static2[2] || static2[3])
        return 2;

    if (static3[0] != 5 || static3[1] || static3[2])
        return 3;

    signed char auto1[5] = {-4, 66, 4.0}; // plain char is unsigned on BESM-6; keep negatives signed
    signed char auto2[3] = {static1[2], -static1[0]};
    unsigned char auto3[2] = {'a'};

    if (auto1[0] != -4 || auto1[1] != 66 || auto1[2] != 4 || auto1[3] || auto1[4])
        return 4;

    if (auto2[0] || auto2[1] != -1 || auto2[2])
        return 5;

    if (auto3[0] != 'a' || auto3[1])
        return 6;

    return 0;
})"));
}


// chars/return_char: character return values don't clobber the stack.
TEST_F(BookTest, Chapter16_ReturnChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can call functions with return values of character type */

signed char return_char(void) { // plain char unsigned on BESM-6; keep signed truncation to -10
    return 5369233654l;  // this will be truncated to -10
}

signed char return_schar(void) {
    return 5369233654l;  // this will be truncated to -10
}

unsigned char return_uchar(void) {
    return 5369233654l;  // this will be truncated to 246
}

int main(void) {
    signed char char_array[3] = {121, -122, -3}; // plain char unsigned on BESM-6
    signed char retval_c = return_char();
    signed char char_array2[3] = {-5, 88, -100};
    signed char retval_sc = return_schar();
    char char_array3[3] = {10, 11, 12};
    unsigned char retval_uc = return_uchar();
    signed char char_array4[2] = {-5, -6};

    if (char_array[0] != 121 || char_array[1] != -122 || char_array[2] != -3) {
        return 1;
    }

    if (retval_c != -10) {
        return 2;
    }
    if (char_array2[0] != -5 || char_array2[1] != 88 ||
        char_array2[2] != -100) {
        return 3;
    }

    if (retval_sc != -10) {
        return 4;
    }
    if (char_array3[0] != 10 || char_array3[1] != 11 || char_array3[2] != 12) {
        return 5;
    }
    if (retval_uc != 246) {
        return 6;
    }
    if (char_array4[0] != -5 || char_array4[1] != -6) {
        return 7;
    }
    return 0;
})"));
}


// chars/type_specifiers: different ways to spell signed & unsigned char.
TEST_F(BookTest, Chapter16_TypeSpecifiers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// make sure we can parse different ways to specify signed & unsigned char

char signed static a = 10;
unsigned static char b = 20;
char c = 30;

int main(void)
{
    extern signed char a;
    char unsigned extern b;
    extern char c;
    if (a != 10) {
        return 1;
    }

    if (b != 20) {
        return 2;
    }

    if (c != 30) {
        return 3;
    }

    int loop_counter = 0;

    for (unsigned char d = 0; d < 100; d = d + 1) {
        loop_counter = loop_counter + 1;
    }

    if (loop_counter != 100) {
        return 4;
    }

    return 0;
})"));
}


// chars/chained_casts: chain multiple explicit casts together.
TEST_F(BookTest, Chapter16_ChainedCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test chaining multiple explicit casts together*/

// start with a global variable so we can't optimize away casts in Part III
unsigned int ui = 4294967200u;  // 2^32 - 96

int main(void) {
    ui = (unsigned int)(unsigned char)ui;
    if (ui != 160) {
        return 1;
    }

    int i = (int)(signed char)ui;
    if (i != -96) {
        return 2;
    }

    return 0;
})"));
}


// chars/rewrite_movz_regression: pure int arithmetic (movz angle is x86 only).
TEST_F(BookTest, Chapter16_RewriteMovzRegression)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int check_12_ints(int start, int a, int b, int c, int d, int e, int f, int g,
                  int h, int i, int j, int k, int l);

unsigned char glob = 5;

int main(void) {
    int should_spill = (int)glob;

    int one = glob - 4;
    int two = one + one;
    int three = 2 + one;
    int four = two * two;
    int five = 6 - one;
    int six = two * three;
    int seven = one + 6;
    int eight = two * 4;
    int nine = three * three;
    int ten = four + six;
    int eleven = 16 - five;
    int twelve = six + six;

    check_12_ints(one, two, three, four, five, six, seven, eight, nine, ten,
                  eleven, twelve, 1);
    int thirteen = 8 + glob;
    int fourteen = thirteen + 1;
    int fifteen = 28 - thirteen;
    int sixteen = fourteen + 2;
    int seventeen = 4 + thirteen;
    int eighteen = 32 - fourteen;
    int nineteen = 35 - sixteen;
    int twenty = fifteen + 5;
    int twenty_one = thirteen * 2 - 5;
    int twenty_two = fifteen + 7;
    int twenty_three = 6 + seventeen;
    int twenty_four = thirteen + 11;

    check_12_ints(thirteen, fourteen, fifteen, sixteen, seventeen, eighteen,
                  nineteen, twenty, twenty_one, twenty_two, twenty_three,
                  twenty_four, 13);
    if (should_spill != 5) {
        return -1;
    }
    return 0;  // success
}

int check_12_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int l, int start) {
    int expected = 0;

    expected = start + 0;
    if (a != expected) {
        return expected;
    }

    expected = start + 1;
    if (b != expected) {
        return expected;
    }

    expected = start + 2;
    if (c != expected) {
        return expected;
    }

    expected = start + 3;
    if (d != expected) {
        return expected;
    }

    expected = start + 4;
    if (e != expected) {
        return expected;
    }

    expected = start + 5;
    if (f != expected) {
        return expected;
    }

    expected = start + 6;
    if (g != expected) {
        return expected;
    }

    expected = start + 7;
    if (h != expected) {
        return expected;
    }

    expected = start + 8;
    if (i != expected) {
        return expected;
    }

    expected = start + 9;
    if (j != expected) {
        return expected;
    }

    expected = start + 10;
    if (k != expected) {
        return expected;
    }

    expected = start + 11;
    if (l != expected) {
        return expected;
    }

    return 0;  // success
})"));
}



// --- strings_as_initializers -------------------------------------------------

// strings_as_lvalues/empty_string: terminating null byte on the empty string.
TEST_F(BookTest, Chapter16_EmptyString)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we add a terminating null byte to the empty string */
int main(void) {
    char *empty = "";
    return empty[0];
})"));
}


// strings_as_lvalues/array_of_strings: array of pointers to strings (inline strcmp).
TEST_F(BookTest, Chapter16_ArrayOfStrings)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test declaring and operating on an array of pointers to strings */

int strcmp(char *s1, char *s2) {
    while (*s1 && *s1 == *s2) {
        s1 = s1 + 1;
        s2 = s2 + 1;
    }
    return *s1 - *s2;
}

int main(void) {
    char *strings[4] = {"yes", "no", "maybe"};
    if (strcmp(strings[0], "yes")) {
        return 1;
    }
    if (strcmp(strings[1], "no")) {
        return 2;
    }
    if (strcmp(strings[2], "maybe")) {
        return 3;
    }
    if (strings[3]) {
        return 4;
    }

    return 0;
})"));
}


// --- extra_credit ------------------------------------------------------------

// extra_credit/incr_decr_unsigned_chars: ++/-- on unsigned char lvalues.
TEST_F(BookTest, Chapter16_IncrDecrUnsignedChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Increment and decrement lvalues of unsigned character type
int main(void) {
    unsigned char chars[5] = {0, 2, 4, 253, 255};

    if (chars[0]--) {  // result is 0
        return 1;      // fail
    }

    if (chars[1]++ != 2) {
        return 2;  // fail
    }

    if (--chars[3] != 252) {
        return 3;
    }

    if (++chars[4] != 0) {  // wraps around
        return 4;
    }

    if (chars[0] != 255) {  // wraps around
        return 5;
    }

    if (chars[1] != 3) {
        return 6;
    }

    if (chars[2] != 4) {  // we didn't change this one
        return 7;
    }

    if (chars[3] != 252) {
        return 8;
    }

    if (chars[4]) {
        return 9;
    }

    return 0;  // success
})"));
}


// extra_credit/switch_on_char_const: character constant as switch controller.
TEST_F(BookTest, Chapter16_SwitchOnCharConst)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we can use character constant in switch statement
int main(void) {
    switch ('x') {
        case 1:
            return 1;  // fail
        case 2:
            return 2;  // fail
        case 120:
            return 0;  // success
        default:
            return -1;  // fail
    }
})"));
}


// extra_credit/promote_switch_cond: switch controller promoted char -> int.
TEST_F(BookTest, Chapter16_PromoteSwitchCond)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Make sure we promote the controlling condition in a switch statement from
// character type to int

int main(void) {
    char c = 100;
    switch (c) {
        case 0:
            return 1;
        case 100:
            return 0;
        // not a duplicate of 100, b/c we're not converting cases to char type
        case 356:
            return 2;
        default:
            return 3;
    }
})"));
}


// extra_credit/promote_switch_cond_2: case labels stay int, not truncated to char.
TEST_F(BookTest, Chapter16_PromoteSwitchCond2)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Another test that we promote switch controlling condition to integer type
int main(void) {
    char c = -56;
    switch (c) {
        // if we reduced this to a char it would be -56
        // but we won't, so this case shouldn't be taken
        case 33554632:
            return 1;  // fail
        default:
            return 0;
    }
})"));
}



// --- libraries (two files merged, client first) ------------------------------

// libraries/char_arguments: pass character-type arguments across translation units.
TEST_F(BookTest, Chapter16_LibCharArguments)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int check_args(char a, signed char b, char c, unsigned char d, char e, char f, signed char g, char h);

int main(void) {
    char a = 5;
    signed char b = -12;
    char c = 117;
    unsigned char d = 254;
    char e = 1;
    char f = -20;
    signed char g = 60;
    char h = 100;


    return check_args(a, b, c, d, e, f, g, h);
}

int check_args(char a, signed char b, char c, unsigned char d, char e, char f, signed char g, char h) {
    char expected_a = 5;
    signed char expected_b = -12;
    char expected_c = 117;
    unsigned char expected_d = 254;
    char expected_e = 1;
    char expected_f = -20;
    signed char expected_g = 60;
    char expected_h = 100;

    if (expected_a != a) {
     return 1;
    }

    if (expected_b != b) {
     return 2;
    }

    if (expected_c != c) {
     return 3;
    }

    if (expected_d != d) {
     return 4;
    }

    if (expected_e != e) {
     return 5;
    }

    if (expected_f != f) {
     return 6;
    }

    if (expected_g != g) {
     return 7;
    }

    if (expected_h != h) {
     return 8;
    }

    return 0;
})"));
}


// libraries/global_char: access global objects of character type across TUs.
TEST_F(BookTest, Chapter16_LibGlobalChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(extern char c;
extern unsigned char uc;
extern signed char sc;

int update_global_chars(void);

int main(void) {
    if (c != 100) {
        return 1;
    }

    if (uc != 250) {
        return 2;
    }

    if (sc != 0) {
        return 3;
    }

    update_global_chars();

    if (c != 110) {
        return 4;
    }

    if (uc != 4) {
        return 5;
    }

    if (sc != -10) {
        return 6;
    }

    return 0;
}

char c = 100;
unsigned char uc = 250;
signed char sc = 0;

int update_global_chars(void) {
    c = c + 10;
    uc = uc + 10; // wraps around
    sc = sc - 10;
    return 0;
})"));
}


// libraries/return_char: character return values across translation units.
TEST_F(BookTest, Chapter16_LibReturnChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(signed char return_char(void);
signed char return_schar(void);

unsigned char return_uchar(void);
int main(void) {
    signed char char_array[3] = {121, -122, -3}; // plain char unsigned on BESM-6
    signed char retval_c = return_char();
    signed char char_array2[3] = {-5, 88, -100};
    signed char retval_sc = return_schar();
    char char_array3[3] = {10, 11, 12};
    unsigned char retval_uc = return_uchar();
    signed char char_array4[2] = {-5, -6};

    if (char_array[0] != 121 || char_array[1] != -122 || char_array[2] != -3) {
        return 1;
    }

    if (retval_c != -10) {
        return 2;
    }
    if (char_array2[0] != -5 || char_array2[1] != 88 ||
        char_array2[2] != -100) {
        return 3;
    }

    if (retval_sc != -10) {
        return 4;
    }
    if (char_array3[0] != 10 || char_array3[1] != 11 || char_array3[2] != 12) {
        return 5;
    }
    if (retval_uc != 246) {
        return 6;
    }
    if (char_array4[0] != -5 || char_array4[1] != -6) {
        return 7;
    }
    return 0;
}

signed char return_char(void) { // plain char unsigned on BESM-6; keep signed truncation to -10
    return 5369233654l;  // this will be truncated to -10
}

signed char return_schar(void) {
    return 5369233654l;  // this will be truncated to -10
}

unsigned char return_uchar(void) {
    return 5369233654l;  // this will be truncated to 246
})"));
}



// char_constants/char_constant_operations: local `double d` shadowed file-scope `d`
// (renamed the local to `d2`).
TEST_F(BookTest, Chapter16_CharConstantOperations)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we treat character constants like integers */

// use character constants to initialize static variables of any arithmetic type
double d = '\\'; // ASCII value 92

int main(void) {

    if (d != 92.0) {
        return 1;
    }

    // You can use character constants to specify array dimensions
    // and initialize array elements of arithmetic type
    unsigned long array['\n'] = {1, 2, 'a', '\b', 3, 4, 5, '!', '%', '~'};

    if (array[2] != 97) {
        return 2;
    }

    if (array[3] != 8) {
        return 3;
    }

    if (array[7] != 33) {
        return 4;
    }

    if (array[8] != 37) {
        return 5;
    }

    if (array[9] != 126) {
        return 6;
    }

    // make sure array has the right length (20) by initializing a pointer to its address
    // if we've got the wrong size this will be a type error
    unsigned long (*array_ptr)[10] = &array;

    if (array_ptr[0][9] != '~') {
        return 7;
    }

    // You can use character constants in subscript expressions
    int i = array['\a']; // ASCII value of \a is 7
    if (i != 33) {
        return 8;
    }

    // You can use character constants in arithmetic expressions
    double d2 = 10 % '\a' + 4.0 * '_' - ~'@'; // 10 % 7 + 4.0 * 95 - ~64

    if (d2 != 448.0) {
        return 9;
    }
    return 0;
})"));
}


// ===========================================================================
// KOI-7-adapted string/char tests (task #16).  The BESM-6 static path repacks
// string-literal bytes to KOI-7, which folds lowercase Latin to uppercase and
// renders printed lowercase as Cyrillic.  These programs use uppercase Latin
// (ASCII == KOI-7) in their string literals, char constants, and expected output
// so the BESM-6 result matches a clean ASCII expectation.
// ===========================================================================

// strings_as_initializers/simple: chars[2] of "ABC" is 'C' == 67.
TEST_F(BookTest, Chapter16_StringInitSimple)
{
    EXPECT_EQ("67\n", CompileAndRunBook(R"(int main(void) {
    // simple test of initializing and subscripting char array
    unsigned char chars[4] = "ABC";
    return chars[2];
})"));
}

// strings_as_lvalues/simple: "HELLO, WORLD!"[2] is 'L' == 76.
TEST_F(BookTest, Chapter16_StringLvalueSimple)
{
    EXPECT_EQ("76\n", CompileAndRunBook(R"(int main(void) {
    char *x = "HELLO, WORLD!";
    return x[2];
})"));
}

// strings_as_lvalues/pointer_operations: pointer arithmetic and subscripting.
TEST_F(BookTest, Chapter16_PointerOperations)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test standard pointer operations on string literals
 * including pointer arithmetic and subscripting.
 */

int main(void) {
    if ("ABCDEFG"[2] != 'C') {
        return 1;
    }

    char *ptr = "THIS IS A STRING!" + 10;  // point to "STRING!"
    if (*ptr != 'S') {
        return 2;
    }

    if (ptr[6] != '!') {
        return 3;
    }

    if (ptr[7]) {  // null byte
        return 4;
    }

    if (!"NOT A NULL POINTER!") {
        return 5;
    }
})"));
}

// strings_as_lvalues/cast_string_pointer: casts from char * to other char pointers.
TEST_F(BookTest, Chapter16_CastStringPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test casts from char * to other character pointer types */

int main(void) {
    char *c = "THIS IS A STRING!";
    unsigned char *uc = (unsigned char *)c;
    if (uc[3] != 'S') {
        return 1;
    }
    signed char *sc = (signed char *)c;
    if (sc[3] != 'S'){
            return 2;
        }
    return 0;
})"));
}

// strings_as_lvalues/strings_in_function_calls: strings as args/return values (libc strlen).
TEST_F(BookTest, Chapter16_StringsInFunctionCalls)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can use strings literals as function arguments/return values */

unsigned long strlen(char *s);

char *return_string(void) {
    // constant strings have static storage duration,
    // so this will persist after the function call;
    return "I'M A STRING!";
}

int pass_string_args(char *s1, char *s2) {
    // neither should be a null pointer
    if (s1 == 0 || s2 == 0) {
        return 0;
    }

    if (strlen(s1) != 45) {
        return 0;
    }

    if (s1[41] != 'D' || s1[42] != 'O' || s1[43] != 'G') {
        return 0;
    }

    // s2 is an empty string so first byte should be null
    if (s2[0]) {
        return 0;
    }

    return 1;  // success
}

int main(void) {
    char *ptr = 0;
    // call return_string and inspect results
    ptr = return_string();
    if (!ptr)
        return 1;

    if (ptr[0] != 'I' || ptr[1] != '\'' || ptr[13]) {
        return 2;
    }

    // pass strings as function arguments
    if (!pass_string_args("THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.",
                          "")) {
        return 3;
    }

    return 0;

    char *ptr2;
    ptr2 = 1 ? ptr + 2 : ptr + 4;
    return *ptr2 == 'M';
})"));
}

// strings_as_initializers/array_init_special_chars: char special[6] = "...".
// Escape sequences round-trip through KOI-7 unchanged, and local char-array
// initialization from a string literal now emits per-byte stores, so this needs
// no value change — it was blocked only by the (now-fixed) translator gap.
TEST_F(BookTest, Chapter16_ArrayInitSpecialChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can handle escape sequences in string literals */
int main(void) {
    // a mix of escaped and unescaped special characters
    char special[6] = "\a\b\n\v\f\t";

    if (special[0] != '\a') {
        return 1;
    }

    if (special[1] != '\b') {
        return 2;
    }

    if (special[2] != '\n') {
        return 3;
    }
    if (special[3] != '\v') {
        return 4;
    }
    if (special[4] != '\f') {
        return 5;
    }

    if (special[5] != '\t') {
        return 6;
    }

    return 0;
})"));
}

// strings_as_initializers/write_to_array: write to a flat and a nested char array.
TEST_F(BookTest, Chapter16_WriteToArray)
{
    EXPECT_EQ("ABC\nABX\nHELLO\nWORLD\nJELLO\n0\n", CompileAndRunBook(R"(// Test writing to a char array

int puts(char *s);

int main(void) {
    // start with a flat array
    char flat_arr[4] = "ABC";
    puts(flat_arr);

    // update it
    flat_arr[2] = 'X';
    puts(flat_arr);

    // similar test with nested array
    char nested_array[2][6] = {"HELLO", "WORLD"};
    puts(nested_array[0]);
    puts(nested_array[1]);

    nested_array[0][0] = 'J';
    puts(nested_array[0]);

    return 0;
})"));
}

// strings_as_lvalues/string_special_characters: special characters in string literals.
// The book's backslash and caret cases are dropped: KOI-7 does not round-trip
// '\\' (0x5C->0x1D) or '^' (0x5E->0x5C), so neither the byte compare nor the
// printed output could match.
TEST_F(BookTest, Chapter16_StringSpecialCharacters)
{
    EXPECT_EQ("HELLO\"WORLD\nLINE\nBREAK!\nTESTING, 123.\n0\n",
              CompileAndRunBook(R"(/* Test that we can handle special characters in string literals
 * that are not array initializers
 */

int puts(char *s);
int strcmp(char *s1, char *s2);

int main(void) {
    // string literal containing escape sequences
    char *escape_sequence = "\a\b";
    if (escape_sequence[0] != 7) {
        return 1;
    }

    if (escape_sequence[1] != 8) {
        return 2;
    }

    if (escape_sequence[2]) {// check for terminating null byte
        return 3;
    }

    // double quote
    char *with_double_quote = "HELLO\"WORLD";
    if (with_double_quote[5] != '"') {
        return 4;
    }
    puts(with_double_quote);

    char *with_newline = "LINE\nBREAK!";
    if (with_newline[4] != 10) {
        return 6;
    }
    puts(with_newline);

    // literal tab
    char *tab = "\t";
    if (strcmp(tab, "\t")) {
        return 7;
    }

    puts("TESTING, 123.");

    return 0;
})"));
}

// strings_as_lvalues/addr_of_string: take the address of a string literal.
TEST_F(BookTest, Chapter16_AddrOfString)
{
    EXPECT_EQ("SAMPLE\tSTRING!\n\n0\n", CompileAndRunBook(R"(/* Test that we can take the address of a string literal and annotate it with the correct type */

int puts(char *s);

int main(void) {
    char(*str)[16] = &"SAMPLE\tSTRING!\n";
    puts(*str);

    // get pointer to one-past-the-end of this string
    char (*one_past_the_end)[16] = str + 1;
    char *last_byte_pointer = (char *)one_past_the_end - 1; // now get pointer to the last byte
    if (*last_byte_pointer != 0) {
        return 1;
    }
    return 0;
})"));
}


// ===========================================================================
// Multi-dimensional char arrays (sub-word row pointers).  Indexing a row of a
// packed char array yields a fat byte pointer into the middle of a word; the
// translator decays a char-innermost array of any rank to a fat pointer and
// pre-scales the index by the row byte size, so ADD_PTR runs at scale 1.
// ===========================================================================

// strings_as_initializers/literals_and_compound_initializers: signed char[3][4].
// Uppercase Latin: an automatic char array keeps its source ASCII bytes while a string
// constant repacks to KOI-7, and the two encodings coincide only for uppercase.
TEST_F(BookTest, Chapter16_LiteralsAndCompoundInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* make sure we can use a mix of string literals and compound initializers to
 * initialize a single nested array */

// array wih static storage duration
signed char static_array[3][4] = {{'A', 'B', 'C', 'D'}, "EFGH", "IJK"};

int main(void) {
    // array with automatic storage duration
    unsigned char auto_array[2][3] = {"LMN", {'O', 'P'}};

    // validate static array
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 4; j = j + 1)
            if (static_array[i][j] != "ABCDEFGHIJK"[i * 4 + j])
                return 1;

    // validate automatic array
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            if (auto_array[i][j] != "LMNOP"[i * 3 + j])
                return 2;

    return 0;
})"));
}

// strings_as_initializers/adjacent_strings_in_initializer: char[2][3] nested.
// Uppercase Latin: a static char array repacks to KOI-7 while the comparison
// string literals stay ASCII, and the two encodings coincide only for uppercase.
TEST_F(BookTest, Chapter16_AdjacentStringsInInitializer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Make sure the parser concatenates adjacent string literals */

int strcmp(char *s1, char *s2);  // from standard library

int main(void) {
    char multi_string[6] =
        "YES"
        "NO";  // can concatenate two string literals in an initializer
    char nested_multi_string[2][3] = {
        "A"
        "B",
        "C"
        "D"};  // first element is "AB", second is "CD"

    // validate multi_string
    if (strcmp(multi_string, "YESNO"))
        return 1;
    if (strcmp(nested_multi_string[0], "AB"))
        return 2;
    if (strcmp(nested_multi_string[1], "CD"))
        return 3;
    return 0;
})"));
}

// strings_as_initializers/transfer_by_eightbyte: char[2][13].
// Uppercase Latin: an automatic char array keeps its source ASCII bytes while a string
// constant repacks to KOI-7, and the two encodings coincide only for uppercase.
TEST_F(BookTest, Chapter16_TransferByEightbyte)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that when we initialize an array whose size isn't divisible by 4 or 8,
 * we don't overrun neighboring memory
 */

int strcmp(char *s1, char *s2);

int main(void) {
    char strings[2][13] = {"ABCDEFGHIJKL", "Z"};
    if (strcmp(strings[0], "ABCDEFGHIJKL"))
        return 1;

    if (strings[1][0] != 'Z')
        return 2;

    // remaining bytes should be 0
    for (int i = 1; i < 13; i = i + 1) {
        if (strings[1][i])
            return 3;
    }
    return 0;
})"));
}


// --- Static locals (now supported) + remaining besm6 gaps -------------------
// Block-scope statics work now; tests still DISABLED_ here have a separate blocker
// (values beyond the BESM-6 integer range, narrow-char/charset semantics, or
// multi-dimensional char-array sub-word scaling).

// chars/explicit_casts: the static-local + 8-char-collision + 41-bit-value parts are
// adapted here (helpers renamed to short forms like `c2uc`/`sc2ui` because the book names
// all collided in Madlen's 8-char limit; `sc2ui` uses the BESM-6 41-bit 2^41-10 value,
// task #14; the `static long *null_ptr` cast works).  The `(double)(unsigned char)` bit-7
// conversion bug (task #30) is fixed: a sub-word integer source is now promoted to a full
// word before the int->FP conversion.
TEST_F(BookTest, Chapter16_ExplicitCasts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test explicit conversions to and from character types */

unsigned char c2uc(char c) { return (unsigned char)c; }
signed char c2sc(char c) { return (signed char)c; }
signed char uc2c(unsigned char u) { return (signed char)u; } // plain char unsigned on BESM-6
signed char uc2sc(unsigned char u) { return (signed char)u; }
unsigned char sc2uc(signed char u) { return (unsigned char)u; }
int c2i(char c) { return (int)c; }
unsigned long c2ul(char c) { return (unsigned long)c; }
long sc2l(signed char s) { return (long)s; }
unsigned int sc2ui(signed char s) { return (unsigned int)s; }
double sc2d(signed char s) { return (double)s; }
int uc2i(unsigned char u) { return (int)u; }
unsigned int uc2ui(unsigned char u) { return (unsigned int)u; }
long uc2l(unsigned char u) { return (long)u; }
unsigned long uc2ul(unsigned char u) { return (unsigned long)u; }
double uc2d(unsigned char u) { return (double)u; }
char i2c(int i) { return (char)i; }
char ui2c(unsigned int u) { return (char)u; }
signed char d2c(double d) { return (signed char)d; } // plain char unsigned on BESM-6: (char)negative-double is UB
signed char ul2sc(unsigned long l) { return (signed char)l; }
unsigned char i2uc(int i) { return (unsigned char)i; }
unsigned char ui2uc(unsigned int ui) { return (unsigned char)ui; }
unsigned char l2uc(long l) { return (unsigned char)l; }
unsigned char ul2uc(unsigned long ul) { return (unsigned char)ul; }
unsigned char d2uc(double d) { return (unsigned char)d; }
signed char l2sc(long l) { return (signed char)l; }

int main(void) {
    char c = 127;
    if (c2uc(c) != 127) return 1;
    if (c2i(c) != 127) return 2;
    if (c2ul(c) != 127) return 3;

    signed char sc = -10;
    if (sc2uc(sc) != 246) return 4;
    if (sc2l(sc) != -10) return 5;
    if (sc2ui(sc) != 2199023255542u) return 6; // (unsigned int)(-10) = 2^41-10 on BESM-6
    if (sc2d(sc) != -10.0) return 7;

    unsigned char uc = 250;
    if (uc2i(uc) != 250) return 8;
    if (uc2l(uc) != 250) return 9;
    if (uc2ui(uc) != 250) return 10;
    if (uc2ul(uc) != 250) return 11;
    if (uc2d(uc) != 250.0) return 12;
    if (uc2sc(uc) != -6) return 13;
    if (uc2c(uc) != -6) return 14;

    c = (char)-128;
    if (i2c(128) != c) return 15;
    c = (char)-6;
    if (ui2c(2147483898u) != c) return 16;
    if (d2c(-2.6) != -2) return 17; // d2c returns signed char on BESM-6

    if (l2sc(1099511627520l)) return 18; // low byte 0
    sc = (signed char)-126;
    if (ul2sc(281474976710530ul) != sc) return 19; // low byte 130

    uc = (unsigned char)200;
    if (i2uc(-1234488) != uc) return 20;
    if (ui2uc(4293732808) != uc) return 21;
    if (l2uc(1099511627720l) != uc) return 22; // low byte 200
    if (ul2uc(281474976710600ul) != uc) return 23; // low byte 200
    if (d2uc(200.99) != uc) return 24;

    static long *null_ptr;
    char zero = (char)null_ptr;
    if (zero) return 25;

    c = 32;
    int *i = (int *)c;
    if ((char)i != c) return 26;

    if ((char)300 != (char)44) return 27;

    return 0;
})"));
}

// chars/convert_by_assignment: out-of-range source values are replaced with
// in-range ones that keep the same low byte (the task #11 "value parts").  Helpers
// whose names collide in Madlen's 8-char limit were renamed: `check_char_on_stack`
// (vs `check_char` → `check_ch`) → `check_stk`, and the `return_extended_uchar` /
// `return_extended_schar` / `return_truncated_long` trio (the first two both →
// `return_e`, and even `ret_ext_uc`/`ret_ext_sc` still share `ret_ext_`) → the
// 8-char-distinct `rxt_uc`/`rxt_sc`/`rtrunc`.  The `check_uint` expectation uses the
// BESM-6 41-bit unsigned value (2^41-10) instead of x86's 2^32-10 (task #14).  Plain
// char is unsigned on BESM-6, so the negative-valued `array` is declared `signed char`.
TEST_F(BookTest, Chapter16_ConvertByAssignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test implicit conversions to and from character types as if by assignment. */

int check_int(int converted, int expected) { return (converted == expected); }
int check_uint(unsigned int converted, unsigned int expected) { return (converted == expected); }
int check_long(long converted, long expected) { return (converted == expected); }
int check_ulong(unsigned long converted, unsigned long expected) { return (converted == expected); }
int check_double(double converted, double expected) { return (converted == expected); }
int check_char(char converted, char expected) { return (converted == expected); }
int check_uchar(unsigned char converted, unsigned char expected) { return (converted == expected); }
int check_stk(signed char expected, int dummy1, int dummy2, int dummy3,
                        int dummy4, int dummy5, int dummy6, signed char converted) {
    return converted == expected;
}

int rxt_uc(unsigned char c) { return c; }
unsigned long rxt_sc(signed char sc) { return sc; }
unsigned char rtrunc(long l) { return l; }

int main(void) {
    signed char sc = -10;
    if (!check_long(sc, -10l)) return 1;
    if (!check_uint(sc, 2199023255542u)) return 2; // (unsigned int)(-10) = 2^41-10 on BESM-6
    if (!check_double(sc, -10.0)) return 3;

    unsigned char uc = 246;
    if (!check_uchar(sc, uc)) return 4;

    char c = -10;
    if (!check_char(-10, c)) return 5;
    if (!check_char(4294967286u, c)) return 6;
    // if (!check_char(-10.0, c)) return 7; -- undefined behavior on ARM
    if (!check_stk(c, 0, 0, 0, 0, 0, 0, -10.0)) return 8;

    if (!check_int(uc, 246)) return 9;
    if (!check_ulong(uc, 246ul)) return 10;
    char expected_char = -10;
    if (!check_char(uc, expected_char)) return 11;

    if (!check_uchar(281474976710646ul, uc)) return 12; // low byte 246

    if (rxt_uc(uc) != 246) return 13;
    if (rxt_sc(sc) != 2199023255542ul) return 14; // (ulong)(-10)=2^41-10
    if (rtrunc(5369233654l) != uc) return 15;

    signed char array[3] = {0, 0, 0}; // plain char unsigned on BESM-6; keep signed wrap
    array[1] = 128;
    if (array[0] || array[2] || array[1] != -128) return 16;
    array[1] = 281474976710530ul; // low byte 130 (was 9.2e18)
    if (array[0] || array[2] || array[1] != -126) return 17;
    array[1] = -2.6;
    if (array[0] || array[2] || array[1] != -2) return 18;

    unsigned char uchar_array[3] = {0, 0, 0};
    uchar_array[1] = 1099511627520l; // low byte 0 (was 2^44)
    if (uchar_array[0] || uchar_array[2] || uchar_array[1] != 0) return 19;
    uchar_array[1] = 2147483898u;
    if (uchar_array[0] || uchar_array[2] || uchar_array[1] != 250) return 20;

    unsigned int ui = 4294967295U;
    static unsigned char uc_static;
    ui = uc_static;
    if (ui) return 21;

    signed long l = -1;
    static signed s_static = 0;
    l = s_static;
    if (l) return 22;

    return 0;
})"));
}

// strings_as_initializers/terminating_null_bytes: static flat/nested arrays.  Multi-
// dimensional char-array initialization and indexing now work (task #5).  The book's
// lowercase letters were upper-cased: the static-data path packs strings as KOI-7, which
// folds lowercase Latin to uppercase codes while char literals stay ASCII, so lowercase
// `arr[i] == 'a'` would compare unequal (see docs/KOI7_Encoding.md); the file-scope
// `nested` shadow was already resolved by renaming the locals.
TEST_F(BookTest, Chapter16_TerminatingNullBytes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
/* When we initialize an array from a string literal,
 * make sure we include the null byte if the array has space for it,
 * and exclude it otherwise
 * */

int strcmp(char *s1, char *s2);  // standard library

int test_flat_static_with_null_byte(void) {
    static unsigned char flat[4] = "DOG";
    return (flat[0] == 'D' && flat[1] == 'O' && flat[2] == 'G' && flat[3] == 0);
}

int test_nested_static_with_null_byte(void) {
    // Renamed from `nested` to avoid shadowing the file-scope `nested` below
    // (no-shadowing is a permanent design decision); behaviour is unchanged.
    static char nested_s[2][4] = {"YES", "YUP"};
    return (nested_s[0][0] == 'Y' && nested_s[0][1] == 'E' && nested_s[0][2] == 'S' &&
            nested_s[0][3] == 0 && nested_s[1][0] == 'Y' && nested_s[1][1] == 'U' &&
            nested_s[1][2] == 'P' && nested_s[1][3] == 0);
}

int test_flat_auto_with_null_byte(void) {
    char flat_auto[2] = "X";
    return (flat_auto[0] == 'X' && flat_auto[1] == 0);
}

int test_nested_auto_with_null_byte(void) {
    char nested_auto[2][2][2] = {{"A", "B"}, {"C", "D"}};
    return (nested_auto[0][0][0] == 'A' && nested_auto[0][0][1] == 0 &&
            nested_auto[0][1][0] == 'B' && nested_auto[0][1][1] == 0 &&
            nested_auto[1][0][0] == 'C' && nested_auto[1][0][1] == 0 &&
            nested_auto[1][1][0] == 'D' && nested_auto[1][1][1] == 0);
}

int test_flat_static_without_null_byte(void) {
    static char letters[4] = "ABCD";
    return letters[0] == 'A' && letters[1] == 'B' && letters[2] == 'C' &&
           letters[3] == 'D';
}

char nested[3][3] = {"YES", "NO", "OK"};
int test_nested_static_without_null_byte(void) {
    char *whole_array = (char *)nested;
    char *word1 = (char *)nested[0];
    char *word2 = (char *)nested[1];
    char *word3 = (char *)nested[2];
    return !(strcmp(whole_array, "YESNO") || strcmp(word1, "YESNO") ||
             strcmp(word2, "NO") || strcmp(word3, "OK"));
}

int test_flat_auto_without_null_byte(void) {
    int x = -1;
    char letters[4] = "ABCD";
    int y = -1;
    return (x == -1 && y == -1 && letters[0] == 'A' && letters[1] == 'B' &&
            letters[2] == 'C' && letters[3] == 'D');
}

int test_nested_auto_without_null_byte(void) {
    // Renamed from `nested` to avoid shadowing the file-scope `nested` (no-shadowing
    // is a permanent design decision); behaviour is unchanged.
    char nested_a[3][3] = {"YES", "NO", "OK"};
    char *whole_array = (char *)nested_a;
    char *word1 = (char *)nested_a[0];
    char *word2 = (char *)nested_a[1];
    char *word3 = (char *)nested_a[2];
    return !(strcmp(whole_array, "YESNO") || strcmp(word1, "YESNO") ||
             strcmp(word2, "NO") || strcmp(word3, "OK"));
}

int main(void) {
    if (!test_flat_static_with_null_byte()) return 1;
    if (!test_nested_static_with_null_byte()) return 2;
    if (!test_flat_auto_with_null_byte()) return 3;
    if (!test_nested_auto_with_null_byte()) return 4;
    if (!test_flat_static_without_null_byte()) return 5;
    if (!test_nested_static_without_null_byte()) return 6;
    if (!test_flat_auto_without_null_byte()) return 7;
    if (!test_nested_auto_without_null_byte()) return 8;
    return 0;
})"));
}

// strings_as_initializers/partial_initialize_via_string: static arrays in fns.  Enabled
// with task #5 (multi-dim char arrays); the book's lowercase letters were upper-cased so the
// KOI-7-packed static data matches the ASCII char literals (see docs/KOI7_Encoding.md).
TEST_F(BookTest, Chapter16_PartialInitializeViaString)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
/* Test that when we initialize an array from a string literal,
 * we zero out elements that aren'T EXPLICITLY INITIALIZED.
 * */

static char static_arr[5] = "HI";
int test_static(void) {
    return (static_arr[0] == 'H' && static_arr[1] == 'I' &&
            !(static_arr[2] || static_arr[3] || static_arr[4]));
}

static signed char nested_static_arr[3][4] = {"", "BC"};
int test_static_nested(void) {
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 4; j = j + 1) {
            signed char c = nested_static_arr[i][j];
            signed char expected = 0;
            if (i == 1 && j == 0) {
                expected = 'B';
            } else if (i == 1 && j == 1) {
                expected = 'C';
            }
            if (c != expected) {
                return 0;
            }
        }
    return 1;
}

int test_automatic(void) {
    unsigned char aut[4] = "AB";
    return (aut[0] == 'A' && aut[1] == 'B' && !(aut[2] || aut[3]));
}

int test_automatic_nested(void) {
    signed char nested_auto[2][2][4] = {{"FOO"}, {"X", "YZ"}};
    for (int i = 0; i < 2; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) {
            for (int k = 0; k < 4; k = k + 1) {
                signed char c = nested_auto[i][j][k];
                signed char expected = 0;
                if (i == 0 && j == 0) {
                    if (k == 0) {
                        expected = 'F';
                    } else if (k == 1 || k == 2) {
                        expected = 'O';
                    }
                } else if (i == 1 && j == 0 && k == 0) {
                    expected = 'X';
                } else if (i == 1 && j == 1 && k == 0) {
                    expected = 'Y';
                } else if (i == 1 && j == 1 && k == 1) {
                    expected = 'Z';
                }
                if (c != expected) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

int main(void) {
    if (!test_static()) return 1;
    if (!test_static_nested()) return 2;
    if (!test_automatic()) return 3;
    if (!test_automatic_nested()) return 4;
    return 0;
})"));
}

// extra_credit/incr_decr_chars: static char chars[5].
TEST_F(BookTest, Chapter16_IncrDecrChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Increment and decrement lvalues of character type
int main(void) {
    static signed char chars[5] = {123, 124, 125, 126, 127}; // plain char unsigned on BESM-6
    if (chars[0]++ != 123) {
        return 1;  // fail
    }

    if (chars[1]-- != 124) {
        return 2;  // fail
    }

    if (++chars[2] != 126) {
        return 3;  // fail
    }

    if (--chars[3] != 125) {
        return 4;  // fail
    }

    if (++chars[4] != -128) {
        return 5;  // fail
    }

    if (chars[0] != 124) {
        return 6;  // fail
    }

    if (chars[1] != 123) {
        return 7;  // fail
    }
    if (chars[2] != 126) {
        return 8;  // fail
    }
    if (chars[3] != 125) {
        return 9;  // fail
    }
    if (chars[4] != -128) {
        return 10;  // fail
    }

    signed char c = -128;
    c--;
    if (c != 127) {
        return 11;  // fail
    }

    return 0;  // success
})"));
}

// extra_credit/char_consts_as_cases: static int i.
TEST_F(BookTest, Chapter16_CharConstsAsCases)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we can use character constants as cases in switch statements
int main(void) {
    static int i = 65;
    switch (i) {
        case 100l:
            return 1;  // fail
        case 'A':
            return 0;  // success
        case 'B':
            return 2;  // fail
        case 2000u:
            return 3;  // fail
        default:
            return -1;  // fail
    }
})"));
}

// extra_credit/compound_assign_chars: static char/uchar/schar.  Exercises C integer
// promotions in compound assignment: an arithmetic compound op (`+=`, `-=`, `/=`, `*=`,
// `%=`) on a narrow-type lvalue is computed in get_common_type(lhs, rhs) (which promotes
// the narrow lvalue to int/double) and the result narrowed back — e.g.
// `unsigned char uc=200; uc /= (char)-100` is (unsigned char)((int)200 / (int)-100) = 254.
// Fixed by typecheck promoting the rhs to the common type plus the translator widening the
// lvalue and narrowing the result around the op (task #29).
TEST_F(BookTest, Chapter16_CompoundAssignChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test compound assignment with characters; make sure we perform integer promotions

int main(void) {

    static signed char c = 100; // plain char unsigned on BESM-6; keep signed wrap
    signed char c2 = 100;
    c += c2; // well-defined b/c of integer promotions
    if (c != -56) {
        return 1; // fail
    }

    static unsigned char uc = 200;
    c2 = -100;
    uc /= c2; // convert uc and c2 to int, then convert back
    if (uc != 254) {
        return 2; // fail
    }

    uc -= 250.0; // convert uc to double, do operation, convert back
    if (uc != 4) {
         return 3;  // fail
    }

    static signed char sc = 70;
    sc = -sc;
    sc *= c;
    if (sc != 80) {
        return 4; // fail
    }

    if ((sc %= c) != 24) {
        return 5;
    }

    return 0;
})"));
}

// extra_credit/compound_bitwise_ops_chars: static long x.
TEST_F(BookTest, Chapter16_CompoundBitwiseOpsChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test bitwise compound assignment operators with character types

int main(void) {
    signed char arr[5] = {-128, -120, -2, 1, 120};
    unsigned char u_arr[4] = {0, 170, 250, 255};

    arr[0] ^= 12345;
    arr[1] |= u_arr[3];
    arr[2] &= u_arr[1] - (unsigned char) 185;
    arr[3] <<= 7u;
    static long x = 32;
    arr[4] >>= 31;

    u_arr[3] <<= 12;
    u_arr[2] >>= (x - 1);
    u_arr[1] |= -399;
    x = -4296140120l;
    u_arr[0] ^= x;

    if (arr[0] != -71) return 1;
    if (arr[1] != -1) return 2;
    if (arr[2] != -16) return 3;
    if (arr[3] != -128) return 4;
    if (arr[4] != 0) return 5;
    if (u_arr[0] != 168) return 6;
    if (u_arr[1] != 251) return 7;
    if (u_arr[2] != 0) return 8;
    if (u_arr[3] != 0) return 9;

    return 0;
})"));
}

// extra_credit/bitwise_ops_character_constants: static char/ulong (also 9.2e18).
TEST_F(BookTest, Chapter16_BitwiseOpsCharacterConstants)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test that we can use character constants in bitwise operations
int main(void) {
    int x = 10;
    if ((x ^ 'A') != 75) {
        return 1;  // fail
    }

    static signed char c = 132;  // converted to -124 (plain char unsigned on BESM-6)
    if (('!' | c) != -91) {
        return 2;  // fail
    }

    static unsigned long ul = 9259400834947493926ul;
    if ((ul & '~') != 38) {
        return 3;  // fail
    }

    if ((ul << ' ') != 4611738958194278400ul) {
        return 4;  // fail
    }

    if (('{' >> 3) != 15) {
        return 5;  // fail
    }

    return 0;
})"));
}


// --- Negative-operand right shift is logical/impl-defined on BESM-6 -----------

// extra_credit/bitshift_chars: BESM-6 right shift of a negative value is logical
// (zero-fill of the 41-bit pattern), so the negative cases yield large positives.
//
// KNOWN FAILURE (return 5, pre-existing, orthogonal to plain-char signedness — task #31):
// `-(uc << 5u) >> 5u` with `unsigned char uc` const-folds to a *uint* (48-bit) value because
// the optimizer represents `(int)(unsigned char)` via ZERO_EXTEND→uint, so the unary minus
// wraps at 2^48 instead of int's 2^41 (yields 8796093021953, not 68719476481).  The integer
// promotion of `unsigned char` is to `int`, not `unsigned int` (the test's own comment), so the
// fix belongs in const-fold's ZERO_EXTEND result-kind handling, not here.
TEST_F(BookTest, Chapter16_BitshiftChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// Test << and >> operators with chars (or mix of chars and other types)

int main(void) {
    unsigned char uc = 255;

    // uc is promoted to int, then shifted
    if ((uc >> 3) != 31) {
        return 2; // fail
    }

    signed char sc = -127;
    signed char c = 5; // plain char unsigned on BESM-6; keep signed for the shift-promotion check
    // sc is promoted to int, then shifted (logical: (2^41 - 127) >> 5)
    if ((sc >> c) != 68719476732) {
        return 3;  // fail
    }

    // make sure c << 3ul is promoted to int, not unsigned long (logical: (2^41 - 40) >> 3)
    if (((-(c << 3ul)) >> 3) != 274877906939) {
        return 4;  // fail
    }

    // make sure uc << 5u is promoted to int, not unsigned int (logical: (2^41 - 8160) >> 5)
    if ((-(uc << 5u) >> 5u) != 68719476481l) {
        return 5; // fail
    }

    return 0;
})"));
}


// --- Value exceeds BESM-6 range / relies on 32-bit unsigned width -------------

// chars/static_initializers: out-of-range long/ulong initializers are replaced
// with in-range ones that keep the same low byte (the task #11 "value parts").
// Still DISABLED: the remaining mismatch is char-signedness (plain char is
// unsigned on BESM-6 — task #16) and static char-init codegen (task #18).
// The signed/unsigned-char globals are renamed to sc_*/uc_* so each stays
// distinct within Madlen's 8-char identifier limit (schar_from_* all collapse
// to SCHAR*FR, uchar_from_* to UCHAR*FR). Plain char is unsigned on BESM-6, so
// from_uint's low byte 129 stays 129 (not -127 as on a signed-char target).
TEST_F(BookTest, Chapter16_StaticInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that initializers for static objects with character type are correctly
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
    if (from_uint != 129) return 3;  // plain char unsigned on BESM-6
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

// chars/common_type: the ternary's unsigned-int common type, narrowed to the long
// return, wraps back to -10 on BESM-6 (41-bit long). char_lt_int/char_lt_uchar are
// renamed c_lt_int/c_lt_uchar so they stay distinct within Madlen's 8-char limit.
TEST_F(BookTest, Chapter16_CommonType)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we correctly find the common type of character types and other
 * types (it's always the other type - or, if both are character types, it's int) */

long ternary(int flag, signed char c) { // plain char unsigned on BESM-6; keep c signed
    return flag ? c : 1u;
}

int c_lt_int(char c, int i) {
    return c < i;
}

int uchar_gt_long(unsigned char uc, long l) {
    return uc > l;
}

int c_lt_uchar(signed char c, unsigned char u) { // plain char unsigned on BESM-6; keep c signed
    return c < u;
}

int signed_char_le_char(signed char s, char c) {
    return s <= c;
}

char ten = 10;
int multiply(void) {
    char i = 10.75 * ten;
    return i == 107;
}

int main(void) {
    if (ternary(1, -10) != -10l) {
        return 1;
    }

    if (!c_lt_int((char)1, 256)) {
        return 2;
    }

    if (!uchar_gt_long((unsigned char)100, -2)) {
        return 3;
    }

    signed char c = -1; // plain char unsigned on BESM-6; keep c signed
    unsigned char u = 2;
    if (!c_lt_uchar(c, u)) {
        return 4;
    }

    signed char s = -1;
    if (!signed_char_le_char(s, c)) {
        return 5;
    }

    if (!multiply()) {
        return 6;
    }

    return 0;
})"));
}

// extra_credit/bitwise_ops_chars: the 48-bit unsigned analogue of 2^32-659 is 2^48-659.
TEST_F(BookTest, Chapter16_BitwiseOpsChars)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(// make sure we perform integer promotions when performing bitwise operations on chars

int main(void) {
    unsigned char uc = 135;
    signed char c = -116; // plain char is unsigned on BESM-6; keep the negative value signed
    if ((uc & c) != 132) {
        return 1;  // fail
    }

    if ((uc | c) != -113) {
        return 2;  // fail
    }

    if (((c ^ 1001u) | 360l) != 281474976709997) { // 2^48 - 659
        return 3; // fail
    }

    return 0;
})"));
}


// --- Standard library calls --------------------------------------------------

// strings_as_lvalues/standard_library_calls: strcmp/puts/strlen/atoi (atoi added — task #22).
// Printed text is UPPERCASE so it survives the run-test charset (lowercase → Cyrillic).
TEST_F(BookTest, Chapter16_StandardLibraryCalls)
{
    EXPECT_EQ("HELLO, WORLD!\n0\n", CompileAndRunBook(R"(/* Test calling string manipulation functions from the standard library */

int strcmp(char *s1, char *s2);
int puts(char *s);
unsigned long strlen(char *s);
int atoi(char *s);

int main(void) {
    if (strcmp("abc", "abc")) {
        return 1;
    }

    // "ab" should compare less than "xy"
    if (strcmp("ab", "xy") >= 0) {
        return 2;
    }

    puts("HELLO, WORLD!");

    if (strlen("")) {
        return 3;
    }

    int i = atoi("10");
    if (i != 10) {
        return 4;
    }

    return 0;
})"));
}

// strings_as_lvalues/adjacent_strings: puts("HELLO," " WORLD").
// The parser concatenates adjacent string-literal tokens (C11 §5.1.1.2 phase 6).
TEST_F(BookTest, Chapter16_AdjacentStrings)
{
    EXPECT_EQ("HELLO, WORLD\n0\n", CompileAndRunBook(R"(/* Test that we concatenate adjacent string literal tokens */

int puts(char *s);

int main(void) {
    char *strings = "HELLO," " WORLD";
    puts(strings);
    return 0;
})"));
}

// --- read an int's big-endian bytes via char* --------------------------------

// chars/access_through_char_pointer (adapted for BESM-6): an int occupies one
// 48-bit word = 6 bytes in big-endian order (byte #0 = MSB, byte #5 = LSB), so
// reading it through a char* inspects those six bytes rather than x86's four.
TEST_F(BookTest, Chapter16_AccessThroughCharPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(/* Test that we can read an object through a pointer to a character type */

int main(void) {

    /* Inspect the six big-endian bytes of an int held in one 48-bit word:
     * byte #0 is the most significant byte, byte #5 the least significant. */
    int x = 100;
    char *byte_ptr = (char *) &x;

    /* the value lives in the low byte; the five higher bytes are zero */
    if (byte_ptr[5] != 100) {
        return 1;
    }

    if (byte_ptr[0] || byte_ptr[1] || byte_ptr[2] || byte_ptr[3] || byte_ptr[4]) {
        return 2;
    }

    /* a value spanning two bytes demonstrates big-endian ordering in the word */
    int y = 0x0102; /* 258 */
    byte_ptr = (char *) &y;
    if (byte_ptr[5] != 2) {
        return 3;
    }

    if (byte_ptr[4] != 1) {
        return 4;
    }

    return 0;
})"));
}
