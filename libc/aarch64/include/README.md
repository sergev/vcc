# AArch64 standard library headers

The C11 headers that are AArch64's own: `setjmp.h`, `stdarg.h`, `stddef.h`,
`stdint.h`.  The LP64 data-model headers it shares with RISC-V are in
[libc/lp64/include](../../lp64/include), searched second, and the target-neutral ones
in [libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/aarch64/include -I libc/lp64/include -I libc/common/include prog.c prog.i
```

`wchar_t` is an `unsigned int` (AAPCS64).  `va_list` is the AAPCS64 structure; the
code generator expands `va_start`, and `va_arg` calls the runtime's `__va_arg` (see
`stdarg.h`).
