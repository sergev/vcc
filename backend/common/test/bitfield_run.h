//
// Bit-field reads, stores and compound assignments, signed and unsigned, at the bottom,
// middle and top of 8-, 16- and 32-bit units, and the shift-and-mask forms they lower
// to, written out by hand: a hash of every result.  The same text is compiled into the
// test (BitfieldRunExpected) and, by our compiler, into kBitfieldRunProgram, whose output
// must be the hash.  32-bit int and little-endian layout, as on the ARM targets.
//
#pragma once

#include <string>

#define BITFIELD_RUN_SOURCE(...)                           \
    static const char kBitfieldRunSource[] = #__VA_ARGS__; \
    namespace bitfield_run {                               \
    __VA_ARGS__                                            \
    }

// clang-format off
BITFIELD_RUN_SOURCE(
struct bfr_u8 { unsigned char a : 1, b : 3, c : 4; };
struct bfr_s16 { short a : 5; unsigned short b : 7; short c : 4; };
struct bfr_w { unsigned a : 1; int b : 7; unsigned c : 12; int d : 11; unsigned e : 1; };
struct bfr_t { unsigned lo : 4; unsigned hi : 28; int s : 31; };

static unsigned bfr_mix(unsigned h, unsigned v) { return h * 31u + v; }

static unsigned bfr_hash(unsigned seed)
{
    struct bfr_u8 u;
    struct bfr_s16 s;
    struct bfr_w w;
    struct bfr_t t;
    unsigned h = 0, x = seed;
    u.a = u.b = u.c = 0;
    s.a = s.b = s.c = 0;
    w.a = w.b = w.c = w.d = w.e = 0;
    t.lo = t.hi = t.s = 0;
    for (int i = 0; i < 200; i++) {
        x = x * 1103515245u + 12345u;
        u.a = x;
        u.b = x >> 3;
        u.c = x >> 7;
        s.a = (short)(x >> 2);
        s.b = x >> 9;
        s.c = (short)(x >> 20);
        w.a = x >> 1;
        w.b = (int)(x >> 4);
        w.c = x >> 6;
        w.d = (int)(x >> 13);
        w.e = x >> 31;
        t.lo = x;
        t.hi = x >> 2;
        t.s = (int)(x ^ (x << 7));
        h = bfr_mix(h, u.a + 2u * u.b + 17u * u.c);
        h = bfr_mix(h, (unsigned)(s.a * 3 + s.b - s.c));
        h = bfr_mix(h, (unsigned)(w.a + w.b + (int)w.c + w.d + (int)w.e));
        h = bfr_mix(h, t.lo ^ t.hi ^ (unsigned)t.s);
        w.c += x;
        w.d -= 5;
        s.a++;
        u.c ^= 9;
        t.hi = t.hi * 3u;
        w.b = w.b < 0 ? -w.b : w.b;
        h = bfr_mix(h, w.c + (unsigned)w.d + (unsigned)s.a + u.c + t.hi + (unsigned)w.b);
        h = bfr_mix(h, u.a + u.b + u.c + w.a + w.e + t.lo);
        h = bfr_mix(h, (x >> 13) & 0x7ffu);
        h = bfr_mix(h, (x >> 3) & 0xfffffu);
        h = bfr_mix(h, (x << 5) >> 9);
        h = bfr_mix(h, (unsigned)((int)(x << 7) >> 20));
        h = bfr_mix(h, (x & 0xfff000ffu) | ((x >> 9) & 0xfffu) << 8);
        h = bfr_mix(h, (x & ~0x3f0u) | (h & 0x3fu) << 4);
        h = bfr_mix(h, x & 0xfffe01ffu);
    }
    return h;
}
)
// clang-format on

// The program printing the hash of seed 1.
static const std::string kBitfieldRunProgram = std::string("#include <stdio.h>\n") +
                                               kBitfieldRunSource +
                                               "\nint main(void) { printf(\"%u\\n\", "
                                               "bfr_hash(1u)); return 0; }\n";

// What it prints.
static inline std::string BitfieldRunExpected()
{
    return std::to_string(bitfield_run::bfr_hash(1u)) + "\n";
}
