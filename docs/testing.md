# Testing Guide

How to run, understand, and extend the Honest test suites.

---

## Quick Start

```bash
make test     # C/CLI tests, Python tests, and conformance
make audit    # make test plus sanitizers and static analysis
```

Or run each verification step individually:

```bash
make check                                    # C suites and black-box CLI checks
make sanitize                                 # C/CLI tests under ASan and UBSan
make analyze                                  # GCC static analysis build
python3 -m pytest python/tests/ -v            # Python tests (135 tests)
bash scripts/cross-impl-conformance.sh        # Cross-impl conformance (L1+L2)
bash scripts/cross-impl-conformance-level3.sh # Cross-impl conformance (L3)
bash scripts/cross-impl-boundaries.sh         # Shared valid/invalid boundary corpus
make install-check                            # Staged CLI/library/header smoke test
```

---

## C Test Suites

All C tests live in `tests/`. Each file is a standalone test program that links against `libhonest.a`.

| Suite | File | Tests | What it covers |
|-------|------|-------|----------------|
| Accessors | `test_accessors.c` | 73 | `hon_get_string`, `hon_get_long`, `hon_get_bool`, `hon_get_real`, `hon_get_char` |
| Antifragile | `test_antifragile.c` | 76 | Unknown types, rulebook divergence, registry extension |
| Conformance | `test_conformance.c` | 216 | Round-trip fidelity, canonical form, edge cases |
| Lexer | `test_lexer.c` | 66 | Token types, string escapes, comments, Unicode |
| Parser | `test_parser.c` | 133 | Type declarations, data values, schema validation, error recovery |
| Roundtrip | `test_roundtrip.c` | 66 | `parse(serialise(doc)) == doc` for all type combinations |
| Strict values | `test_strict_values.c` | 233 | Numeric limits, Unicode scalars, UTF-8, records, variants, declarations |
| Tools | `test_tools.c` | 61 | CLI integration for get, build, and extract |

`tests/test_cli.sh` adds black-box contract checks for all five installed
programs, including malformed input, argument boundaries, Unicode construction,
format idempotence, closed pipes, and output failures where `/dev/full` is
available. It currently contains 66 checks.

### Running C Tests

```bash
make check          # build and run all suites
make sanitize       # clean build with AddressSanitizer and UBSan
make check-asan     # AddressSanitizer only
make check-ubsan    # UndefinedBehaviorSanitizer only
make check-leak     # clean build with LeakSanitizer, run all suites
```

`make check-asan` performs a clean build (`make clean`) with `-fsanitize=address` and then runs all tests. Any memory error (use-after-free, buffer overflow, stack overflow) causes immediate failure with a detailed trace.

### Adding a C Test

Tests follow a simple pattern:

```c
/* In tests/test_<suite>.c */
static void test_my_feature(void) {
    const char *src = "type\n  X = Integer;\nvar x : X = 42;\n";
    hon_document *doc = hon_parse(src, strlen(src), NULL);
    ASSERT(doc != NULL, "parse succeeds");

    long val;
    int rc = hon_get_long(doc, "x", "", &val);
    ASSERT(rc == 0, "get_long returns 0");
    ASSERT(val == 42, "correct value");

    hon_doc_free(doc);
}

/* Call from main() at the bottom of the file */
/* test_my_feature(); */
```

---

## Python Tests

Python tests live in `python/tests/` and currently contain 135 cases.

```bash
python3 -m pytest python/tests/ -v          # all tests, verbose
python3 -m pytest python/tests/ -k "extract" # run only extract tests
```

Coverage includes parsing, accessors, serialisation, extraction, identifier
grammar, exact primitive ranges, finite reals, Unicode `Char` escaping, and
fixed/variant record schema enforcement.

### Adding a Python Test

```python
# In python/tests/test_parse.py
def test_my_feature():
    doc = honest.parse("""
    type
      X = Integer;
    var x : X = 42;
    """)
    assert doc["x"] == 42
    assert isinstance(doc["x"], int)
```

---

## Cross-Implementation Conformance

These scripts verify that the C and Python implementations produce identical results.

### Level 1 + Level 2

```bash
bash scripts/cross-impl-conformance.sh
```

Tests all files in `examples/` plus the NBS standard rulebook:
- **Level 1 (mutual parseability):** Each implementation's wire output is parseable by the other.
- **Level 2 (byte-identical wire):** Both implementations produce the exact same wire-mode bytes for every input.

### Level 3

```bash
bash scripts/cross-impl-conformance-level3.sh
```

- **Level 3 (document model equivalence):** Parse both wire outputs through the Python parser, re-serialise, and compare. Confirms the document model is preserved identically.

### What the Levels Mean

| Level | What it proves | Current status |
|-------|---------------|----------------|
| L1 | Both parsers accept valid Honest | 6/6 PASS |
| L2 | Serialisers agree on canonical form | 6/6 PASS |
| L3 | Same abstract document after round-trip | 6/6 PASS |

A new implementation should pass Level 1 first, then work toward Level 2.

### Boundary and rejection parity

```bash
PYTHON=/path/to/venv/bin/python bash scripts/cross-impl-boundaries.sh
```

The committed corpus covers every integer boundary, escaped and supplementary
`Char` values, Unicode strings, fixed records, and enum/Boolean variants. It
also presents the same malformed numeric, identifier, record, variant, and
empty-declaration cases to both parsers. The current corpus performs 54 checks,
including mutual parsing, byte-identical wire output, and idempotence.

---

## Full Audit

The `make audit` target runs all verification in sequence:

```bash
make audit
```

This executes:
1. `make test` — 924 C assertions, 72 CLI checks, 135 Python tests, both
   conformance levels, and the 54-check boundary corpus
2. `make sanitize` — C and CLI tests under AddressSanitizer and UBSan
3. `make analyze` — warning-clean GCC static-analysis build

If any step fails, the audit stops immediately. A successful run prints `Full audit passed.`
