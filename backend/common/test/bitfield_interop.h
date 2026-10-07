//
// Bit-field structures across the call boundary, for each backend's interop tests: one
// side defines functions and data, the other calls them and checks every field; each
// backend builds it both ways, ours and the reference compiler's.  The caller's main
// returns 0, or the number of the check that failed.  Fields wider than 16 bits are long,
// for the 16-bit targets.
//
#pragma once

#include <string>

static const std::string kBitfieldDecls = R"(
struct bf_small { unsigned a : 3; int b : 9; unsigned char c; };
struct bf_wide { long q : 30; int r : 7; char s; short t : 11; int u[2]; };
struct bf_flt { float f; int x : 5; };
struct bf_pair { unsigned char p : 4, q : 4; };
struct bf_fpair { float f; unsigned char p : 3, q : 4; };
struct bf_small bf_mk_small(int a, int b, int c);
int bf_sum_small(struct bf_small s);
struct bf_wide bf_mk_wide(long q, int r);
long bf_sum_wide(struct bf_wide w);
struct bf_flt bf_mk_flt(float f, int x);
int bf_sum_flt(struct bf_flt v);
int bf_sum_pair(struct bf_pair a, struct bf_pair b);
struct bf_fpair bf_mk_fpair(float f, int p, int q);
int bf_sum_fpair(struct bf_fpair v, float g, int k);
void bf_bump(struct bf_wide *w);
extern struct bf_wide bf_global;
)";

static const std::string kBitfieldCallee = kBitfieldDecls + R"(
struct bf_small bf_mk_small(int a, int b, int c) { struct bf_small s = { a, b, c }; return s; }
int bf_sum_small(struct bf_small s) { return s.a + s.b + s.c; }
struct bf_wide bf_mk_wide(long q, int r) {
    struct bf_wide w = { q, r, 'x', -5, { 1, 2 } };
    return w;
}
long bf_sum_wide(struct bf_wide w) { return w.q + w.r + w.s + w.t + w.u[1]; }
struct bf_flt bf_mk_flt(float f, int x) { struct bf_flt v; v.f = f; v.x = x; return v; }
int bf_sum_flt(struct bf_flt v) { return (int)v.f + v.x; }
int bf_sum_pair(struct bf_pair a, struct bf_pair b) { return a.p * 1000 + a.q * 100 + b.p * 10 + b.q; }
struct bf_fpair bf_mk_fpair(float f, int p, int q) {
    struct bf_fpair v = { f, p, q };
    return v;
}
int bf_sum_fpair(struct bf_fpair v, float g, int k) {
    return (int)v.f * 1000 + v.p * 100 + v.q * 10 + (int)g + k;
}
void bf_bump(struct bf_wide *w) { w->q -= 1; w->r++; w->t = -w->t; }
struct bf_wide bf_global = { -100000000l, -60, 'g', 1000, { 7, 8 } };
)";

static const std::string kBitfieldCaller = kBitfieldDecls + R"(
int main(void) {
    struct bf_small s = bf_mk_small(5, -200, 77);
    if (s.a != 5 || s.b != -200 || s.c != 77) return 1;
    s.b = 100;
    if (bf_sum_small(s) != 182) return 2;
    struct bf_wide w = bf_mk_wide(123456789l, -33);
    if (w.q != 123456789l || w.r != -33 || w.s != 'x' || w.t != -5 || w.u[1] != 2) return 3;
    if (bf_sum_wide(w) != 123456789l - 33 + 'x' - 5 + 2) return 4;
    struct bf_flt v = bf_mk_flt(7.0f, -9);
    if (v.f != 7.0f || v.x != -9) return 5;
    v.x = 15;
    if (bf_sum_flt(v) != 22) return 6;
    struct bf_pair a = { 1, 2 }, b = { 3, 4 };
    if (bf_sum_pair(a, b) != 1234) return 7;
    if (bf_global.q != -100000000l || bf_global.r != -60 || bf_global.s != 'g' ||
        bf_global.t != 1000 || bf_global.u[0] != 7)
        return 8;
    bf_bump(&bf_global);
    if (bf_global.q != -100000001l || bf_global.r != -59 || bf_global.t != -1000) return 9;
    bf_global.r = 63;
    bf_bump(&bf_global);
    if (bf_global.r != -64 || bf_global.s != 'g') return 10;
    struct bf_fpair fp = bf_mk_fpair(3.0f, 5, 9);
    if (fp.f != 3.0f || fp.p != 5 || fp.q != 9) return 11;
    if (bf_sum_fpair(fp, 2.0f, 4) != 3596) return 12;
    return 0;
}
)";
