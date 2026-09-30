#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
bin_dir=${1:-"$root/build"}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/honest-cli.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

tests=0
passed=0
status=0

fail() {
    printf 'FAIL: %s\n' "$1" >&2
}

expect_status() {
    local expected=$1
    local description=$2
    shift 2
    tests=$((tests + 1))
    set +e
    "$@" >"$tmp_dir/stdout" 2>"$tmp_dir/stderr"
    status=$?
    set -e
    if [[ $status -eq $expected ]]; then
        passed=$((passed + 1))
    else
        fail "$description (expected status $expected, got $status)"
    fi
}

expect_empty_stdout() {
    local description=$1
    tests=$((tests + 1))
    if [[ ! -s "$tmp_dir/stdout" ]]; then
        passed=$((passed + 1))
    else
        fail "$description (stdout was not empty)"
    fi
}

expect_stdout() {
    local expected=$1
    local description=$2
    tests=$((tests + 1))
    if [[ $(<"$tmp_dir/stdout") == "$expected" ]]; then
        passed=$((passed + 1))
    else
        fail "$description (unexpected stdout)"
    fi
}

expect_broken_pipe_status() {
    local description=$1
    shift
    local pipe_status
    tests=$((tests + 1))
    set +e
    "$@" 2>"$tmp_dir/stderr" | head -c 0 >/dev/null
    pipe_status=${PIPESTATUS[0]}
    set -e
    if [[ $pipe_status -eq 1 ]]; then
        passed=$((passed + 1))
    else
        fail "$description (expected status 1, got $pipe_status)"
    fi
}

for tool in honest-parse honest-fmt honest-get honest-build honest-extract; do
    tests=$((tests + 1))
    if [[ -x "$bin_dir/$tool" ]]; then
        passed=$((passed + 1))
    else
        fail "$tool is not executable"
    fi

    expect_status 0 "$tool --help succeeds" "$bin_dir/$tool" --help
    tests=$((tests + 1))
    if [[ -s "$tmp_dir/stdout" && ! -s "$tmp_dir/stderr" ]]; then
        passed=$((passed + 1))
    else
        fail "$tool --help writes only to stdout"
    fi
done

valid="$root/examples/01-simple-record.honest"
malformed="$tmp_dir/malformed.honest"
printf '%s\n' 'type X = Integer; var x : X = nope;' >"$malformed"

expect_status 1 "fmt rejects a partial parse" \
    "$bin_dir/honest-fmt" "$malformed"
expect_empty_stdout "fmt emits no malformed document"

expect_status 1 "extract rejects a partial parse" \
    "$bin_dir/honest-extract" "$malformed"
expect_empty_stdout "extract emits no malformed document"

expect_status 2 "parse rejects multiple input paths" \
    "$bin_dir/honest-parse" "$valid" "$valid"
expect_empty_stdout "parse emits no output for ambiguous input"

expect_status 2 "fmt rejects multiple input paths" \
    "$bin_dir/honest-fmt" "$valid" "$valid"
expect_empty_stdout "fmt emits no output for ambiguous input"

expect_status 2 "extract rejects multiple input paths" \
    "$bin_dir/honest-extract" "$valid" "$valid"
expect_empty_stdout "extract emits no output for ambiguous input"

expect_status 2 "parse rejects quiet plus verbose" \
    "$bin_dir/honest-parse" --quiet --verbose "$valid"
expect_empty_stdout "quiet parse never writes stdout"

expect_status 1 "build rejects an unknown field" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Integer; end;' x=1 surprise=2
expect_empty_stdout "build emits no document with an unknown field"

expect_status 1 "build rejects both declaration sources" \
    "$bin_dir/honest-build" --type R --rulebook "$valid" \
    --typedef 'type R = record x : Integer; end;' x=1
expect_empty_stdout "build emits no document with conflicting sources"

expect_status 1 "build rejects an invalid output variable name" \
    "$bin_dir/honest-build" --type R --var-name bad-name \
    --typedef 'type R = record x : Integer; end;' x=1
expect_empty_stdout "build emits no document with an invalid variable name"

expect_status 1 "build rejects a leading underscore variable name" \
    "$bin_dir/honest-build" --type R --var-name _bad \
    --typedef 'type R = record x : Integer; end;' x=1

expect_status 1 "build rejects a reserved output variable name" \
    "$bin_dir/honest-build" --type R --var-name type \
    --typedef 'type R = record x : Integer; end;' x=1

expect_status 1 "build rejects a malformed field path" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Integer; end;' x..ignored=1
expect_empty_stdout "build emits no document for a malformed path"

deep_path=x
for _ in {1..32}; do
    deep_path="$deep_path.x"
done
expect_status 1 "build rejects paths deeper than 32 components" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Integer; end;' "$deep_path=1"

assignments=()
for _ in {1..257}; do
    assignments+=(x=1)
done
expect_status 1 "build rejects more than 256 assignments" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Integer; end;' "${assignments[@]}"

expect_status 1 "build enforces Byte range" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Byte; end;' x=256

expect_status 1 "build rejects a non-finite Real result" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Real; end;' x=1.0e999
expect_empty_stdout "non-finite Real does not produce a document"

expect_status 1 "build enforces Real literal grammar" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Real; end;' x=1e2
expect_empty_stdout "malformed Real does not produce a document"

expect_status 1 "build requires a decimal point for Real" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record x : Real; end;' x=1
expect_empty_stdout "integer spelling of Real does not produce a document"

bad_utf8=$'s=\xC0\x80'
expect_status 1 "build rejects non-canonical UTF-8" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record s : String; end;' "$bad_utf8"

expect_status 0 "build accepts a Unicode scalar Char" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record c : Char; end;' c=é
cp "$tmp_dir/stdout" "$tmp_dir/unicode-char.honest"
expect_status 0 "get reads a Unicode scalar Char" \
    "$bin_dir/honest-get" "$tmp_dir/unicode-char.honest" result c
expect_stdout '\u00E9' "Unicode Char round-trips as the original scalar"

expect_status 0 "build accepts a supplementary Unicode Char" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record c : Char; end;' c=🚀
cp "$tmp_dir/stdout" "$tmp_dir/emoji-char.honest"
expect_status 0 "get reads a supplementary Unicode Char" \
    "$bin_dir/honest-get" "$tmp_dir/emoji-char.honest" result c
expect_stdout '\u1F680' "supplementary Unicode Char preserves its scalar"

expect_status 1 "build rejects more than one Unicode scalar as Char" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record c : Char; end;' c=ab
expect_empty_stdout "invalid Char does not produce a document"

once="$tmp_dir/once.honest"
twice="$tmp_dir/twice.honest"
"$bin_dir/honest-fmt" --wire "$valid" >"$once"
"$bin_dir/honest-fmt" --wire "$once" >"$twice"
tests=$((tests + 1))
if cmp -s "$once" "$twice"; then
    passed=$((passed + 1))
else
    fail "wire formatting is idempotent"
fi

if [[ -e /dev/full ]]; then
    for tool in honest-parse honest-fmt honest-get honest-build honest-extract; do
        tests=$((tests + 1))
        set +e
        case "$tool" in
            honest-parse)
                "$bin_dir/$tool" "$valid" >/dev/full 2>"$tmp_dir/stderr"
                ;;
            honest-fmt|honest-extract)
                "$bin_dir/$tool" "$valid" >/dev/full 2>"$tmp_dir/stderr"
                ;;
            honest-get)
                "$bin_dir/$tool" "$valid" origin label >/dev/full 2>"$tmp_dir/stderr"
                ;;
            honest-build)
                "$bin_dir/$tool" --type R \
                    --typedef 'type R = record x : Integer; end;' x=1 \
                    >/dev/full 2>"$tmp_dir/stderr"
                ;;
        esac
        status=$?
        set -e
        if [[ $status -ne 0 ]]; then
            passed=$((passed + 1))
        else
            fail "$tool reports an output failure"
        fi
    done
fi

# Force writes beyond a typical pipe buffer so a reader that closes
# immediately reliably exposes SIGPIPE/EPIPE handling.
large_value=$(dd if=/dev/zero bs=131072 count=1 2>/dev/null | tr '\0' x)
large_doc="$tmp_dir/large.honest"
printf "type\n  R = record s : String; end;\nvar result : R = (s : '%s');\n" \
    "$large_value" >"$large_doc"

large_types="$tmp_dir/large-types.honest"
{
    printf 'type\n'
    for ((i = 0; i < 6000; i++)); do
        printf '  T%d = Integer;\n' "$i"
    done
} >"$large_types"

expect_broken_pipe_status "parse maps SIGPIPE to an operational failure" \
    "$bin_dir/honest-parse" --verbose "$large_types"
expect_broken_pipe_status "fmt maps SIGPIPE to an operational failure" \
    "$bin_dir/honest-fmt" "$large_doc"
expect_broken_pipe_status "get maps SIGPIPE to an operational failure" \
    "$bin_dir/honest-get" "$large_doc" result s
expect_broken_pipe_status "build maps SIGPIPE to an operational failure" \
    "$bin_dir/honest-build" --type R \
    --typedef 'type R = record s : String; end;' "s=$large_value"
expect_broken_pipe_status "extract maps SIGPIPE to an operational failure" \
    "$bin_dir/honest-extract" "$large_doc"

printf '%d/%d CLI checks passed\n' "$passed" "$tests"
[[ $passed -eq $tests ]]
