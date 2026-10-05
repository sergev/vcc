# 16-bit standard library headers

The C11 headers that depend only on the 16-bit data model shared by AVR and MSP430:
`inttypes.h`, `stddef.h` and `stdint.h`; MSP430 has its own `stddef.h` and `stdint.h`
(its `wchar_t` is `long`, as msp430-elf-gcc has it), so only `inttypes.h` is shared.
They are searched after the target's own
directory ([libc/avr/include](../../avr/include) or
[libc/msp430/include](../../msp430/include)) and before
[libc/common/include](../../common/include):

```sh
cc -E -nostdinc -I libc/msp430/include -I libc/ip16/include -I libc/common/include prog.c prog.i
```

`int`, `short` and pointers are 16 bits, `long` 32 and `long long` 64; `size_t` is
`unsigned int`, `ptrdiff_t` and `wchar_t` are `int` (on AVR).  `stddef.h` also defines
`_RAND_MAX`, which makes the shared `stdlib.h`'s `RAND_MAX` fit an `int`.
`limits.h` is not here: plain `char` is signed on AVR and unsigned on MSP430; nor is
`float.h`: `double` is binary32 on AVR and binary64 on MSP430.
