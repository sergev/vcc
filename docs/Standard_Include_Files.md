# Standard Include Files

This article is a guided tour of the C11 standard-library headers that ship with the
BESM-6 toolchain, found in [`libc/besm6/include/`](../libc/besm6/include/) (the
headers that depend on the target) and [`libc/common/include/`](../libc/common/include/)
(those shared with the RISC-V target, searched second). It
explains what each header is *for*, what it *declares*, how the headers *relate* to one
another, and — most importantly — how the peculiarities of the BESM-6 machine shape every
one of them.

It is written to be read front to back like a textbook chapter, but it also doubles as a
reference: the [conformance summary](#conformance-summary) at the end lists all 26 headers
in one table.

For the underlying machine model the headers encode, keep
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md) open alongside this one. For
the helper routines the *compiler itself* emits (as opposed to the user-callable library
described here), see [Besm6_Runtime_Library.md](../backend/besm6/Besm6_Runtime_Library.md).

---

## What the standard library is

The C language proper is small: it has operators, control flow, and a handful of built-in
types, but it cannot, on its own, print a line, allocate memory, or compute a square root.
Those services live in the **standard library**, and the library presents itself to your
program through a fixed set of **headers** — files you pull in with `#include`. A header
declares the *interface* (types, macros, function prototypes); the *implementation* lives
in a compiled library — here the BESM-6 runtime, built as the Madlen `libc.bin` (Dubna)
and the Unix `libc0.a` (`b6as`/`b6ld`/`b6sim`).

This article describes the full C11 header *interface*. Only a subset is implemented so
far (see "What is implemented vs. planned" in
[`libc/besm6/include/README.md`](../libc/besm6/include/README.md)); the rest are
declarations awaiting an implementation. Availability notes call out the gaps that
matter — most notably the allocator, which is provided in the Unix `libc0.a` only.

### Freestanding versus hosted

C11 (§4) recognises two kinds of conforming implementation:

- A **freestanding** implementation may run with no operating system at all — think boot
  code or an embedded monitor. It must supply only the headers that declare *types and
  macros* and need no runtime support. There are nine of them.
- A **hosted** implementation runs atop an environment that provides I/O, memory, and the
  rest. It must supply *all* the standard headers, including the nine freestanding ones.

The BESM-6 toolchain provides the full hosted set **except** three headers tied to
language features this target does not offer:

- `<complex.h>` — no complex-number arithmetic (and therefore no complex half of
  `<tgmath.h>`);
- `<stdatomic.h>` — no atomic operations;
- `<threads.h>` — no threading.

Everything else is present. The nine freestanding headers are
`<float.h>`, `<iso646.h>`, `<limits.h>`, `<stdalign.h>`, `<stdarg.h>`, `<stdbool.h>`,
`<stddef.h>`, `<stdint.h>`, and `<stdnoreturn.h>`.

---

## Using the headers

This compiler has **no preprocessor of its own**. The front end (`parse`) reads source in
which translation phases 1–4 are already done: it understands `#`-line markers but not
`#include` or `#define`. The headers are therefore meant to be expanded by an *external* C
preprocessor first, and the preprocessed result fed to the toolchain:

```sh
cc -E -nostdinc -Ilibc/besm6/include -Ilibc/common/include prog.c prog.i
parse  prog.i  prog.ast
lower  prog.ast prog.tac
genbesm prog.tac prog.mad
```

Use the C compiler's preprocessor (`cc -E`), **not** a standalone `cpp`: a traditional `cpp`
(such as Apple's `/usr/bin/cpp`) only recognizes a `#` directive in column 1 and ignores
`-std`, so indented `#include` lines silently fail to expand. `cc -E` accepts directives
anywhere on the line. `-nostdinc` keeps the *host's* system headers out of the way so that
only the BESM-6 headers are seen. No `-P` is needed: `parse` consumes the `# line` markers,
and keeping them preserves the original source line numbers in diagnostics. See
[Technical_Reference.md](Technical_Reference.md) for the rest of the pipeline.

A first complete program:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("HELLO, BESM-6\n");
    return EXIT_SUCCESS;
}
```

(Output is upper-cased by the console device — more on that next.)

---

## The BESM-6 environment in one page

Nearly every surprising thing in these headers traces back to a handful of hardware facts.
They are documented fully in [Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md);
here is the short version, each fact paired with its consequence for the library.

| Hardware fact | Consequence for the headers |
|---|---|
| Memory is **word-addressed**; one word is 48 bits = 6 bytes. Every C scalar and pointer occupies exactly one word. | `sizeof` of every scalar except `char` is 6; alignment is always 1 word. |
| `CHAR_BIT` is **8**; plain `char` is **unsigned**. | `CHAR_MIN` is 0; `<ctype.h>` and string code never see a negative `char`. |
| Signed `int`/`short`/`long`/`long long` share a **41-bit** field (1 sign + 40 value bits): range −2⁴⁰ … 2⁴⁰−1. | `INT_MAX` = `LONG_MAX` = 1 099 511 627 775 in `<limits.h>`; `intmax_t` is only 41-bit. |
| Unsigned types use the **full 48 bits**: 0 … 2⁴⁸−1. | `UINT_MAX` = 281 474 976 710 655. |
| There are **no 16/32/64-bit storage types**. | `<stdint.h>` provides only `int8_t`/`uint8_t` as exact-width types; no `int16_t`/`int32_t`/`int64_t`. |
| `float`, `double`, and `long double` are one and the same **native 48-bit FP format**: 40-bit mantissa (~12 decimal digits), 7-bit biased exponent, and **no NaN, infinity, or denormals**. | `<math.h>` omits `INFINITY`/`NAN`; `<tgmath.h>` collapses to one function per name; `<float.h>` describes a single format three times. |
| `char *` and `void *` are **fat pointers**: a word carrying a marker bit, a 3-bit in-word byte offset, and a 15-bit word address. | `<string.h>` byte traversal crosses word boundaries correctly; `char *` and `int *` are *not* interchangeable bit patterns. |
| The console works in **KOI7** and folds letters to **upper case**. | `printf` output is upper case; `%x`/`%X`, `%e`/`%E`, `%g`/`%G` print identically; wide/`ctype` classification is KOI7-based. |
| Each function argument occupies **exactly one word**, in consecutive words of the caller's parameter block. | `<stdarg.h>` is trivial and exact: a `va_list` is a word pointer and `va_arg` steps one word. |

---

## Freestanding headers

These nine declare only types and macros. They require no runtime and are the minimum a
freestanding program needs.

### `<stddef.h>` — common definitions

The bedrock header. It defines the types that pervade the rest of the library:

| Name | BESM-6 definition | Meaning |
|---|---|---|
| `size_t` | `unsigned long` (48-bit) | result of `sizeof`; an object size or count |
| `ptrdiff_t` | `long` (41-bit signed) | result of subtracting two pointers |
| `wchar_t` | `int` | a wide character |
| `max_align_t` | `double` | a type with the strictest alignment (here, any word) |
| `NULL` | `((void *)0)` | the null pointer constant |
| `offsetof(type, member)` | — | byte offset of a member within a struct |

`<stddef.h>` is the *canonical home* of `size_t`, `ptrdiff_t`, and `wchar_t`: other headers
that need them `#include <stddef.h>` rather than redefining them (see
[How the headers relate](#how-the-headers-relate)).

### `<stdint.h>` — integers of specified width

Where most platforms offer a ladder of widths (8/16/32/64), the BESM-6 has only the 8-bit
byte and the 48-bit word, so this header is necessarily sparse:

- **Exact width**: only `int8_t` / `uint8_t` exist (as `signed char` / `unsigned char`).
  There is deliberately no `int16_t`, `int32_t`, or `int64_t` — the machine has no type of
  those widths, and C11 makes exact-width types optional for exactly this reason.
- **Minimum width** (`int_leastN_t`) and **fastest** (`int_fastN_t`) for N = 8, 16, 32 all
  resolve to `int`/`unsigned`, because one word holds them comfortably. There are no
  64-bit least/fast types.
- **Pointer-holding**: `intptr_t`/`uintptr_t` are `int`/`unsigned` (a 15-bit address fits).
- **Greatest width**: `intmax_t` is `long long` (41-bit signed) and `uintmax_t` is
  `unsigned long long` (48-bit). This 41-bit signed ceiling is the widest integer the
  machine can represent.

It also supplies the limit macros (`INT8_MAX`, `INTMAX_MAX`, `SIZE_MAX`, …) and the
constant-builder macros (`INT8_C`, `UINTMAX_C`, …).

### `<limits.h>` — ranges of the integer types

Pure macros giving the bounds of each integer type. The BESM-6 values:

| Macro | Value | Macro | Value |
|---|---|---|---|
| `CHAR_BIT` | 8 | `MB_LEN_MAX` | 6 |
| `CHAR_MIN` / `CHAR_MAX` | 0 / 255 | `SCHAR_MIN` / `SCHAR_MAX` | −128 / 127 |
| `UCHAR_MAX` | 255 | `INT_MIN` / `INT_MAX` | −2⁴⁰ / 2⁴⁰−1 |
| `SHRT`/`LONG`/`LLONG_MAX` | 2⁴⁰−1 | `UINT`/`ULONG`/`ULLONG_MAX` | 2⁴⁸−1 |

Plain `char` is unsigned, so `CHAR_MAX` equals `UCHAR_MAX`. `MB_LEN_MAX` is 6 because up to
six KOI7 bytes pack into one word.

### `<float.h>` — characteristics of the floating types

Because `float == double == long double`, the `FLT_`, `DBL_`, and `LDBL_` families all hold
the *same* values, describing the one native format:

- `FLT_RADIX` 2, `*_MANT_DIG` 40, `*_DIG` 12 (decimal digits of precision);
- exponent range `*_MIN_EXP` −63 … `*_MAX_EXP` 63;
- `*_EPSILON` = 2⁻³⁹, `*_MIN` ≈ 2⁻⁶⁴, `*_MAX` ≈ (1 − 2⁻⁴⁰)·2⁶³;
- `FLT_ROUNDS` 1 (round to nearest), `FLT_EVAL_METHOD` 0.

There are no macros for infinities or NaNs because the format has none.

### `<stdbool.h>` — the boolean type

Defines `bool` as `_Bool`, plus `true`, `false`, and
`__bool_true_false_are_defined`. A `_Bool` occupies a word but only its low bit is
significant.

### `<stdarg.h>` — variable arguments

This header is usually subtle, but the BESM-6 calling convention makes it trivial and
*exact*: every argument — `int`, pointer, `double`, even a fat `char *` — occupies one
word, and arguments sit in consecutive words of the caller's parameter block (see
[Besm6_Calling_Conventions.md](../backend/besm6/Besm6_Calling_Conventions.md)). Therefore:

- `va_list` is simply a word pointer;
- `va_start(ap, last)` aims it just past the last named parameter;
- `va_arg(ap, T)` reads the current word as `T` and advances one word;
- `va_end` is a no-op; `va_copy` is assignment.

This is the very same walk the formatting engine uses internally (it reads its arguments
with `*ap++`). Reading a `char *`/`void *` through `va_arg` works because the stored word
*is* the fat pointer.

```c
#include <stdarg.h>
#include <stdio.h>

int sum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}
/* sum(4, 10, 20, 30, 40) == 100 */
```

### `<stdalign.h>` — alignment keywords

Defines `alignas`/`alignof` as `_Alignas`/`_Alignof`. On a word-addressed machine where
every type is word-aligned, alignment is mostly a formality, but the keywords exist for
portability.

### `<stdnoreturn.h>` — the `noreturn` convenience macro

Defines `noreturn` as `_Noreturn`, used to mark functions that never return (such as
`exit` and `abort`).

### `<iso646.h>` — alternative spellings

Defines readable spellings of the operator tokens: `and`, `or`, `not`, `xor`, `compl`,
`bitand`, `bitor`, `not_eq`, `and_eq`, `or_eq`, `xor_eq`.

---

## Hosted headers

These need runtime support and are present only in a hosted implementation. They are
grouped below by purpose.

### Input and output — `<stdio.h>`

The workhorse header. It declares the formatted-output family, character and line I/O,
file handling, and — unique to this target — the low-level console primitives.

| Group | Functions |
|---|---|
| Formatted output | `printf`, `fprintf`, `sprintf`, `snprintf`, `vprintf`, `vfprintf`, `vsnprintf` |
| Formatted input | `scanf`, `sscanf`, `fscanf` |
| Character / line | `putchar`, `getchar`, `puts`, `fputs`, `fputc`, `fgetc`, `fgets` |
| Files | `fopen`, `fclose`, `fflush`, `fread`, `fwrite`, `perror` |
| BESM-6 console | `putbyte`, `putch`, `getch`, `flush` |

BESM-6 specifics worth remembering:

- **Upper-case output.** The console works in KOI7 and folds letters to upper case, so
  every letter prints upper case and the case-variant conversions are indistinguishable:
  `%x` and `%X` both print upper-case hex, as do `%e`/`%E` and `%g`/`%G`.
- **Fat-pointer strings.** A `char *` passed to `%s` is a fat pointer; the engine walks it
  byte by byte across word boundaries.
- **Console primitives.** `putbyte` buffers one KOI7 byte, `putch` folds and emits a
  character, `getch` reads one byte, and `flush` forces the output buffer out. These are
  the foundation the higher-level routines are built on.
- **Floating output** reflects the 48-bit format: about 12 significant decimal digits.

The `printf` family uses ordinary ISO variadic prototypes — `int printf(const char *fmt, ...)`
— and you call them exactly as on any platform. Read-only pointer parameters throughout these
headers carry `const` exactly where ISO C11 specifies it (the format string here, the source
operand of `strcpy`/`memcpy`, and so on), so the prototypes match modern systems.

### General utilities — `<stdlib.h>`

A grab-bag of essentials:

| Group | Functions / macros |
|---|---|
| Program control | `exit`, `abort`, `atexit`, `EXIT_SUCCESS`, `EXIT_FAILURE` |
| Memory | `malloc`, `calloc`, `realloc`, `free` |
| String → number | `atoi`, `atol`, `atof`, `strtol`, `strtoul`, `strtod` |
| Arithmetic | `abs`, `labs`, `div`, `ldiv` (with `div_t`/`ldiv_t`) |
| Search / sort | `qsort`, `bsearch` |
| Randomness | `rand`, `srand`, `RAND_MAX` |
| Environment | `getenv`, `system` |

`exit` is marked `_Noreturn`. `RAND_MAX` is the signed-integer ceiling (2⁴⁰−1). Memory
returned by `malloc` is word-aligned, which on this machine is the only alignment there is.

**Runtime availability.** `exit` and `atoi` are implemented in both runtime builds. The
allocator (`malloc`/`calloc`/`realloc`/`free`) is provided only on the Unix
(`b6as`/`b6ld`/`b6sim`) runtime (`libc0.a`): it claims its heap over the linker `_end`
symbol up to the `b6sim` stack base, a layout the Madlen (Dubna) `libc.bin` does not
supply, so the allocator is **absent from `libc.bin`**. The remaining `<stdlib.h>`
routines are declared but not yet implemented.

### Strings and memory — `<string.h>`

The familiar `str*` and `mem*` operations:

`strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`,
`strrchr`, `strstr`, `strtok`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`,
`strerror`.

Because `char *`/`void *` are fat pointers, byte-by-byte traversal *just works* across word
boundaries: incrementing a `char *` decrements its in-word offset and steps to the next
word when the offset wraps. You never have to think about the six-chars-per-word packing;
the pointer arithmetic hides it.

### Character classification — `<ctype.h>` and `<wctype.h>`

`<ctype.h>` declares the narrow classifiers and case mappers — `isalpha`, `isdigit`,
`isalnum`, `isspace`, `isupper`, `islower`, `ispunct`, `iscntrl`, `isprint`, `isgraph`,
`isblank`, `isxdigit`, `toupper`, `tolower`. They classify against the KOI7 character set.
As always, the argument is an `int` holding an `unsigned char` value or `EOF`.

`<wctype.h>` is the wide analogue: `iswalpha`, `iswdigit`, … `towupper`, `towlower`, plus
the `wctype`/`wctrans` extensible-property mechanism. It draws `wint_t` and `WEOF` from
`<wchar.h>`.

### Mathematics — `<math.h>`, `<tgmath.h>`, `<fenv.h>`

`<math.h>` declares the usual real-valued functions, all taking and returning `double`
(which *is* `float` and `long double` here):

| Group | Functions |
|---|---|
| Rounding / remainder | `fabs`, `floor`, `ceil`, `round`, `trunc`, `fmod`, `modf`, `frexp`, `ldexp` |
| Powers / logs | `sqrt`, `pow`, `exp`, `log`, `log10` |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` |
| Hyperbolic | `sinh`, `cosh`, `tanh` |
| Misc | `hypot`, `fmin`, `fmax`, `copysign` |

Because the FP format has no infinities or NaNs, `<math.h>` provides **no** `INFINITY` or
`NAN`; `HUGE_VAL` is defined as the largest finite value (`DBL_MAX`). It also offers the
handy constants `M_PI` and `M_E`.

`<tgmath.h>` would normally choose, via `_Generic`, among `float`/`double`/`long double`
(and complex) versions of each function. Here all real types are the *same* one-word
format and there is no complex support, so the type-generic machinery collapses: each macro
(`sqrt`, `pow`, `sin`, …) simply promotes its argument to `double` and calls the single
function in `<math.h>`. Integer arguments promote to `double`, exactly as the standard
requires.

`<fenv.h>` describes the floating-point environment — exception flags and rounding modes.
The BESM-6 exposes neither IEEE sticky flags nor a settable rounding mode through this
library, so the environment is *degenerate*: a single mode, no flags. The surface
(`feclearexcept`, `fetestexcept`, `fegetround`, `FE_TONEAREST`, …) is provided for source
portability.

### Wide and Unicode characters — `<wchar.h>` and `<uchar.h>`

`<wchar.h>` is the canonical home of `wint_t` (an `int`), `mbstate_t`, and `WEOF`, and
declares the wide string and conversion routines (`wcslen`, `wcscpy`, `wcscmp`, `mbrtowc`,
`wcrtomb`, `wcstol`, …). A `wchar_t` is one word, wide enough for any KOI7 or Unicode code
point.

`<uchar.h>` adds `char16_t` and `char32_t` (each an `unsigned` word) and the
`mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb` conversion functions.

### Diagnostics and errors — `<assert.h>` and `<errno.h>`

`<assert.h>` defines the `assert` macro, which checks a condition and, on failure, reports
the expression, file, and line and aborts. It honours `NDEBUG` (defining it turns `assert`
into a no-op) and is deliberately **re-includable**: its meaning is recomputed at each
`#include` according to `NDEBUG`.

`<errno.h>` declares the `errno` object and a small, non-POSIX set of error numbers —
`EDOM`, `ERANGE`, `EILSEQ`, `EINVAL`, `ENOMEM`, `EIO`.

### Non-local control flow — `<setjmp.h>` and `<signal.h>`

`<setjmp.h>` declares `jmp_buf`, `setjmp`, and `longjmp` for non-local jumps. A `jmp_buf`
is an array of words large enough to save the registers the calling convention requires to
be preserved — the return address (r13), the parameter and auto pointers (r6, r7), and the
stack pointer (r15); see [Besm6_Calling_Conventions.md](../backend/besm6/Besm6_Calling_Conventions.md).

`<signal.h>` declares `signal`, `raise`, `sig_atomic_t`, and the `SIG*` numbers. The Dubna
environment has no POSIX-style asynchronous signal delivery, so the practical use is
synchronous `raise`; the full surface exists for portability.

### Time and locale — `<time.h>` and `<locale.h>`

`<time.h>` declares `time_t` and `clock_t` (each one signed word), `struct tm`, and the
calendar/clock functions (`time`, `clock`, `difftime`, `mktime`, `localtime`, `gmtime`,
`asctime`, `ctime`, `strftime`), plus `CLOCKS_PER_SEC`.

`<locale.h>` declares `setlocale`, `localeconv`, `struct lconv`, and the `LC_*` category
macros. Only the `"C"` locale is meaningful on this target.

### Integer formatting — `<inttypes.h>`

Extends `<stdint.h>` with `imaxabs`, `imaxdiv` (and `imaxdiv_t`), `strtoimax`,
`strtoumax`, and the `PRI*`/`SCN*` format-string macros for `printf`/`scanf`. Because every
integer type is one word, the length modifiers all collapse: the macros expand to plain
`"d"`, `"u"`, `"x"`, … with no length prefix (the formatting engine ignores `l`/`h`
anyway).

---

## How the headers relate

Two relationships tie the headers together.

**Type provenance.** Several types are needed by many headers but must be *defined in
exactly one place*, because this compiler forbids redeclaring a typedef (a consequence of
its strict no-shadowing rule — see [Technical_Reference.md](Technical_Reference.md)). Each
shared type therefore has a single canonical home, and any other header that needs it pulls
it in with `#include`:

| Type | Canonical header |
|---|---|
| `size_t`, `ptrdiff_t`, `wchar_t`, `NULL`, `offsetof`, `max_align_t` | `<stddef.h>` |
| `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` | `<stdarg.h>` |
| the exact/least/fast integer types and their limits | `<stdint.h>` |
| `wint_t`, `mbstate_t`, `WEOF` | `<wchar.h>` |
| `char16_t`, `char32_t` | `<uchar.h>` |
| `FILE`, `EOF`, the standard streams | `<stdio.h>` |
| `time_t`, `clock_t`, `struct tm` | `<time.h>` |

**Include dependencies.** A few headers are built atop others and include them directly:

- `<stdio.h>` → `<stddef.h>` (for `size_t`) and `<stdarg.h>` (for `va_list`);
- `<tgmath.h>` → `<math.h>` → `<float.h>`;
- `<inttypes.h>` → `<stdint.h>`;
- `<wctype.h>` and `<uchar.h>` → `<wchar.h>`;
- `<stdlib.h>`, `<string.h>`, `<time.h>`, `<locale.h>` → `<stddef.h>`;
- `<assert.h>` → `<stdlib.h>` (for `abort`).

Because each header guards itself and types are defined once, you may `#include` any
combination in any order without clashes.

---

## Conformance summary

All 26 shipped headers. "Kind" marks the nine C11 freestanding headers versus the hosted
remainder.

| Header | Kind | Role |
|---|---|---|
| `<float.h>` | freestanding | characteristics of the floating types |
| `<iso646.h>` | freestanding | alternative operator spellings |
| `<limits.h>` | freestanding | ranges of the integer types |
| `<stdalign.h>` | freestanding | `alignas` / `alignof` |
| `<stdarg.h>` | freestanding | variable arguments |
| `<stdbool.h>` | freestanding | `bool`, `true`, `false` |
| `<stddef.h>` | freestanding | `size_t`, `ptrdiff_t`, `NULL`, `offsetof`, … |
| `<stdint.h>` | freestanding | fixed-width integer types |
| `<stdnoreturn.h>` | freestanding | `noreturn` macro |
| `<assert.h>` | hosted | run-time assertions |
| `<ctype.h>` | hosted | narrow character classification |
| `<errno.h>` | hosted | error numbers and `errno` |
| `<fenv.h>` | hosted | floating-point environment (degenerate) |
| `<inttypes.h>` | hosted | integer format macros and conversions |
| `<locale.h>` | hosted | localization (`"C"` locale) |
| `<math.h>` | hosted | real mathematics |
| `<setjmp.h>` | hosted | non-local jumps |
| `<signal.h>` | hosted | signal handling |
| `<stdio.h>` | hosted | input / output and console primitives |
| `<stdlib.h>` | hosted | general utilities |
| `<string.h>` | hosted | string and memory operations |
| `<tgmath.h>` | hosted | type-generic math (degenerate) |
| `<time.h>` | hosted | date and time |
| `<uchar.h>` | hosted | Unicode characters |
| `<wchar.h>` | hosted | wide characters and multibyte conversion |
| `<wctype.h>` | hosted | wide character classification |

Full hosted conformance still excludes `<complex.h>`, `<stdatomic.h>`, and `<threads.h>`,
which depend on language features the BESM-6 target does not provide. With those three
exceptions, a program written against the standard headers will compile and run on this
toolchain.
