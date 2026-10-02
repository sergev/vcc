# The RISC-V backend

`genriscv` (`backend/riscv/`) turns TAC into GNU assembly for RV64IMFD with the LP64D
psABI. Programs run on bare-metal `qemu-system-riscv64 -M virt`, and link with code
compiled by clang. It is the second backend, beside BESM-6, and the proof that the
frontend is not tied to one machine.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | RV64IMFD, no C extension | 64-bit registers hold every scalar but `long double`; M has mul/div |
| ABI | LP64D psABI | link-compatible with clang, which is the test oracle |
| Output | GNU assembler (`.s`) | assembled by clang or binutils; no assembler of our own |
| Run | `qemu-system-riscv64 -M virt -bios none` | runs on macOS, where there is no qemu user mode |
| IR | `Rv_Instr` lists (`rv.h`) over real registers | small; covers only what is emitted |
| `long double` | IEEE binary128 in software | what the psABI specifies |

## Passes

For each function (`codegen.c`):

1. **Register allocation** (`regalloc.c`), on TAC: liveness from `backend/common/flow.c`,
   then graph colouring with Briggs coalescing and optimistic spilling. A candidate is a
   scalar parameter or local whose address is never taken. A value live across a call
   gets s1–s11 or fs0–fs11; others may also get a0–a7 or fa0–fa7, first. Hints steer a
   parameter to its incoming register, an argument to its outgoing one, a result to a0.
2. **Frame layout**: a slot for every name without a register.
3. **Instruction selection** (`instr.c`, `call.c`), one TAC instruction at a time, on the
   allocated registers, with t0–t6 and ft0–ft11 as scratch. Integer values are kept in
   canonical form: 32-bit ones sign-extended, narrower ones extended by signedness.
4. **Prologue and epilogue** (`frame.c`), once the frame is known.
5. **Peephole** (`peephole.c`): immediate forms, the zero register, scratch moves folded
   away, reloads of a frame slot just stored, branch and jump cleanup.

Options: `--no-regalloc` (every variable in memory), `--no-peephole`, `--frame-pointer`.

## Frame

```
s0 + 0 ...      incoming stack arguments
s0 - 64 ...     a0-a7, in a variadic function only
s0 - H + 8      saved ra   (H = 16, or 80 when variadic)
s0 - H          saved s0
s0 - H - ...    saved s1-s11/fs0-fs11 in use, then slots
sp + 0 ...      outgoing stack arguments
```

A leaf function that touches no slot and saves no register has no frame at all. A
frame that fits 12-bit offsets is addressed from `sp`, without setting up `s0`; then
`ra` is saved only when there are calls, and the slots move up into the unused header.

## Calls

The full LP64D convention: scalars in a0–a7 and fa0–fa7, then the stack; a struct of up
to 16 bytes in one or two registers, FP ones when it flattens to one or two floating
members; a larger one by reference; a struct return wider than 16 bytes through a hidden
pointer (lowered by the translator). A variadic function stores a0–a7 below its stack
arguments, so `<stdarg.h>` walks one array; `va_list` is a `char *`.

A `long double` lives in a 16-byte slot and is passed in an integer register pair (a7
and the stack when one register is left; 16-byte aligned on the stack; an even pair
when variadic). Its arithmetic, comparisons and conversions call the libgcc-named
routines (`__addtf3`, `__lttf2`, `__fixtfdi`, ...) in `libc/riscv/float128.c`, which
includes the compiler's own `libutil/float128.c`: constants are folded by the same code.

## Runtime

`libc/riscv/`: `crt0.S` (sets up gp, sp and the FPU, clears .bss, calls `main`, then
`exit`), `console.s` (`putbyte` on the ns16550 UART at 0x10000000; `exit` through the
SiFive test finisher at 0x100000: status 0 ends qemu with 0, any other status becomes
qemu's exit code; a trap exits with 255), a bump `malloc`, `link.ld` (RAM at
0x80000000), and C sources compiled by our own toolchain: `printf` and friends, the
`<string.h>` routines, the math helpers, and the binary128 routines.

## Running a program by hand

Needs a RISC-V clang and `ld.lld` (Homebrew `llvm` and `lld`) and `qemu-system-riscv64`.
With an installed compiler (`make install`; `$P` is the prefix):

```sh
cc -E -nostdinc -I$P/share/riscv/include hello.c -o hello.i
b6parse hello.i hello.ast
b6lower -t riscv64 hello.ast hello.tac
rv64codegen hello.tac hello.s
clang --target=riscv64 -march=rv64imfd -mabi=lp64d -c hello.s -o hello.o
ld.lld -T $P/share/riscv/lib/link.ld -o hello.elf \
    $P/share/riscv/lib/crt0.o hello.o $P/share/riscv/lib/libc.a
qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none \
    -kernel hello.elf
```

In the tree, the same with `build/parse`, `build/lower`, `build/backend/genriscv`, the
headers in `libc/riscv/include` and `libc/common/include`, and the runtime in
`build/libc/riscv/`. Add `--yaml` to `lower` to read the TAC.

## Tests

`riscv-tests` (`backend/riscv/test/`): golden assembly, register allocation, peephole and
frame tests; run tests on qemu; interop tests linked with clang-compiled code in both
directions; the libc tests; `long double` against exact results and against clang; and
the shared book suite, every program also compiled by clang and the outputs compared.
Run tests skip themselves when the tools are missing.
