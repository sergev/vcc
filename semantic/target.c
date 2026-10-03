#include "target.h"

#include "tac.h"

#include <stdio.h>
#include <string.h>

//
// The <besm6.h> intrinsics with an immediate first argument (backend/besm6/Besm6_Intrinsics.md
// §3.3, §3.4, §5).
//
static const ImmediateArg besm6_immediate_args[] = {
    // `op` *is* the extracode's opcode; only 050..077 are extracodes, anything else names a
    // different instruction entirely.
    { "__besm6_extracode", "opcode", "is not an extracode (050..077)", 050, 077 },
    // The mask of the register-0 `vtm` mode write, and the halt code of `033`: both ride in
    // the instruction's own 15-bit address field.
    { "__besm6_maskpsw", "mask", "does not fit the 15-bit address field", 0, 077777 },
    { "__besm6_stop", "halt code", "does not fit the 15-bit address field", 0, 077777 },
    {},
};

// clang-format off
static const Target targets[] = {
    // name
    //            bool_size   bool_align
    //            short_size  short_align
    //            int_size    int_align
    //            long_size   long_align
    //            llong_size  llong_align
    //            float_size  float_align
    //            double_size double_align
    //            ldouble_size ldouble_align
    //            pointer_size pointer_align

    { "avr",
      1, 1,   // _Bool
      2, 1,   // short
      2, 1,   // int
      4, 1,   // long
      8, 1,   // long long
      4, 1,   // float
      4, 1,   // double  (same as float in avr-gcc default mode)
      4, 1,   // long double (same as float)
      2, 1,   // pointer (16-bit data address)
      16, 16, 32, 64, // signed bits: short int long llong
      0,   // plain char unsigned (avr-gcc)
      0,   // signed >> arithmetic
      1 }, // aggregate_align (1)

    { "msp430",
      1, 1,   // _Bool
      2, 2,   // short
      2, 2,   // int
      4, 2,   // long
      8, 2,   // long long
      4, 2,   // float
      8, 2,   // double
      8, 2,   // long double (same as double)
      2, 2,   // pointer (16-bit, 4 bytes on MSP430X)
      16, 16, 32, 64, // signed bits
      1,   // plain char signed (msp430-gcc)
      0,   // signed >> arithmetic
      1 }, // aggregate_align (1)

    { "arm32",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      4, 4,   // long (ILP32: same size as int)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
      8, 8,   // long double (same as double on ARM EABI)
      4, 4,   // pointer
      16, 32, 32, 64, // signed bits
      0,   // plain char unsigned (ARM EABI)
      0,   // signed >> arithmetic
      1,   // aggregate_align (1)
      SIZE_MAX, // struct_return_max: never lowered by the front end; the backend
                // returns an HFA in VFP registers although it is wider than 4 bytes
      0,        // struct_args_split
      NULL,     // immediate_args
      NULL },   // va_class: va_arg is a pointer walk, no argument classes

    { "aarch64",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      8, 8,   // long (LP64)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
     16,16,   // long double (IEEE 754 binary128)
      8, 8,   // pointer
      16, 32, 64, 64, // signed bits
      0,   // plain char unsigned (AAPCS64)
      0,   // signed >> arithmetic
      1,   // aggregate_align (1)
      SIZE_MAX, // struct_return_max: never lowered by the front end; the backend
                // passes a large result's address in x8, not as an argument
      0,        // struct_args_split
      NULL,     // immediate_args
      tac_aapcs64_class }, // va_class

    { "x86_64",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      8, 8,   // long (LP64)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
     16,16,   // long double (x87 80-bit value in 16-byte slot)
      8, 8,   // pointer
      16, 32, 64, 64, // signed bits
      1,   // plain char signed (x86-64 System V)
      0,   // signed >> arithmetic
      1 }, // aggregate_align (1)

    { "riscv32",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      4, 4,   // long (ILP32: same size as int)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
     16,16,   // long double (IEEE 754 binary128, software)
      4, 4,   // pointer
      16, 32, 32, 64, // signed bits
      0,   // plain char unsigned (RISC-V ABI)
      0,   // signed >> arithmetic
      1,   // aggregate_align (1)
      16 }, // struct_return_max: ILP32D returns {double, double} in fa0/fa1; the
            // backend passes the hidden pointer for the other structs over 8 bytes

    { "riscv64",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      8, 8,   // long (LP64)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
     16,16,   // long double (IEEE 754 binary128, software)
      8, 8,   // pointer
      16, 32, 64, 64, // signed bits
      0,   // plain char unsigned (RISC-V ABI)
      0,   // signed >> arithmetic
      1 }, // aggregate_align (1)

    { "mmix",
      1, 1,   // _Bool
      2, 2,   // short
      4, 4,   // int
      8, 8,   // long (LP64)
      8, 8,   // long long
      4, 4,   // float
      8, 8,   // double
      8, 8,   // long double (same as double; no wider FP hardware)
      8, 8,   // pointer
      16, 32, 64, 64, // signed bits
      1,   // plain char signed (MMIXware convention)
      0,   // signed >> arithmetic
      1 }, // aggregate_align (1)

    // BESM-6: 48-bit word-oriented machine.
    // sizeof() values are in 8-bit bytes (CHAR_BIT = 8).
    // One machine word = 6 bytes.  Every scalar type is a single word — there are
    // no two-word scalar types.  long long is the same as long; long double is the
    // same as double.  All alignments are 1 word = 6 bytes.
    // Signed int/long/long long are 41-bit (sign + 40 value bits) inside the word;
    // unsigned types use the full 48-bit storage width.
    // _Bool is a word too: see the Target comment — a byte-sized _Bool would mean
    // packed storage and fat byte pointers on this machine.
    { "besm6",
      6, 6,   // _Bool   (1 word)
      6, 6,   // short   (1 word)
      6, 6,   // int     (1 word)
      6, 6,   // long    (1 word)
      6, 6,   // long long  (1 word, same as long)
      6, 6,   // float   (1 word, BESM-6 native FP)
      6, 6,   // double  (1 word, same as float)
      6, 6,   // long double (1 word, same as double)
      6, 6,   // pointer (1 word, 15-bit word address)
      41, 41, 41, 41, // signed bits
      0,   // plain char unsigned
      1,   // signed >> logical (BESM-6 shift unit does no sign extension)
      6,   // aggregate_align (6)
      6,   // struct_return_max (1 word)
      1,   // struct_args_split
      besm6_immediate_args },
};
// clang-format on

#define NUM_TARGETS ((int)(sizeof(targets) / sizeof(targets[0])))

// Index of the default target (x86_64).
#define DEFAULT_TARGET_INDEX 4

const Target *target_config = &targets[DEFAULT_TARGET_INDEX];

const Target *target_lookup(const char *name)
{
    for (int i = 0; i < NUM_TARGETS; i++) {
        if (strcmp(targets[i].name, name) == 0) {
            return &targets[i];
        }
    }
    return NULL;
}

void target_list(void)
{
    for (int i = 0; i < NUM_TARGETS; i++) {
        fprintf(stderr, "  %s\n", targets[i].name);
    }
}

int target_word_addressed(void)
{
    return target_config->aggregate_align > 1;
}

int64_t sign_narrow(uint64_t bits, int w)
{
    if (w <= 0 || w >= 64)
        return (int64_t)bits;
    uint64_t mask = ((uint64_t)1 << w) - 1;
    uint64_t sbit = (uint64_t)1 << (w - 1);
    uint64_t low  = bits & mask;
    return (int64_t)((low ^ sbit) - sbit);
}

uint64_t unsigned_narrow(uint64_t bits, int w)
{
    if (w <= 0 || w >= 64)
        return bits;
    return bits & (((uint64_t)1 << w) - 1);
}
