# LP64 standard library headers

The C11 headers that depend only on the LP64 data model shared by riscv64 and
aarch64: `float.h`, `inttypes.h`, `limits.h`, `math.h`.  x86-64 and MMIX take
`inttypes.h` and `math.h` from here, with a `float.h` and `limits.h` of their own.  They are searched after the
target's own directory ([libc/riscv64/include](../../riscv64/include) or
[libc/aarch64/include](../../aarch64/include)) and before
[libc/common/include](../../common/include):

```sh
cc -E -nostdinc -I libc/aarch64/include -I libc/lp64/include -I libc/common/include prog.c prog.i
```

`int` is 32 bits, `long` and pointers 64, plain `char` unsigned; `float`, `double`
and `long double` are IEEE binary32, binary64 and binary128.  `stddef.h` and
`stdint.h` are not here: `wchar_t` is `int` on RISC-V but `unsigned int` on AArch64.
The C sources of the LP64 libc routines are in the parent directory.
