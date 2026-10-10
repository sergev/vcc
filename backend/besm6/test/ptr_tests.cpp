#include "codegen_test.h"

//
// ADD_PTR — pointer/array index scaling (task #19).
//
// dst = ptr + index * scale.  `scale` is the element size in bytes; the backend
// converts it to a word scale (scale / 6) since BESM-6 is word-addressed.
//

// Global array, variable index, variable source: word scale 1 (plain).  The translator
// decays the array to its label address (GET_ADDRESS: vtm arr / ita), then ADD_PTR
// adds the scaled index to that pointer value with a plain A+X.
TEST_F(CodegenTest, AddPtrGlobalArrayStore)
{
    std::string output = CompileToMadlen("int arr[3]; void f(long i, int v){ arr[i] = v; }");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 3
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
      arr:   ,subp,
             ,its, 13
             ,call, b/save
          15 ,utm, 2
          14 ,vtm, arr
             ,ita, 14
           7 ,atx,
           6 ,xta,
           7 ,a+x,
           7 ,atx, 1
           6 ,xta, 1
           7 ,wtc, 1
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Pointer parameter, variable index: word scale 1, pointer base — the pointer's
// stored value is the base address, so the scaled index is added with a plain A+X.
// The element address lands in a temp, which the LOAD then dereferences via WTC.
TEST_F(CodegenTest, AddPtrPointerLoad)
{
    std::string output = CompileToMadlen("int f(int *a, long i){ return a[i]; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
          15 ,utm, 1
           6 ,xta, 1
           6 ,a+x,
           7 ,atx,
           7 ,wtc,
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Two-word element (a 2-word struct, 12 bytes → word scale 2): the index is scaled by a
// left shift (,asn, 63 = shift left 1) before adding the pointer base.  No scalar type is
// two words on BESM-6, so a struct supplies the power-of-two element size here.  A signed
// index left-shifts its sign bits into the exponent field, so the shift is masked back to
// 41 bits (,aax, =37777777777777) to keep a negative index a valid signed offset.
TEST_F(CodegenTest, AddPtrPowerOfTwoScale)
{
    std::string output = CompileToMadlen(
        "struct S { long a, b; }; struct S *f(struct S *p, long i){ return &p[i]; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 1
             ,asn, 63
             ,aax, =37777777777777
           6 ,a+x,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Runtime: store through a variable index, then load it back.
TEST_F(CodegenTest, AddPtrVarIndexRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int arr[3];
        void program() {
            int v = 42;
            long i = 1;
            arr[i] = v;
            printf("%d\n", arr[i]);
        }
    )");
    EXPECT_EQ("42\n", result);
}

// Runtime: the address of a global now comes from a lone `14 ,vtm, <name>` rather than
// `,utc, <name>` / `14 ,vtm, 0`.  Store through the taken address and through the global's
// own name, and read both ways back, so a wrong address could not go unnoticed.
TEST_F(CodegenTest, AddressOfGlobalRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int g;
        int arr[3];
        void program() {
            int *p = &g;
            *p = 42;
            int *q = &arr[2];
            *q = 7;
            printf("%d %d %d\n", g, *p, arr[2]);
        }
    )");
    EXPECT_EQ("42 42 7\n", result);
}

// Runtime: fill a global array element-by-element, then read every element back.
TEST_F(CodegenTest, AddPtrGlobalArrayRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int arr[3];
        void program() {
            int a = 10, b = 20, c = 30;
            arr[0] = a;
            arr[1] = b;
            arr[2] = c;
            printf("%d %d %d\n", arr[0], arr[1], arr[2]);
        }
    )");
    EXPECT_EQ("10 20 30\n", result);
}

// A local array reserves a contiguous multi-word frame slot (task #23): int a[4]
// occupies 4 words, so the frame prologue extends the stack by exactly 4 (no other
// autos in this function).  Previously frame.c reserved a single word per name.
TEST_F(CodegenTest, LocalArrayFrameSize)
{
    std::string output = CompileToMadlen("void f(void){ int a[4]; }");
    EXPECT_NE(output.find("15 ,utm, 4"), std::string::npos) << output;
}

// Runtime: a local array indexes into its own contiguous frame slot, so distinct
// elements hold distinct values.  Returns 30 = 10 + 20.
TEST_F(CodegenTest, LocalArrayRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int a[4];
            a[0] = 10;
            a[3] = 20;
            printf("%d\n", a[0] + a[3]);
        }
    )");
    EXPECT_EQ("30\n", result);
}

// Runtime: an array decays to a pointer when assigned to a pointer variable, and
// indexing through that pointer reaches the array's elements.  Before array-to-pointer
// decay happened in the translator, `p = arr` loaded arr[0] instead of arr's address.
TEST_F(CodegenTest, ArrayDecayToPointerRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int arr[3];
        void program() {
            arr[0] = 10;
            arr[1] = 20;
            arr[2] = 30;
            int *p = arr;
            printf("%d\n", p[0] + p[1] + p[2]);
        }
    )");
    EXPECT_EQ("60\n", result);
}

// A block-scope `static int *` initialized with the address of a global: a plain
// (word) pointer static local.  Exercises the Z00 `label` field for the
// TAC_STATIC_INIT_POINTER chain (SUBP + two Z00 words).
TEST_F(CodegenTest, StaticLocalPtrToGlobalRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int g = 7;
        void program() {
            static int *q = &g;
            printf("%d\n", *q);
        }
    )");
    EXPECT_EQ("7\n", result);
}

// A `static int *` initialized with the address of an array element (&arr[2]):
// exercises a non-zero byte_offset in the pointer init.
TEST_F(CodegenTest, StaticLocalPtrToArrayElemRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int arr[3] = {11, 22, 33};
        void program() {
            static int *q = &arr[2];
            printf("%d\n", *q);
        }
    )");
    EXPECT_EQ("33\n", result);
}

// Wide word-pointer difference: int(*)[2] - int(*)[2] divides the raw word-address
// difference by the element word size to yield a C element count. Task #11.
TEST_F(CodegenTest, WideWordPtrDifferenceRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int a[4][2];
            int (*p)[2] = a;
            int (*q)[2] = &a[3];
            putbyte('0' + (q - p));   /* 3 */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("3\n", result);
}
