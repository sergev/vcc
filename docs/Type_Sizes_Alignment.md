# Type Sizes and Alignment

## 1. Introduction

The C11 standard deliberately leaves the sizes of most integer types unspecified so
that each implementation can pick the sizes that are most efficient on its target
hardware. A `long` is 32 bits on a 32-bit ARM but 64 bits on 64-bit RISC-V. An `int`
is 16 bits on an 8-bit AVR but 32 bits on every 32-bit-and-above platform. A compiler
that targets multiple CPU families must track these differences explicitly.

This document uses the RISC-V 64 target (`riscv64`, the LP64D ABI that `genriscv`
implements) as its running example: Section 3 lists its types, Section 4 works through
struct layout on it, and Section 6 compares it with every other target VCC knows about.

### C11 minimum guarantees

The standard only guarantees minimum precisions (in bits):

| Type        | Minimum precision |
|-------------|-------------------|
| `char`      | 8                 |
| `short`     | 16                |
| `int`       | 16                |
| `long`      | 32                |
| `long long` | 64                |

For floating point it only requires minimum ranges and decimal precisions
(`FLT_DIG` ≥ 6, `DBL_DIG` ≥ 10); IEEE 754 binary32/binary64 is what Annex F asks for,
but Annex F is optional.

The standard also guarantees `sizeof(char) == 1` by definition, where "1" means one
*addressable unit*, not necessarily one octet.

### Data models

The combination of sizes chosen for `int`, `long`, and pointers defines a *data model*.
The common models on byte-addressed machines are:

| Model | `int`  | `long` | `long long` | pointer | Typical platform |
|-------|--------|--------|-------------|---------|------------------|
| LP16  | 16-bit | 32-bit | 64-bit      | 16-bit  | AVR, MSP430      |
| ILP32 | 32-bit | 32-bit | 64-bit      | 32-bit  | ARM32, RISC-V 32 |
| LP64  | 32-bit | 64-bit | 64-bit      | 64-bit  | RISC-V 64, AArch64, x86_64 (Linux/macOS), MMIX |
| LLP64 | 32-bit | 32-bit | 64-bit      | 64-bit  | x86_64 (Windows) |

RISC-V 64 is LP64. BESM-6, the compiler's other backend, does not fit any of these
models because it is not byte-addressed; see Section 7.

---

## 2. Natural Alignment

A datum of size *N* bytes is *naturally aligned* when its address is a multiple of *N*.
For example, a 4-byte `int` is naturally aligned at addresses 0, 4, 8, 12, …

### Why alignment matters

- **Fault or trap**: Some CPUs (many ARM Cortex-M, MIPS, SPARC) raise a hardware
  exception on a misaligned load or store. The RISC-V base ISA allows an implementation
  to do the same, or to emulate the access in a trap handler — correct, but very slow.
- **Performance**: x86_64 and AArch64 accept misaligned accesses but may require two
  cache-line fetches instead of one.
- **Atomicity**: C11 `_Atomic` operations are only guaranteed lock-free when naturally
  aligned.

On every byte-addressed target VCC describes except AVR and MSP430, each scalar type is
naturally aligned: its alignment equals its size.

---

## 3. RISC-V 64 (LP64D)

The `riscv64` target follows the RISC-V ELF psABI with the LP64D calling convention:
RV64IMFD, little-endian, 64-bit `long` and pointers, `float` and `double` in hardware
(F and D extensions). `long double` is IEEE 754 binary128; no RISC-V extension in use
here computes it, so its arithmetic is done in software by `libc/riscv64/float128.c`
(see [Riscv_Backend.md](Riscv_Backend.md)).

| Type                    | Size | Alignment | Notes |
|-------------------------|------|-----------|-------|
| `_Bool`                 | 1    | 1         | |
| `char`                  | 1    | 1         | **Unsigned** (`CHAR_MIN` = 0, `CHAR_MAX` = 255) |
| `signed char`           | 1    | 1         | |
| `short`                 | 2    | 2         | |
| `int`                   | 4    | 4         | |
| enum                    | 4    | 4         | Same as `int` (on every target) |
| `long`                  | 8    | 8         | LP64 |
| `long long`             | 8    | 8         | |
| `float`                 | 4    | 4         | IEEE 754 binary32 |
| `double`                | 8    | 8         | IEEE 754 binary64 |
| `long double`           | 16   | 16        | IEEE 754 binary128, software |
| pointer                 | 8    | 8         | |

Unsigned variants have the size and alignment of their signed counterparts, and all
integer types use their full width for the value: `int` is 32 bits, `long` 64.

The headers in `libc/riscv64/include/` spell out the same choices:

| Header       | Definitions |
|--------------|-------------|
| `limits.h`   | `CHAR_BIT` 8; `INT_MAX` 2147483647; `LONG_MAX` = `LLONG_MAX` = 9223372036854775807 |
| `stddef.h`   | `size_t` = `unsigned long`, `ptrdiff_t` = `long`, `wchar_t` = `int`; `max_align_t` holds a `long long` and a `long double`, so it is 32 bytes aligned to 16 |
| `stdint.h`   | `int64_t`, `intptr_t`, `intmax_t` = `long`; `int_fast16_t` and `int_fast32_t` are `long` too |
| `float.h`    | `FLT_MANT_DIG` 24, `DBL_MANT_DIG` 53, `LDBL_MANT_DIG` 113; `LDBL_MAX_EXP` 16384 |

Plain `char` being unsigned is the RISC-V ABI's choice (ARM's too), and differs from
x86_64, where it is signed. Code that stores a negative value in a plain `char` and
compares it with a negative constant behaves differently on the two.

---

## 4. Struct and Union Layout on RISC-V 64

The compiler lays out a struct by placing each member, in declaration order, at the
next offset that is a multiple of the member's alignment. The struct's alignment is the
strictest alignment of its members, and its size is rounded up to a multiple of that
alignment. The `offsetof` macro (from `<stddef.h>`) returns the byte offset of each
member including any padding. The code is `register_struct_type` in
`semantic/declarations.c`.

Every layout below was produced by the compiler itself (see "Checking a layout" at the
end of this section).

### Padding between members

```c
struct Example {      // riscv64
    char   a;         // offset 0, size 1
    // 3 bytes padding
    int    b;         // offset 4, size 4
    double c;         // offset 8, size 8
};                    // size 16, alignment 8
```

### Member order matters

The same three-byte payload costs 24 bytes or 16 depending on the order:

```c
struct Reorder {      // size 24, alignment 8
    char   a;         // offset 0
    // 7 bytes padding
    double b;         // offset 8
    char   c;         // offset 16
    // 7 bytes tail padding
};

struct Sorted {       // size 16, alignment 8
    double b;         // offset 0
    char   a;         // offset 8
    char   c;         // offset 9
    // 6 bytes tail padding
};
```

Sorting members from the strictest alignment down minimizes padding.

### Tail padding and arrays

Tail padding is what keeps every element of an array aligned. In

```c
struct Tail {         // size 16, alignment 8
    long l;           // offset 0
    char c;           // offset 8
    // 7 bytes tail padding
};
struct Tail arr[3];   // 48 bytes; arr[1].l is at offset 16
```

without the padding `arr[1].l` would sit at offset 9.

### Smaller and larger alignments

A struct with no member wider than `short` is only 2-aligned, and a `long double`
member forces 16:

```c
struct Small {        // size 4, alignment 2
    char  a;          // offset 0
    short b;          // offset 2
};

struct LD {           // size 32, alignment 16
    char        c;    // offset 0
    // 15 bytes padding
    long double x;    // offset 16
};
```

On x86_64 `struct LD` has the same layout; there the `long double` is an 80-bit x87
value in a 16-byte slot.

### Unions and `_Alignas`

A union places every member at offset 0; its size is the largest member rounded up to
the union's alignment. `_Alignas` raises a member's alignment above its natural one:

```c
union U {             // size 8, alignment 4
    char c[5];        // offset 0, size 5
    int  i;           // offset 0, size 4
};

struct A {            // size 32, alignment 16
    char c;                // offset 0
    _Alignas(16) int i;    // offset 16
};
```

### Structs at a call boundary

Layout also decides how a struct crosses a call (`backend/riscv/call.c`):

- A struct of up to 16 bytes travels in one or two registers. If it flattens to one or
  two scalars, at least one of them floating point, it goes in `fa` registers (or one
  `fa` and one `a` register); otherwise as one or two doublewords in `a` registers.
- A larger struct is passed by reference to a copy, and returned through a hidden
  pointer to a caller-allocated slot. The 16-byte limit is two pointers; the target
  descriptor leaves `struct_return_max` at 0, which means exactly that.
- A `long double` is passed like a 16-byte struct, in two integer registers; a value of
  16 bytes aligned to 16 starts at a 16-byte boundary on the stack.

### Checking a layout

`lower --yaml` prints each struct's size, alignment and member offsets for any target:

```sh
./build/parse file.c file.ast
./build/lower -t riscv64 --yaml file.ast - | grep -E "tag|size|alignment|offset"
```

`lower` defaults to `-t riscv64`; pass `-t` to lay the same struct out for another target.

---

## 5. Where the Numbers Live

`get_size()` / `get_alignment()` in `semantic/type_utils.c` read the active target
descriptor, a `Target` record (`semantic/target.h`) chosen from the table in
`semantic/target.c` by `target_lookup()` — `lower -t NAME` selects it. Only
`char`/`signed char`/`unsigned char` are hard-coded at 1, which C requires; enums are
always the size of `int`. Besides the scalar sizes and alignments, each descriptor
records:

| Field                     | Meaning |
|---------------------------|---------|
| `short_bits` … `llong_bits` | Signed value width in bits, which the constant folders wrap to |
| `char_signed`             | Whether plain `char` is signed |
| `right_shift_is_logical`  | Whether `>>` of a signed value zero-fills |
| `aggregate_align`         | Minimum alignment of any struct or union |
| `struct_return_max`       | Widest struct returned in registers (0 = two pointers) |
| `struct_args_split`       | Pass a struct wider than a word as separate word arguments |

The TAC carries each struct's size, alignment and member offsets, so a backend takes
aggregate layout from its input rather than recomputing it.

---

## 6. Target Comparison

`semantic/target.c` defines nine target descriptors. Two of them have a code
generator: `riscv64` (`genriscv`) and `besm6` (`genbesm`). The other seven describe
real ABIs so that the front end and the TAC can be produced for them, for example to
compare layouts; `x86_64` is the default when a program using the libraries sets no
target.

Sizes, in bytes (`sizeof` units):

| Type          | riscv64 | riscv32 | x86_64 | aarch64 | arm32 | mmix | msp430 | avr | besm6 |
|---------------|---------|---------|--------|---------|-------|------|--------|-----|-------|
| `_Bool`       | 1       | 1       | 1      | 1       | 1     | 1    | 1      | 1   | 6     |
| `char`        | 1       | 1       | 1      | 1       | 1     | 1    | 1      | 1   | 1     |
| `short`       | 2       | 2       | 2      | 2       | 2     | 2    | 2      | 2   | 6     |
| `int`         | 4       | 4       | 4      | 4       | 4     | 4    | 2      | 2   | 6     |
| `long`        | 8       | 4       | 8      | 8       | 4     | 8    | 4      | 4   | 6     |
| `long long`   | 8       | 8       | 8      | 8       | 8     | 8    | 8      | 8   | 6     |
| `float`       | 4       | 4       | 4      | 4       | 4     | 4    | 4      | 4   | 6     |
| `double`      | 8       | 8       | 8      | 8       | 8     | 8    | 8      | 4   | 6     |
| `long double` | 16      | 16      | 16     | 16      | 8     | 8    | 8      | 4   | 6     |
| pointer       | 8       | 4       | 8      | 8       | 4     | 8    | 2      | 2   | 6     |

Alignments, in bytes:

| Type          | riscv64 | riscv32 | x86_64 | aarch64 | arm32 | mmix | msp430 | avr | besm6 |
|---------------|---------|---------|--------|---------|-------|------|--------|-----|-------|
| `_Bool`       | 1       | 1       | 1      | 1       | 1     | 1    | 1      | 1   | 6     |
| `char`        | 1       | 1       | 1      | 1       | 1     | 1    | 1      | 1   | 1     |
| `short`       | 2       | 2       | 2      | 2       | 2     | 2    | 2      | 1   | 6     |
| `int`         | 4       | 4       | 4      | 4       | 4     | 4    | 2      | 1   | 6     |
| `long`        | 8       | 4       | 8      | 8       | 4     | 8    | 2      | 1   | 6     |
| `long long`   | 8       | 8       | 8      | 8       | 8     | 8    | 2      | 1   | 6     |
| `float`       | 4       | 4       | 4      | 4       | 4     | 4    | 2      | 1   | 6     |
| `double`      | 8       | 8       | 8      | 8       | 8     | 8    | 2      | 1   | 6     |
| `long double` | 16      | 16      | 16     | 16      | 8     | 8    | 2      | 1   | 6     |
| pointer       | 8       | 4       | 8      | 8       | 4     | 8    | 2      | 1   | 6     |
| struct (min.) | 1       | 1       | 1      | 1       | 1     | 1    | 1      | 1   | 6     |

Other target-defined choices:

| Property              | riscv64 | riscv32 | x86_64 | aarch64 | arm32 | mmix | msp430 | avr | besm6 |
|-----------------------|---------|---------|--------|---------|-------|------|--------|-----|-------|
| plain `char`          | unsigned | unsigned | signed | unsigned | unsigned | signed | signed | unsigned | unsigned |
| signed `int` bits     | 32      | 32      | 32     | 32      | 32    | 32   | 16     | 16  | 41    |
| signed `long` bits    | 64      | 32      | 64     | 64      | 32    | 64   | 32     | 32  | 41    |
| signed `>>`           | arith.  | arith.  | arith. | arith.  | arith. | arith. | arith. | arith. | logical |

Notes on the individual targets:

- **riscv32** (ILP32): the 32-bit RISC-V ABI. Same sizes as ARM32 except `long double`,
  which stays binary128 as on riscv64. Compiled by `genriscv --rv32`; see
  [Riscv_Backend.md](Riscv_Backend.md).
- **x86_64** (System V; Windows' LLP64 is not described): `long double` is an x87
  80-bit value stored in a 16-byte, 16-aligned slot.
- **aarch64** (AAPCS64): the same sizes as riscv64, including binary128 `long double`.
- **arm32** (ARM EABI): `long double` is the same 64-bit format as `double`.
- **mmix** (Knuth's MMIX, big-endian): LP64 integers, but `long double` is the same
  8-byte format as `double` (the GCC MMIX port's choice; the FPU has no wider format).
- **msp430**: everything 2 bytes or wider is aligned to 2. On MSP430X pointers widen to
  4 bytes; the descriptor describes the 16-bit variant.
- **avr**: no alignment requirement at all; `double` and `long double` are the same
  4-byte format as `float` (avr-gcc's default).
- **besm6**: every scalar is one 48-bit word; see the next section.

Taken together: riscv64, aarch64, x86_64 and mmix share LP64 integers; riscv32 and
arm32 share ILP32; `long double` is a true binary128 on riscv64, riscv32 and aarch64.

---

## 7. BESM-6: a Word-Addressed Target

BESM-6 is the one target that is not byte-addressed. Memory is an array of 48-bit
words, an address is a 15-bit word index, and there is no instruction that loads or
stores a single byte. VCC keeps `CHAR_BIT` at 8 and counts `sizeof` in bytes, so one
word is 6:

- **Every scalar is one word.** `_Bool`, `short`, `int`, `long`, `long long`, `float`,
  `double`, `long double` and pointers all have size 6 and alignment 6; there is no
  two-word scalar. Signed integers carry 41 value bits (sign included) in the word,
  unsigned ones all 48, and the floating-point types share the machine's native 48-bit
  format, not IEEE 754.
- **`char` is packed six to a word** in arrays and among struct members (in
  `struct Sorted` from Section 4 the two `char`s sit at offsets 6 and 7), while a
  standalone `char` variable takes a whole word.
- **`char *` and `void *` are fat pointers**: a word address plus a byte position held
  in the pointer's high bits. Loading a byte is a word load, a shift and a mask; storing
  one is a read-modify-write.
- **Aggregates are whole words.** Every struct and union is aligned to at least a word
  (`aggregate_align` = 6), so array strides stay word multiples; `struct Example` from
  Section 4 is 18 bytes (three words). A struct wider than a word is passed as separate
  word arguments, and only a one-word struct is returned in a register.
- **`_Bool` is a word** rather than a byte, because on this machine a 1-byte type means
  packed storage and fat pointers — six `_Bool`s to a word and a read-modify-write for
  every store, all to carry one bit.

Bit layouts, ranges and the fat-pointer encoding are in
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md).
