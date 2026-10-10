#include "codegen_test.h"

//
// Fat-pointer char access (task #21).
//
// char*/void* are fat pointers: bit 48 set (marker), bits 47-45 the byte offset,
// bits 15-1 the word address.  Dereferencing extracts one byte (WTC/XTA/ASX/AAX);
// storing read-modify-writes the containing word via the b/stb helper; taking the
// address of a char sets the fat marker; int*->char* sets marker + offset 5.
//

// &c where c is a char yields a fat pointer: the GET_ADDRESS sets bit 48 (offset 0).
TEST_F(CodegenTest, GetAddressCharSetsFatMarker)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char c; char *p; void f(void){ p = &c; }");
    EXPECT_NE(output.find(",aox, =:40"), std::string::npos) << output;
}

// Dereferencing a char* emits the byte-load sequence wtc / xta / asx / aax =377.
TEST_F(CodegenTest, LoadCharEmitsByteSequence)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char c; int f(void){ char *p = &c; return *p; }");
    EXPECT_NE(output.find(",wtc,"), std::string::npos) << output;
    EXPECT_NE(output.find(",asx,"), std::string::npos) << output;
    EXPECT_NE(output.find(",aax, =377"), std::string::npos) << output;
}

// Storing through a char* calls the b/stb runtime helper.
TEST_F(CodegenTest, StoreCharCallsStb)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char c; void f(void){ char *p = &c; *p = 'Q'; }");
    EXPECT_NE(output.find(",call, b/stb"), std::string::npos) << output;
}

// int*->char* cast sets the fat marker plus offset 5 (points at the first/MSB byte).
TEST_F(CodegenTest, IntPtrToCharPtrSetsOffset5)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char *f(int *q){ return (char*)q; }");
    EXPECT_NE(output.find(",aox, =:64"), std::string::npos) << output;
}

// Regression: an int* load uses the plain word dereference (WTC + bare XTA) and none of
// the byte machinery — no ASX shift and no =377 byte mask.
TEST_F(CodegenTest, LoadIntStillWord)
{
    DisableOptimization();
    std::string output = CompileToMadlen("int v; int f(void){ int *p = &v; return *p; }");
    EXPECT_NE(output.find(",wtc,"), std::string::npos) << output;
    EXPECT_EQ(output.find(",asx,"), std::string::npos) << output;
    EXPECT_EQ(output.find("=377"), std::string::npos) << output;
}

// Runtime: store and load a byte through a fat pointer at offset 0 (standalone char).
TEST_F(CodegenTest, CharStoreLoadRoundtrip)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char c;
            char *p = &c;
            *p = 'Q';
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("Q\n", result);
}

// Runtime: compound assignment through a char* (byte load + byte store).
TEST_F(CodegenTest, CharCompoundAssign)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char c;
            char *p = &c;
            *p = 'A';
            *p += 1;
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("B\n", result);
}

// Runtime: post-increment through a char* returns the old byte and stores the new one.
TEST_F(CodegenTest, CharPostIncrement)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char c;
            char *p = &c;
            *p = 'A';
            char old = (*p)++;
            putbyte(old);
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("AB\n", result);
}

// Runtime: byte store and load through a *module-level global* char* pointer (not a frame
// slot).  Mirrors the msgbuf/msgbufp idiom that regressed with "variable not in frame":
// the byte paths must route the fat pointer through the global-safe emit helpers.
TEST_F(CodegenTest, GlobalCharPtrStoreLoad)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        char buf[4];
        char *p = buf;
        void program() {
            *p = 'A';          /* byte store through a global fat pointer */
            ++p;               /* pointer increment on a global */
            *p = 'B';
            putbyte(buf[0]);   /* read back the stored bytes */
            putbyte(*p);       /* byte load through the global fat pointer */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("AB\n", result);
}

// Runtime: int*->char* cast yields a fat pointer at offset 5 (the int's MSB byte).
// Storing and loading through it exercises the byte shift by 40 bits (offset 5).
TEST_F(CodegenTest, CharCastOffset5Roundtrip)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int x = 0;
            char *p = (char*)&x;
            *p = 'Z';
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("Z\n", result);
}

//
// char* arithmetic & packed char members (task #22).
//
// Pointer ± integer on a char*/void* adjusts the 3-bit byte offset of the fat pointer
// (ADD_PTR scale 1): a constant ±1 uses b/pinc / b/pdec, any other delta uses b/padd.
// A char array / string literal decays to a fat pointer at byte#0 (offset_enc 5).  Packed
// char struct members are read with a byte extract and written via the b/stb helper.
//

// Runtime: index a local char array (decay + ADD_PTR scale 1 via b/padd).
TEST_F(CodegenTest, CharArrayIndexReadWrite)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[6];
            a[0] = 'H';
            a[5] = '!';
            putbyte(a[0]);
            putbyte(a[5]);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("H!\n", result);
}

// Runtime: a string literal decays to a char* at its first byte (MSB); deref walks it.
TEST_F(CodegenTest, StringDecayDeref)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char *s = "HI";
            putbyte(*s);
            putbyte(*(s + 1));
            putbyte('\n');
        }
    )");
    EXPECT_EQ("HI\n", result);
}

// Runtime: char*++ across a word boundary (b/pinc offset_enc 0 -> 5 with word carry).
TEST_F(CodegenTest, CharPtrIncWordBoundary)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[8];
            for (int i = 0; i < 7; i++)
                a[i] = 'A' + i;
            char *p = a;
            p += 5;          /* a[5] = 'F' */
            putbyte(*p);
            p++;             /* crosses into the next word: a[6] = 'G' */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("FG\n", result);
}

// Runtime: char*-- across a word boundary backward (b/pdec offset_enc 5 -> 0).
TEST_F(CodegenTest, CharPtrDecWordBoundary)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[8];
            for (int i = 0; i < 7; i++)
                a[i] = 'A' + i;
            char *p = a;
            p += 6;          /* a[6] = 'G' */
            putbyte(*p);
            p--;             /* a[5] = 'F' (crosses back a word) */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("GF\n", result);
}

// Runtime: char*-- within a word, not crossing a boundary (b/pdec non-wrap path,
// offset_enc N -> N+1).  Regression for a wrap-test bug: b/pdec detected offset_enc==5
// with a cyclic subtract whose non-zero result still tripped uza, so every in-word
// decrement wrongly took the word-step-back path.
TEST_F(CodegenTest, CharPtrDecWithinWord)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[8];
            a[0]='X'; a[1]='Y'; a[2]='Z';
            char *p = a + 2;   /* 'Z' */
            p--;               /* a+1 = 'Y' */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("Y\n", result);
}

// Runtime: char* '<' forward walk spanning a word boundary (a[0..7], crosses at a[6]).
// Complements CharPtrRelationalCompare (>=); the four relational operators share one
// PTR_DIFF-based lowering, so this also guards < / the signed compare-against-0.
TEST_F(CodegenTest, CharPtrLessThanForward)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[10];
            for (int i = 0; i < 10; i++)
                a[i] = '0' + i;
            char *p = a;
            char *end = a + 8;
            while (p < end) {
                putbyte(*p);
                ++p;
            }
            putbyte('\n');
        }
    )");
    EXPECT_EQ("01234567\n", result);
}

// Runtime: char* '>' backward walk spanning a word boundary (a[7..2], crosses at a[6]).
TEST_F(CodegenTest, CharPtrGreaterThanBackward)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[10];
            for (int i = 0; i < 10; i++)
                a[i] = '0' + i;
            char *start = a + 1;
            char *p = a + 7;
            while (p > start) {
                putbyte(*p);
                --p;
            }
            putbyte('\n');
        }
    )");
    EXPECT_EQ("765432\n", result);
}

// Runtime: char* '<=' inclusive forward walk (a[0..3]).
TEST_F(CodegenTest, CharPtrLessOrEqualForward)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[10];
            for (int i = 0; i < 10; i++)
                a[i] = '0' + i;
            char *p = a;
            char *end = a + 3;
            while (p <= end) {
                putbyte(*p);
                ++p;
            }
            putbyte('\n');
        }
    )");
    EXPECT_EQ("0123\n", result);
}

// Runtime: char* + n with a carry across a word (b/padd floored division by 6).
TEST_F(CodegenTest, CharPtrPlusNCarry)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[12];
            for (int i = 0; i < 10; i++)
                a[i] = '0' + i;
            char *p = a;
            p = p + 8;       /* a[8] = '8' */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("8\n", result);
}

// Runtime: char* - n with a negative byte delta (b/padd floored division, negative path).
TEST_F(CodegenTest, CharPtrMinusN)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[12];
            for (int i = 0; i < 10; i++)
                a[i] = '0' + i;
            char *p = a + 9; /* a[9] = '9' */
            p = p - 7;       /* a[2] = '2' */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("2\n", result);
}

// Runtime: packed char struct members at byte offsets 0..5 plus a word member after them.
// The char at offset 0 has offset%6==0 but must still use byte access (driven by the flag).
TEST_F(CodegenTest, PackedStructCharMembers)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct S { char a; char b; char c; char d; char e; char f; int g; };
        void program() {
            struct S s;
            s.a = 'A'; s.b = 'B'; s.c = 'C';
            s.d = 'D'; s.e = 'E'; s.f = 'F';
            putbyte(s.a); putbyte(s.b); putbyte(s.c);
            putbyte(s.d); putbyte(s.e); putbyte(s.f);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("ABCDEF\n", result);
}

// Runtime: a subscript-array base and a decayed fat pointer agree on byte#0.
TEST_F(CodegenTest, MixedSubscriptAndDecayConsistency)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[3];
            a[0] = 'X';
            char *p = a;
            putbyte(p[0]);
            putbyte(a[0]);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("XX\n", result);
}

// Shape: char* + i lowers to ADD_PTR scale 1 -> the b/padd helper.
TEST_F(CodegenTest, CharPtrPlusIntCallsPadd)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char *f(char *p, int i){ return p + i; }");
    EXPECT_NE(output.find(",call, b/padd"), std::string::npos) << output;
}

// Shape: char*++ uses the b/pinc helper (constant +1 byte step).
TEST_F(CodegenTest, CharPtrIncCallsPinc)
{
    DisableOptimization();
    std::string inc = CompileToMadlen("char *f(char *p){ p++; return p; }");
    EXPECT_NE(inc.find(",call, b/pinc"), std::string::npos) << inc;
}

// Shape: char*-- uses the b/pdec helper (constant -1 byte step).
TEST_F(CodegenTest, CharPtrDecCallsPdec)
{
    DisableOptimization();
    std::string dec = CompileToMadlen("char *f(char *p){ p--; return p; }");
    EXPECT_NE(dec.find(",call, b/pdec"), std::string::npos) << dec;
}

// Shape: a char array / string decays to a fat pointer at offset_enc 5 (MSB byte#0).
TEST_F(CodegenTest, ArrayDecaySetsOffset5)
{
    DisableOptimization();
    std::string output = CompileToMadlen("char g[6]; char *f(void){ return g; }");
    EXPECT_NE(output.find(",aox, =:64"), std::string::npos) << output;
}

// Shape: reading a packed char member emits a byte extract (asx-style shift + mask =377).
TEST_F(CodegenTest, StructCharMemberReadIsByte)
{
    DisableOptimization();
    std::string output =
        CompileToMadlen("struct S { char a; char b; int c; }; int f(struct S s){ return s.b; }");
    EXPECT_NE(output.find(",aax, =377"), std::string::npos) << output;
}

// Shape: writing a packed char member calls the b/stb read-modify-write helper.
TEST_F(CodegenTest, StructCharMemberWriteCallsStb)
{
    DisableOptimization();
    std::string output =
        CompileToMadlen("struct S { char a; char b; int c; }; void f(struct S *s){ s->b = 'X'; }");
    EXPECT_NE(output.find(",call, b/stb"), std::string::npos) << output;
}

// Regression: a word pointer ++ stays on the inline integer path (no b/pinc / b/padd).
TEST_F(CodegenTest, WordPtrIncStaysInline)
{
    DisableOptimization();
    std::string output = CompileToMadlen("int *f(int *p){ p++; return p; }");
    EXPECT_EQ(output.find(",call, b/pinc"), std::string::npos) << output;
    EXPECT_EQ(output.find(",call, b/padd"), std::string::npos) << output;
}

// Regression: a word struct member stays a plain word load (no byte extract / b/stb).
TEST_F(CodegenTest, StructIntMemberStaysWord)
{
    DisableOptimization();
    std::string output =
        CompileToMadlen("struct S { char a; char b; int c; }; int f(struct S s){ return s.c; }");
    EXPECT_EQ(output.find(",call, b/stb"), std::string::npos) << output;
}

// Runtime: a global char array decays and indexes correctly through b/padd.
TEST_F(CodegenTest, GlobalCharArrayIndexRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        char g[6];
        void program() {
            g[0] = 'P'; g[3] = 'Q';
            char *p = g;
            putbyte(p[0]);
            putbyte(p[3]);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("PQ\n", result);
}

// Runtime: compound += through a char* lvalue (load fat pointer, b/padd, store).
TEST_F(CodegenTest, CharPtrLvalueCompoundAdd)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[6];
            for (int i = 0; i < 6; i++)
                a[i] = 'A' + i;
            char *p = a;
            char **pp = &p;
            *pp += 4;        /* p now points at a[4] = 'E' */
            putbyte(*p);
            putbyte('\n');
        }
    )");
    EXPECT_EQ("E\n", result);
}

// Shape: char* - char* lowers to PTR_DIFF -> the b/pdiff helper. Task #22b.
TEST_F(CodegenTest, CharPtrDifferenceCallsPdiff)
{
    DisableOptimization();
    std::string output = CompileToMadlen("long f(char *p, char *q){ return p - q; }");
    EXPECT_NE(output.find(",call, b/pdiff"), std::string::npos) << output;
}

// Runtime: char* - char* within one word (both byte offsets in the same word).
TEST_F(CodegenTest, CharPtrDifferenceSameWordRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[12];
            char *p = a + 5;
            char *q = a + 2;
            putbyte('0' + (p - q));   /* 3 */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("3\n", result);
}

// Runtime: char* - char* spanning a word boundary (delta > 6 -> word + byte# decode).
TEST_F(CodegenTest, CharPtrDifferenceCrossWordRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[12];
            char *p = a + 8;
            char *q = a + 1;
            putbyte('0' + (p - q));   /* 7 */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("7\n", result);
}

// Runtime: a negative char* - char* difference (subtrahend past the minuend).
TEST_F(CodegenTest, CharPtrDifferenceNegativeRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[12];
            char *p = a + 2;
            char *q = a + 9;
            long d = p - q;          /* -7 */
            if (d < 0) putbyte('-');
            putbyte('0' - d);         /* '0' - (-7) = '7' */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("-7\n", result);
}

// Runtime: char(*)[N] - char(*)[N] divides the byte difference by the row size to yield
// a row (element) count. Task #11.
TEST_F(CodegenTest, CharRowPtrDifferenceRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[4][3];
            char (*p)[3] = a;
            char (*q)[3] = a + 2;
            putbyte('0' + (q - p));   /* 2 */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("2\n", result);
}

//
// Multi-dimensional char arrays (task #5).
//
// A char-innermost array of any rank is a flat byte blob, packed 6 bytes per word with rows
// contiguous (no per-row word padding).  Indexing m[i][j] decays the array to a fat byte
// pointer and advances it by i*rowsize + j bytes.  Tests use UPPERCASE letters because the
// static-data path packs strings as KOI-7, which folds lowercase Latin to uppercase codes
// while char literals stay ASCII (see backend/besm6/KOI7_Encoding.md); uppercase keeps both equal.

// Runtime: static multi-dim char array — contiguous packing, per-row null padding, indexing.
TEST_F(CodegenTest, MultiDimCharArrayStaticRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            static char m[2][4] = {"YES", "YUP"};
            putbyte(m[0][0]); putbyte(m[0][2]);            /* Y S */
            putbyte('0' + m[0][3]);                        /* null -> '0' */
            putbyte(m[1][0]); putbyte(m[1][2]);            /* Y P */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("YS0YP\n", result);
}

// Runtime: automatic multi-dim char array initialized from string rows, then indexed.
TEST_F(CodegenTest, MultiDimCharArrayAutoRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char m[2][2][2] = {{"A", "B"}, {"C", "D"}};
            putbyte(m[0][0][0]); putbyte(m[0][1][0]);      /* A B */
            putbyte(m[1][0][0]); putbyte(m[1][1][0]);      /* C D */
            putbyte('0' + m[0][0][1]);                     /* null -> '0' */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("ABCD0\n", result);
}

// Runtime: global multi-dim char array read through a flat char* (contiguous bytes), and a
// row decayed to a char*.  Rows that fit a null keep it, so a flat walk stops at the first.
TEST_F(CodegenTest, MultiDimCharArrayGlobalFlatRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int strcmp(const char *a, const char *b);
        char nested[3][3] = {"YES", "NO", "OK"};
        void program() {
            char *whole = (char *)nested;
            char *row2 = (char *)nested[2];
            putbyte('0' + strcmp(whole, "YESNO"));         /* "NO" null-terminates -> 0 */
            putbyte('0' + strcmp(row2, "OK"));             /* 0 */
            putbyte(nested[1][0]);                         /* N */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("00N\n", result);
}

// Runtime: automatic 1-D char array initialized from a string is a real byte copy into the
// frame slot (not an alias of the string constant) — a later store does not corrupt others.
TEST_F(CodegenTest, CharArrayStringInitIsCopyRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[4] = "ABC";
            char b[4] = "ABC";
            a[0] = 'X';                                    /* must not touch b */
            putbyte(a[0]); putbyte(b[0]);                  /* X A */
            putbyte('0' + a[3]);                           /* null -> '0' */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("XA0\n", result);
}

// Runtime: a static multi-dim char array with an empty-string row (the binary .tac
// round-trip used to crash genbesm with strlen(NULL); the layout zero-fills the row).
TEST_F(CodegenTest, MultiDimCharArrayEmptyRowRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            static char m[3][4] = {"", "BC"};
            putbyte('0' + m[0][0]);                        /* empty row -> '0' */
            putbyte(m[1][0]); putbyte(m[1][1]);            /* B C */
            putbyte('0' + m[2][0]);                        /* unset row -> '0' */
            putbyte('\n');
        }
    )");
    EXPECT_EQ("0BC0\n", result);
}

// A decoded string literal keeps its embedded NUL bytes: "A\0C" is four bytes (three
// plus the terminator), not one.  The static data must hold all of them — the literal
// used to be cut short at the first decoded NUL, emitting just 'A' and a terminator.
TEST_F(CodegenTest, StringWithEmbeddedNulData)
{
    std::string output = CompileToUnix("char a[] = \"A\\0C\";");
    EXPECT_NE(output.find(".word 02020010300000000"), std::string::npos) << output;
}

// sizeof counts the decoded bytes, so an embedded NUL is one of them.
TEST_F(CodegenTest, StringWithEmbeddedNulSizeof)
{
    std::string output = CompileToUnix("int n = sizeof \"A\\0C\";");
    EXPECT_NE(output.find(".word 4"), std::string::npos) << output;
}

// Octal and hex escapes are decoded, not copied through: "\x41\101" is "AA".
TEST_F(CodegenTest, StringOctalAndHexEscapes)
{
    std::string output = CompileToUnix("char a[] = \"\\x41\\101\";");
    EXPECT_NE(output.find(".word 02024040000000000"), std::string::npos) << output;
}

// Runtime: read back every byte of a local char array initialized with an embedded NUL.
TEST_F(CodegenTest, EmbeddedNulCharArrayRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char a[5] = "A\0C";
            putbyte(a[0]);
            putbyte('0' + a[1]);                           /* the embedded null */
            putbyte(a[2]);
            putbyte('0' + a[3]);                           /* the terminator */
            putbyte('0' + sizeof "A\0C");
            putbyte('\n');
        }
    )");
    EXPECT_EQ("A0C04\n", result);
}
