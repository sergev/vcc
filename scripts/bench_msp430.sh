#!/bin/sh
# Cycles and code size of the MSP430 benchmarks (bench/msp430/*.c) on mspsim, built
# by the in-tree passes and by msp430-elf-gcc -O2, both linked with our runtime.
# Usage: scripts/bench_msp430.sh [bench.c ...]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build
L=$B/libc/msp430
T=${TMPDIR:-/tmp}/bench_msp430.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
LG=$(msp430-elf-gcc -mcpu=msp430 -print-libgcc-file-name)

link() { # object elf
    msp430-elf-ld --gc-sections -T "$R/libc/msp430/link.ld" -o "$2" "$L/crt0.o" "$1" \
        "$L/libc.a" "$LG"
}
run() { # elf -> "cycles/bytes", or "FAIL" when the program's status is not 0
    c=$(mspsim "$1" 2>&1 | sed -n 's/.*Exit code 0 after \([0-9]*\) cycles.*/\1/p')
    s=$(msp430-elf-size -A "$1" | awk '$1 ~ /^\.text/ { n += $2 } END { print n }')
    echo "${c:-FAIL}/$s"
}

[ $# -gt 0 ] || set -- "$R"/bench/msp430/*.c
printf '%-10s %16s %16s\n' bench "ours cyc/bytes" "gcc cyc/bytes"
for f in "$@"; do
    n=$(basename "$f" .c)
    "$B/cpp/cpp" -t msp430 -nostdinc -I"$R/libc/msp430/include" -I"$R/libc/ip16/include" \
        -I"$R/libc/common/include" "$f" "$T/$n.i"
    "$B/parse" "$T/$n.i" "$T/$n.ast"
    "$B/lower" -t msp430 "$T/$n.ast" "$T/$n.tac"
    "$B/backend/genmsp430" "$T/$n.tac" "$T/$n.s"
    msp430-elf-as -mcpu=msp430 -o "$T/$n.o" "$T/$n.s"
    link "$T/$n.o" "$T/$n.elf"
    msp430-elf-gcc -mcpu=msp430 -O2 -ffunction-sections -fdata-sections -c \
        -o "$T/$n-gcc.o" "$f"
    link "$T/$n-gcc.o" "$T/$n-gcc.elf"
    printf '%-10s %16s %16s\n' "$n" "$(run "$T/$n.elf")" "$(run "$T/$n-gcc.elf")"
done
