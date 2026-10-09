//
// Lowering defer: the deferred statements on every edge that leaves their block
// (translator/stmt.c; docs/Coroutines_in_C.md, section 1).
//
#include <sstream>

#include "translate_test.h"

// The order of what a function does, from its TAC YAML: each call as its name and
// first constant argument (g1 for g(1)), "jump", "label", and each return as "ret"
// with its constant, if any.  Conditional jumps and everything else are left out.
static std::string Trace(const std::string &yaml)
{
    std::istringstream in(yaml);
    std::string line, out, pending;
    enum { NONE, CALL, RET } want = NONE;
    auto flush                    = [&]() {
        if (!pending.empty())
            out += (out.empty() ? "" : " ") + pending;
        pending.clear();
        want = NONE;
    };
    while (std::getline(in, line)) {
        std::string t = line.substr(line.find_first_not_of(' ') == std::string::npos
                                        ? line.size()
                                        : line.find_first_not_of(' '));
        if (t == "- instruction:" || t == "- toplevel:") {
            flush();
        } else if (t == "kind: fun_call") {
            want = CALL;
        } else if (t == "kind: return") {
            want    = RET;
            pending = "ret";
        } else if (t == "kind: jump") {
            pending = "jump";
        } else if (t == "kind: label") {
            pending = "label";
        } else if (want == CALL && t.rfind("fun_name: ", 0) == 0) {
            pending = t.substr(10);
        } else if (want != NONE && t.rfind("value: ", 0) == 0) {
            pending += t.substr(7);
            want = NONE;
        }
    }
    flush();
    return out;
}

// The fixture lowers without optimizing: an if leaves a jump and two labels, and a
// void function falling off its end has no return.

// Falling off the end of a block: last registered first.
TEST_F(TranslateTest, DeferFallOff)
{
    EXPECT_EQ("g3 g2 g1", Trace(CompileToYaml(R"(
void g(int);
void f(void) { _Defer g(1); _Defer g(2); g(3); }
)")));
}

// Every return runs the defers registered so far, and only those.
TEST_F(TranslateTest, DeferReturn)
{
    EXPECT_EQ("g1 ret jump label label g2 g1 ret0 jump label label g3 g2 g1 ret0", Trace(CompileToYaml(R"(
void g(int);
int f(int x) { _Defer g(1); if (x) return x; _Defer g(2); if (x > 1) return 0; g(3); return 0; }
)")));
}

// An inner block's defers run at its end, before the outer block's later ones.
TEST_F(TranslateTest, DeferNested)
{
    EXPECT_EQ("g3 g2 g5 g4 g1", Trace(CompileToYaml(R"(
void g(int);
void f(void) { _Defer g(1); { _Defer g(2); g(3); } _Defer g(4); g(5); }
)")));
}

// The unbraced body of an if is a block of its own: its defer runs at once.
TEST_F(TranslateTest, DeferUnbracedIf)
{
    EXPECT_EQ("g1 jump label label g2", Trace(CompileToYaml(R"(
void g(int);
void f(int x) { if (x) _Defer g(1); g(2); }
)")));
}

// break and continue leave the loop body's block; the deferred statement is lowered
// on each of the three exits.
TEST_F(TranslateTest, DeferBreakContinue)
{
    EXPECT_EQ("label g1 jump jump label label g1 jump jump label label g2 g1 jump label",
              Trace(CompileToYaml(R"(
void g(int);
void f(int x) { while (x) { _Defer g(1); if (x == 1) continue; if (x == 2) break; g(2); } }
)")));
}

// A goto out of a block runs that block's defers; one back over a defer of its own
// block runs that defer.
TEST_F(TranslateTest, DeferGoto)
{
    EXPECT_EQ("g1 jump jump label label g1 label g2 label g3 g4 jump jump label label g4", Trace(CompileToYaml(R"(
void g(int);
void f(int x)
{
    { _Defer g(1); if (x) goto out; }
out:
    g(2);
again:
    g(3);
    _Defer g(4);
    if (x > 1) goto again;
}
)")));
}

// The value is taken before the defers run: r is copied, and the copy returned.
TEST_F(TranslateTest, DeferReturnValueFirst)
{
    std::string yaml = CompileToYaml("int f(void) { int r = 1; _Defer r = 2; return r; }");
    EXPECT_NE(std::string::npos, yaml.find(R"(      kind: copy
      src:
        kind: var
        name: %r
      dst:
        kind: var
        name: %0
    - instruction:
      kind: copy
      src:
        kind: constant
        const:
          kind: int
          value: 2
      dst:
        kind: var
        name: %r
    - instruction:
      kind: return
      src:
        kind: var
        name: %0
)")) << yaml;
}

// A deferred loop is lowered once per exit, with labels of its own each time: the
// verifier and the optimizer see distinct labels.
TEST_F(TranslateTest, DeferLoopTwice)
{
    std::string yaml = CompileToYaml(R"(
void g(int);
void f(int x)
{
    _Defer { for (int i = 0; i < x; i++) { if (i == 3) break; g(i); } here: g(9); }
    if (x) return;
    g(8);
}
)");
    EXPECT_EQ(2u, [&] {
        size_t n = 0;
        for (size_t at = yaml.find("value: 9"); at != std::string::npos;
             at        = yaml.find("value: 9", at + 1))
            n++;
        return n;
    }());
}

// A large cleanup that several exits leave by is lowered once more, at the end of its
// block, and the exits share that copy: each sets the "where next" variable (a copy,
// not traced) and jumps in; the chain runs the cleanup, then the return (phase C9).
static const char *const shared_src = R"(
void g(int);
int f(int k) { _Defer { g(1); g(2); g(3); g(4); } if (k) return 5; return 6; }
)";

TEST_F(TranslateTestX86, DeferShared)
{
    EXPECT_EQ("jump jump label label jump label g1 g2 g3 g4 jump label ret label",
              Trace(CompileToYaml(shared_src)));
}

// With sharing off, each exit has its own copy; and BESM-6, whose code must not change,
// never shares.
TEST_F(TranslateTestX86, DeferSharedOff)
{
    translate_shared_cleanup = false;
    std::string trace        = Trace(CompileToYaml(shared_src));
    translate_shared_cleanup = true;
    EXPECT_EQ("g1 g2 g3 g4 ret5 jump label label g1 g2 g3 g4 ret6", trace);
}

TEST_F(TranslateTest, DeferSharedNotOnBesm6)
{
    EXPECT_EQ("g1 g2 g3 g4 ret5 jump label label g1 g2 g3 g4 ret6", Trace(CompileToYaml(shared_src)));
}
