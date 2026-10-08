#!/bin/sh
#
# check_headers.sh — verify a target's standard headers preprocess and parse.
#
# For every <header.h> and <sys/header.h> in the include directories (and once for all of them
# together) build a tiny translation unit that includes it, run it through the
# system C preprocessor, then feed the result to the `parse` front end.  A
# non-zero exit from cpp or parse fails the test, catching syntax errors, bad
# macro expansions and accidental comment terminators in the headers.
#
# Usage: check_headers.sh <cpp> <parse> <work-dir> <include-dir>...
# The include directories are searched in the order given.  Extra preprocessor
# flags (e.g. our own cpp's -t target) may be passed in $CPPFLAGS.
#
set -u

CPP="$1"
PARSE="$2"
WORK="$3"
shift 3
INCDIRS="$*"
INCFLAGS="${CPPFLAGS:-}"
for d in $INCDIRS; do
    INCFLAGS="$INCFLAGS -I$d"
done

mkdir -p "$WORK"
status=0
combined="$WORK/_all.c"
: > "$combined"

for h in $(for d in $INCDIRS; do ls "$d"/*.h; ls "$d"/sys/*.h 2>/dev/null; done); do
    case "$h" in
    */sys/*.h) n="sys/$(basename "$h")" ;;
    *)         n=$(basename "$h") ;;
    esac
    u=$(echo "${n%.h}" | tr / _)
    src="$WORK/use_$u.c"
    pre="$WORK/use_$u.i"
    ast="$WORK/use_$u.ast"

    printf '#include <%s>\nint main(void){return 0;}\n' "$n" > "$src"

    if ! "$CPP" -E -nostdinc $INCFLAGS "$src" > "$pre" 2>"$WORK/cpp.err"; then
        echo "PREPROCESS FAILED: $n"
        cat "$WORK/cpp.err"
        status=1
        continue
    fi
    if ! "$PARSE" "$pre" "$ast" > "$WORK/parse.err" 2>&1; then
        echo "PARSE FAILED: $n"
        cat "$WORK/parse.err"
        status=1
        continue
    fi

    printf '#include <%s>\n' "$n" >> "$combined"
done

# One TU that includes everything at once (catches cross-header clashes).
printf 'int main(void){return 0;}\n' >> "$combined"
if ! "$CPP" -E -nostdinc $INCFLAGS "$combined" > "$WORK/_all.i" 2>"$WORK/cpp.err"; then
    echo "PREPROCESS FAILED: <all headers>"
    cat "$WORK/cpp.err"
    status=1
elif ! "$PARSE" "$WORK/_all.i" "$WORK/_all.ast" > "$WORK/parse.err" 2>&1; then
    echo "PARSE FAILED: <all headers>"
    cat "$WORK/parse.err"
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "All headers preprocess and parse cleanly."
fi
exit "$status"
