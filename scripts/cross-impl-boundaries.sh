#!/usr/bin/env bash
# Hardened valid/invalid corpus shared by the C and Python implementations.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
c_parse="$project_dir/build/honest-parse"
c_fmt="$project_dir/build/honest-fmt"
python=${PYTHON:-python3}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/honest-cross.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

if [[ ! -x $c_parse || ! -x $c_fmt ]]; then
    printf '%s\n' "ERROR: C tools not built. Run 'make' first." >&2
    exit 1
fi

checks=0
passed=0

pass() {
    checks=$((checks + 1))
    passed=$((passed + 1))
}

fail() {
    checks=$((checks + 1))
    printf 'FAIL: %s\n' "$1" >&2
}

python_parse() {
    "$python" -c \
        'import sys; from honest.parser import parse_file; parse_file(sys.argv[1])' \
        "$1"
}

python_wire() {
    "$python" -c \
        'import sys; from honest.parser import parse_file; from honest.serialise import serialise; print(serialise(parse_file(sys.argv[1]), wire=True), end="")' \
        "$1"
}

for fixture in "$project_dir"/tests/corpus/valid/*.honest; do
    name=$(basename -- "$fixture")
    c_wire="$tmp_dir/$name.c"
    py_wire="$tmp_dir/$name.py"
    c_again="$tmp_dir/$name.c-again"
    py_again="$tmp_dir/$name.py-again"

    if "$c_parse" --quiet "$fixture" >/dev/null 2>&1; then
        pass
    else
        fail "C rejects valid fixture $name"
    fi
    if PYTHONPATH="$project_dir/python" python_parse "$fixture" >/dev/null 2>&1; then
        pass
    else
        fail "Python rejects valid fixture $name"
    fi
    if "$c_fmt" --wire "$fixture" >"$c_wire" 2>/dev/null &&
       PYTHONPATH="$project_dir/python" python_wire "$fixture" >"$py_wire" 2>/dev/null; then
        pass
    else
        fail "wire serialisation fails for $name"
        continue
    fi
    if cmp -s "$c_wire" "$py_wire"; then
        pass
    else
        fail "wire serialisation differs for $name"
    fi
    if "$c_parse" --quiet "$py_wire" >/dev/null 2>&1; then
        pass
    else
        fail "C rejects Python output for $name"
    fi
    if PYTHONPATH="$project_dir/python" python_parse "$c_wire" >/dev/null 2>&1; then
        pass
    else
        fail "Python rejects C output for $name"
    fi
    "$c_fmt" --wire "$c_wire" >"$c_again"
    if cmp -s "$c_wire" "$c_again"; then
        pass
    else
        fail "C formatting is not idempotent for $name"
    fi
    PYTHONPATH="$project_dir/python" python_wire "$py_wire" >"$py_again"
    if cmp -s "$py_wire" "$py_again"; then
        pass
    else
        fail "Python formatting is not idempotent for $name"
    fi
done

invalid_documents=(
    "type E = (A, A); var value : E = A;"
    "type E = (Boolean); var value : E = Boolean;"
    "type R = record Boolean : Integer; end; var value : R = (Boolean : 1);"
    "type E = (A); R = record case Boolean : E of A : (); end; var value : R = (Boolean : A);"
    "type E = (A); R = record case kind : E of A : (Boolean : Integer); end; var value : R = (kind : A; Boolean : 1);"
    "type K = (A); R = record case kind : K of end; var value : R = (kind : A);"
    "type K = (A); R = record kind : Integer; case kind : K of A : (); end; var value : R = (kind : 1; kind : A);"
    "type K = (A); R = record x : Integer; case kind : K of A : (x : Integer); end; var value : R = (x : 1; kind : A; x : 2);"
    "type K = (A); R = record case kind : K of A : (kind : Integer); end; var value : R = (kind : A; kind : 1);"
    "type T = Byte; var value : T = 256;"
    "type T = Word; var value : T = 65536;"
    "type T = Integer; var value : T = 2147483648;"
    "type T = LongInt; var value : T = 9223372036854775808;"
    "type T = Real; var value : T = 1;"
    "type T = Real; var value : T = 1e+;"
    "type T = Real; var value : T = 1e2;"
    "type T = Real; var value : T = 1.0e999;"
    "type T = Char; var value : T = 'ab';"
    "type Café = Integer; var value : Café = 1;"
    "type R = record x : Integer; end; var r : R = (wrong : 7);"
    "type K = (A, B); R = record case kind : K of A : (); B : (); end; var r : R = (wrong : A);"
    "type K = (A, B); R = record case kind : K of A : (); B : (); end; var r : R = (kind : Z);"
    "type R = record case kind : Integer of A : (); end; var r : R = (kind : 1);"
    "type"
)

index=0
for source in "${invalid_documents[@]}"; do
    index=$((index + 1))
    fixture="$tmp_dir/invalid-$index.honest"
    printf '%s\n' "$source" >"$fixture"
    if "$c_parse" --quiet "$fixture" >/dev/null 2>&1; then
        fail "C accepts invalid corpus case $index"
    else
        pass
    fi
    if PYTHONPATH="$project_dir/python" python_parse "$fixture" >/dev/null 2>&1; then
        fail "Python accepts invalid corpus case $index"
    else
        pass
    fi
done

printf '%d/%d cross-implementation boundary checks passed\n' "$passed" "$checks"
[[ $passed -eq $checks ]]
