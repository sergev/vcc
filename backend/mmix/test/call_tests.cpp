//
// MMIX calls: pushj $1 with rJ kept in $0, arguments in $2..$17 and on the stack, the
// result in $1; the register stack across calls, ours and GCC's.
//
#include "mmix_test.h"

// A non-leaf function saves rJ in $0 after storing its parameters, calls with pushj $1,
// and moves the result from $1 to $0 after restoring rJ.
EXPECT_CODE(CallSequence,
            "subu $254,$254,16\n"
            "sto $0,$254,0\n"
            "get $0,rJ\n"
            "ldo $2,$254,0\n"
            "setl $3,#2\n"
            "pushj $1,g\n"
            "sto $1,$254,8\n"
            "ldo $1,$254,8\n"
            "put rJ,$0\n"
            "set $0,$1\n"
            "addu $254,$254,16\n"
            "pop 1,0\n",
            "long g(long, long); long f(long a) { return g(a, 2); }")

// A call through a pointer: pushgo to the address in $249, loaded before the arguments,
// since one of their registers may hold it.
TEST_F(MmixTest, IndirectCall)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("long f(long (*p)(long)) { return p(3); }"));
    EXPECT_NE(std::string::npos, code.find("ldo $249,$254,0\nsetl $2,#3\npushgo $1,$249,0\n"))
        << code;
}

// The 17th and 18th arguments go on the stack at 0 and 8 of the caller's frame, below
// its slots; a narrow one extended to 64 bits, an unsigned one with zeros.
TEST_F(MmixTest, StackArguments)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        "long g(long, long, long, long, long, long, long, long, long, long, long, long, "
        "long, long, long, long, int, unsigned);\n"
        "long f(int x, unsigned y) { return g(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, x, y); }"));
    EXPECT_NE(std::string::npos, code.find("ldt $248,$254,16\nsto $248,$254,0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldtu $248,$254,20\nsto $248,$254,8\n")) << code;
    EXPECT_NE(std::string::npos, code.find("setl $17,#f\npushj $1,g\n")) << code;
}

// An int variable passed for an unsigned parameter (a cast that emits no TAC) goes as
// the parameter's type: zero-extended, which GCC's callee trusts.
TEST_F(MmixTest, ArgumentInParameterType)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(
        "unsigned long g(unsigned); unsigned long f(int i) { return g((unsigned)i); }"));
    EXPECT_NE(std::string::npos, code.find("ldtu $2,")) << code;
}

// A float argument goes as its binary32 bits.
TEST_F(MmixTest, FloatArgument)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix("float g(float); float f(float x) { return g(x); }"));
    EXPECT_NE(std::string::npos, code.find("ldt $2,$254,0\npushj $1,g\nsttu $1,")) << code;
}

// Run: a caller's values survive a callee that writes all 32 local registers: the
// register stack hides the caller's $0 (its rJ) below the call's hole.
TEST_F(MmixTest, RunRegistersSurviveCall)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string clobber = "\t.text\n\t.global\tclobber\n\t.p2align 2\nclobber:\n";
    for (int r = 0; r < 32; r++)
        clobber += "\tnegu\t$" + std::to_string(r) + ",0," + std::to_string(r + 1) + "\n";
    clobber += "\tpop\t1,0\n";
    std::string ours = CompileToMmix(R"(
        long clobber(long a);
        long twice(long x) { long y = clobber(x); return x + x + (y == -1 ? 0 : 1000); }
        int main(void) { return twice(21) + twice(0); }
    )");
    EXPECT_EQ("", Run(ours + clobber, "crt0.o"));
    EXPECT_EQ(42, exit_status);
}

// Run: recursion 100000 deep, which spills the register ring to memory and fills it back.
TEST_F(MmixTest, RunDeepRecursion)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        long sum(long n) { if (n == 0) return 0; return n + sum(n - 1); }
        int main(void) { return sum(100000) == 5000050000L ? 0 : 1; }
    )"));
    EXPECT_EQ(0, exit_status);
}

static const char eighteen_params[] =
    "long a0, int a1, unsigned a2, short a3, unsigned short a4, signed char a5, "
    "unsigned char a6, long a7, long a8, long a9, long a10, long a11, long a12, long a13, "
    "long a14, long a15, int a16, unsigned char a17";
static const char eighteen_body[] =
    "{ return a0 + 2 * a1 + 3 * (long)a2 + 4 * a3 + 5 * a4 + 6 * a5 + 7 * a6 + a7 + a8 + a9 "
    "+ a10 + a11 + a12 + a13 + a14 + (long)(4 * a15) + 1000 * a16 + 10000 * a17; }\n";
static const char eighteen_args[] = "(-1, -2, 4000000000u, -3, 60000, -4, 200, 1, 2, 3, 4, 5, 6, 7, "
                                    "8, 10, -5, 250)";

// The expected sum of eighteen_args, on the host.
static long Eighteen()
{
    return -1 + 2 * -2 + 3 * 4000000000L + 4 * -3 + 5 * 60000 + 6 * -4 + 7 * 200 + 1 + 2 + 3 +
           4 + 5 + 6 + 7 + 8 + 40 + 1000 * -5 + 10000 * 250;
}

// Run: eighteen arguments of every width, two of them on the stack, our caller and our
// callee.
TEST_F(MmixTest, RunEighteenArguments)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string src = std::string("long f(") + eighteen_params + ")\n" + eighteen_body +
                      "int main(void) { return f" + eighteen_args + " == " +
                      std::to_string(Eighteen()) + "L ? 0 : 1; }\n";
    EXPECT_EQ("", CompileAndRunMmix(src));
    EXPECT_EQ(0, exit_status);
}

// Run: the same between our code and GCC's, both ways: GCC's callee trusts our
// extension of narrow arguments, and ours re-extends what GCC passes.
TEST_F(MmixTest, RunEighteenArgumentsWithGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string expected = std::to_string(Eighteen()) + "L";
    std::string gcc = std::string("long gf(") + eighteen_params + ")\n" + eighteen_body +
                      "long of(" + eighteen_params + ");\n" +
                      "long gcall(void) { return of" + eighteen_args + "; }\n";
    std::string ours = CompileToMmix(
        (std::string("long gf(") + eighteen_params + ");\nlong gcall(void);\n" + "long of(" +
         eighteen_params + ")\n" + eighteen_body + "int main(void) { if (gf" + eighteen_args +
         " != " + expected + ") return 1; if (gcall() != " + expected + ") return 2; return 0; }\n")
            .c_str());
    EXPECT_EQ("", Run(ours, "crt0.o", &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}

// Run: GCC's narrow results come back unextended; ours re-extends them on the store.
TEST_F(MmixTest, RunNarrowResultsFromGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = R"(
        signed char gsc(int x) { return x; }
        unsigned char guc(int x) { return x; }
        short gs(int x) { return x; }
        unsigned gu(long x) { return x; }
        int gi(long x) { return x; }
    )";
    std::string ours = CompileToMmix(R"(
        signed char gsc(int); unsigned char guc(int); short gs(int); unsigned gu(long); int gi(long);
        int main(void)
        {
            long a = gsc(255), b = guc(-1), c = gs(65535), d = gu(-1L), e = gi(0x180000000L);
            if (a != -1) return 1;
            if (b != 255) return 2;
            if (c != -1) return 3;
            if (d != 4294967295L) return 4;
            if (e != -2147483648L) return 5;
            return 0;
        }
    )");
    EXPECT_EQ("", Run(ours, "crt0.o", &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}
