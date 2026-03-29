#!/bin/bash
# Cross-implementation conformance test — Level 3 (document model equivalence).
#
# For each example file, verifies that parsing C wire output and Python wire
# output through the Python parser produces identical document models by
# re-serialising both and comparing the result.
#
# This tests a stronger property than Level 1 (mutual parseability):
# not only can each parser consume the other's output, but the resulting
# document models are semantically equivalent.
#
# Exit 0 = all files conformant, 1 = mismatch or error.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

C_FMT="$PROJECT_DIR/build/honest-fmt"
PYTHON="python3"

if [ ! -x "$C_FMT" ]; then
    echo "ERROR: C tools not built. Run 'make' first." >&2
    exit 1
fi

FAILURES=0
TESTED=0

test_file() {
    local f="$1"
    local name
    name="$(basename "$f")"

    # Get C wire output
    local c_wire
    c_wire=$("$C_FMT" --wire "$f" 2>/dev/null) || {
        echo "FAIL: C wire serialisation failed for $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    }

    # Get Python wire output
    local py_wire
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

    # Parse both wire outputs with Python, re-serialise, compare
    local result
    result=$("$PYTHON" -c "
import sys
sys.path.insert(0, '$PROJECT_DIR/python')
from honest.parser import parse
from honest.serialise import serialise

c_wire = '''$c_wire'''
py_wire = '''$py_wire'''

doc_from_c = parse(c_wire)
doc_from_py = parse(py_wire)

re_c = serialise(doc_from_c, wire=True)
re_py = serialise(doc_from_py, wire=True)

if re_c == re_py:
    print('MATCH')
else:
    print('DIVERGENT')
    print(f'C re-serialised length: {len(re_c)}')
    print(f'Py re-serialised length: {len(re_py)}')
" 2>/dev/null) || {
        echo "FAIL: Level 3 comparison failed for $name"
        FAILURES=$((FAILURES + 1))
        TESTED=$((TESTED + 1))
        return
    }

    local status
    status=$(echo "$result" | head -1)

    if [ "$status" = "MATCH" ]; then
        echo "PASS: $name (Level 3 — document model equivalent)"
    else
        echo "FAIL: $name (Level 3 — document models diverge)"
        echo "$result"
        FAILURES=$((FAILURES + 1))
    fi
    TESTED=$((TESTED + 1))
}

echo "=== Cross-Implementation Conformance Test — Level 3 ==="
echo ""

# Test all examples
for f in "$PROJECT_DIR"/examples/*.honest; do
    [ -f "$f" ] && test_file "$f"
done

# Test rulebook (declarations only — no var section, so wire output is type block only)
for f in "$PROJECT_DIR"/lib/*.honest-rulebook; do
    [ -f "$f" ] && test_file "$f"
done

echo ""
echo "$TESTED files tested, $FAILURES failures."
[ "$FAILURES" -eq 0 ]
