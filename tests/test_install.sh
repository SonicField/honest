#!/usr/bin/env bash
# Verify that staged public artifacts work without source-tree include/library paths.

set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/honest-install.XXXXXX")
trap 'rm -rf "$stage"' EXIT

make -C "$root" install DESTDIR="$stage"

bindir="$stage/usr/local/bin"
includedir="$stage/usr/local/include"
libdir="$stage/usr/local/lib"
example="$root/examples/01-simple-record.honest"

for tool in honest-parse honest-fmt honest-get honest-build honest-extract; do
    test -x "$bindir/$tool"
    "$bindir/$tool" --help >/dev/null
done
test -f "$libdir/libhonest.a"
test -f "$includedir/honest.h"

"$bindir/honest-parse" --quiet "$example"
"$bindir/honest-fmt" --wire "$example" >"$stage/formatted.honest"
test "$("$bindir/honest-get" "$example" origin label)" = Origin
"$bindir/honest-build" --type R \
    --typedef 'type R = record n : Integer; end;' n=42 \
    >"$stage/built.honest"
"$bindir/honest-parse" --quiet "$stage/built.honest"
"$bindir/honest-extract" "$example" >"$stage/extracted.honest"
"$bindir/honest-parse" --quiet "$stage/extracted.honest"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$includedir" "$root/tests/install_smoke.c" \
    -L"$libdir" -lhonest -o "$stage/install-smoke"
"$stage/install-smoke"

printf '%s\n' 'staged install checks passed'
