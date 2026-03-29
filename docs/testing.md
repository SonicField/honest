# Testing Guide

How to run, understand, and extend the Honest test suites.

---

## Quick Start

```bash
make audit    # run everything: C tests, ASAN, Python tests, conformance
```

Or run each verification step individually:

```bash
make check                                    # C tests (637 tests, 7 suites)
make check-asan                               # C tests under AddressSanitizer
python3 -m pytest python/tests/ -v            # Python tests (34 tests)
bash scripts/cross-impl-conformance.sh        # Cross-impl conformance (L1+L2)
bash scripts/cross-impl-conformance-level3.sh # Cross-impl conformance (L3)
```

---

## C Test Suites

All C tests live in `tests/`. Each file is a standalone test program that links against `libhonest.a`.

| Suite | File | Tests | What it covers |
|-------|------|-------|----------------|
| Accessors | `test_accessors.c` | 71 | `hon_get_string`, `hon_get_long`, `hon_get_bool`, `hon_get_real`, `hon_get_char` |
| Antifragile | `test_antifragile.c` | 76 | Unknown types, rulebook divergence, registry extension |
| Conformance | `test_conformance.c` | 215 | Round-trip fidelity, canonical form, edge cases |
| Lexer | `test_lexer.c` | 66 | Token types, string escapes, comments, Unicode |
| Parser | `test_parser.c` | 92 | Type declarations, data values, error recovery |
| Roundtrip | `test_roundtrip.c` | 66 | `parse(serialise(doc)) == doc` for all type combinations |
| Tools | `test_tools.c` | 51 | CLI tool integration (parse, fmt, get, build, extract) |

### Running C Tests

```bash
make check          # build and run all suites
make check-asan     # clean build with AddressSanitizer, run all suites
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

All Python tests live in `python/tests/test_parse.py`.

```bash
python3 -m pytest python/tests/ -v          # all tests, verbose
python3 -m pytest python/tests/ -k "extract" # run only extract tests
```

### Test Categories

| Category | Tests | What it covers |
|----------|-------|----------------|
| Parsing | 18 | Records, enums, aliases, sequences, variants, booleans, strings, numbers |
| Validation | 5 | BOM rejection, forward references, duplicates, invalid enums, control chars |
| Serialisation | 2 | Document-mode and wire-mode round-trip |
| File parsing | 1 | `parse_file()` reads from disk |
| Access patterns | 4 | Dict access, attribute access, `.get()`, auto-unwrapping, nested navigation |
| Extract | 3 | Code fences, plain text, missing documents |
| Conformance | 1 | Trailing semicolon handling matches C implementation |

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

---

## Full Audit

The `make audit` target runs all verification in sequence:

```bash
make audit
```

This executes:
1. `make check` — 637 C tests across 7 suites
2. `make check-asan` — same tests under AddressSanitizer (clean rebuild)
3. `python3 -m pytest python/tests/ -v` — 34 Python tests
4. `bash scripts/cross-impl-conformance.sh` — Level 1+2 conformance
5. `bash scripts/cross-impl-conformance-level3.sh` — Level 3 conformance

If any step fails, the audit stops immediately. A successful run prints `Full audit passed.`
