#!/bin/bash
# Cross-implementation conformance test.
#
# For each example file, verifies:
# 1. Both C and Python parsers accept the input
# 2. C wire output can be parsed by the Python parser
# 3. Python wire output can be parsed by the C parser
#
# This proves cross-implementation compatibility even when wire-mode
# formatting differs (e.g. trailing semicolons in sequences).
#
# Exit 0 = all files conformant, 1 = mismatch or error.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

C_PARSE="$PROJECT_DIR/build/honest-parse"
C_FMT="$PROJECT_DIR/build/honest-fmt"
PYTHON="python3"

if [ ! -x "$C_PARSE" ] || [ ! -x "$C_FMT" ]; then
    echo "ERROR: C tools not built. Run 'make' first." >&2
    exit 1
fi

FAILURES=0
TESTED=0

test_file() {
    local f="$1"
    local name
    name="$(basename "$f")"
    local fail=0

    # 1. Both parsers accept the original file
    if ! "$C_PARSE" -q "$f" >/dev/null 2>&1; then
        echo "FAIL: C parser rejects $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    fi

    if ! "$PYTHON" -c "
import sys
sys.path.insert(0, '$PROJECT_DIR/python')
from honest.parser import parse_file
parse_file('$f')
" >/dev/null 2>&1; then
        echo "FAIL: Python parser rejects $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    fi

    # 2. C wire output → Python parser
    c_wire=$("$C_FMT" --wire "$f" 2>/dev/null) || {
        echo "FAIL: C wire serialisation failed for $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    }

    if ! echo "$c_wire" | "$PYTHON" -c "
import sys
sys.path.insert(0, '$PROJECT_DIR/python')
from honest.parser import parse
parse(sys.stdin.read())
" >/dev/null 2>&1; then
        echo "FAIL: Python cannot parse C wire output for $name"
        fail=1
    fi

    # 3. Python wire output → C parser
    py_wire=$("$PYTHON" -c "
import sys
sys.path.insert(0, '$PROJECT_DIR/python')
from honest.parser import parse_file
from honest.serialise import serialise
doc = parse_file('$f')
print(serialise(doc, wire=True), end='')
" 2>/dev/null) || {
        echo "FAIL: Python wire serialisation failed for $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    }

    if ! echo "$py_wire" | "$C_PARSE" -q - >/dev/null 2>&1; then
        echo "FAIL: C parser cannot parse Python wire output for $name"
        fail=1
    fi

    if [ "$fail" -eq 0 ]; then
        echo "PASS: $name"
    else
        FAILURES=$((FAILURES + 1))
    fi
    TESTED=$((TESTED + 1))
}

echo "=== Cross-Implementation Conformance Test ==="
echo ""

# Test all examples
for f in "$PROJECT_DIR"/examples/*.honest; do
    [ -f "$f" ] && test_file "$f"
done

# Test rulebook (declarations only — parse-only, both parsers)
for f in "$PROJECT_DIR"/lib/*.honest-rulebook; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"

    c_ok=true
    "$C_PARSE" -q "$f" >/dev/null 2>&1 || c_ok=false

    py_ok=$("$PYTHON" -c "
import sys
sys.path.insert(0, '$PROJECT_DIR/python')
from honest.parser import parse_file
parse_file('$f')
print('ok')
" 2>/dev/null) || true

    if $c_ok && [ "$py_ok" = "ok" ]; then
        echo "PASS: $name (parse-only)"
    else
        echo "FAIL: $name (C=$c_ok, Py=$py_ok)"
        FAILURES=$((FAILURES + 1))
    fi
    TESTED=$((TESTED + 1))
done

echo ""
echo "$TESTED files tested, $FAILURES failures."
[ "$FAILURES" -eq 0 ]
