# Standard Include Files

This article is a guided tour of the C11 standard-library headers that ship with VCC, as
a RISC-V (riscv64) programmer sees them. They live in three source directories:
[`libc/riscv64/include/`](../libc/riscv64/include/) holds the headers that are the
target's own, [`libc/lp64/include/`](../libc/lp64/include/) those that depend only on the
LP64 data model (shared with aarch64), searched second, and
[`libc/common/include/`](../libc/common/include/) the target-neutral rest, searched
last. `make install` copies all three into one directory,
`share/vcc/riscv64/include/`. The article explains what each header is *for*, what it
*declares*, how the headers *relate* to one another, and how the RISC-V LP64D data model
shapes the values they define.

It is written to be read front to back like a textbook chapter, but it also doubles as a
reference: the [conformance summary](#conformance-summary) at the end lists all 26 headers
in one table.

For the backend, the calling rules and the runtime these headers describe, keep
[Riscv_Backend.md](Riscv_Backend.md) open alongside this one.

---

## What the standard library is

The C language proper is small: it has operators, control flow, and a handful of built-in
types, but it cannot, on its own, print a line, allocate memory, or compute a square root.
Those services live in the **standard library**, and the library presents itself to your
program through a fixed set of **headers** — files you pull in with `#include`. A header
declares the *interface* (types, macros, function prototypes); the *implementation* lives
in a compiled library — here the RISC-V runtime `libc.a`, together with the start-up
object `crt0.o` and the linker script `link.ld`, all built in `build/libc/riscv64/` and
installed to `share/vcc/riscv64/lib/`.

This article describes the full C11 header *interface*. Only a subset is implemented so
far; the rest are declarations awaiting an implementation. Each section says which
routines actually exist in `libc.a`, and the headers themselves mark the rest `TODO`.

### Freestanding versus hosted

C11 (§4) recognises two kinds of conforming implementation:

- A **freestanding** implementation may run with no operating system at all — think boot
  code or an embedded monitor. It must supply only the headers that declare *types and
  macros* and need no runtime support. There are nine of them.
- A **hosted** implementation runs atop an environment that provides I/O, memory, and the
  rest. It must supply *all* the standard headers, including the nine freestanding ones.

VCC provides the full hosted set **except** three headers tied to language features the
compiler does not offer:

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
preprocessor first, and the preprocessed result fed to the toolchain. In the build tree:

```sh
cc -E -nostdinc -Ilibc/riscv64/include -Ilibc/lp64/include -Ilibc/common/include prog.c -o prog.i
build/parse  prog.i prog.ast
build/lower  -t riscv64 prog.ast prog.tac
build/backend/genriscv prog.tac prog.s
```

After `make install` the two include directories are one, and the tools carry a `v`
prefix:

```sh
P=~/.local
cc -E -nostdinc -I$P/share/vcc/riscv64/include prog.c -o prog.i
vparse prog.i prog.ast
vlower -t riscv64 prog.ast prog.tac
vgenriscv64 prog.tac prog.s
```

Use the C compiler's preprocessor (`cc -E`), **not** a standalone `cpp`: a traditional `cpp`
(such as Apple's `/usr/bin/cpp`) only recognizes a `#` directive in column 1 and ignores
`-std`, so indented `#include` lines silently fail to expand. `cc -E` accepts directives
anywhere on the line. `-nostdinc` keeps the *host's* system headers out of the way so that
only VCC's headers are seen. No `-P` is needed: `parse` consumes the `# line` markers,
and keeping them preserves the original source line numbers in diagnostics. Assembling,
linking against `crt0.o` and `libc.a`, and running under qemu are shown in
[Riscv_Backend.md](Riscv_Backend.md#running-a-program-by-hand); see
[Technical_Reference.md](Technical_Reference.md) for the rest of the pipeline.

A first complete program:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("Hello, RISC-V\n");
    return EXIT_SUCCESS;
}
```

`crt0.o` calls `main` and passes its result to `exit`, which stops qemu: a zero status
makes qemu exit with 0, any other value becomes qemu's exit status.

---

## The RISC-V environment in one page

The target-dependent headers encode a handful of facts about the RV64IMFD machine and the
LP64D psABI. Here is the short version, each fact paired with its consequence for the
library.

| Target fact | Consequence for the headers |
|---|---|
| Memory is **byte-addressed**; `short` is 16 bits, `int` 32, `long`, `long long` and pointers 64 (**LP64**). | `<stdint.h>` has the full exact-width ladder `int8_t` … `int64_t`; `int64_t`, `intptr_t`, `intmax_t`, `size_t` and `ptrdiff_t` are all `long`. |
| `CHAR_BIT` is **8**; plain `char` is **unsigned** (the RISC-V ABI). | `CHAR_MIN` is 0, `CHAR_MAX` 255. |
| `_Bool` is one byte; converting any scalar to it yields 0 or 1. | `<stdbool.h>` is plain macros over `_Bool`. |
| `float` and `double` are IEEE-754 **binary32** and **binary64**, in hardware (the F and D extensions). | `<float.h>` has the familiar IEEE values; `<math.h>` defines `INFINITY` and `NAN`; `printf` prints `inf`/`nan`. |
| `long double` is IEEE **binary128**, 16 bytes, computed in software (`libc/common/float128.c`). | `LDBL_MANT_DIG` is 113; `max_align_t` has 16-byte alignment. |
| A variadic function saves `a0`–`a7` just below its stack arguments. | `<stdarg.h>` needs no compiler builtins: `va_list` is a `char *` that walks 8-byte slots. |
| No operating system: programs run bare-metal on qemu `virt`. Output goes byte by byte to the ns16550 UART. | `<stdio.h>` works on `stdout` only; there is no file layer; `flush` is a no-op. |

---

## Freestanding headers

These nine declare only types and macros. They require no runtime and are the minimum a
freestanding program needs. Five of them (`<float.h>`, `<limits.h>`, `<stdarg.h>`,
`<stddef.h>`, `<stdint.h>`) are RISC-V-specific; the other four come from
`libc/common/include/`.

### `<stddef.h>` — common definitions

The bedrock header. It defines the types that pervade the rest of the library:

| Name | RISC-V definition | Meaning |
|---|---|---|
| `size_t` | `unsigned long` (64-bit) | result of `sizeof`; an object size or count |
| `ptrdiff_t` | `long` (64-bit signed) | result of subtracting two pointers |
| `wchar_t` | `int` (32-bit) | a wide character |
| `max_align_t` | `struct { long long; long double; }` | a type with the strictest alignment (16 bytes) |
| `NULL` | `((void *)0)` | the null pointer constant |
| `offsetof(type, member)` | — | byte offset of a member within a struct |

`<stddef.h>` is the *canonical home* of `size_t`, `ptrdiff_t`, and `wchar_t`: other headers
that need them `#include <stddef.h>` rather than redefining them (see
[How the headers relate](#how-the-headers-relate)).

### `<stdint.h>` — integers of specified width

On RISC-V every width the standard names exists, so this header is complete:

- **Exact width**: `int8_t`/`uint8_t` (`signed char`/`unsigned char`), `int16_t`
  (`short`), `int32_t` (`int`), `int64_t` (`long`), and their unsigned partners.
- **Minimum width** (`int_leastN_t`) for N = 8, 16, 32, 64 are the same types as the
  exact-width ones.
- **Fastest** (`int_fastN_t`): `int_fast8_t` is `signed char`; `int_fast16_t`,
  `int_fast32_t` and `int_fast64_t` are all `long`, the machine's native register width.
- **Pointer-holding**: `intptr_t`/`uintptr_t` are `long`/`unsigned long`.
- **Greatest width**: `intmax_t`/`uintmax_t` are `long`/`unsigned long` (64-bit).

It also supplies the limit macros (`INT8_MAX`, `INT64_MIN`, `INTMAX_MAX`, `SIZE_MAX`,
`PTRDIFF_MAX`, `WCHAR_MAX`, …) and the constant-builder macros: `INT64_C(c)` appends `L`,
`UINT64_C(c)` appends `UL`, `UINT32_C(c)` appends `U`.

### `<limits.h>` — ranges of the integer types

Pure macros giving the bounds of each integer type. The RISC-V values:

| Macro | Value | Macro | Value |
|---|---|---|---|
| `CHAR_BIT` | 8 | `MB_LEN_MAX` | 1 |
| `CHAR_MIN` / `CHAR_MAX` | 0 / 255 | `SCHAR_MIN` / `SCHAR_MAX` | −128 / 127 |
| `UCHAR_MAX` | 255 | `SHRT_MIN` / `SHRT_MAX` | −32768 / 32767 |
| `USHRT_MAX` | 65535 | `INT_MIN` / `INT_MAX` | −2³¹ / 2³¹−1 |
| `UINT_MAX` | 2³²−1 | `LONG_MAX` = `LLONG_MAX` | 2⁶³−1 |
| `LONG_MIN` = `LLONG_MIN` | −2⁶³ | `ULONG_MAX` = `ULLONG_MAX` | 2⁶⁴−1 |

Plain `char` is unsigned, so `CHAR_MAX` equals `UCHAR_MAX`. `MB_LEN_MAX` is 1: there is no
multibyte encoding beyond the single-byte `"C"` locale.

### `<float.h>` — characteristics of the floating types

The three families describe three different IEEE-754 formats:

| | `FLT_` (binary32) | `DBL_` (binary64) | `LDBL_` (binary128) |
|---|---|---|---|
| `*_MANT_DIG` | 24 | 53 | 113 |
| `*_DIG` | 6 | 15 | 33 |
| `*_DECIMAL_DIG` | 9 | 17 | 36 |
| `*_MIN_EXP` / `*_MAX_EXP` | −125 / 128 | −1021 / 1024 | −16381 / 16384 |
| `*_EPSILON` | ≈ 1.19 × 10⁻⁷ | ≈ 2.22 × 10⁻¹⁶ | ≈ 1.93 × 10⁻³⁴ |
| `*_MAX` | ≈ 3.40 × 10³⁸ | ≈ 1.80 × 10³⁰⁸ | ≈ 1.19 × 10⁴⁹³² |

`FLT_RADIX` is 2, `FLT_ROUNDS` 1 (round to nearest), `FLT_EVAL_METHOD` 0 (each operation
in its own type), and `DECIMAL_DIG` 36. The formats have subnormals, and the header gives
their smallest values as `FLT_TRUE_MIN`, `DBL_TRUE_MIN`, and `LDBL_TRUE_MIN`.

### `<stdbool.h>` — the boolean type

Defines `bool` as `_Bool`, plus `true`, `false`, and
`__bool_true_false_are_defined`. A `_Bool` occupies one byte. Converting any scalar to it
is a zero test, so it never holds anything but 0 or 1.

### `<stdarg.h>` — variable arguments

The compiler has no `va_*` builtins, so this header implements the LP64D variadic
convention with ordinary C. A function declared with `...` saves its argument registers
`a0`–`a7` just below its incoming stack arguments (see the stack frame in
[Riscv_Backend.md](Riscv_Backend.md#stack-frame)). Every argument passed in an integer
register or on the stack therefore sits in one array of consecutive 8-byte slots — and
the psABI passes *all* variadic arguments, `double` included, that way. So:

- `va_list` is a plain `char *`;
- `va_start(ap, last)` aims it just past the last named parameter, whose size is rounded
  up to 8 bytes;
- `va_arg(ap, T)` reads a `T` and advances by `sizeof(T)` rounded up to 8. A type aligned
  to more than 8 bytes (`long double`) is first realigned to a 16-byte boundary; a struct
  wider than 16 bytes was passed as a pointer to a copy, so `va_arg` reads the pointer
  and dereferences it;
- `va_end` is a no-op; `va_copy` is assignment.

One restriction follows from the design: the last named parameter must live in that slot
array, so it must not be one passed in an FP register (a `float` or `double`) or by
reference (a large struct).

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

Defines `alignas`/`alignof` as `_Alignas`/`_Alignof`, plus `__alignas_is_defined` and
`__alignof_is_defined`. On RISC-V every scalar is aligned to its size, from 1 byte for
`char` to 16 for `long double`.

### `<stdnoreturn.h>` — the `noreturn` convenience macro

Defines `noreturn` as `_Noreturn`, used to mark functions that never return (such as
`exit` and `abort`).

### `<iso646.h>` — alternative spellings

Defines readable spellings of the operator tokens: `and`, `or`, `not`, `xor`, `compl`,
`bitand`, `bitor`, `not_eq`, `and_eq`, `or_eq`, `xor_eq`.

### `<coro.h>` — vcc's extensions (not C11)

Defines `defer` as `_Defer`, the statement that runs when its block is left, on every
target; the coroutines planned for wasm32 will add their names here.
[Coroutines_in_C.md](Coroutines_in_C.md) is the manual. Like `<stdnoreturn.h>` it is
only names, so it is installed with the compiler's own headers for BESM-6 too.

---

## Hosted headers

These need runtime support and are present only in a hosted implementation. They are
grouped below by purpose. All but `<inttypes.h>`, `<math.h>` and `<setjmp.h>` are shared
with every target and come from `libc/common/include/`.

### Input and output — `<stdio.h>`

The workhorse header. It declares the formatted-output family, character and line I/O,
file handling, and — as an extension — the low-level console primitives.

| Group | Functions | In `libc.a` |
|---|---|---|
| Formatted output | `printf`, `sprintf`, `snprintf` | yes |
| | `fprintf`, `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf` | not yet |
| Formatted input | `scanf`, `sscanf`, `fscanf` | not yet |
| Character / line | `putchar`, `puts` | yes |
| | `getchar`, `fputs`, `fputc`, `fgetc`, `fgets` | not yet |
| Files | `fopen`, `fclose`, `fflush`, `fread`, `fwrite`, `perror` | not yet |
| Console extensions | `putbyte`, `putch`, `flush` | yes |
| | `getch` | not on RISC-V |

What to know on RISC-V:

- **Where output goes.** Everything ends in `putbyte`, which waits for the qemu `virt`
  UART's transmitter and writes one byte; run qemu with `-serial stdio` to see it. Output
  is unbuffered, so `flush` does nothing, and `putch` is simply `putbyte`. There is no
  input: `getch` is declared but not implemented for this target.
- **The format engine** (`libc/common/doprnt.c`) handles `%d %i %u %o %x %X %c %s %p %f
  %F %e %E %g %G %%`, the flags `-+ #0`, a width and precision (also as `*`), and the
  length modifiers `hh`, `h`, `l`, `ll`, `j`, `z`, `t`, which select the argument type.
  Conversion letters keep their case: `%x` prints lower-case hex, `%X` upper-case.
- **Floating output.** Infinities and NaNs print as `inf`/`nan` (`INF`/`NAN` for the
  upper-case conversions). `%Lf` accepts a `long double` but prints it with `double`
  precision.
- `snprintf`'s size parameter is declared `int`, not `size_t`.

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

`exit` and `abort` are marked `_Noreturn`. `RAND_MAX` is 2³¹−1, the 32-bit `INT_MAX`.

**Runtime availability.** `libc.a` implements `exit`, `atoi`, `malloc`, `calloc`,
`realloc` and `free`. `exit` stops qemu through its test-finisher device, with the status as qemu's exit
code. The allocator is a **bump allocator** over a 4 MiB heap that `link.ld` places after
`.bss`: `malloc` returns 16-byte-aligned memory (enough for any type, `long double`
included) or `NULL` when the heap is exhausted, `calloc` zeroes it, `realloc` returns
the block itself when it is already big enough and otherwise copies it into a new one
(each block carries its size in a 16-byte header), and `free` does nothing. The remaining
`<stdlib.h>` routines are declared but not yet implemented.

### Strings and memory — `<string.h>`

The familiar `str*` and `mem*` operations, every one of them implemented in `libc.a` from
the target-neutral sources in `libc/common/`:

`strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`,
`strrchr`, `strstr`, `strtok`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`,
`strerror`.

These are compiled by VCC itself from the same C source that the other target uses, so
they are written in plain byte-pointer C. `strerror` knows the six error numbers of
`<errno.h>` and returns a fixed upper-case message such as `"OUT OF MEMORY"`.

### Character classification — `<ctype.h>` and `<wctype.h>`

`<ctype.h>` declares the narrow classifiers and case mappers — `isalpha`, `isdigit`,
`isalnum`, `isspace`, `isupper`, `islower`, `ispunct`, `iscntrl`, `isprint`, `isgraph`,
`isblank`, `isxdigit`, `toupper`, `tolower`. As always, the argument is an `int` holding an
`unsigned char` value or `EOF`. None is implemented yet.

`<wctype.h>` is the wide analogue: `iswalpha`, `iswdigit`, … `towupper`, `towlower`, plus
the `wctype`/`wctrans` extensible-property mechanism. It draws `wint_t` and `WEOF` from
`<wchar.h>`. It too is declarations only.

### Mathematics — `<math.h>`, `<tgmath.h>`, `<fenv.h>`

`<math.h>` declares the usual real-valued functions, all taking and returning `double`
(there are no `f`- or `l`-suffixed variants, but for `sqrtf`):

| Group | Functions |
|---|---|
| Rounding / remainder | `fabs`, `floor`, `ceil`, `round`, `trunc`, `fmod`, `modf`, `frexp`, `ldexp` |
| Powers / logs | `sqrt`, `pow`, `exp`, `log`, `log10` |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` |
| Hyperbolic | `sinh`, `cosh`, `tanh` |
| Misc | `hypot`, `fma`, `fmin`, `fmax`, `copysign` |

Implemented in `libc.a` so far: `fabs`, `fmin`, `fmax`, `fma` (shared C sources), and the
binary64-specific `modf`, `frexp` and `ldexp` (in `libc/riscv64/`), which work on the
IEEE bit layout directly, and `sqrt` and `sqrtf`, one instruction each (`sqrt.s` in each
target's runtime; the compiler emits the instruction for a call of `sqrt` itself). The
rest are declared for future implementation.

Because the formats are IEEE, `<math.h>` defines `INFINITY` and `NAN`. The compiler has
no builtins for them, so they are spelled as overflowing constant expressions:
`INFINITY` is `(1e30f * 1e30f)` and `NAN` is `(INFINITY * 0.0f)`. `HUGE_VAL`,
`HUGE_VALF` and `HUGE_VALL` are `INFINITY` converted to each type. It also offers the handy
constants `M_PI` and `M_E`.

`<tgmath.h>` would normally choose, via `_Generic`, among `float`/`double`/`long double`
(and complex) versions of each function. Here `<math.h>` has only the `double` versions and
there is no complex support, so the type-generic machinery collapses: each macro (`sqrt`,
`pow`, `sin`, …) simply converts its argument to `double` and calls the single function in
`<math.h>`. Integer arguments promote to `double`, exactly as the standard requires.

`<fenv.h>` describes the floating-point environment — exception flags and rounding modes.
RISC-V does have both in its `fcsr` register, but this library does not expose them yet,
so the environment is *degenerate*: a single mode (`FE_TONEAREST`), no flags
(`FE_ALL_EXCEPT` is 0). The surface (`feclearexcept`, `fetestexcept`, `fegetround`,
`fesetenv`, …) is declared for source portability.

### Wide and Unicode characters — `<wchar.h>` and `<uchar.h>`

`<wchar.h>` is the canonical home of `wint_t` (an `int`), `mbstate_t`, and `WEOF`, and
declares the wide string and conversion routines (`wcslen`, `wcscpy`, `wcscmp`, `mbrtowc`,
`wcrtomb`, `wcstol`, …). A `wchar_t` is a 32-bit `int`, wide enough for any Unicode code
point.

`<uchar.h>` adds `char16_t` (`unsigned short`) and `char32_t` (`unsigned`, 32-bit) and the
`mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb` conversion functions. Neither header is
implemented yet.

### Diagnostics and errors — `<assert.h>` and `<errno.h>`

`<assert.h>` defines the `assert` macro, which checks a condition and, on failure, calls
`__assert_fail` with the expression, file, and line, to report them and abort. It honours
`NDEBUG` (defining it turns `assert` into a no-op) and is deliberately **re-includable**:
its meaning is recomputed at each `#include` according to `NDEBUG`. The runtime does not
provide `__assert_fail` yet, so a program that uses an active `assert` does not link.

`<errno.h>` declares the `errno` object and a small, non-POSIX set of error numbers —
`EDOM` (1), `ERANGE`, `EILSEQ`, `EINVAL`, `ENOMEM`, `EIO` (6). The `errno` object itself is
not yet defined in the runtime.

### Non-local control flow — `<setjmp.h>` and `<signal.h>`

`<setjmp.h>` declares `jmp_buf`, `setjmp`, and `longjmp` for non-local jumps. A `jmp_buf`
is `long[26]`: room for the registers the psABI requires a call to preserve — the return
address `ra`, the stack pointer `sp`, `s0`–`s11`, and `fs0`–`fs11`. `setjmp` and
`longjmp` are not implemented yet.

`<signal.h>` declares `signal`, `raise`, `sig_atomic_t`, and the `SIG*` numbers. A
bare-metal qemu program receives no asynchronous signals, so the practical use would be
synchronous `raise`; the surface exists for portability and is not implemented.

### Time and locale — `<time.h>` and `<locale.h>`

`<time.h>` declares `time_t` and `clock_t` (each a 64-bit `long`), `struct tm`, and the
calendar/clock functions (`time`, `clock`, `difftime`, `mktime`, `localtime`, `gmtime`,
`asctime`, `ctime`, `strftime`), plus `CLOCKS_PER_SEC` (1 000 000). None is implemented.

`<locale.h>` declares `setlocale`, `localeconv`, `struct lconv`, and the `LC_*` category
macros. Only the `"C"` locale is meaningful.

### Integer formatting — `<inttypes.h>`

Extends `<stdint.h>` with `imaxabs`, `imaxdiv` (and `imaxdiv_t`), `strtoimax`,
`strtoumax`, and the `PRI*`/`SCN*` format-string macros for `printf`/`scanf`. On LP64 the
8-, 16- and 32-bit `PRI*` macros are the bare letter (`"d"`, `"u"`, `"x"`, …), since
those types promote to `int`, while the 64-bit, `MAX` and `PTR` ones carry the `l`
modifier (`PRId64` is `"ld"`, `PRIxPTR` is `"lx"`). The `SCN*` macros use `hh`/`h` for
the narrow types, because `scanf` stores through a pointer of exactly that width. The
four functions are declared but not yet implemented.

```c
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    int64_t big = INT64_MAX;
    printf("%" PRId64 "\n", big);   /* 9223372036854775807 */
    return 0;
}
```

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

This is also what lets the shared headers be shared: a `libc/common/include/` header
never spells out a data-model-dependent type itself, but takes `size_t` from the target's
`<stddef.h>`, `va_list` from its `<stdarg.h>`, and so on.

**Include dependencies.** A few headers are built atop others and include them directly:

- `<stdio.h>` and `<wchar.h>` → `<stddef.h>` (for `size_t`) and `<stdarg.h>` (for `va_list`);
- `<tgmath.h>` → `<math.h>` → `<float.h>`;
- `<inttypes.h>` → `<stdint.h>`;
- `<wctype.h>` and `<uchar.h>` → `<wchar.h>`;
- `<stdlib.h>`, `<string.h>`, `<time.h>`, `<locale.h>`, `<uchar.h>` → `<stddef.h>`;
- `<assert.h>` → `<stdlib.h>`.

Because each header guards itself and types are defined once, you may `#include` any
combination in any order without clashes. The `riscv-headers` test (run by `make run`)
preprocesses and parses every header to keep it that way.

---

## Other targets

The split into a target directory and a shared one is what makes the library retargetable:
each target supplies its own copy of the headers whose contents follow from its data model
— `<float.h>`, `<limits.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdint.h>`, `<inttypes.h>`,
`<math.h>`, `<setjmp.h>` — and shares everything in `libc/common/include/`.

The BESM-6 target keeps its own in [`libc/besm6/include/`](../libc/besm6/include/). Its
data model is very different (48-bit words, no IEEE floating point; see
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md)), and it adds
two headers of its own: `<malloc.h>` and `<besm6.h>`, which declares the machine
intrinsics described in [Besm6_Intrinsics.md](../backend/besm6/Besm6_Intrinsics.md). Unlike
RISC-V, BESM-6 installs only the nine freestanding headers plus `<besm6.h>` to
`share/vcc/besm6/include/`; the hosted headers for that target ship with the sibling
v7besm project, which owns its C library.

---

## Conformance summary

All 26 shipped RISC-V headers. "Kind" marks the nine C11 freestanding headers versus the
hosted remainder; "Source" says whether a header is RISC-V-specific (`riscv`) or shared
(`common`).

| Header | Kind | Source | Role |
|---|---|---|---|
| `<float.h>` | freestanding | riscv | characteristics of the floating types |
| `<iso646.h>` | freestanding | common | alternative operator spellings |
| `<limits.h>` | freestanding | riscv | ranges of the integer types |
| `<stdalign.h>` | freestanding | common | `alignas` / `alignof` |
| `<stdarg.h>` | freestanding | riscv | variable arguments |
| `<stdbool.h>` | freestanding | common | `bool`, `true`, `false` |
| `<stddef.h>` | freestanding | riscv | `size_t`, `ptrdiff_t`, `NULL`, `offsetof`, … |
| `<stdint.h>` | freestanding | riscv | fixed-width integer types |
| `<stdnoreturn.h>` | freestanding | common | `noreturn` macro |
| `<assert.h>` | hosted | common | run-time assertions |
| `<ctype.h>` | hosted | common | narrow character classification |
| `<errno.h>` | hosted | common | error numbers and `errno` |
| `<fenv.h>` | hosted | common | floating-point environment (degenerate) |
| `<inttypes.h>` | hosted | riscv | integer format macros and conversions |
| `<locale.h>` | hosted | common | localization (`"C"` locale) |
| `<math.h>` | hosted | riscv | real mathematics |
| `<setjmp.h>` | hosted | riscv | non-local jumps |
| `<signal.h>` | hosted | common | signal handling |
| `<stdio.h>` | hosted | common | input / output and console primitives |
| `<stdlib.h>` | hosted | common | general utilities |
| `<string.h>` | hosted | common | string and memory operations |
| `<tgmath.h>` | hosted | common | type-generic math (degenerate) |
| `<time.h>` | hosted | common | date and time |
| `<uchar.h>` | hosted | common | Unicode characters |
| `<wchar.h>` | hosted | common | wide characters and multibyte conversion |
| `<wctype.h>` | hosted | common | wide character classification |
| `<coro.h>` | vcc extension | common | `defer` |

Full hosted conformance still excludes `<complex.h>`, `<stdatomic.h>`, and `<threads.h>`,
which depend on language features the compiler does not provide. With those three
exceptions, a program written against the standard headers will compile; whether it links
depends on which of the declared routines `libc.a` implements so far.
