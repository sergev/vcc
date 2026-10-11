#!/bin/sh
# Code size of hosted RV64 objects (riscv64-linux): the text bytes of each C file compiled
# by the in-tree passes (genriscv as is, and with no peephole pass), against gcc's
# -Os -fno-inline (gcc inlines static helpers otherwise) and -O2.  All are assembled for
# MARCH, default rv64gc, so ours is compressed too; MARCH=rv64imfd counts 4 bytes an
# instruction.  The compiler is riscv64-linux-gnu-gcc, else cc on a RISC-V host.  With no
# files given, bench/msp430/*.c and libc/common/*.c.  Prints the totals, and with -v each
# file too.
# Usage: [MARCH=...] scripts/bench_riscv64.sh [-v] [file.c ...]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build
T=${TMPDIR:-/tmp}/bench_riscv64.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
CC=riscv64-linux-gnu-gcc
command -v $CC >/dev/null || CC=cc
CC="$CC -march=${MARCH:-rv64gc} -mabi=lp64d"
SIZE=riscv64-linux-gnu-size
command -v $SIZE >/dev/null || SIZE=size
INC="-I$R/libc/linux/riscv64/include -I$R/libc/linux/include -I$R/libc/riscv64/include"
INC="$INC -I$R/libc/lp64/include -I$R/libc/common/include"
CFLAGS="-ffreestanding -fno-builtin -nostdinc $INC -w"
text() { $SIZE -A "$1" | awk '$1 ~ /^\.text/ { n += $2 } END { print n + 0 }'; }
verbose=
[ "$1" = -v ] && { verbose=1; shift; }
[ $# -gt 0 ] || set -- "$R"/bench/msp430/*.c "$R"/libc/common/*.c

ours() { # source object [genriscv flags]
    src=$1 obj=$2
    shift 2
    "$B/cpp/cpp" -t riscv64-linux -nostdinc $INC "$src" "$T/x.i"
    "$B/parse" "$T/x.i" "$T/x.ast"
    "$B/lower" -t riscv64-linux "$T/x.ast" "$T/x.tac"
    "$B/backend/genriscv" --linux "$@" "$T/x.tac" "$T/x.s"
    $CC -c -o "$obj" "$T/x.s"
}

n=0 to=0 tn=0 ts=0 t2=0
[ -n "$verbose" ] && printf '%-16s %8s %8s %8s %8s\n' file ours naive "-Os" "-O2"
for f in "$@"; do
    { ours "$f" "$T/o.o" 2>/dev/null && ours "$f" "$T/n.o" --no-peephole 2>/dev/null &&
        $CC -Os -fno-inline $CFLAGS -c -o "$T/cs.o" "$f" 2>/dev/null &&
        $CC -O2 $CFLAGS -c -o "$T/c2.o" "$f" 2>/dev/null; } || { echo "$f: not compiled" >&2; continue; }
    o=$(text "$T/o.o") nv=$(text "$T/n.o") cs=$(text "$T/cs.o") c2=$(text "$T/c2.o")
    [ -n "$verbose" ] && printf '%-16s %8d %8d %8d %8d\n' "$(basename "$f" .c)" "$o" "$nv" "$cs" "$c2"
    n=$((n + 1)) to=$((to + o)) tn=$((tn + nv)) ts=$((ts + cs)) t2=$((t2 + c2))
done
printf '%d files, text bytes:\n' "$n"
printf '  ours                  %8d\n' "$to"
printf '  ours, no peephole     %8d  (%s of ours)\n' "$tn" "$(awk "BEGIN { printf \"%.2fx\", $tn / $to }")"
printf '  gcc -Os -fno-inline   %8d  (ours is %s)\n' "$ts" "$(awk "BEGIN { printf \"%.2fx\", $to / $ts }")"
printf '  gcc -O2               %8d  (ours is %s)\n' "$t2" "$(awk "BEGIN { printf \"%.2fx\", $to / $t2 }")"
