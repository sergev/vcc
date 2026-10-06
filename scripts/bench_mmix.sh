#!/bin/sh
# mmix -s counts of the MMIX benchmarks (bench/mmix/*.c): instructions, oops (υ, time)
# and mems (μ, memory accesses), less those of an empty main.  Each is built by the
# in-tree passes and by mmix-knuth-mmixware-gcc -O2, both linked with our runtime, and by
# GCC with newlib (its printf, for the printf benchmarks).
# Usage: scripts/bench_mmix.sh [bench.c ...]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build
L=$B/libc/mmix
T=${TMPDIR:-/tmp}/bench_mmix.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
GCC=mmix-knuth-mmixware-gcc
LG=$($GCC -print-libgcc-file-name)
INC="-I$R/libc/mmix/include -I$R/libc/lp64/include -I$R/libc/common/include"

link() { # object mmo
    mmix-knuth-mmixware-ld --defsym __.MMIX.start..text=0x100 -o "$2" "$L/crt0.o" "$1" \
        "$L/libc.a" "$LG"
}
count() { # mmo -> "instructions oops mems"
    mmix -q -s "$1" 2>/dev/null | sed -n 's/,//g; s/^ *\([0-9]*\) instructions \([0-9]*\) mems \([0-9]*\) oops.*/\1 \3 \2/p'
}
build() { # source name: $T/name.mmo (ours), name-gcc.mmo, name-newlib.mmo
    "$B/cpp/cpp" -t mmix -nostdinc $INC "$1" "$T/$2.i"
    "$B/parse" "$T/$2.i" "$T/$2.ast"
    "$B/lower" -t mmix "$T/$2.ast" "$T/$2.tac"
    "$B/backend/genmmix" "$T/$2.tac" "$T/$2.s"
    mmix-knuth-mmixware-as -x -no-predefined-syms -o "$T/$2.o" "$T/$2.s"
    link "$T/$2.o" "$T/$2.mmo"
    $GCC -O2 -ffreestanding -fno-builtin -nostdinc $INC -c -o "$T/$2-gcc.o" "$1"
    link "$T/$2-gcc.o" "$T/$2-gcc.mmo"
    $GCC -O2 -o "$T/$2-newlib.mmo" "$1"
}
less() { # "a b c" "x y z" -> "a-x/b-y/c-z"
    echo "$1 $2" | awk '{ printf "%d/%d/%d", $1 - $4, $2 - $5, $3 - $6 }'
}

printf 'int main(void) { return 0; }\n' > "$T/empty.c"
build "$T/empty.c" empty
eo=$(count "$T/empty.mmo"); eg=$(count "$T/empty-gcc.mmo"); en=$(count "$T/empty-newlib.mmo")
[ $# -gt 0 ] || set -- "$R"/bench/mmix/*.c
printf '%-10s %26s %26s %26s\n' bench "ours instr/oops/mems" "gcc -O2" "gcc -O2, newlib"
for f in "$@"; do
    n=$(basename "$f" .c)
    build "$f" "$n"
    printf '%-10s %26s %26s %26s\n' "$n" "$(less "$(count "$T/$n.mmo")" "$eo")" \
        "$(less "$(count "$T/$n-gcc.mmo")" "$eg")" "$(less "$(count "$T/$n-newlib.mmo")" "$en")"
done
