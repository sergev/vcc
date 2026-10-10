# CLAUDE.md

Guidance for Claude Code in this repository. Details live in the linked docs; read the
relevant one before working on a component.

## Project

`vcc`: a C11 compiler, one frontend and nine backends.

| Target | Generator | Doc |
|---|---|---|
| x86-64 (bare metal, `--linux`) | `backend/x86/` `genx86` | [docs/X86_64_Backend.md](docs/X86_64_Backend.md) |
| AArch64 (bare metal, `--linux`, `--darwin`) | `backend/aarch64/` `genaarch64` | [docs/Aarch64_Backend.md](docs/Aarch64_Backend.md) |
| ARM32 ARMv7-A | `backend/arm32/` `genarm32` | [docs/Arm32_Backend.md](docs/Arm32_Backend.md) |
| RISC-V RV64 / RV32 (`--rv32`) | `backend/riscv/` `genriscv` | [docs/Riscv_Backend.md](docs/Riscv_Backend.md) |
| AVR ATmega1280 | `backend/avr/` `genavr` | [docs/Avr_Backend.md](docs/Avr_Backend.md) |
| MSP430 | `backend/msp430/` `genmsp430` | [docs/Msp430_Backend.md](docs/Msp430_Backend.md) |
| MMIX | `backend/mmix/` `genmmix` | [docs/Mmix_Backend.md](docs/Mmix_Backend.md) |
| wasm32, wasm32-braam | `backend/wasm/` `genwasm` | [docs/Wasm_Backend.md](docs/Wasm_Backend.md), [docs/Braam.md](docs/Braam.md) |
| BESM-6 (Unix `b6as`, Madlen, Bemsh dialects) | `backend/besm6/` `genbesm` | [backend/besm6/](backend/besm6/) `*.md` |

Hosted targets `x86_64-linux`, `aarch64-linux`, `aarch64-darwin` reuse those generators
and link with the system `cc` (headers in `libc/linux/`, `libc/darwin/`). `vcc`'s default
target is the host (`HOST_TARGET` in `cc/cc.c`); `cpp` and `lower` default to `riscv64`.

Pipeline: `vcc` (`cc/`) → `cpp` (`cpp/`) → `parse` (`scanner/`, `parser/`, AST in `ast/`)
→ `lower` (`semantic/`, `translator/`, `optimize/`, TAC in `tac/`) → `gen*` (`backend/`)
→ assembler and linker. Shared backend code: `backend/common/` (CFG, liveness,
register allocation). Runtime and headers: `libc/<target>/`, shared ones in
`libc/common/`, data-model ones in `libc/lp64/`, `libc/ilp32/`, `libc/ip16/`.

## Build & test

```sh
make              # compiler + runtime into ./build (no tests)
make test         # also build all *-tests
make run          # make test, then ctest
make install      # install to ~/.local (v-prefixed binaries, share/vcc/<target>/)
make self         # self-host: stage 1 in build/stage, stage 2 in build/self (rm -rf build/self first)
make self-test    # make self, then the tests against stage 2
make debug / make clean
```

- Run tests with `ctest -j8` in `build/` (faster than the binaries). Single binary:
  `./build/backend/riscv/riscv-tests --gtest_filter='*Foo*'`; each binary `chdir`s to its
  build directory itself.
- Test executables are named `*-tests`, excluded from `all`, collected by target `tests`.
- Tests are GoogleTest (C++17), next to the module: `<module>/test/`. Book tests
  (`chapter*_tests.cpp`, shared run programs in `backend/common/test/book/`) are compiled
  into the same binaries. Coroutine and `alloca` run programs are shared in
  `backend/common/test/coro/` and `backend/common/test/alloca/`.
- `fatal_error()` is defined once per test binary, in a regular unit-test source, never in
  a chapter source.
- Runs need the external tools (qemu, mspsim, mmix, node, dubna, b6sim…); tests skip
  without them. Cross tools are found by `scripts/CrossTools.cmake`.
- Benchmarks: `scripts/bench_{msp430,mmix,wasm,aarch64}.sh`.

Try the passes by hand:
```sh
./build/parse in.c [-|--yaml|--dot]          # → in.ast (not stdout unless "-")
./build/lower -t <target> in.ast [-|--yaml]  # → in.tac
./build/backend/genbesm [--madlen|--bemsh] in.tac
./build/cc/cc -t <target> -S in.c
```

## Rules

- A shared-code change must keep every backend green and must not change BESM-6 output
  except to fix a bug.
- `-Wall -Werror -Wshadow`; C is C11. `cppcheck`, when installed, runs in the build and
  fails it on any finding.
- Sources must stay compilable by `vcc` itself: no GCC builtins outside `#ifdef __GNUC__`,
  no local named like a libc function.
- All allocation through `xalloc`/`xfree`; IR serialization through `wio`.
- `.asdl` files (`ast/ast.asdl`, `tac/tacky.asdl`, `backend/besm6/besm6.asdl`) are specs;
  keep the hand-written headers in sync.
- No identifier shadowing — a permanent design decision; don't add support for it.
- TAC names: `%`+digit is a temporary, `%`+letter a parameter or local, anything else a
  global. TAC is typed (`params`/`locals` carry types); `tac_verify` checks it.
- Anonymous struct tags (`__anon_N`) are minted by the parser, not in `semantic/`.
- Initializers are normalized only by `semantic/init_normalize.c`; static initializers
  bypass typecheck (`build_static_init`).
- Bit-fields are lowered in the translator; backends never see them.
- `long double` constants are binary128 (`libutil/float128.c`) on every host.
- Loop optimizations, shared cleanup and condition jumps are off for BESM-6.

## Documentation

- [README.md](README.md) — overview, getting started
- [docs/Technical_Reference.md](docs/Technical_Reference.md) — repo layout, components, build, TAC YAML format
- [cc/README.md](cc/README.md) — the driver; [cpp/README.md](cpp/README.md) — the preprocessor
- [docs/TAC_Optimization.md](docs/TAC_Optimization.md) — the optimizer passes
- [docs/Type_Coercion.md](docs/Type_Coercion.md), [docs/Type_Sizes_Alignment.md](docs/Type_Sizes_Alignment.md) — types per target
- [docs/Standard_Include_Files.md](docs/Standard_Include_Files.md), [libc/besm6/include/README.md](libc/besm6/include/README.md) — headers
- [docs/Coroutines_in_C.md](docs/Coroutines_in_C.md) (tutorial, frame ABI), [docs/Coroutines_Internals.md](docs/Coroutines_Internals.md) — `defer`, coroutines, `alloca`
- [docs/Braam.md](docs/Braam.md), [docs/Braam_Example.md](docs/Braam_Example.md) — the `wasm32-braam` target
- [docs/Memory_Allocation.md](docs/Memory_Allocation.md), [docs/String_Map.md](docs/String_Map.md), [docs/Word_Oriented_IO.md](docs/Word_Oriented_IO.md) — `libutil`
- [docs/C_Grammar.md](docs/C_Grammar.md), [grammar/README.md](grammar/README.md) — grammar
- [docs/Tests_From_The_Book.md](docs/Tests_From_The_Book.md) — the book tests
- [docs/Howto_build_MSP430_GCC.md](docs/Howto_build_MSP430_GCC.md), [docs/Howto_build_MMIXware.md](docs/Howto_build_MMIXware.md) — reference toolchains
- BESM-6: [Data representation](backend/besm6/Besm6_Data_Representation.md),
  [calling conventions](backend/besm6/Besm6_Calling_Conventions.md),
  [instruction set](backend/besm6/Besm6_Instruction_Set.md),
  [runtime library](backend/besm6/Besm6_Runtime_Library.md),
  [intrinsics](backend/besm6/Besm6_Intrinsics.md),
  [peephole](backend/besm6/Peephole_Rewrites.md),
  [KOI-7](backend/besm6/KOI7_Encoding.md),
  [Madlen](backend/besm6/Madlen.md), [Bemsh](backend/besm6/Bemsh.md),
  [b6as](backend/besm6/Besm6_Unix_Assembler.md),
  [frexp/ldexp](backend/besm6/Frexp_Ldexp.md), [TODO](backend/besm6/TODO.md)
