//
// Chapter 14 — Pointers: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_14/valid + casts +
// comparisons + declarators + dereference + function_calls + extra_credit +
// libraries).  Each program defines int main(void); b6sim --status prints its
// return value, and we compare program output against the value computed by host
// cc.  The book's host-only "#ifdef SUPPRESS_WARNINGS / #pragma" blocks are dropped
// (our scanner has no preprocessor); two-file "libraries" cases are merged into
// one source, client first.
//
// Key architectural facts that drive the split.  A BESM-6 pointer is a *word*
// address, not a byte address, so x86 "8-byte-aligned, ptr % 8 == 0"
// assumptions do not hold.  There is NO block-scope static-local storage (a
// `static` inside a function emits no toplevel, so the body references an
// undefined name).  The integer types are narrow: signed int/long is 41-bit
// (max ~1.1e12), unsigned is 48-bit (max ~2.8e14) — no 32-bit int wrap and no
// 64-bit long/unsigned-long wrap.  Floating-point is one 48-bit format
// (exponent ~1e-19 .. 9.2e18, ~12 significant digits, no NaN/Inf).  There is no
// putchar in libc (we substitute putch).
//
// Chapter 14 is written for an x86 machine with 64-bit pointers/longs and
// byte-addressed, 8-byte-aligned objects.  The corpus therefore splits:
//
//   * Programs whose every value is within the BESM-6 integer/FP ranges, that
//     use no static locals and no x86 byte-addressing or width assumptions,
//     compute the same result the book expects and are enabled run tests below.
//
//   * Programs depending on static-local storage, on x86 byte-addressing, or on
//     values / wraparound beyond BESM-6's ranges cannot reproduce the book
//     result on BESM-6.  They are DISABLED_ (grouped at the bottom with a
//     one-line reason each).  These are not compiler bugs — they exercise
//     x86 semantics BESM-6 lacks.  Like chapters 11–13, the programs self-check
//     and return an error code on mismatch, so a BESM-6-valued expectation would
//     just encode a meaningless failure code; DISABLED_ is the honest call.
//
// Programs whose results depend on integer widths or sizes are in their generic
// LP64 form; BESM-6 runs its own versions of them (see README.md).
//
#include "book_test.h"

// --- dereference ------------------------------------------------------------

// dereference/simple: read an int through a pointer.
TEST_F(BookTest, Chapter14_Simple)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int main(void) {
    int x = 3;
    int *ptr = &x;
    return *ptr;
})"));
}

// dereference/address_of_dereference: &*e just evaluates e (so &*null is valid).
TEST_F(BookTest, Chapter14_AddressOfDereference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int *null_ptr = 0;
    if (&*null_ptr != 0)
        return 1;

    int **ptr_to_null = &null_ptr;

    if (&**ptr_to_null)
        return 2;

    return 0;
})"));
}

// dereference/multilevel_indirection: pointers to pointers (double***).
TEST_F(BookTest, Chapter14_MultilevelIndirection)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    double d = 10.0;
    double *d_ptr = &d;
    double **d_ptr_ptr = &d_ptr;
    double ***d_ptr_ptr_ptr = &d_ptr_ptr;

    if (d != 10.0) {
        return 1;
    }
    if (*d_ptr != 10.0) {
        return 2;
    }

    if (**d_ptr_ptr != 10.0) {
        return 3;
    }

    if (***d_ptr_ptr_ptr != 10.0) {
        return 4;
    }

    if (&d != d_ptr) {
        return 5;
    }
    if (*d_ptr_ptr != d_ptr) {
        return 6;
    }
    if (**d_ptr_ptr_ptr != d_ptr) {
        return 7;
    }

    ***d_ptr_ptr_ptr = 5.0;
    if (d != 5.0) {
        return 8;
    }
    if (*d_ptr != 5.0) {
        return 9;
    }
    if (**d_ptr_ptr != 5.0) {
        return 10;
    }

    if (***d_ptr_ptr_ptr != 5.0) {
        return 11;
    }

    double d2 = 1.0;

    double *d2_ptr = &d2;
    double *d2_ptr2 = d2_ptr;

    double **d2_ptr_ptr = &d2_ptr;

    *d_ptr_ptr_ptr = d2_ptr_ptr;

    if (**d_ptr_ptr_ptr != d2_ptr) {
        return 12;
    }

    if (***d_ptr_ptr_ptr != 1.0) {
        return 13;
    }

    if (d2_ptr_ptr == &d2_ptr2)
        return 14;

    d2_ptr = d_ptr;

    if (**d_ptr_ptr_ptr != d_ptr) {
        return 15;
    }


    if (*d2_ptr_ptr != d_ptr) {
        return 16;
    }

    if (**d_ptr_ptr_ptr == d2_ptr2) {
        return 17;
    }

    if (***d_ptr_ptr_ptr != 5.0) {
        return 18;
    }

    return 0;
})"));
}

// --- comparisons ------------------------------------------------------------

// comparisons/compare_pointers: == and != on pointers.
TEST_F(BookTest, Chapter14_ComparePointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    int b;

    int *a_ptr = &a;
    int *a_ptr2 = &a;
    int *b_ptr = &b;

    if (a_ptr == b_ptr) {
        return 1;
    }

    if (a_ptr != a_ptr2) {
        return 2;
    }

    if (!(a_ptr == a_ptr2)) {
        return 3;
    }

    if (!(a_ptr != b_ptr)) {
        return 4;
    }

    *b_ptr = *a_ptr;
    if (a_ptr == b_ptr) {
        return 5;
    }
    b_ptr = a_ptr;
    if (b_ptr != a_ptr) {
        return 6;
    }

    return 0;
})"));
}

// comparisons/compare_to_null: comparisons to several null pointer constants.
TEST_F(BookTest, Chapter14_CompareToNull)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(double *get_null_pointer(void) {
    return 0;
}

int main(void)
{
    double x;
    double *null = get_null_pointer();
    double *non_null = &x;

    if (non_null == 0) {
        return 1;
    }

    if (!(null == 0l)) {
        return 2;
    }

    if (!(non_null != 0u)) {
        return 3;
    }

    if (null != 0ul) {
        return 4;
    }

    return 0;
})"));
}

// comparisons/pointers_as_conditions: pointers in boolean/ternary/loop contexts.
TEST_F(BookTest, Chapter14_PointersAsConditions)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(long *get_null_pointer(void) {
    return 0;
}

int main(void)
{
    long x;
    long *ptr = &x;
    long *null_ptr = get_null_pointer();

    if (5.0 && null_ptr) {
        return 1;
    }

    int a = 0;
    if (!(ptr || (a = 10))) {
        return 2;
    }

    if (a != 0) {
        return 3;
    }

    if (!ptr) {
        return 4;
    }

    int j = ptr ? 1 : 2;
    int k = null_ptr ? 3 : 4;
    if (j != 1) {
        return 5;
    }

    if (k != 4) {
        return 6;
    }

    int i = 0;
    while (ptr)
    {
        if (i >= 10) {
            ptr = 0;
            continue;
        }
        i = i + 1;
    }
    if (i != 10) {
        return 7;
    }

    return 0;
})"));
}

// --- declarators ------------------------------------------------------------

// declarators/abstract_declarators: a range of abstract declarators casting 0.
TEST_F(BookTest, Chapter14_AbstractDeclarators)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    long int unsigned *x = 0;

    if (x != (unsigned long (*)) 0)
        return 1;

    if (x != (long unsigned int ((((*))))) 0)
        return 2;

    double ***y = 0;

    if (y != (double *(**)) 0)
        return 3;

    if (y != (double (***)) 0)
        return 4;

    if ((double (*(*(*)))) 0 != y)
        return 5;

    return 0;
})"));
}

// declarators/declare_pointer_in_for_loop: pointer declarator in for-init.
TEST_F(BookTest, Chapter14_DeclarePointerInForLoop)
{
    EXPECT_EQ("5\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    for (int *i = &x; i != 0; ) {
        *i = 5;
        i = 0;
    }
    return x;
})"));
}

// --- casts ------------------------------------------------------------------

// casts/null_pointer_conversion: implicit null-pointer-constant conversions.
TEST_F(BookTest, Chapter14_NullPointerConversion)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(double *d = 0l;
int *i = 0ul;
int *i2 = 0u;

int expect_null_param(int *val)
{
    return (val == 0ul);
}

long *return_null_ptr(void)
{
    return 0;
}

int main(void)
{
    int x = 10;
    int *ptr = &x;

    if (d) {
        return 1;
    }

    if (i) {
        return 2;
    }
    if (i2) {
        return 3;
    }

    ptr = 0ul;
    if (ptr) {
        return 4;
    }

    int *y = 0;
    if (y != 0)
        return 5;

    if (!expect_null_param(0)) {
        return 6;
    }

    long *null_ptr = return_null_ptr();
    if (null_ptr != 0) {
        return 7;
    }

    ptr = &x;
    int *ternary_result = 10 ? 0 : ptr;
    if (ternary_result) {
        return 8;
    }

    return 0;
})"));
}

// --- function_calls ---------------------------------------------------------

// function_calls/address_of_argument: take the address of a parameter.
TEST_F(BookTest, Chapter14_AddressOfArgument)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int addr_of_arg(int a) {
    int *ptr = &a;
    *ptr = 10;
    return a;
}

int main(void) {
    int result = addr_of_arg(-20);
    if (result != 10) {
        return 1;
    }

    int var = 100;
    result = addr_of_arg(var);
    if (result != 10) {
        return 2;
    }
    if (var != 100) {
        return 3;
    }
    return 0;
})"));
}

// function_calls/return_pointer: return a pointer from a function.
TEST_F(BookTest, Chapter14_ReturnPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int *return_pointer(int *in) {
    return in;
}

int main(void) {
    int x = 10;
    int *x_ptr = return_pointer(&x);

    if (*x_ptr != 10)
        return 1;

    x = 100;
    if (*x_ptr != 100)
        return 2;

    if (x_ptr != &x)
        return 3;

    return 0;
})"));
}

// function_calls/update_value_through_pointer_parameter: callee writes via ptr.
TEST_F(BookTest, Chapter14_UpdateValueThroughPointerParameter)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int update_value(int *ptr) {
    int old_val = *ptr;
    *ptr = 10;
    return old_val;
}

int main(void) {
    int x = 20;
    int result = update_value(&x);
    if (result != 20) {
        return 1;
    }
    if (x != 10) {
        return 2;
    }
    return 0;
})"));
}

// --- libraries (two files merged, client first) -----------------------------

// libraries/global_pointer_client.c + global_pointer.c : update a global object
// through a global pointer.
TEST_F(BookTest, Chapter14_LibrariesGlobalPointer)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(extern double *d_ptr;
int update_thru_ptr(double new_val);

int main(void) {
    double d = 0.0;
    d_ptr = &d;
    update_thru_ptr(10.0);
    return (d == 10.0);

}

double *d_ptr;

int update_thru_ptr(double new_val) {
    *d_ptr = new_val;
    return 0;
})"));
}

// libraries/static_pointer_client.c + static_pointer.c : read/write a static
// pointer only through functions.
TEST_F(BookTest, Chapter14_LibrariesStaticPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(long *get_pointer(void);
int set_pointer(long *new_ptr);

static long private_long = 100l;

int main(void) {
    long *initial_ptr = get_pointer();
    if (initial_ptr) {
        return 1;
    }

    set_pointer(&private_long);

    long *new_ptr = get_pointer();
    if (initial_ptr == new_ptr) {
        return 2;
    }

    if (*new_ptr != 100l) {
        return 3;
    }

    if (new_ptr != &private_long) {
        return 4;
    }

    set_pointer(0);

    if (get_pointer()) {
        return 5;
    }

    if (new_ptr != &private_long) {
        return 6;
    }

    return 0;
}

static long *long_ptr;

long *get_pointer(void) {
    return long_ptr;
}

int set_pointer(long *new_ptr) {
    long_ptr = new_ptr;
    return 0;
})"));
}

// --- extra_credit -----------------------------------------------------------

// extra_credit/compound_assign_through_pointer: small-int compound assignment
// through a dereferenced pointer.
TEST_F(BookTest, Chapter14_CompoundAssignThroughPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    int *ptr = &x;

    *ptr += 5;
    if (x != 15) {
        return 1;
    }

    if ((*ptr -= 12) != 3) {
        return 2;
    }

    if (x != 3) {
        return 3;
    }

    *ptr *= 6;
    if (x != 18) {
        return 4;
    }

    *ptr /= 9;
    if (x != 2) {
        return 5;
    }

    *ptr %= 3;
    if (x != 2) {
        return 6;
    }

    return 0;
})"));
}

// extra_credit/eval_compound_lhs_once: the lhs of a compound assignment is
// evaluated only once.  Book uses putchar; we substitute libc putch.  The two
// helper calls print 'A' and 'B' once each, then main returns 0 -> "AB0\n".
TEST_F(BookTest, Chapter14_EvalCompoundLhsOnce)
{
    EXPECT_EQ("AB0\n", CompileAndRunBook(R"(int i = 0;

void putch(int c);
int *print_A(void) {
    putch(65);
    return &i;
}

int *print_B(void) {
    putch(66);
    return &i;
}

int main(void) {

    *print_A() += 5;
    if (i != 5) {
        return 1;
    }

    *print_B() += 5l;
    if (i != 10) {
        return 2;
    }

    return 0;
})"));
}

// ===========================================================================
// DISABLED_ — programs BESM-6 cannot reproduce, grouped by reason.
// ===========================================================================

// --- Static locals (now supported) + remaining besm6 gaps -------------------
// Block-scope statics work now; the tests still DISABLED_ below have a separate,
// non-static blocker noted on each.

// casts/cast_between_pointer_types: uses `static long *long_ptr` inside a fn.
TEST_F(BookTest, Chapter14_CastBetweenPointerTypes)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int check_null_ptr_cast(void) {
    static long *long_ptr = 0;
    double *dbl_ptr = (double *)long_ptr;
    unsigned int *int_ptr = (unsigned int *)long_ptr;
    int **ptr_ptr = (int **)long_ptr;

    if (long_ptr) {
        return 1;
    }
    if (dbl_ptr) {
        return 2;
    }
    if (int_ptr) {
        return 3;
    }
    if (ptr_ptr) {
        return 4;
    }
    return 0;
}

int check_round_trip(void) {
    long l = -1;
    long *long_ptr = &l;
    double *dbl_ptr = (double *)long_ptr;
    long *other_long_ptr = (long *)dbl_ptr;
    if (*other_long_ptr != -1) {
        return 5;
    }
    return 0;
}

int main(void)
{
    int result = check_null_ptr_cast();

    if (result) {
        return result;
    }

    result = check_round_trip();
    return result;
})"));
}

// declarators/declarators: redundant-parenthesized declarator forms (e.g.
// `int((return_3))(void)`, `long(*two_pointers(double val, double(*d)));`).
TEST_F(BookTest, Chapter14_Declarators)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int return_3(void);
int(return_3(void));
int(return_3)(void);
int((return_3))(void)
{
    return 3;
}


long l = 100;
long *two_pointers(double val, double *ptr)
{
    *ptr = val;
    return &l;
}
long(*two_pointers(double val, double(*d)));
long *(two_pointers)(double val, double *(d));
long *(two_pointers)(double val, double(*(d)));

unsigned **pointers_to_pointers(int **p)
{
    static unsigned u;
    static unsigned *u_ptr;
    u_ptr = &u;
    u = **p;
    return &u_ptr;
}
unsigned(**(pointers_to_pointers(int *(*p))));
unsigned *(*pointers_to_pointers(int(**p)));
unsigned(*(*((pointers_to_pointers)(int(*(*(p)))))));

int main(void)
{
    int i = 0;
    int(*i_ptr) = &i;
    int(**ptr_to_iptr) = &i_ptr;

    double(d1) = 0.0;
    double d2 = 10.0;

    double *(d_ptr) = &d1;

    long(*(l_ptr));

    unsigned *(*(ptr_to_uptr));

    i = return_3();
    if (i != 3)
        return 1;

    if (*i_ptr != 3) {
        return 2;
    }

    l_ptr = two_pointers(d2, d_ptr);
    if (l_ptr != &l) {
        return 3;
    }

    if (*l_ptr != 100) {
        return 4;
    }

    if (*d_ptr != 10.0) {
        return 5;
    }

    if (d1 != 10.0) {
        return 6;
    }


    ptr_to_uptr = pointers_to_pointers(ptr_to_iptr);

    if (**ptr_to_uptr != 3) {
        return 7;
    }

    return 0;
})"));
}

// dereference/dereference_expression_result: uses `static int var = 10`.
TEST_F(BookTest, Chapter14_DereferenceExpressionResult)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int *return_pointer(void) {
    static int var = 10;
    return &var;
}

int one = 1;

int main(void) {
    int val = 100;
    int *ptr_var = &val;

    if (*return_pointer() != 10) {
        return 1;
    }

    if (*(one ? return_pointer() : ptr_var) != 10)
        return 2;

    if (*(one - 1 ? return_pointer() : ptr_var) != 100) {
        return 3;
    }


    int *ptr_to_one = &one;
    if (*(ptr_var = ptr_to_one) != 1) {
        return 4;
    }

    *return_pointer() = 20;
    *(one ? ptr_var : return_pointer()) = 30;

    if (*return_pointer() != 20) {
        return 5;
    }
    if (*ptr_var != 30) {
        return 6;
    }
    if (one != 30) {
        return 7;
    }

    return 0;
})"));
}

// dereference/static_var_indirection: uses `static long *p` inside modify_ptr.
TEST_F(BookTest, Chapter14_StaticVarIndirection)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int w = 4294967295U;
int x = 10;
unsigned int y = 4294967295U;
double *dbl_ptr;

long modify_ptr(long *new_ptr) {
    static long *p;
    if (new_ptr)
    {
        p = new_ptr;
    }
    return *p;
}


int increment_ptr(void)
{
    *dbl_ptr = *dbl_ptr + 5.0;
    return 0;
}

int main(void) {

    int *pointer_to_static = &x;
    x = 20;
    if (*pointer_to_static != 20) {
        return 1;
    }

    *pointer_to_static = 100;

    if (x != 100) {
        return 2;
    }
    if (w != 4294967295U) {
        return 3;
    }
    if (y != 4294967295U) {
        return 4;
    }
    if (dbl_ptr) {
        return 5;
    }

    long l = 1000l;

    if (modify_ptr(&l) != 1000l) {
        return 6;
    }

    l = -1;
    if (modify_ptr(0) != l) {
        return 7;
    }

    double d = 10.0;
    dbl_ptr = &d;
    increment_ptr();
    if (*dbl_ptr != 15) {
        return 8;
    }

    return 0;
})"));
}

// --- Out-of-range values replaced with in-range ones ------------------------

// dereference/read_through_pointers: original used 1.38e19 / 1.44e17, both
// beyond the 48-bit unsigned range; use values near it instead.
TEST_F(BookTest, Chapter14_ReadThroughPointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {

    int i = -100;
    unsigned long ul = 281474976710000ul;
    double d = 3.5;

    int *i_ptr = &i;
    unsigned long *ul_ptr = &ul;
    double *d_ptr = &d;

    if (*i_ptr != -100) {
        return 1;
    }
    if (*ul_ptr != 281474976710000ul) {
        return 2;
    }
    if (*d_ptr != 3.5) {
        return 3;
    }

    i = 12;
    ul = 1000;
    d = -000.001;

    if (*i_ptr != 12) {
        return 4;
    }
    if (*ul_ptr != 1000) {
        return 5;
    }
    if (*d_ptr != -000.001) {
        return 6;
    }

    int i2 = 1;
    unsigned long ul2 = 200000000000000ul;
    double d2 = -33.3;

    i_ptr = &i2;
    ul_ptr = &ul2;
    d_ptr = &d2;


    if (*i_ptr != 1) {
        return 7;
    }
    if (*ul_ptr != 200000000000000ul) {
        return 8;
    }
    if (*d_ptr != -33.3) {
        return 9;
    }

    return 0;

})"));
}

// dereference/update_through_pointers: original used 1.44e17 (> 41-bit) and
// 1e50 (> FP range) as initial values; both are overwritten before use, so any
// in-range placeholders work.
TEST_F(BookTest, Chapter14_UpdateThroughPointers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int i = 2185232384u;
    signed long l = 1099511627000l;
    double d = 1.0;

    unsigned *i_ptr = &i;
    long *l_ptr = &l;
    double *d_ptr = &d;

    *i_ptr = 10;
    *l_ptr = -20;
    *d_ptr = 30.1;

    if (i != 10) {
        return 1;
    }
    if (l != -20) {
        return 2;
    }
    if (d != 30.1) {
        return 3;
    }
    return 0;
})"));
}

// extra_credit/bitwise_ops_with_dereferenced_ptrs: original used -1u (all 64
// bits) and 2^63. On BESM-6 -1u is all 48 bits and (int)-1 fills only 41 bits,
// so use two disjoint in-range masks (bits 0-19 and bits 20-40) whose union is
// 2^41-1, matching the 41-bit pattern of (int)-1.
TEST_F(BookTest, Chapter14_BitwiseOpsWithDereferencedPtrs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    unsigned int ui = 1048575u;          // 0xFFFFF, bits 0-19
    unsigned long ul = 2199022206976ul;  // bits 20-40
    unsigned int *ui_ptr = &ui;
    unsigned long *ul_ptr = &ul;

    if ((*ui_ptr & *ul_ptr) != 0) {
        return 1;
    }

    if ((*ui_ptr | *ul_ptr) != 2199023255551ul) { // 2^41 - 1
        return 2;
    }

    int i = -1;
    signed int *i_ptr = &i;
    if ((*i_ptr & ul) != *ul_ptr) {
        return 3;
    }

    if ((*i_ptr | *ul_ptr) != i) {
        return 4;
    }

    return 0;
})"));
}

// extra_credit/compound_assign_conversion: ul reduced into the 48-bit range;
// the unsigned remainder is recomputed against BESM-6's 41/48-bit widths.
TEST_F(BookTest, Chapter14_CompoundAssignConversion)
{
    // `*i_ptr %= 4294967200U` runs in unsigned int (C11 §6.5.16.2p3): -50 converts to
    // 2^32 - 50 on LP64, giving 46; BESM-6 keeps the 41-bit pattern 2^41 - 50 (see
    // docs/Besm6_Data_Representation.md), giving 49102.
    std::string rem = IsTarget("besm6") ? "49102" : "46";
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    double d = 5.0;
    double *d_ptr = &d;
    *d_ptr *= 1000u;
    if (d != 5000.0) {
        return 1;
    }
    int i = -50;
    int *i_ptr = &i;
    *i_ptr %= 4294967200U;
    if (*i_ptr != )" + rem + R"() {
        return 2;
    }

    unsigned int ui = 4294967295U;
    ui /= *d_ptr;
    if (ui != 858993u) {
        return 3;
    }

    i = -10;
    unsigned long ul = 2199023255551ul; // 2^41 - 1
    unsigned long *ul_ptr = &ul;
    *i_ptr -= *ul_ptr;
    if (i != -9) {
        return 4;
    }

    if (ul != 2199023255551ul) {
        return 5;
    }

    return 0;
})"));
}

// extra_credit/compound_bitwise_dereferenced_ptrs: compound bitwise operators
// through pointers, with operands of mixed widths.
TEST_F(BookTest, Chapter14_CompoundBitwiseDereferencedPtrs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned long ul = 18446460386757245432ul;

int main(void) {

    unsigned long *ul_ptr = &ul;
    *ul_ptr &= -1000;
    if (ul != 18446460386757244952ul ) {
        return 1;
    }
    *ul_ptr |= 4294967040u;

    if (ul != 18446460386824683288ul ) {
        return 2;
    }
    int i = 123456;
    unsigned int ui = 4042322160u; // 2^32 - 252645136
    long l = -252645136;
    unsigned int *ui_ptr = &ui;
    long *l_ptr = &l;
    if (*ui_ptr ^= *l_ptr) {
        return 3;
    }
    if (ui) {
        return 4;
    }

    if (i != 123456) {
        return 5;
    }
    if (l != -252645136) {
        return 6;
    }

    return 0;
})"));
}

// --- Relies on x86 32/64-bit unsigned wraparound/truncation -----------------

// extra_credit/bitshift_dereferenced_ptrs: 4294967295u << 2 wraps to 4294967292.
TEST_F(BookTest, Chapter14_BitshiftDereferencedPtrs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(unsigned int ui = 4294967295;

unsigned int *get_ui_ptr(void){
    return &ui;
}

int shiftcount = 5;

int main(void) {

    if ((*get_ui_ptr() << 2l) != 4294967292) {
        return 1;
    }

    if ((*get_ui_ptr() >> 2) != 1073741823) {
        return 2;
    }

    int *shiftcount_ptr = &shiftcount;
    if ((1000000u >> *shiftcount_ptr) != 31250) {
        return 3;
    }
    if ((1000000u << *shiftcount_ptr) != 32000000) {
        return 4;
    }

    return 0;
})"));
}

// extra_credit/incr_and_decr_through_pointer: 0ul-- wraps to ULONG_MAX.
TEST_F(BookTest, Chapter14_IncrAndDecrThroughPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    int *y = &x;

    if (++*y != 11) {
        return 1;
    }
    if (x != 11) {
        return 2;
    }

    if (--*y != 10) {
        return 3;
    }

    if (x != 10) {
        return 4;
    }

    if ((*y)++ != 10) {
        return 5;
    }

    if (x != 11) {
        return 6;
    }

    if ((*y)-- != 11) {
        return 7;
    }

    if (x != 10) {
        return 8;
    }

    unsigned long ul = 0;
    unsigned long *ul_ptr = &ul;
    if ((*ul_ptr)--) {
        return 9;
    }
    if (ul != 18446744073709551615UL) { // wraparound from 0 to ULONG_MAX
        return 10;
    }

    double d = 0.0;
    double *d_ptr = &d;
    if (++(*d_ptr) != 1.0) {
        return 11;
    }
    if (d != 1.0) {
        return 12;
    }

    return 0;
})"));
}

// extra_credit/switch_dereferenced_pointer: a case label (18446744073709551600UL,
// ~1.8e19) exceeds BESM-6's range and the x86 truncation semantics it relies on
// (l % 2^32) do not apply to a 41-bit long.
// extra_credit/switch_dereferenced_pointer: the unused 1.8e19 case label is
// replaced with an in-range long distinct from the matched case.
TEST_F(BookTest, Chapter14_SwitchDereferencedPointer)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(long l = 4294967300l;

long *get_ptr(void) {
    return &l;
}
int main(void) {
    switch (*get_ptr()) {
        case 1:
            return 1;
        case 4:
            return 2;
        case 4294967300l:
            return 0;
        case 1000000000000l:
            return 3;
        default:
            return 4;
    }
})"));
}
