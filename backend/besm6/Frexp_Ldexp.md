# `frexp` and `ldexp` on BESM-6

`frexp` and `ldexp` are the C11 `<math.h>` pair that take a floating-point number apart by
its binary exponent and put it back together (C11 §7.12.6.4 and §7.12.6.6). They are pure
exponent surgery — no rounding, no approximation — and on the BESM-6 they map almost directly
onto the hardware, because a BESM-6 floating-point word already *is* a (biased exponent,
signed mantissa) pair. This page explains what the two functions mean, how they are normally
used, and proposes a hand-written Madlen implementation for the BESM-6 target.

On the BESM-6, `float`, `double`, and `long double` are all one 48-bit machine word
(see [Besm6_Data_Representation.md](Besm6_Data_Representation.md)), so the single `double`
entry point serves every floating type; no `frexpf` / `ldexpf` variants are needed.

Both functions are currently *declared* in [libc/besm6/include/math.h](../../libc/besm6/include/math.h)
but not yet implemented (`modf` is the only implemented `<math.h>` routine so far). This
document is the implementation proposal.

## `frexp` — break a number into fraction and exponent

```c
double frexp(double value, int *exp);
```

`frexp` splits `value` into a *normalized fraction* and an *integer power of two* such that

```
value == fraction × 2^(*exp)
```

The fraction `m` is returned as the function result; the exponent is stored through the
`exp` pointer. The fraction is normalized to the half-open range

```
0.5 ≤ |m| < 1.0
```

(it carries the sign of `value`). The special case is zero: `frexp(0.0, &e)` returns `0.0`
and sets `*e = 0`. The BESM-6 floating-point format has **no infinities or NaNs**, so the
C standard's inf/NaN cases (return the argument unchanged, `*exp` unspecified) cannot arise
here.

## `ldexp` — scale a number by a power of two

```c
double ldexp(double x, int exp);
```

`ldexp` is the exact inverse of `frexp`:

```
ldexp(x, exp) == x × 2^(exp)
```

It computes the scaled value with a single adjustment of the binary exponent — no
multiplication, no precision loss. If the resulting exponent is out of the representable
range (`exp` outside roughly [−64, 63] relative to `x`), the BESM-6 floating-point unit
raises an exponent overflow/underflow exception rather than producing an infinity, because
the format has no infinities.

## Typical usage

The two functions are inverses, so the canonical use is a lossless round trip:

```c
int    e;
double m = frexp(x, &e);     /* x  == m * 2^e,  0.5 <= |m| < 1 (or m == 0) */
double y = ldexp(m, e);      /* y  == x  exactly                          */
```

Common idioms:

- **Read the binary order of magnitude.** `frexp(x, &e)` makes `e` the position of the most
  significant bit of `x`; `e - 1` is `floor(log2(|x|))`. Useful for range reduction and for
  printing numbers in a normalized "mantissa × 2^exp" form.
- **Scale without multiplying.** `ldexp(x, k)` multiplies or divides by a power of two by
  touching only the exponent field — cheaper and exactly rounded compared to `x * 8.0`.
- **Normalize before a numeric kernel.** Pull a value into `[0.5, 1)` with `frexp`, run an
  iteration that assumes a bounded argument (a polynomial approximation, a CORDIC step, a
  Newton iteration for `sqrt`), then restore the scale with `ldexp`. This keeps intermediate
  results away from the ends of the exponent range and avoids spurious overflow.

A worked round trip (note: printed text uses UPPERCASE Latin, which the BESM-6 output path
renders correctly — see [KOI7_Encoding.md](KOI7_Encoding.md)):

```c
#include <stdio.h>
#include <math.h>

int main(void)
{
    int e;
    double m = frexp(12.0, &e);   /* m = 0.75, e = 4   (12 = 0.75 * 2^4) */
    printf("M=%f E=%d\n", m, e);
    printf("BACK=%f\n", ldexp(m, e));   /* 12.000000 */
    return 0;
}
```

## How the functions map onto the BESM-6 word

A BESM-6 floating-point word packs the number as

```
 bit:  48        42 41 40                          1
      ┌────────────┬──┬─────────────────────────────┐
      │  exponent  │S │       mantissa (2's compl.)  │
      │  (7 bits)  │  │           (40 bits)          │
      └────────────┴──┴─────────────────────────────┘

 value = (0.mantissa − S) × 2^(exponent_field − 64)
```

The exponent field is **biased by 64** (a stored field of 64 means 2⁰). A *normalized*
nonzero number has bits 41 and 40 different, which forces `0.5 ≤ |mantissa| < 1` — exactly
the range `frexp` must return. So the decomposition the two functions perform is already
sitting in the bit layout:

| C concept | BESM-6 word |
|---|---|
| `frexp` exponent `*exp` | `exponent_field − 64` |
| `frexp` fraction `m` | the same word with its exponent field forced to 64 |
| `ldexp(x, n)` | `x` with `n` added to its exponent field |

Adding to / subtracting from the exponent field is a hardware primitive
(see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md)):

- **`E+X` (024)** — `exponent(A) += field(X) − 64`; i.e. multiply `A` by `2^(field(X) − 64)`.
  Normalization applies afterward. This is the whole of `ldexp`.
- **`E−X` (025)**, **`E+N` / `E−N` (034 / 035)** — the subtract and immediate forms.
- **`ASN` (036)** — immediate shift by `N − 64` bits (`,asn, 64+41` shifts right 41,
  `,asn, 64-41` shifts left 41); used to slide the exponent field down to the low bits and
  back.
- **`AEX` (logical XOR) / `A-X`** — toggle or subtract the 64 bias on the small integer
  exponent (`,aex, =100` adds 64 to a value with no bit-7 set; `,a-x, =100` subtracts it;
  `100` is octal 64).

## Calling convention used here

These two routines are hand-written in Madlen and deliberately use a **lightweight, frameless
convention** — no `b/save` / `b/ret` prologue, no `r6`/`r7` frame. They read their arguments
straight off the caller's stack and return through `13 ,uj,`.

A two-argument C call leaves the **first** argument on the stack (at `mem[r15-1]`) and the
**last** argument in the accumulator (see [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)).
The key instruction is **`STI` (041)**, "store to index and pop":

```
M[I] = A[15:1]
if I ≠ 017:  M[017] -= 1 ;  A = mem[M[017]]
```

So a single `15 ,sti, 14` does three useful things at once:

1. stores the second argument (in the accumulator) into index register `r14`,
2. decrements the stack pointer `r15` by one word, and
3. loads the first argument (the word just below) into the accumulator.

After it, `r14` holds arg#2, the accumulator holds arg#1, and `r15` is already adjusted by
the one word a parameterful function must drop on return. For `frexp`, arg#2 is the `int *`
output pointer, and `r14` is then used directly as the pointer base for the store
(`14 ,atx, 0` writes `*exp`). `r14` (which held the negative argument count) is free to
reuse — there is no frame to restore.

## Proposed Madlen — `ldexp`

`ldexp` adds the integer `n` to `x`'s exponent field. The addend is built as a word whose
exponent field equals `n + 64`; `E+X` then adds `field − 64 = n`.

```madlen
     ldexp: ,name,
c
c double ldexp(double x, int n) -- return x * 2^n.
c Entry: x at mem[r15-1], n in the accumulator.  Result in the accumulator.
c
            ,aex, =100          . n + 64
            ,asn, 64-41         . (n+64) << 41  into the exponent field
         15 ,xts, -2            . x             push stack
         15 ,e+x,               . x * 2^n       pop stack
         15 ,utm, -1            . drop x
         13 ,uj,                . return
            ,end,
```

## Proposed Madlen — `frexp`

`frexp` extracts the exponent field (minus the bias) into `*exp`, then returns the same word
with its exponent field reset to 64 (so the value lands in `[0.5, 1)`).

```madlen
     frexp: ,name,
c
c double frexp(double value, int *exp) -- split value = fraction * 2^(*exp),
c  with 0.5 <= |fraction| < 1, or fraction = 0 (and *exp = 0) when value = 0.
c Entry: value at mem[r15-1], exp pointer in the accumulator.  Fraction in the accumulator.
c
            ,sti, 14            . r14 := exp ; A := value ; r15 -= 1
         14 ,atx,               . *exp := 0
         13 ,uza,               . value == 0  ->  fraction 0, *exp 0
         15 ,atx,               . push value : value now at mem[r15-1]
c        --- *exp := exponent_field(value) - 64 ---
            ,asn, 64+41         . A := value >> 41 ; the 7-bit field lands in bits 7-1
            ,aax, =177          . isolate the field (0..127)
            ,a-x, =100          . r1 := field - 64 = e
         14 ,stx,               . *exp := e ; pop value from stack
c        --- fraction := value with the exponent field forced to 64 ---
            ,aax, =37 7777 7777 7777   . keep sign + mantissa (bits 41-1)
            ,aox, =:4           . set the exponent field to 64 (bit 48)
         13 ,uj,                . return the fraction in A
            ,end,
```

## Edge cases and notes

- **Zero.** A BESM-6 true zero is the all-zero word. `frexp` handles it without a separate
  branch target: right after the `STI` it speculatively stores the accumulator through the
  pointer (so `*exp` becomes `0` when the value is zero), then `13 ,uza,` returns immediately
  with the `0.0` still in the accumulator; a non-zero value falls through and overwrites
  `*exp` with the real exponent. `ldexp(0.0, n)` needs no special case — adding to the
  exponent of a zero mantissa still normalizes to zero.
- **Exponent over/underflow.** `ldexp` with a shift that pushes the exponent past the
  representable range trips the FPU's overflow/underflow exception (bit 8 of the exponent),
  since there is no infinity to return. Callers that must clamp should range-check `exp`
  first.
- **No renormalization needed.** Both routines only move the exponent field; the mantissa of
  a normalized input is already in `[0.5, 1)`, so the result is normalized without an explicit
  normalize step. `frexp` builds the fraction with logical `AAX`/`AOX` masks, which leave the
  mode register `R` untouched.
- **Sign is preserved** by keeping bits 41–1 (sign + mantissa) in the `frexp` mask
  `=37 7777 7777 7777` (= 2⁴¹ − 1, the same 41-bit mask used by `b/mul`), so a negative
  argument returns a fraction in `(−1.0, −0.5]`.
- **The bias bookkeeping** is the only subtlety: the stored field is the true exponent plus
  64, `E+X` subtracts the bias when it adds, and `frexp` subtracts 64 explicitly after
  extracting the field. The exponent range that round-trips is `[−64, 63]`, matching
  `FLT_MIN_EXP` / `FLT_MAX_EXP` in [float.h](../../libc/besm6/include/float.h).

## Integrating into `libc.bin`

`frexp` and `ldexp` are now shipped in the runtime. For the record, integration was:

1. Added `libc/besm6/madlen/ldexp.madlen` and `libc/besm6/madlen/frexp.madlen` (no `b_`
   prefix — these are user-facing libc entry points, not internal `b/…` helpers).
2. Appended `ldexp frexp` to the `LIBC_MADLEN` list in
   [libc/besm6/CMakeLists.txt](../../libc/besm6/CMakeLists.txt) (so they build into both the
   Madlen `libc.bin` and, via the shared helper list, the Unix `libc0.a`).
3. Listed the `frexp` / `ldexp` prototypes in [math.h](../../libc/besm6/include/math.h) among
   the implemented routines.
4. Added a `besm-tests` `CompileAndRun` round-trip case, run from `build/backend/besm6`
   (where `libc.bin` lives). If the exponent math misbehaves, trace it with
   `dubna -d c <Test>.dub` and follow the `ldexp` / `frexp` labels to inspect the
   accumulator's exponent at each step (see the debugging notes in the project `CLAUDE.md`).

## See also

- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — the floating-point word layout.
- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — `E+X`, `ASN`, `AEX`, `STI`.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — argument passing and the stack.
- [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) — the other runtime helpers.
