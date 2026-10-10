//
// Chapter 20 — register allocation: imported from "Writing a C Compiler"
// (tests/chapter_20).  Chapter 20 of the book implements the x86-64 register
// allocator (interference graphs, spilling, Briggs/George coalescing).  The
// BESM-6 backend has no register allocator in that sense, so — as with the
// chapter 17-19 imports — these programs do not exercise a BESM-6 feature; the
// value is regression coverage that these arithmetic-heavy, many-variable,
// many-argument programs still compile and run correctly end-to-end through the
// BESM-6 pipeline (optimizer on) on the b6sim simulator.
//
// The book validates each program by inspecting generated assembly plus an x86
// wrapper (helper_libs/wrapper_*.s) that clobbers callee-saved/argument
// registers, supplies fixed argument values, and calls `target`.  We drop the
// register-level checks (no BESM-6 analog) and keep the programs' behavioral
// self-checks (the helper_libs/util.c check_* helpers, inlined below, return 0
// on success and exit(-1) on mismatch).  Programs with no `main` of their own
// rely on the wrapper for arguments; we add a main() that calls the entry point
// with the wrapper's fixed values — ints 1,2,3,4,5,6 and doubles 1.0..8.0.
// b6sim --status prints main()'s return value; success prints "0\n".
//
// All chapter-20 programs are enabled (task #27 triage): struct-by-value args and
// returns now have a working ABI, and the few x86-specific assertions (byte-address
// alignment, 32/64-bit width wraparound, plain-char signedness) were adapted to the
// BESM-6 type model.  One program — DontCoalesceMovzx, an x86-only "don't coalesce a
// movzx" check with no BESM-6 analogue — was removed (see the note at its old site).
//
// Programs whose results depend on integer widths or sizes are in their generic
// LP64 form; BESM-6 runs its own versions of them (see README.md).
//
#include "book_test.h"

// --- inlined helper_libs/util.c check_* / id helpers (exit on mismatch) ---
static const std::string EX    = "#include <stdlib.h>\n";
static const std::string ID    = "int id(int x) { return x; }\n";
static const std::string DBLID = "double dbl_id(double x) { return x; }\n";
static const std::string UID   = "unsigned unsigned_id(unsigned u) { return u; }\n";
static const std::string UCID  = "unsigned char uchar_id(unsigned char uc) { return uc; }\n";
static const std::string C1I =
    R"H(int check_one_int(int actual, int expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1U =
    R"H(int check_one_uint(unsigned int actual, unsigned int expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1UC =
    R"H(int check_one_uchar(unsigned char actual, unsigned char expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1L =
    R"H(int check_one_long(long actual, long expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1UL =
    R"H(int check_one_ulong(unsigned long actual, unsigned long expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C1D =
    R"H(int check_one_double(double actual, double expected) { if (actual != expected) exit(-1); return 0; }
)H";
static const std::string C5I = R"H(int check_5_ints(int a, int b, int c, int d, int e, int start) {
    int args[5] = {a, b, c, d, e};
    for (int i = 0; i < 5; i++) { if (args[i] != start + i) exit(-1); }
    return 0;
}
)H";
static const std::string C12I =
    R"H(int check_12_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int l, int start) {
    int args[12] = {a, b, c, d, e, f, g, h, i, j, k, l};
    for (int n = 0; n < 12; n++) { if (args[n] != start + n) exit(-1); }
    return 0;
}
)H";
static const std::string C12L =
    R"H(int check_12_longs(long a, long b, long c, long d, long e, long f, long g,
                   long h, long i, long j, long k, long l, long start) {
    long args[12] = {a, b, c, d, e, f, g, h, i, j, k, l};
    for (int n = 0; n < 12; n++) { if (args[n] != start + n) exit(-1); }
    return 0;
}
)H";
static const std::string C6C =
    R"H(int check_six_chars(char a, char b, char c, char d, char e, char f, int start) {
    char args[6] = {a, b, c, d, e, f};
    for (int i = 0; i < 6; i++) { if (args[i] != start + i) exit(-1); }
    return 0;
}
)H";
static const std::string C14D =
    R"H(int check_14_doubles(double a, double b, double c, double d, double e, double f,
                     double g, double h, double i, double j, double k, double l,
                     double m, double n, double start) {
    double args[14] = {a, b, c, d, e, f, g, h, i, j, k, l, m, n};
    for (int p = 0; p < 14; p++) { if (args[p] != start + p) exit(-1); }
    return 0;
}
)H";
static const std::string C12V =
    R"H(int check_12_vals(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, long* k, double* l, int start) {
    int args[10] = {a, b, c, d, e, f, g, h, i, j};
    for (int n = 0; n < 10; n++) { if (args[n] != start + n) exit(-1); }
    if (*k != start + 10) exit(-1);
    if (*l != start + 11) exit(-1);
    return 0;
}
)H";

// ===========================================================================
// int_only / no_coalescing
// ===========================================================================

TEST_F(BookTest, Chapter20_IntNoCoal_BinUsesOperands)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int src_test(int arg) {
    int x = 5 + arg;
    check_one_int(x, 6);
    return 0;
}
int glob = 1;
int glob2;
int flag = 1;
int dst_test(void) {
    int a = id(100);
    if (flag) {
        glob2 = a + glob;
        a = a - 1;
    }
    check_one_int(a, 99);
    check_one_int(glob2, 101);
    return 0;
}
int main(void) {
    src_test(1);
    dst_test();
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_CalleeSavedStackAlignment)
{
    // check_alignment is x86 asm (RSP alignment); stubbed to 0 on BESM-6.
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + C5I + R"WP(
int check_alignment(int exit_code) { return 0; }
int test1(void) {
    int a = id(1);
    int b = id(2);
    int c = id(3);
    check_alignment(-1);
    check_one_int(a, 1);
    check_one_int(b, 2);
    check_one_int(c, 3);
    return 0;
}
int test2(void) {
    int a = id(4);
    int b = id(5);
    check_alignment(-2);
    check_one_int(a, 4);
    check_one_int(b, 5);
    return 0;
}
int test3(void) {
    int a = id(4);
    int b = id(5);
    int c = id(6);
    int d = id(7);
    int e = id(8);
    int f = id(9);
    int g = id(10);
    int h = id(11);
    check_alignment(-3);
    check_5_ints(a, b, c, d, e, 4);
    check_one_int(f, 9);
    check_one_int(g, 10);
    check_one_int(h, 11);
    return 0;
}
int main(void) { test1(); test2(); test3(); return 0; }
)WP"));
}

// DISABLED: block-scope `static int i` has no BESM-6 storage.
TEST_F(BookTest, Chapter20_IntNoCoal_CdqInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
int target(int a, int b, int c) {
    static int i = 100;
    if (a || b) {
        return 0;
    }
    return i / c;
}
int main(void) {
    if (target(0, 0, 10) != 10) {
        return 1;
    }
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_CmpGeneratesOperands)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int glob = 10;
int glob2 = 20;
int main(void) {
    int a = glob + 5;
    int b = glob2 - 5;
    glob = a + glob;
    glob2 = b + glob2;
    if (a != b) {
        return -1;
    }
    check_one_int(glob, 25);
    check_one_int(glob2, 35);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_CmpNoUpdates)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
int glob0 = 0;
int glob1 = 1;
int glob2 = 2;
int glob3 = 3;
int glob4 = 4;
int increment_globals(void) {
    glob0 = glob0 + 1;
    glob1 = glob1 + 1;
    glob2 = glob2 + 1;
    glob3 = glob3 + 1;
    glob4 = glob4 + 1;
    return 0;
}
int validate(int zero, int one, int two, int three, int four);
int target(void) {
    int a = glob0;
    int b = glob1;
    int c = glob2;
    int d = glob3;
    int e = glob4;
    increment_globals();
    int x = a;
    if (a > b) {
        return 1;
    }
    if (b) {
        x = 100;
    }
    increment_globals();
    return validate(x, b, c, d, e);
}
int validate(int hundred, int one, int two, int three, int four) {
    if (glob0 != 2) return 2;
    if (glob1 != 3) return 3;
    if (glob2 != 4) return 4;
    if (glob3 != 5) return 5;
    if (glob4 != 6) return 6;
    if (hundred != 100) return 7;
    if (one != 1) return 8;
    if (two != 2) return 9;
    if (three != 3) return 10;
    if (four != 4) return 11;
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_CopyNoInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int glob0 = 0;
int glob1 = 1;
int glob2 = 2;
int glob3 = 3;
int glob4 = 4;
int glob5 = 5;
int reset_globals(void) {
    glob0 = 0; glob1 = 0; glob2 = 0; glob3 = 0; glob4 = 0; glob5 = 0;
    return 0;
}
int flag = 1;
int target(void) {
    int a = glob0;
    int b = glob1;
    int c = glob2;
    int d = glob3;
    int e = glob4;
    int f;
    int g;
    int h;
    int i;
    int j;
    if (flag) {
        reset_globals();
        f = a; check_one_int(a, 0);
        g = b; check_one_int(b, 1);
        h = c; check_one_int(c, 2);
        i = d; check_one_int(d, 3);
        j = e; check_one_int(e, 4);
    } else {
        e = 0; f = 0; g = 0; h = 0; i = 0; j = 0;
    }
    check_one_int(f, 0);
    check_one_int(g, 1);
    check_one_int(h, 2);
    check_one_int(i, 3);
    check_one_int(j, 4);
    check_one_int(glob0, 0);
    check_one_int(glob1, 0);
    check_one_int(glob2, 0);
    check_one_int(glob3, 0);
    check_one_int(glob4, 0);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_DivisionUsesAx)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int main(void) {
    int coalesce_into_eax = id(10);
    int sum = coalesce_into_eax + 4;
    if (sum != 14) {
        return -1;
    }
    int rem = coalesce_into_eax % 10;
    check_one_int(rem, 0);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_EaxLiveAtExit)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int glob = 10;
int glob2 = 0;
int target(void) {
    int x = glob + 1;
    glob2 = x + glob;
    return x;
}
int main(void) {
    int retval = target();
    check_one_int(retval, 11);
    check_one_int(glob2, 21);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_ForceSpill)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12I + R"WP(
int glob_three = 3;
int target(void) {
    int should_spill = glob_three + 3;
    int one = glob_three - 2;
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
    int thirteen = 10 + glob_three;
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
    if (should_spill != 6) {
        return -1;
    }
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_FuncallGeneratesArgs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int f(int a, int b) {
    if (a != 11) exit(-1);
    if (b != 12) exit(-1);
    return 0;
}
int glob = 10;
int x = 0;
int y = 0;
int target(void) {
    int a = glob + 1;
    int b = glob + 2;
    x = a * glob;
    y = b * glob;
    f(a, b);
    check_one_int(x, 110);
    check_one_int(y, 120);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_IdivInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int glob = 3;
int target(void) {
    int dividend = glob * 16;
    int quotient = dividend / 4;
    glob = dividend;
    check_one_int(quotient, 12);
    return 0;
}
int main(void) {
    target();
    check_one_int(glob, 48);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_Loop)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int counter = 5;
int expected_a = 2;
int update_expected_a(void);
int times_two(int x);
int target(void) {
    int z;
    int a;
    int one = counter - 4;
    int two = counter / 2;
    int three = -counter + 8;
    int four = counter - 1;
    while (counter > 0) {
        if (counter == 5)
            z = 4;
        else
            z = times_two(a);
        update_expected_a();
        a = 1 - z;
        check_one_int(a, expected_a);
        counter = counter - 1;
    }
    check_one_int(one, 1);
    check_one_int(two, 2);
    check_one_int(three, 3);
    check_one_int(four, 4);
    return 0;
}
int update_expected_a(void) {
    expected_a = 1 - (2 * expected_a);
    return 0;
}
int times_two(int x) {
    return x * 2;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_ManyPseudosFewerConflicts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C5I + R"WP(
int return_five(void) {
    return 5;
}
int target(int one, int two, int three) {
    int sum = one + three;
    int product = sum * three;
    int diff = product - (three + two);
    check_one_int(sum, 4);
    check_one_int(product, 12);
    check_one_int(diff, 7);
    for (int i = 0; i < 2; i = i + 1) {
        if (i % 2) {
            int five = return_five();
            int quotient = 25 / five;
            int remainder = 27 % five;
            int complex = (quotient + 3) * (remainder + 4);
            check_one_int(quotient, 5);
            check_one_int(remainder, 2);
            check_one_int(complex, 48);
        } else {
            int hundred = return_five() * 20;
            int ninety = hundred - 10;
            int seventy = i % 2 ? 0 : hundred / 2 + 20;
            int negative_one_forty_five = (-ninety / 2) - hundred;
            check_one_int(hundred, 100);
            check_one_int(ninety, 90);
            check_one_int(seventy, 70);
            check_one_int(negative_one_forty_five, -145);
        }
    }
    int negative_six = ~return_five();
    int negative_five = -11 - negative_six;
    int negative_four = return_five() - 9;
    int negative_three = negative_six / 2;
    int negative_two = negative_six / 3;
    check_5_ints(negative_six, negative_five, negative_four, negative_three,
                 negative_two, -6);
    return 0;
}
int main(void) { return target(1, 2, 3); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_OptimisticColoring)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C5I + R"WP(
int flag = 0;
int result = 0;
int glob0;
int glob1;
int glob2;
int glob3;
int glob4;
int set_globals(int start) {
    glob0 = start;
    glob1 = start + 1;
    glob2 = start + 2;
    glob3 = start + 3;
    glob4 = start + 4;
    return 0;
}
int target(void) {
    set_globals(0);
    int zero = glob0;
    int one = glob1;
    int two = glob2;
    int three = glob3;
    int four = glob4;
    set_globals(5);
    int five = glob0;
    int six = glob1;
    int seven = glob2;
    int eight = glob3;
    int nine = glob4;
    check_5_ints(zero, one, two, three, four, 0);
    check_5_ints(five, six, seven, eight, nine, 5);
    set_globals(10);
    int ten = glob0;
    int eleven = glob1;
    int twelve = glob2;
    int thirteen = glob3;
    int fourteen = glob4;
    check_5_ints(zero, one, two, three, four, 0);
    check_5_ints(ten, eleven, twelve, thirteen, fourteen, 10);
    check_5_ints(ten, eleven, twelve, thirteen, fourteen, 10);
    check_5_ints(ten, eleven, twelve, thirteen, fourteen, 10);
    check_5_ints(ten, eleven, twelve, thirteen, fourteen, 10);
    check_5_ints(zero - 3, one - 3, two - 3, three - 3, four - 3, -3);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_PreserveAcrossFunCall)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C5I + R"WP(
int glob1 = 1;
int glob2 = 2;
int glob3 = 3;
int glob4 = 4;
int glob5 = 5;
int callee(int a, int b, int c, int d, int e) {
    glob1 = -a;
    glob2 = -b;
    glob3 = -c;
    glob4 = -d;
    glob5 = -e;
    check_5_ints(1, 2, 3, 4, 5, 1);
    return 0;
}
int target(void) {
    int a = 99 * glob1;
    int b = 200 / glob2;
    int c = glob3 ? 104 - glob3 : 0;
    int d = c + (glob4 || glob1);
    int e = 108 - glob5;
    callee(a, b, c, d, e);
    check_one_int(glob1, -99);
    check_one_int(glob2, -100);
    check_one_int(glob3, -101);
    check_one_int(glob4, -102);
    check_one_int(glob5, -103);
    int f = a - 100;
    int g = b - 100;
    int h = c - 100;
    int i = d - 100;
    int j = e - 100;
    glob1 = f;
    glob2 = g;
    glob3 = h;
    glob4 = i;
    glob5 = j;
    check_one_int(a, 99);
    check_one_int(b, 100);
    check_one_int(c, 101);
    check_one_int(d, 102);
    check_one_int(e, 103);
    check_5_ints(glob1, glob2, glob3, glob4, glob5, -1);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_RewriteRegressionTest)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12I + R"WP(
int glob_three = 3;
int glob_four = 4;
int target(void) {
    int imul_result = glob_three * glob_four;
    int add_result = glob_three + glob_four;
    int sub_result = glob_four - glob_three;
    int one = glob_three - 2;
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
    int thirteen = 10 + glob_three;
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
    if (imul_result != 12) return 100;
    if (add_result != 7) return 101;
    if (sub_result != 1) return 102;
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_SameInstrInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int main(void) {
    int a = id(1);
    int b = a + a;
    check_one_int(-1, -1);
    check_one_int(a, 1);
    check_one_int(b, 2);
    int c = id(3);
    int d = c - c;
    check_one_int(0, 0);
    check_one_int(c, 3);
    check_one_int(d, 0);
    int x = id(4);
    int y = x * x;
    check_one_int(-1, -1);
    check_one_int(x, 4);
    check_one_int(y, 16);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_SameInstrNoInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + C5I + R"WP(
int target(void) {
    int a = id(2);
    int b = id(3);
    int c = id(4);
    int d = id(5);
    int e = id(6);
    check_5_ints(a, b, c, d, e, 2);
    int f = a * a;
    int g = b + b;
    int h = c - c;
    int i = d * d;
    int j = e + e;
    check_one_int(0, 0);
    check_one_int(f, 4);
    check_one_int(g, 6);
    check_one_int(h, 0);
    check_one_int(i, 25);
    check_one_int(j, 12);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_TestSpillMetric2)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + C5I + R"WP(
int target(void) {
    int to_spill = id(1);
    int a = id(2);
    int b = id(3);
    int c = id(4);
    int d = id(5);
    int e = id(6);
    check_one_int(to_spill, 1);
    check_5_ints(a, b, c, d, e, 2);
    check_5_ints(1 + a, 1 + b, 1 + c, 1 + d, 1 + e, 3);
    check_one_int(to_spill, 1);
    int f = id(7);
    int g = id(8);
    int h = id(9);
    int i = id(10);
    int j = id(11);
    check_5_ints(f, g, h, i, j, 7);
    check_5_ints(1 + f, 1 + g, 1 + h, 1 + i, 1 + j, 8);
    check_one_int(to_spill, 1);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_TestSpillMetric)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + C5I + R"WP(
int target(void) {
    int a = id(1);
    int b = id(2);
    int c = id(10);
    int d = id(3);
    int e = id(4);
    int f = id(5);
    check_one_int(c, 10);
    check_5_ints(a, b, d, e, f, 1);
    check_5_ints(a + 3, b + 3, d + 3, e + 3, f + 3, 4);
    check_one_int(a * 2, 2);
    check_one_int(b * 2, 4);
    check_one_int(d * 2, 6);
    check_one_int(e * 2, 8);
    check_one_int(f * 2, 10);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_TrackArgRegisters)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12I + R"WP(
int glob1;
int glob2;
int glob3;
int glob4;
int glob5;
int glob6;
int glob7;
int glob8;
int glob9;
int callee(int a, int b, int c) {
    if (a != 10) exit(-1);
    if (b != 11) exit(-1);
    if (c != 12) exit(-1);
    return 0;
}
int target(int one, int two, int three) {
    int four = two + 2;
    int five = three + two;
    int six = 12 - one - two - three;
    int seven = 13 - six;
    int eight = four * two;
    int nine = three * three;
    int ten = six + four;
    int eleven = six * two - one;
    int twelve = six * two;
    glob1 = one;
    glob2 = two;
    glob3 = three;
    glob4 = four;
    glob5 = five;
    glob6 = six;
    glob7 = seven;
    glob8 = eight;
    glob9 = nine;
    callee(ten, eleven, twelve);
    check_12_ints(glob1, glob2, glob3, glob4, glob5, glob6, glob7, glob8, glob9,
                  ten, eleven, twelve, 1);
    return 0;
}
int main(void) { return target(1, 2, 3); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_TriviallyColorable)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C5I + R"WP(
int target(int one, int two) {
    int three = one + two;
    int four = two * two;
    int five = three + two;
    return check_5_ints(one, two, three, four, five, 1);
}
int main(void) { return target(1, 2); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_UnaryInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int main(void) {
    int a = id(100);
    int b = -a;
    check_one_int(-1, -1);
    check_one_int(a, 100);
    check_one_int(b, -100);
    int c = id(200);
    int d = ~c;
    check_one_int(0, 0);
    check_one_int(c, 200);
    check_one_int(d, -201);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_UnaryUsesOperand)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int glob = 1;
int target(void) {
    int a = 0;
    if (id(1)) {
        a = id(100);
    }
    if (id(1)) {
        glob = a + glob;
        a = -a;
    }
    check_one_int(a, -100);
    check_one_int(glob, 101);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntNoCoal_UseAllHardregs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12I + R"WP(
int global_one = 1;
int target(void) {
    int one = 2 - global_one;
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
    return 0;
}
int main(void) { return target(); }
)WP"));
}

// ===========================================================================
// int_only / with_coalescing
// ===========================================================================

TEST_F(BookTest, Chapter20_IntCoal_BriggsCoalesceHardreg)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C5I + R"WP(
int flag = 1;
int target(void) {
    int coalesce_into_eax = 0;
    if (flag) {
        coalesce_into_eax = id(10);
    }
    int high_degree = 2 * coalesce_into_eax;
    if (coalesce_into_eax != 10) {
        return -1;
    }
    int twelve = 32 - high_degree;
    int eleven = 23 - twelve;
    int ten = 21 - eleven;
    int nine = 19 - ten;
    int eight = 17 - nine;
    int seven = 15 - eight;
    int six = 13 - seven;
    int five = 11 - six;
    int four = 24 - high_degree;
    int three = 23 - high_degree;
    int two = 22 - high_degree;
    int one = 21 - high_degree;
    check_5_ints(one, two, three, four, five, 1);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_BriggsCoalesce)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C12I + R"WP(
int glob = 5;
int glob7;
int glob8;
int glob9;
int glob10;
int glob11;
int glob12;
int target(int one, int two, int three, int four, int five, int six) {
    int seven = (glob - 2) + four;
    int eight = (glob - 1) * two;
    int nine = (glob - 2) * three;
    int ten = (10 - glob) * two;
    int eleven = (glob * two) + one;
    int twelve = (glob + 1) * two;
    glob7 = seven;
    glob8 = eight;
    glob9 = nine;
    glob10 = ten;
    glob11 = eleven;
    glob12 = twelve;
    check_12_ints(one, two, three, four, five, six, 7, 8, 9, 10, 11, 12, 1);
    check_one_int(glob7, 7);
    check_one_int(glob8, 8);
    check_one_int(glob9, 9);
    check_one_int(glob10, 10);
    check_one_int(glob11, 11);
    check_one_int(glob12, 12);
    return 0;
}
int main(void) { return target(1, 2, 3, 4, 5, 6); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_BriggsDontCoalesce)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
int glob = 5;
int update_glob(void) {
    glob = glob + 1;
    return 0;
}
int target(void) {
    int z = glob + 10;
    int a;
    int one = glob - 4;
    int two = glob / 2;
    int three = -glob + 8;
    int four = glob - 1;
    update_glob();
    if (glob) {
       a = 1 - z;
    } else {
        a = 5;
    }
    check_one_int(one, 1);
    check_one_int(two, 2);
    check_one_int(three, 3);
    check_one_int(four, 4);
    check_one_int(a, -14);
    check_one_int(glob, 6);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_CoalescePreventsSpill)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + R"WP(
int glob = 5;
int flag = 0;
int validate(int a, int b, int c, int d, int e, int f, int g, int h, int i,
             int j, int k, int l, int m) {
    int args[13] = {a, b, c, d, e, f, g, h, i, j, k, l, m};
    for (int n = 0; n < 13; n++) { if (args[n] != 10) exit(-1); }
    return 0;
}
int target(int arg) {
    int a; int b; int c; int d; int e; int f; int g; int h; int i; int j;
    int k; int l; int m;
    if (flag) {
        a = arg; b = arg; c = arg; d = arg; e = arg; f = arg; g = arg;
        h = arg; i = arg; j = arg; k = arg; l = arg; m = arg;
    } else {
        a = glob * 2;
        b = a; c = a; d = a; e = a; f = a; g = a; h = a; i = a; j = a;
        k = a; l = a; m = a;
    }
    return validate(a, b, c, d, e, f, g, h, i, j, k, l, m);
}
int main(void) { return target(1); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_GeorgeCoalesce)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C5I + R"WP(
int glob = 1;
int increment_glob(void) {
    glob = glob + 1;
    return 0;
}
int target(int a, int b, int c, int d, int e, int f) {
    if (a != 1) return 1;
    if (b != 2) return 2;
    if (c != 3) return 3;
    if (d != 4) return 4;
    if (e != 5) return 5;
    if (f != 6) return 6;
    int one = glob * 1;
    int two = glob * 2;
    int three = glob * 3;
    int four = glob * 4;
    if (one != 1) return 7;
    if (two != 2) return 8;
    if (three != 3) return 9;
    if (four != 4) return 10;
    increment_glob();
    int five = 4 + one;
    int six = 4 + two;
    int seven = 4 + three;
    int eight = 4 + four;
    if (five != 5) return 11;
    if (six != 6) return 12;
    if (seven != 7) return 13;
    if (eight != 8) return 14;
    increment_glob();
    int nine = 14 - five;
    int ten = 16 - six;
    int eleven = 18 - seven;
    int twelve = 20 - eight;
    increment_glob();
    if (nine != 9) return 15;
    if (ten != 10) return 16;
    if (eleven != 11) return 17;
    if (twelve != 12) return 18;
    int s = glob - 3;
    int t = glob - 2;
    int u = glob - 1;
    int v = glob * 2 - 4;
    int w = glob + 1;
    check_5_ints(s, t, u, v, w, 1);
    return check_one_int(glob, 4);
}
int main(void) { return target(1, 2, 3, 4, 5, 6); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_GeorgeDontCoalesce2)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C5I + R"WP(
int glob = 1;
int update_glob(void) {
    glob = 0;
    return 0;
}
int target(void) {
    int a = glob * 2;
    int b = glob * 3;
    int c = glob * 4;
    int d = glob * 5;
    int e = glob * 6;
    update_glob();
    int f = a + d;
    int g = b * 3 - 1;
    int h = c + d;
    int i = c + e;
    int j = d * 2 + 1;
    int k = e * 2;
    int l = a + b + c + d + e;
    int m = 3 * f;
    int n = g * 3 - 2;
    int o = h * 2 + 5;
    int p = i * 2 + 4;
    glob = glob + f + g + h + i + j + k;
    check_5_ints(l, m, n, o, p, 20);
    check_one_int(glob, 57);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_GeorgeDontCoalesce)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12I + R"WP(
int glob = 1;
int target(int a, int b, int c, int d, int e, int f) {
    int g = a + f;
    int h = b * d;
    int i = c * c;
    int j = d + f;
    int k = e + f;
    int l = f * b;
    int m = (a + b + c + d + e + f) - 7;
    int n = g + h;
    int o = i + 7;
    int p = j * 2 - 3;
    int q = k + g;
    check_12_ints(g, h, i, j, k, l, 13, m, n, o, p, q, 7);
    return 0;
}
int main(void) { return target(1, 2, 3, 4, 5, 6); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_GeorgeOffByOne)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C12I + R"WP(
int glob = 0;
int target(int a) {
    int one = 2 - a;
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
    int twelve = eleven + one;
    glob = one;
    check_12_ints(1, two, three, four, five, six, seven, eight, nine, ten,
                  eleven, twelve, 1);
    check_one_int(glob, 1);
    return 0;
}
int main(void) { return target(1); }
)WP"));
}

TEST_F(BookTest, Chapter20_IntCoal_NoGeorgeTestForPseudos)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + C5I + R"WP(
int target(void) {
    int a = id(1);
    int b = id(2);
    int x = id(10);
    check_one_int(x, 10);
    int c = x;
    if (!a) {
        c = 100;
    }
    int d = id(3);
    int e = id(4);
    int f = id(5);
    check_5_ints(a, b, d, e, f, 1);
    check_5_ints(a + 3, b + 3, d + 3, e + 3, f + 3, 4);
    check_5_ints(a + 4, b + 4, d + 4, e + 4, f + 4, 5);
    check_one_int(a * 2, 2);
    check_one_int(b * 2, 4);
    check_one_int(d * 2, 6);
    check_one_int(e * 2, 8);
    check_one_int(f * 2, 10);
    check_one_int(a * 3, 3);
    check_one_int(b * 3, 6);
    check_one_int(d * 3, 9);
    check_one_int(e * 3, 12);
    check_one_int(f * 3, 15);
    int g = c;
    if (!f) {
        g = -1;
    }
    check_one_int(g, 10);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

// ===========================================================================
// all_types / no_coalescing
// ===========================================================================

TEST_F(BookTest, Chapter20_AllNoCoal_AliasingOptimizedAway)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
int target(int arg) {
    int *optimized_away = &arg;
    return arg + 10;
}
int main(void) { return target(1) == 11 ? 0 : 1; }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DblBinUsesOperands)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + DBLID + C1D + R"WP(
double src_test(double arg) {
    double x = 5 + arg;
    check_one_double(x, 6.0);
    return 0;
}
double glob = 1;
double glob2;
int flag = 1;
int dst_test(void) {
    double a = dbl_id(100.0);
    if (flag) {
        glob2 = a + glob;
        a = a / 2.0;
    }
    check_one_double(a, 50.0);
    check_one_double(glob2, 101.0);
    return 0;
}
int main(void) {
    src_test(1);
    dst_test();
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DblFunCall)
{
    // callee() is x86 asm (clobber_xmm_regs); stubbed to return 10.0.
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
double glob = 3.0;
double callee(void) { return 10.0; }
int main(void) {
    double d = glob;
    double x = callee();
    return (d + x == 13.0) ? 0 : 1;
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DblFuncallGeneratesArgs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + R"WP(
int use_dbls(double a, double b) {
    if (a != 11.0) exit(-1);
    if (b != 12.0) exit(-1);
    return 0;
}
double glob = 10.0;
double x = 0.0;
double y = 0.0;
int target(void) {
    double a = glob + 1.0;
    double b = glob + 2.0;
    x = a * glob;
    y = b * glob;
    use_dbls(a, b);
    check_one_double(x, 110.0);
    check_one_double(y, 120.0);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DblTriviallyColorable)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
int target(double x, double y) {
    return 10 - (3.0 * y + x);
}
int main(void) { return target(1.0, 2.0) == 3 ? 0 : 1; }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DivInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
unsigned int glob = 3;
int target(void) {
    unsigned int dividend = glob * 16;
    unsigned int quotient = dividend / 4;
    glob = dividend;
    check_one_int(quotient, 12);
    return 0;
}
int main(void) {
    target();
    check_one_int(glob, 48);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_DivUsesAx)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + UID + C1U + R"WP(
int main(void) {
    unsigned int coalesce_into_eax = unsigned_id(10);
    unsigned int sum = coalesce_into_eax + 4;
    if (sum != 14) {
        return -1;
    }
    unsigned int rem = coalesce_into_eax % 10;
    check_one_uint(rem, 0);
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_ForceSpillDoubles)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C14D + R"WP(
int glob = 3;
double glob2 = 4.0;
int target(void) {
    double should_spill = (double)glob;
    double one = 4.0 - glob;
    double two = one + one;
    double three = (double)glob;
    double four = two * two;
    double five = glob2 + 1;
    double six = glob * 2;
    double seven = one * one + 6.0;
    double eight = two * 4;
    double nine = three * three;
    double ten = four + six;
    double eleven = 16 - five;
    double twelve = six + six;
    double thirteen = five + eight;
    double fourteen = 21 - seven;
    check_14_doubles(one, two, three, four, five, six, seven, eight, nine, ten,
                     eleven, twelve, thirteen, fourteen, 1.0);
    double fifteen = glob2 * 4.0 - 1;
    double sixteen = glob2 * 4.0;
    double seventeen = fifteen + 2.0;
    double eighteen = 35.0 - seventeen;
    double nineteen = sixteen + glob;
    double twenty = glob2 * 5.0;
    double twenty_one = glob * 7.0;
    double twenty_two = 4.0 + eighteen;
    double twenty_three = nineteen + glob + 1;
    double twenty_four = glob2 + twenty;
    double twenty_five = twenty_one + glob2;
    double twenty_six = twenty_five - nineteen + twenty;
    double twenty_seven = glob * 9.0;
    double twenty_eight = twenty_two + 6;
    check_14_doubles(fifteen, sixteen, seventeen, eighteen, nineteen, twenty,
                     twenty_one, twenty_two, twenty_three, twenty_four,
                     twenty_five, twenty_six, twenty_seven, twenty_eight, 15.0);
    if (should_spill != 3.0) {
        return -1;
    }
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_ForceSpillMixedInts)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C12V + R"WP(
unsigned int glob_three = 3;
long glob_11 = 11l;
double glob_12 = 12.0;
long glob_23 = 23l;
double glob_24 = 24.0;
int target(void) {
    long should_spill = glob_three + 3;
    unsigned int one = glob_three - 2;
    long two = one + one;
    unsigned long three = 2 + one;
    char four = two * two;
    signed char five = 6 - one;
    int six = two * three;
    unsigned char seven = one + 6;
    long eight = two * 4;
    unsigned long nine = three * three;
    char ten = four + six;
    long* eleven = &glob_11;
    double* twelve = &glob_12;
    check_12_vals(one, two, three, four, five, six, seven, eight, nine, ten,
                  eleven, twelve, 1);
    unsigned int thirteen = 10 + glob_three;
    long fourteen = thirteen + 1;
    unsigned long fifteen = 28 - thirteen;
    char sixteen = fourteen + 2;
    signed char seventeen = 4 + thirteen;
    int eighteen = 32 - fourteen;
    unsigned char nineteen = 35 - sixteen;
    unsigned int twenty = fifteen + 5;
    long twenty_one = thirteen * 2 - 5;
    unsigned long twenty_two = fifteen + 7;
    long* twenty_three = &glob_23;
    double* twenty_four = &glob_24;
    check_12_vals(thirteen, fourteen, fifteen, sixteen, seventeen, eighteen,
                  nineteen, twenty, twenty_one, twenty_two, twenty_three,
                  twenty_four, 13);
    if (should_spill != 6) {
        return -1;
    }
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_FourteenPseudosInterfere)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
double glob = 20.0;
double glob2 = 30.0;
int glob3 = 40.0;
int target(void) {
    double a = glob * glob;
    double b = glob2 + 2.0;
    double c = a + 5.0;
    double d = b - glob3;
    double e = glob + 7.0;
    double f = glob2 * 2.0;
    double g = c * 3.0;
    double h = d * 112.;
    double i = e / 3.0;
    double j = g + f;
    double k = h - j;
    double l = i + 1000.;
    double m = j - d;
    double n = m * l;
    if (a == 400.0 && b == 32.0 && c == 405.0 && d == -8.0 && e == 27.0 &&
        f == 60.0 && g == 1215.0 && h == -896. && i == 9.0 && j == 1275. &&
        k == -2171. && l == 1009. && m == 1283. && n == 1294547.) {
        return 0;
    } else {
        return 1;
    }
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_GpXmmMixed)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C5I + C14D + R"WP(
int target(int one, int two, double one_d, double two_d, int three,
           double three_d) {
    long four = two * two;
    long five = three + two_d;
    double ten_d = three * two_d + four;
    double eleven_d = ten_d + one;
    long six = three * two_d;
    long seven = four + 3;
    double twelve_d = six * two_d;
    double thirteen_d = 14.0 - one_d;
    double fourteen_d = seven * two;
    double fifteen_d = twelve_d + three;
    double sixteen_d = four * four;
    double seventeen_d = ten_d + seven;
    double eighteen_d = three_d * six;
    unsigned long eight = four * two;
    double nineteen_d = 20 - one;
    double twenty_d = four * five;
    double twenty_one_d = three * 7;
    double twenty_two_d = eleven_d * 2;
    double twenty_three_d = ten_d + thirteen_d;
    check_14_doubles(ten_d, eleven_d, twelve_d, thirteen_d, fourteen_d,
                     fifteen_d, sixteen_d, seventeen_d, eighteen_d, nineteen_d,
                     twenty_d, twenty_one_d, twenty_two_d, twenty_three_d,
                     10.0);
    check_5_ints(four, five, six, seven, eight, 4);
    return 0;
}
int main(void) { return target(1, 2, 1.0, 2.0, 3, 3.0); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_IndexedOperandReadsRegs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C1L + R"WP(
int arr[2] = {1, 2};
long arr2[2] = {3, 4};
int three = 3;
int main(void) {
    long one = three - 2;
    long zero = three - 3;
    int *ptr = arr;
    long *ptr2 = arr2;
    int *other_ptr = ptr + one;
    long *other_ptr2 = ptr2 + zero;
    check_one_int(*other_ptr, 2);
    check_one_long(*other_ptr2, 3);
    return 0;
}
)WP"));
}

// Passes mixed-member structs by value — now covered by the by-value ABI.
TEST_F(BookTest, Chapter20_AllNoCoal_MixedTypeArgRegisters)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C14D + R"WP(
struct s1 { double d; char c; int i; };
struct s2 { unsigned long ul; double d; };
struct s3 { double d1; double d2; signed char s; };
int callee(struct s1 a, struct s2 b, char c, struct s3 in_mem) {
    if (a.d != 11.0) exit(-1);
    if (a.c != 8) exit(-1);
    if (a.i != 9) exit(-1);
    if (b.ul != 10ul) exit(-1);
    if (b.d != 12.0) exit(-1);
    if (c != 12) exit(-1);
    if (in_mem.d1 != 13.0) exit(-1);
    if (in_mem.d2 != 14.0) exit(-1);
    if (in_mem.s != 11) exit(-1);
    return 0;
}
int check_some_args(int one, long two, unsigned int three, unsigned long four,
                    char five, unsigned char six, signed char seven) {
    if (one != 1) exit(-1);
    if (two != 2l) exit(-1);
    if (three != 3u) exit(-1);
    if (four != 4ul) exit(-1);
    if (five != 5) exit(-1);
    if (six != 6) exit(-1);
    if (seven != 7) exit(-1);
    return 0;
}
int glob1; long glob2; unsigned int glob3; unsigned long glob4;
char glob5; unsigned char glob6; signed char glob7; long glob8;
double glob1_d; double glob2_d; double glob3_d; double glob4_d; double glob5_d;
double glob6_d; double glob7_d; double glob8_d; double glob9_d; double glob10_d;
double glob11_d; double glob12_d; double glob13_d;
int target(int one, int two, int three, double one_d, double two_d) {
    long four = two + 2;
    char five = three + two;
    int six = 12 - one - two - three;
    unsigned int seven = 13 - six;
    unsigned char eight = four * two;
    unsigned long nine = three * three;
    signed long ten = six + four;
    signed char eleven = six * two - one;
    int twelve = six * two;
    double three_d = one_d + two_d;
    double four_d = three_d + one_d;
    double five_d = two_d + three_d;
    double six_d = three_d * two_d;
    double seven_d = 13. - six_d;
    double eight_d = four_d * two_d;
    double nine_d = three_d * three_d;
    double ten_d = five_d * two_d;
    double eleven_d = seven_d * two_d - three_d;
    double twelve_d = eight_d * four_d - 20.;
    double thirteen_d = (nine_d + ten_d) - six_d;
    double fourteen_d = eleven_d + 3;
    glob1 = one; glob2 = two; glob3 = three; glob4 = four; glob5 = five;
    glob6 = six; glob7 = seven;
    glob1_d = one_d; glob2_d = two_d; glob3_d = three_d; glob4_d = four_d;
    glob5_d = five_d; glob6_d = six_d; glob7_d = seven_d; glob8_d = eight_d;
    glob9_d = nine_d; glob10_d = ten_d;
    struct s1 arg1 = {eleven_d, eight, nine};
    struct s2 arg2 = {ten, twelve_d};
    struct s3 in_mem = {thirteen_d, fourteen_d, eleven};
    callee(arg1, arg2, twelve, in_mem);
    check_some_args(glob1, glob2, glob3, glob4, glob5, glob6, glob7);
    check_14_doubles(glob1_d, glob2_d, glob3_d, glob4_d, glob5_d, glob6_d,
                     glob7_d, glob8_d, glob9_d, glob10_d, 11.0, 12.0, 13., 14.,
                     1);
    return 0;
}
int main(void) { return target(1, 2, 3, 1.0, 2.0); }
)WP"));
}

// Passes a struct by value — now covered by the by-value ABI.
TEST_F(BookTest, Chapter20_AllNoCoal_MixedTypeFuncallGeneratesArgs)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1L + C1D + R"WP(
struct s { long l; double d; };
long glob = 100;
double glob_d = 200.0;
long x = 0;
double y = 0;
int callee(struct s s1, long a, double b) {
    if (s1.l != -50) exit(-1);
    if (s1.d != -40.0) exit(-1);
    if (a != 101) exit(-1);
    if (b != 202.0) exit(-1);
    return 0;
}
int main(void) {
    long a = glob + 1;
    double b = glob_d + 2.0;
    struct s s1 = {-50, -40.0};
    x = a * glob;
    y = b * glob_d;
    callee(s1, a, b);
    check_one_long(x, 10100);
    check_one_double(y, 40400.0);
    return 0;
}
)WP"));
}

// DISABLED: block-scope static + char-array string init + x86 alignment asm.
TEST_F(BookTest, Chapter20_AllNoCoal_MixedTypeStackAlignment)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + C1I + R"WP(
int check_alignment(int exit_code) { return 0; }
int test1(void) {
    long a = id(1);
    unsigned long b = id(2);
    long c = id(3);
    unsigned long d = id(4);
    long e = id(5);
    unsigned long f = id(6);
    long g = id(7);
    check_alignment(-1);
    check_one_int(a, 1);
    check_one_int(b, 2);
    check_one_int(c, 3);
    check_one_int(d, 4);
    check_one_int(e, 5);
    check_one_int(f, 6);
    check_one_int(g, 7);
    return 0;
}
int test2(void) {
    char a = id(4);
    unsigned int b = id(5);
    char arr[11] = { 'a', 'b', 'c', 'd','e', 'f', 'g', 'h', 'i', 'j', 'k', };
    check_alignment(-2);
    check_one_int(a, 4);
    check_one_int(b, 5);
    for (int i = 0; i < 11; i = i + 1) {
        check_one_int(arr[i], 'a' + i);
    }
    return 0;
}
int test3(void) {
    static int *ptr;
    char a = id(4);
    unsigned char b = id(5);
    long c = id(6);
    int aliased = 10;
    ptr = &aliased;
    check_alignment(-3);
    check_one_int(a, 4);
    check_one_int(b, 5);
    check_one_int(c, 6);
    check_one_int(*ptr, 10);
    return 0;
}
int main(void) { test1(); test2(); test3(); return 0; }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_OneAliasedVar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C1L + C1D + R"WP(
void increment(int *ptr) {
    *ptr = *ptr + 1;
    return;
}
double deref(double *ptr) {
    return *ptr;
}
int target(int one, int two, int three, double one_d) {
    int *ptr = &one;
    double *d_ptr = &one_d;
    check_one_double(deref(d_ptr), 1.0);
    increment(ptr);
    long five = two + three;
    check_one_int(one, 2);
    check_one_long(five, 5l);
    return 0;
}
int main(void) { return target(1, 2, 3, 1.0); }
)WP"));
}

// Adapted: dropped the x86 `(long)ptr % 8` 8-byte-alignment check (BESM-6
// pointers are word addresses); the nonzero check still keeps the pointer live.
TEST_F(BookTest, Chapter20_AllNoCoal_PtrRaxLiveAtExit)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + R"WP(
long arr[3] = {100, 200, 300};
long glob2;
long *target(void) {
    long *ptr = arr;
    glob2 = (long)ptr + 80;
    return ptr;
}
int main(void) {
    long *retval = target();
    check_one_int(retval[0], 100);
    check_one_int(retval[1], 200);
    check_one_int(retval[2], 300);
    if (glob2 == 0) {
        return -2;
    }
    return 0;
}
)WP"));
}

// Returns a struct by value — now covered by the hidden-pointer sret ABI.
TEST_F(BookTest, Chapter20_AllNoCoal_ReturnAllIntStruct)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + R"WP(
struct s { int a; int b; long l; };
double glob = 20.0;
double glob2 = 30.0;
int glob3 = 40.0;
struct s return_struct(void) {
    double a = glob * glob;
    double b = glob2 + 2.0;
    double c = a + 5.0;
    double d = b - glob3;
    double e = glob + 7.0;
    double f = glob2 * 2.0;
    double g = c * 3.0;
    double h = d * 112.;
    double i = e / 3.0;
    double j = g + f;
    double k = h - j;
    double l = i + 1000.;
    double m = j - d;
    double n = m * l;
    if (a == 400.0 && b == 32.0 && c == 405.0 && d == -8.0 && e == 27.0 &&
        f == 60.0 && g == 1215.0 && h == -896. && i == 9.0 && j == 1275. &&
        k == -2171. && l == 1009. && m == 1283. && n == 1294547.) {
        struct s retval = {20, 30, 40};
        return retval;
    } else {
        struct s retval = {-1, -2, -3};
        return retval;
    }
}
int target(void) {
    struct s retval = return_struct();
    if (retval.a != 20) exit(-1);
    if (retval.b != 30) exit(-1);
    if (retval.l != 40) exit(-1);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

// Returns a struct by value — now covered by the hidden-pointer sret ABI.
TEST_F(BookTest, Chapter20_AllNoCoal_ReturnDoubleStruct)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + R"WP(
struct s { double d1; double d2; };
int global_one = 1;
struct s return_struct(void) {
    int one = 2 - global_one;
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
    if (one == 1 && two == 2 && three == 3 && four == 4 && five == 5 &&
        six == 6 && seven == 7 && eight == 8 && nine == 9 && ten == 10 &&
        eleven == 11 && twelve == 12) {
        struct s retval = {0.0, 200.0};
        return retval;
    } else {
        struct s retval = {1.0, -1.0};
        return retval;
    }
}
int target(void) {
    struct s retval = return_struct();
    if (retval.d1 != 0.0) exit(-1);
    if (retval.d2 != 200.0) exit(-1);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_ReturnDouble)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"WP(
int global_one = 1;
double return_double(void) {
    int one = 2 - global_one;
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
    if (one == 1 && two == 2 && three == 3 && four == 4 && five == 5 &&
        six == 6 && seven == 7 && eight == 8 && nine == 9 && ten == 10 &&
        eleven == 11 && twelve == 12) {
        return 0.0;
    } else {
        return 1.0;
    }
}
int target(void) {
    return (int) return_double();
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_StorePointerInRegister)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C5I + R"WP(
int glob1 = 1;
int glob2 = 2;
int glob3 = 3;
int glob4 = 4;
int glob5 = 5;
int glob6 = 6;
int glob7 = 7;
int flag = 1;
int* store_a;
int target(void) {
    int callee_saved1 = glob1;
    int callee_saved2 = glob2;
    int callee_saved3 = glob3;
    int callee_saved4 = glob4;
    int callee_saved5 = glob5;
    check_5_ints(1, 2, 3, 4, 5, 1);
    int* a;
    int* b;
    int* c;
    int* d;
    int* e;
    int* f;
    int* g;
    if (flag) {
        a = &glob1; *a = 2;
        b = &glob2; *b = 4;
        c = &glob3; *c = 6;
        d = &glob4; *d = 8;
        e = &glob5; *e = 10;
        f = &glob6; *f = 12;
        g = &glob7; *g = 14;
        store_a = a;
    } else {
        a = 0; b = 0; c = 0; d = 0; e = 0; f = 0; g = 0;
    }
    if (b != &glob2 || c != &glob3 || d != &glob4 || e != &glob5 ||
        f != &glob6 || g != &glob7) {
        return 1;
    }
    if (glob1 != 2 || glob2 != 4 || glob3 != 6 || glob4 != 8 || glob5 != 10 ||
        glob6 != 12 || glob7 != 14) {
        return 2;
    }
    if (store_a != &glob1) {
        return 3;
    }
    if (callee_saved1 != 1 || callee_saved2 != 2 || callee_saved3 != 3 ||
        callee_saved4 != 4 || callee_saved5 != 5) {
        return 4;
    }
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_TrackDblArgRegisters)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C14D + R"WP(
int callee(double a, double b, double c) {
    if (a != 12) exit(-1);
    if (b != 13) exit(-1);
    if (c != 14) exit(-1);
    return 0;
}
double glob1; double glob2; double glob3; double glob4; double glob5;
double glob6; double glob7; double glob8; double glob9; double glob10;
double glob11;
int target(double one, double two, double three) {
    double four = three + one;
    double five = two + three;
    double six = three * two;
    double seven = 13. - six;
    double eight = four * two;
    double nine = three * three;
    double ten = five * two;
    double eleven = seven * two - three;
    double twelve = eight * four - 20.;
    double thirteen = (nine + ten) - six;
    double fourteen = eleven + 3;
    glob1 = one; glob2 = two; glob3 = three; glob4 = four; glob5 = five;
    glob6 = six; glob7 = seven; glob8 = eight; glob9 = nine; glob10 = ten;
    glob11 = eleven;
    callee(twelve, thirteen, fourteen);
    check_14_doubles(glob1, glob2, glob3, glob4, glob5, glob6, glob7, glob8,
                     glob9, glob10, glob11, 12., 13., 14., 1);
    return 0;
}
int main(void) { return target(1.0, 2.0, 3.0); }
)WP"));
}

// Plain `char` may be unsigned, so neg_char/not_char use `signed char` to keep the
// sign extension.
TEST_F(BookTest, Chapter20_AllNoCoal_TypeConversionInterference)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + ID + DBLID + UID + UCID + C1I + C1U + C1UC + C1L +
                                       C1UL + C1D + C14D + R"WP(
int glob;
int test_movsx_src(int i) {
    check_one_int(i - 10, -5);
    long l = 0;
    l = (long)i;
    check_one_long(l, 5l);
    return 0;
}
signed char glob_char = 10;
int test_movsx_dst(void) {
    unsigned long a = id(-1);
    unsigned long b = id(2);
    signed char neg_char = -glob_char;
    signed char not_char = ~glob_char;
    int c = (int)glob_char;
    long d = id(4);
    unsigned int e = (unsigned int)neg_char;
    long f = (long) not_char;
    check_one_ulong(a, 18446744073709551615ul);
    check_one_ulong(b, 2ul);
    check_one_int(c, 10);
    check_one_long(d, 4l);
    check_one_uint(e, -10);
    check_one_long(f, -11);
    return 0;
}
unsigned int glob_uint;
int test_movzx_src(unsigned int u) {
    check_one_uint(u + 10u, 30u);
    long l = (long)u;
    check_one_long(l, 20l);
    return 0;
}
int test_movzx_dst(void) {
    long a = (long)unsigned_id(2000u);
    unsigned long b = (unsigned long)unsigned_id(1000u);
    unsigned long c = (unsigned long)unsigned_id(255u);
    long d = (long)unsigned_id(4294967295U);
    long e = (long)unsigned_id(2147483650u);
    unsigned long f = (unsigned long)unsigned_id(80u);
    check_one_long(a, 2000l);
    check_one_ulong(b, 1000ul);
    check_one_ulong(c, 255ul);
    check_one_long(d, 4294967295l);
    check_one_long(e, 2147483650l);
    check_one_ulong(f, 80ul);
    return 0;
}
int test_movzbq_src(unsigned char c) {
    unsigned char d = c + 1;
    check_one_uchar(d, 13);
    long l = (long)c;
    check_one_long(l, 12);
    return 0;
}
int test_movzb_dst(void) {
    int a = (int)uchar_id(200);
    unsigned int b = (unsigned int)uchar_id(100);
    unsigned long c = (unsigned long)uchar_id(255);
    long d = (long)uchar_id(77);
    long e = (long)uchar_id(125);
    unsigned long f = (unsigned long)uchar_id(80);
    check_one_int(a, 200);
    check_one_uint(b, 100u);
    check_one_ulong(c, 255ul);
    check_one_long(d, 77l);
    check_one_long(e, 125l);
    check_one_ulong(f, 80ul);
    return 0;
}
int test_cvtsi2sd_src(int i) {
    check_one_int(i + 10, 16);
    double d = (double)i;
    check_one_double(d, 6.0);
    return 0;
}
int global_int = 5000;
long global_long = 5005;
int test_cvtsi2sd_dst(void) {
    double d0 = (double)global_int;
    double d1 = (double)(global_long - 4l);
    double d2 = (double)(global_int + 2);
    double d3 = (double)(global_long - 2l);
    double d4 = (double)(global_int + 4);
    double d5 = (double)(global_int + 5);
    double d6 = (double)(global_int + 6);
    double d7 = (double)(global_int + 7);
    double d8 = (double)(global_int + 8);
    double d9 = (double)(global_int + 9);
    double d10 = (double)(global_int + 10);
    double d11 = (double)(global_int + 11);
    double d12 = (double)(global_int + 12);
    double d13 = (double)(global_int + 13);
    double d14 = (double)(global_int + 14);
    global_long = (long)d14;
    check_14_doubles(d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13,
                     5000);
    check_one_int(global_int, 5000);
    check_one_long(global_long, 5014l);
    return 0;
}
double glob_dbl;
int test_cvttsd2si_src(double d) {
    glob_dbl = d + 10.0;
    int i = (int)d;
    check_one_int(i, 7);
    check_one_double(glob_dbl, 17.0);
    return 0;
}
int test_cvttsd2si_dst(void) {
    int a = (int)dbl_id(-200.0);
    long b = (long)dbl_id(-300.0);
    int c = (int)dbl_id(-400.0);
    long d = (long)dbl_id(-500.0);
    int e = (int)dbl_id(-600.0);
    long f = (long)dbl_id(-700.0);
    check_one_int(a, -200);
    check_one_long(b, -300l);
    check_one_int(c, -400);
    check_one_long(d, -500l);
    check_one_int(e, -600);
    check_one_long(f, -700l);
    return 0;
}
int main(void) {
    test_movsx_src(5);
    test_movsx_dst();
    test_movzx_src(20u);
    test_movzx_dst();
    test_movzbq_src(12);
    test_movzb_dst();
    test_cvtsi2sd_src(6);
    test_cvtsi2sd_dst();
    test_cvttsd2si_src(7.0);
    test_cvttsd2si_dst();
    return 0;
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllNoCoal_Xmm0LiveAtExit)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + R"WP(
double glob = 10.0;
double glob2 = 0.0;
double target(void) {
    double x = glob + 1.0;
    glob2 = x + glob;
    return x;
}
int main(void) {
    double retval = target();
    check_one_double(retval, 11.0);
    check_one_double(glob2, 21.0);
    return 0;
}
)WP"));
}

// ===========================================================================
// all_types / with_coalescing
// ===========================================================================

TEST_F(BookTest, Chapter20_AllCoal_BriggsCoalesceLong)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1L + C12L + R"WP(
long glob = 5l;
long glob7;
long glob8;
long glob9;
long glob10;
long glob11;
long glob12;
int target(long one, long two, long three, long four, long five, long six) {
    long seven = (glob - 2l) + four;
    long eight = (glob - 1l) * two;
    long nine = (glob - 2l) * three;
    long ten = (10l - glob) * two;
    long eleven = (glob * two) + one;
    long twelve = (glob + 1l) * two;
    glob7 = seven;
    glob8 = eight;
    glob9 = nine;
    glob10 = ten;
    glob11 = eleven;
    glob12 = twelve;
    check_12_longs(one, two, three, four, five, six, 7l, 8l, 9l, 10l, 11l, 12l,
                   1l);
    check_one_long(glob7, 7l);
    check_one_long(glob8, 8l);
    check_one_long(glob9, 9l);
    check_one_long(glob10, 10l);
    check_one_long(glob11, 11l);
    check_one_long(glob12, 12l);
    return 0;
}
int main(void) { return target(1, 2, 3, 4, 5, 6); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllCoal_BriggsCoalesceXmm)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + C14D + R"WP(
double glob = 5.0;
double glob9;
double glob10;
double glob11;
double glob12;
double glob13;
double glob14;
int target(double one, double two, double three, double four, double five,
           double six, double seven, double eight) {
    double nine = (glob - 2.0) * three;
    double ten = (10.0 - glob) * two;
    double eleven = (glob * two) + one;
    double twelve = (glob + 1.0) * two;
    double thirteen = (2. * two) + 9.;
    double fourteen = (3. + four) * 2.;
    glob9 = nine;
    glob10 = ten;
    glob11 = eleven;
    glob12 = twelve;
    glob13 = thirteen;
    glob14 = fourteen;
    check_14_doubles(one, two, three, four, five, six, seven, eight, 9.0, 10.0,
                     11.0, 12.0, 13.0, 14.0, 1.0);
    check_one_double(glob9, 9.0);
    check_one_double(glob10, 10.0);
    check_one_double(glob11, 11.0);
    check_one_double(glob12, 12.0);
    check_one_double(glob13, 13.0);
    check_one_double(glob14, 14.0);
    return 0;
}
int main(void) { return target(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllCoal_BriggsXmmKValue)
{
    // Result globals shortened to gr0..gr14: the book's glob_four / glob_fourteen
    // collide under the BESM-6 8-character identifier truncation ("glob_fou").
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + R"WP(
double glob0 = 0;
double glob1 = 1.;
double glob2 = 2.;
double glob10 = 10.;
double gr0;
double gr1;
double gr2;
double gr3;
double gr4;
double gr5;
double gr6;
double gr7;
double gr8;
double gr9;
double gr10;
double gr11;
double gr12;
double gr13;
double gr14;
void incr_glob1(void) {
    glob1 = glob1 + 1;
}
int target(void) {
    double zero = glob0 * 10.;
    double one = glob10 / 2. - 4.;
    double two = glob10 / 2. - 3.;
    double three = glob2 * 2. - 1;
    double four = (6. - glob2) * glob1;
    double five = (glob10 / 2.) * glob1;
    double six = (glob10 + 2.) / 2;
    double seven = 3. * glob2 + 1.;
    double eight = glob2 * glob2 * 2.;
    double nine = (glob1 + glob2) * 3.;
    double ten = (glob2 + 3.) * 2.;
    double eleven = (glob10 + 1.) * glob1;
    double twelve = (glob1 + glob2) * 4.;
    double thirteen = (glob2 * 3.) + 7.;
    gr0 = zero;
    double fourteen = glob2 * 7.;
    gr1 = one;
    gr2 = two;
    gr3 = three;
    gr4 = four;
    gr5 = five;
    gr6 = six;
    gr7 = seven;
    gr8 = eight;
    gr9 = nine;
    gr10 = ten;
    gr11 = eleven;
    gr12 = twelve;
    gr13 = thirteen;
    gr14 = fourteen;
    incr_glob1();
    check_one_double(gr0, 0.);
    check_one_double(gr1, 1.0);
    check_one_double(gr2, 2.0);
    check_one_double(gr3, 3.0);
    check_one_double(gr4, 4.0);
    check_one_double(gr5, 5.0);
    check_one_double(gr6, 6.0);
    check_one_double(gr7, 7.0);
    check_one_double(gr8, 8.0);
    check_one_double(gr9, 9.0);
    check_one_double(gr10, 10.0);
    check_one_double(gr11, 11.0);
    check_one_double(gr12, 12.0);
    check_one_double(gr13, 13.0);
    check_one_double(gr14, 14.0);
    check_one_double(glob1, 2.);
    return 0;
}
int main(void) { return target(); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllCoal_CoalesceChar)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1I + C6C + R"WP(
char glob_a;
char glob_b;
char glob_c;
char glob_d;
char glob_e;
char glob_f;
int glob = 0;
void set_glob(void) {
    glob = 10;
}
int target(char a, char b, char c, char d, char e, char f) {
    glob_a = a;
    glob_b = b;
    glob_c = c;
    glob_d = d;
    glob_e = e;
    glob_f = f;
    set_glob();
    check_six_chars(glob_a, glob_b, glob_c, glob_d, glob_e, glob_f, 1);
    check_one_int(glob, 10);
    return 0;
}
int main(void) { return target(1, 2, 3, 4, 5, 6); }
)WP"));
}

// Removed (task #27): DontCoalesceMovzx tested x86 "don't coalesce a movzx" via
// (double)(unsigned int)-1 == 4294967295.0 — a 32-bit-wraparound assertion with no
// BESM-6 analogue (unsigned int is 48-bit, and a signed int's high bits are zero so
// the reinterpretation yields 2^41-256, not C's 2^48-256). Any faithful large-value
// variant must emit that value as an FP literal, which the Madlen assembler rejects
// as too large for an immediate (=R… overflows the address field), an unrelated
// codegen limitation. The other 65 ch20 programs cover uint→double conversion.

TEST_F(BookTest, Chapter20_AllCoal_GeorgeCoalesceXmm)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + C14D + R"WP(
double glob = 4.0;
double dbl_target(double a, double b, double c, double d, double e, double f,
                  double g, double h) {
    if (a != 1.0) return 1.0;
    if (b != 2.0) return 2.0;
    if (c != 3.0) return 3.0;
    if (d != 4.0) return 4.0;
    if (e != 5.0) return 5.0;
    if (f != 6.0) return 6.0;
    if (g != 7.0) return 7.0;
    if (h != 8.0) return 8.0;
    double s = glob - 3.0;
    double t = glob - 2.0;
    double u = glob - 1.0;
    double v = glob * 2.0 - 4.0;
    double w = glob + 1.0;
    double x = glob + 2.0;
    double y = glob + 3.0;
    double z = glob * 2.0;
    double spill = w * 10.0;
    check_14_doubles(s, t, u, v, w, x, y, z, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 1.0);
    if (spill != 50.0) {
        return 9.0;
    }
    return check_one_double(glob, 4.0);
}
int main(void) {
    return (int) dbl_target(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0);
}
)WP"));
}

TEST_F(BookTest, Chapter20_AllCoal_GeorgeOffByOneXmm)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C1D + C14D + R"WP(
double glob = 0.0;
int target(double a) {
    double one = 2.0 - a;
    double two = one + one;
    double three = 2.0 + one;
    double four = two * two;
    double five = 6.0 - one;
    double six = two * three;
    double seven = one + 6.0;
    double eight = two * 4.0;
    double nine = three * three;
    double ten = four + six;
    double eleven = 16.0 - five;
    double twelve = eleven + one;
    double thirteen = five + eight;
    double fourteen = seven * two;
    glob = one;
    check_14_doubles(1, two, three, four, five, six, seven, eight, nine, ten,
                     eleven, twelve, thirteen, fourteen, 1);
    check_one_double(glob, 1.0);
    return 0;
}
int main(void) { return target(1.0); }
)WP"));
}

TEST_F(BookTest, Chapter20_AllCoal_GeorgeXmmKValue)
{
    EXPECT_EQ("0\n", CompileAndRunBook(EX + C14D + R"WP(
double glob1 = 1.;
double glob2 = 2.;
double glob10 = 10.;
int target(double one, double two, double three, double four, double five,
           double six, double seven, double eight) {
    double nine = (glob1 + glob2) * 3.;
    double ten = (glob2 + 3.) * 2.;
    double eleven = (glob10 + 1.) * glob1;
    double twelve = (glob1 + glob2) * 4.;
    double thirteen = (glob2 * 3.) + 7.;
    double fourteen = glob2 * 6. + 2.;
    check_14_doubles(one, two, three, four, five, six, seven, eight, nine, ten,
                     eleven, twelve, thirteen, fourteen, 1.);
    return 0;
}
int main(void) { return target(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0); }
)WP"));
}
