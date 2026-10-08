#!/bin/sh
# Code size of wasm32 objects: the bytes of the Code section of each C file compiled by
# the in-tree passes (genwasm as is, and with no rewrites of the finished code), against
# clang -O2 and -Os with the same features.  With no files given, the book programs the
# wasm32-tests leave in build/backend/wasm (run them first).  Prints the totals, and with
# -v each file too.
# Usage: scripts/bench_wasm.sh [-v] [file.c ...]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build
T=${TMPDIR:-/tmp}/bench_wasm.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
CLANG=$(sed -n 's/^WASM32_CLANG:INTERNAL=//p' "$B/CMakeCache.txt")
FEATURES=$(sed -n 's/^WASM32_FEATURES:INTERNAL=//p' "$B/CMakeCache.txt" | tr ';' ' ')
CC="$CLANG --target=wasm32 --no-default-config $FEATURES"
INC="-I$R/libc/wasm32/include -I$R/libc/ilp32/include -I$R/libc/common/include"
CFLAGS="-ffreestanding -fno-builtin -nostdinc $INC -w -Wno-parentheses"
verbose=
[ "$1" = -v ] && { verbose=1; shift; }
[ $# -gt 0 ] || set -- "$B"/backend/wasm/*.clang-clang.c

code() { # object -> bytes of its Code section, 0 when it has none
    wasm-objdump -h "$1" | sed -n 's/^ *Code .*(size=\(0x[0-9a-f]*\)).*/\1/p' |
        { read -r s && printf '%d\n' "$s" || echo 0; }
}
ours() { # source object [genwasm flags]
    src=$1 obj=$2
    shift 2
    "$B/cpp/cpp" -t wasm32 -nostdinc $INC "$src" "$T/x.i"
    "$B/parse" "$T/x.i" "$T/x.ast"
    "$B/lower" -t wasm32 "$T/x.ast" "$T/x.tac"
    "$B/backend/genwasm" "$@" "$T/x.tac" "$T/x.s"
    $CC -c -o "$obj" "$T/x.s"
}

n=0 to=0 tn=0 t2=0 ts=0
[ -n "$verbose" ] && printf '%-48s %8s %8s %8s %8s\n' file ours naive "-O2" "-Os"
for f in "$@"; do
    { ours "$f" "$T/o.o" && ours "$f" "$T/n.o" --no-peephole --no-stackify --no-coalesce &&
        $CC -O2 $CFLAGS -c -o "$T/c2.o" "$f" && $CC -Os $CFLAGS -c -o "$T/cs.o" "$f"; } ||
        { echo "$f: not compiled" >&2; continue; }
    o=$(code "$T/o.o") nv=$(code "$T/n.o") c2=$(code "$T/c2.o") cs=$(code "$T/cs.o")
    [ -n "$verbose" ] && printf '%-48s %8d %8d %8d %8d\n' "$(basename "$f" .c)" "$o" "$nv" "$c2" "$cs"
    n=$((n + 1)) to=$((to + o)) tn=$((tn + nv)) t2=$((t2 + c2)) ts=$((ts + cs))
done
printf '%d files, Code section bytes:\n' "$n"
printf '  ours          %8d\n' "$to"
printf '  ours, naive   %8d  (%s of ours)\n' "$tn" "$(awk "BEGIN { printf \"%.2fx\", $tn / $to }")"
printf '  clang -O2     %8d  (ours is %s)\n' "$t2" "$(awk "BEGIN { printf \"%.2fx\", $to / $t2 }")"
printf '  clang -Os     %8d  (ours is %s)\n' "$ts" "$(awk "BEGIN { printf \"%.2fx\", $to / $ts }")"
