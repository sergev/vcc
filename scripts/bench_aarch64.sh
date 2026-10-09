#!/bin/sh
# Code size of hosted AArch64 objects (macOS on Apple silicon, or Linux): the text bytes
# of each C file compiled by the in-tree passes (genaarch64 as is, and with no peephole
# pass), against the system compiler's -Os -fno-inline (clang inlines static helpers
# otherwise) and -O2.  With no files given, bench/msp430/*.c and libc/common/*.c.
# Prints the totals, and with -v each file too.
# Usage: scripts/bench_aarch64.sh [-v] [file.c ...]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build
T=${TMPDIR:-/tmp}/bench_aarch64.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
case $(uname -s) in
Darwin)
    TARGET=aarch64-darwin GEN=--darwin
    INC="-I$R/libc/darwin/include"
    text() { size -m "$1" | awk '/Section \(__TEXT, __text\)/ { print $NF; f = 1 } END { if (!f) print 0 }'; } ;;
*)
    TARGET=aarch64-linux GEN=--linux
    INC="-I$R/libc/linux/aarch64/include -I$R/libc/linux/include"
    text() { size -A "$1" | awk '$1 ~ /^\.text/ { n += $2 } END { print n + 0 }'; } ;;
esac
INC="$INC -I$R/libc/aarch64/include -I$R/libc/lp64/include -I$R/libc/common/include"
CFLAGS="-ffreestanding -fno-builtin -nostdinc $INC -w"
verbose=
[ "$1" = -v ] && { verbose=1; shift; }
[ $# -gt 0 ] || set -- "$R"/bench/msp430/*.c "$R"/libc/common/*.c

ours() { # source object [genaarch64 flags]
    src=$1 obj=$2
    shift 2
    "$B/cpp/cpp" -t $TARGET -nostdinc $INC "$src" "$T/x.i"
    "$B/parse" "$T/x.i" "$T/x.ast"
    "$B/lower" -t $TARGET "$T/x.ast" "$T/x.tac"
    "$B/backend/genaarch64" $GEN "$@" "$T/x.tac" "$T/x.s"
    cc -c -o "$obj" "$T/x.s"
}

n=0 to=0 tn=0 ts=0 t2=0
[ -n "$verbose" ] && printf '%-16s %8s %8s %8s %8s\n' file ours naive "-Os" "-O2"
for f in "$@"; do
    { ours "$f" "$T/o.o" 2>/dev/null && ours "$f" "$T/n.o" --no-peephole 2>/dev/null &&
        cc -Os -fno-inline $CFLAGS -c -o "$T/cs.o" "$f" 2>/dev/null &&
        cc -O2 $CFLAGS -c -o "$T/c2.o" "$f" 2>/dev/null; } || { echo "$f: not compiled" >&2; continue; }
    o=$(text "$T/o.o") nv=$(text "$T/n.o") cs=$(text "$T/cs.o") c2=$(text "$T/c2.o")
    [ -n "$verbose" ] && printf '%-16s %8d %8d %8d %8d\n' "$(basename "$f" .c)" "$o" "$nv" "$cs" "$c2"
    n=$((n + 1)) to=$((to + o)) tn=$((tn + nv)) ts=$((ts + cs)) t2=$((t2 + c2))
done
printf '%d files, text bytes:\n' "$n"
printf '  ours                  %8d\n' "$to"
printf '  ours, no peephole     %8d  (%s of ours)\n' "$tn" "$(awk "BEGIN { printf \"%.2fx\", $tn / $to }")"
printf '  cc -Os -fno-inline    %8d  (ours is %s)\n' "$ts" "$(awk "BEGIN { printf \"%.2fx\", $to / $ts }")"
printf '  cc -O2                %8d  (ours is %s)\n' "$t2" "$(awk "BEGIN { printf \"%.2fx\", $to / $t2 }")"
