#!/usr/bin/env python3
#
# Test cases for the binary128 runtime (libc/common/float128.c): random and special
# operands, with results computed exactly by rational arithmetic and rounded to
# nearest even.  Writes float128_cases.inc, a C++ raw string of C tables, for
# float128_tests.cpp.  Deterministic: rerun to regenerate.
#
import os
import random
from fractions import Fraction

random.seed(128)

FBITS, EBITS = 112, 15
NAN = "nan"


def encode(x, fbits, ebits):
    """The bits of x (a Fraction, inf or NAN) rounded to nearest even."""
    emax = (1 << ebits) - 1
    bias = emax >> 1
    if x == NAN:
        return emax << fbits | 1 << (fbits - 1)
    if x == float("inf") or x == float("-inf"):
        return (1 if x < 0 else 0) << (fbits + ebits) | emax << fbits
    if x == 0:
        return 0
    if x < 0:
        top, x = 1 << (fbits + ebits), -x
    else:
        top = 0
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    if e + bias < 1:  # subnormal
        scale = Fraction(2) ** (bias - 1 + fbits)
    else:
        scale = Fraction(2) ** (fbits - e)
    y = x * scale
    m = y.numerator // y.denominator
    rest = y - m
    if rest > Fraction(1, 2) or (rest == Fraction(1, 2) and m & 1):
        m += 1
    if e + bias < 1:
        return top | m  # a carry into the exponent field is right
    if m >> (fbits + 1):
        m >>= 1
        e += 1
    if e + bias >= emax:
        return top | emax << fbits
    return top | (e + bias) << fbits | (m - (1 << fbits))


def decode(b):
    """The value of binary128 bits b: a Fraction, +-inf, or NAN; and the sign."""
    sign = b >> 127
    e = (b >> FBITS) & 0x7FFF
    f = b & ((1 << FBITS) - 1)
    if e == 0x7FFF:
        return (NAN if f else (float("-inf") if sign else float("inf"))), sign
    if e == 0:
        v = Fraction(f, 1 << (FBITS + 16382))
    else:
        v = Fraction((1 << FBITS) | f) * Fraction(2) ** (e - 16383 - FBITS)
    return (-v if sign else v), sign


def rand_bits(kind=None):
    kind = kind or random.choice(["near", "near", "wide", "low", "sub", "huge", "tiny"])
    sign = random.getrandbits(1)
    frac = random.getrandbits(FBITS)
    if kind == "near":
        e = 16383 + random.randint(-40, 40)
    elif kind == "wide":
        e = random.randint(1, 0x7FFE)
    elif kind == "low":  # few significant bits: exact results, ties
        e = 16383 + random.randint(-8, 8)
        frac &= ~((1 << random.randint(60, 111)) - 1) & ((1 << FBITS) - 1)
    elif kind == "sub":
        e = 0
        frac >>= random.randint(0, 100)
    elif kind == "huge":
        e = 0x7FFE - random.randint(0, 3)
    else:
        e = random.randint(1, 4)
    return sign << 127 | e << FBITS | frac


SPECIALS = [
    0,
    1 << 127,  # -0
    0x7FFF << FBITS,  # inf
    1 << 127 | 0x7FFF << FBITS,  # -inf
    0x7FFF << FBITS | 1 << 111,  # NaN
    0x3FFF << FBITS,  # 1
    1 << 127 | 0x3FFF << FBITS,  # -1
    0x7FFE << FBITS | ((1 << FBITS) - 1),  # max
    1 << FBITS,  # min normal
    1,  # min subnormal
    (1 << FBITS) - 1,  # max subnormal
]


def pairs(n):
    out = [(a, b) for a in SPECIALS for b in SPECIALS[:7]]
    for _ in range(n):
        a = rand_bits()
        r = random.random()
        if r < 0.3:  # close exponents: cancellation
            b = (a ^ (random.getrandbits(1) << 127)) + random.randint(-(1 << 70), 1 << 70)
            b &= (1 << 128) - 1
        elif r < 0.5:  # the same kind
            b = rand_bits("near" if (a >> FBITS) & 0x7FFF else "sub")
        else:
            b = rand_bits()
        out.append((a, b))
    # Exact ties: 1 + 2^-113 and 1 + 3 * 2^-113.
    out.append((0x3FFF << FBITS, encode(Fraction(1, 2**113), FBITS, EBITS)))
    out.append((0x3FFF << FBITS, encode(Fraction(3, 2**113), FBITS, EBITS)))
    return out


def arith(op, a, b):
    x, sx = decode(a)
    y, sy = decode(b)
    if x == NAN or y == NAN:
        return NAN
    inf = float("inf")
    if op == "-":
        y, sy = (-y if y != 0 else y), sy ^ 1
    if op in "+-":
        if abs(x) == inf or abs(y) == inf:
            if abs(x) == inf and abs(y) == inf and (x > 0) != (y > 0):
                return NAN
            return encode(x if abs(x) == inf else y, FBITS, EBITS)
        if x == 0 and y == 0:
            return (sx & sy) << 127
        return encode(x + y, FBITS, EBITS)
    s = sx ^ sy
    if op == "*":
        if abs(x) == inf or abs(y) == inf:
            if x == 0 or y == 0:
                return NAN
            return s << 127 | 0x7FFF << FBITS
        if x == 0 or y == 0:
            return s << 127
        return encode(x * y, FBITS, EBITS)
    # division
    if abs(x) == inf:
        return NAN if abs(y) == inf else s << 127 | 0x7FFF << FBITS
    if abs(y) == inf:
        return s << 127
    if y == 0:
        return NAN if x == 0 else s << 127 | 0x7FFF << FBITS
    if x == 0:
        return s << 127
    return encode(x / y, FBITS, EBITS)


def compare(a, b):
    """Bits: <, <=, >, >=, ==, != (bit 0 first)."""
    x, _ = decode(a)
    y, _ = decode(b)
    if x == NAN or y == NAN:
        return 1 << 5
    res = [x < y, x <= y, x > y, x >= y, x == y, x != y]
    return sum(1 << i for i, r in enumerate(res) if r)


def words(b):
    return "0x%016xULL, 0x%016xULL" % (b & ((1 << 64) - 1), b >> 64)


def table(name, rows):
    lines = ["static const unsigned long long %s[][%d] = {" % (name, rows[0][0])]
    for _, text in rows:
        lines.append("    { %s }," % text)
    lines.append("};")
    return "\n".join(lines)


def main():
    out = []
    for name, op in (("add", "+"), ("sub", "-"), ("mul", "*"), ("div", "/")):
        rows = []
        for a, b in pairs(150):
            r = arith(op, a, b)
            # A NaN result is any NaN: written as the default one.
            r = 0x7FFF << FBITS | 1 << 111 if r == NAN else r
            rows.append((6, "%s, %s, %s" % (words(a), words(b), words(r))))
        out.append(table("t_" + name, rows))

    rows = []
    for a, b in pairs(80) + [(a, a) for a in SPECIALS]:
        rows.append((5, "%s, %s, %d" % (words(a), words(b), compare(a, b))))
    out.append(table("t_cmp", rows))

    # long double -> double and float, rounded; -> long, truncated (in range only).
    rows = []
    for a in SPECIALS + [rand_bits() for _ in range(80)] + [
        rand_bits("near") for _ in range(40)
    ] + [(0x3FFF - 1022 - random.randint(1, 60)) << FBITS | random.getrandbits(FBITS) for _ in range(20)]:
        x, s = decode(a)
        if x == NAN:
            d, f = 0x7FF8 << 48, 0x7FC00000
        elif abs(x) == float("inf"):
            d = s << 63 | 0x7FF << 52
            f = s << 31 | 0xFF << 23
        else:
            d = encode(x, 52, 11) | (s << 63 if x == 0 else 0)
            f = encode(x, 23, 8) | (s << 31 if x == 0 else 0)
        rows.append((4, "%s, 0x%016xULL, 0x%08xULL" % (words(a), d, f)))
    out.append(table("t_narrow", rows))

    rows = []
    for _ in range(80):
        a = random.getrandbits(1) << 127 | (16383 + random.randint(-2, 61)) << FBITS | random.getrandbits(FBITS)
        x, _ = decode(a)
        if abs(x) < 2**62:
            t = int(x)
            rows.append((3, "%s, 0x%016xULL" % (words(a), t & ((1 << 64) - 1))))
    out.append(table("t_fix", rows))

    # long, double and float -> long double, exact.
    rows = []
    for v in [0, 1, -1, 2**63 - 1, -(2**63), 12345] + [
        random.randint(-(2**63), 2**63 - 1) >> random.randint(0, 62) for _ in range(30)
    ]:
        rows.append((3, "0x%016xULL, %s" % (v & ((1 << 64) - 1), words(encode(Fraction(v), FBITS, EBITS)))))
    out.append(table("t_float", rows))

    rows = []
    for d in [0, 1 << 63, 0x7FF << 52, 1, (1 << 52) - 1, 0x3FF << 52] + [
        random.getrandbits(63) for _ in range(30)
    ]:
        e = (d >> 52) & 0x7FF
        if e == 0x7FF:
            r = (d >> 63) << 127 | 0x7FFF << FBITS
        else:
            f = d & ((1 << 52) - 1)
            v = Fraction(f, 1 << (52 + 1022)) if e == 0 else Fraction((1 << 52) | f) * Fraction(2) ** (e - 1023 - 52)
            r = encode(-v if d >> 63 else v, FBITS, EBITS) | (d >> 63) << 127
        rows.append((3, "0x%016xULL, %s" % (d, words(r))))
    out.append(table("t_extend", rows))

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "float128_cases.inc")
    with open(path, "w") as f:
        f.write("// Generated by gen_float128_cases.py; do not edit.\n")
        f.write('R"(\n' + "\n".join(out) + '\n)"\n')


main()
