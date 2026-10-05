#!/bin/sh
#
# console_test.sh — the MMIX runtime on its own, before any compiled code uses it.
# console_test.s prints "hello" through putbyte, copies StdIn through getch, and returns
# the number of bytes copied, or -3 with a command-line argument.  Checks stdout, the
# "[exit N]" report on stderr and the exit status, with crt0.o and crt0-status.o.
#
# Usage: console_test.sh <as> <ld> <mmix> <lib-dir> <src-dir> <work-dir>
#
set -u
AS="$1" LD="$2" SIM="$3" LIB="$4" SRC="$5" WORK="$6"
mkdir -p "$WORK"
cd "$WORK" || exit 1
status=0

"$AS" -x -no-predefined-syms -o console_test.o "$SRC/console_test.s" || exit 1
for crt0 in crt0 crt0-status; do
    "$LD" --defsym __.MMIX.start..text=0x100 -o $crt0.mmo "$LIB/$crt0.o" console_test.o \
        "$LIB/libc.a" || exit 1
done
printf 'abc\nxy\n' > input.txt

# check <name> <expected stdout> <expected status> <mmix arguments...>
check() {
    name="$1" want_out="$2" want_status="$3"
    shift 3
    "$SIM" -q "$@" > $name.out 2> $name.err
    got_status=$?
    want_err="[exit $(printf '%s' "$want_status" | sed 's/^253$/-3/')]"
    if [ "$(cat $name.out)" != "$want_out" ]; then
        echo "$name: stdout"; cat $name.out; echo "expected"; echo "$want_out"; status=1
    fi
    if [ "$(cat $name.err)" != "$want_err" ]; then
        echo "$name: stderr"; cat $name.err; echo "expected $want_err"; status=1
    fi
    if [ $got_status != $want_status ]; then
        echo "$name: status $got_status, expected $want_status"; status=1
    fi
}

check copy "$(printf 'hello\nabc\nxy')" 7 -finput.txt crt0.mmo
check empty hello 0 -f/dev/null crt0.mmo
check status "$(printf 'hello\nabc\nxy\n7')" 7 -finput.txt crt0-status.mmo
check negative -3 253 crt0-status.mmo arg
exit $status
