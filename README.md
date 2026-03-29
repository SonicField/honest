# Honest

An antifragile, self-describing interchange format for humans, AI agents, and machines.

Honest is a Pascal-inspired data format where every document carries its own type definitions. You never need external documentation, a schema registry, or prior context to understand what a document contains. It is the native wire format of the [NBS](https://github.com/anthropics/nbs) multi-agent framework.

## Quick Example

```pascal
type
  Confidence = (Certain, High, Moderate, Low, Speculative);
  NaturalText = String;

  Finding = record
    confidence : Confidence;
    summary    : NaturalText;
  end;

  Findings = sequence of Finding;

var findings : Findings = (
  (confidence : High;
   summary    : 'Session tokens are not rotated after privilege escalation');
  (confidence : Moderate;
   summary    : 'Rate limiting assumes single-process deployment');
);
```

Read it top to bottom. The declarations tell you what the types mean. The data uses them. No external schema required.

## Why Honest?

| Property | JSON | Protobuf | Honest |
|----------|------|----------|--------|
| Human readable | Yes | No | Yes |
| Self-describing | Shape only | No (needs .proto) | Full types in every document |
| Discriminated unions | No | Oneof (limited) | Variant records (first-class) |
| Semantic types | No | No | Enumerations, type aliases |
| Unknown fields | Ignored or dropped | Preserved as bytes | Parsed, understood, extended |

## Documentation

See the **[full documentation index](docs/index.md)** for all guides and references.

Key entry points:

- **[User Guide](docs/user-guide.md)** — tutorial introduction to reading and writing Honest documents
- **[CLI Reference](docs/cli-reference.md)** — all 5 command-line tools with options, exit codes, and examples
- **[Python Library](docs/python-library.md)** — API reference, auto-unwrapping, extract, and examples
- **[Format Specification](specs/honest-spec.md)** — the formal language specification with complete EBNF grammar

## Building

Honest includes a C reference implementation (`libhonest` + CLI tools) and a Python library.

```bash
make            # build libhonest and CLI tools
make check      # run C test suite (637 tests)
make check-asan # run tests under AddressSanitizer
make audit      # run all verification: C tests, ASAN, pytest, conformance
pip install python/  # install Python library
```

### C Library

```c
#include "honest.h"

/* Parse a document */
hon_diag_list diags = {0};
hon_document *doc = hon_parse(src, len, &diags);
if (diags.count > 0) {
    /* handle warnings/errors */
}

/* Extract a field value */
char buf[256];
hon_get_string(doc, "result", "name", buf, sizeof(buf));

long score;
hon_get_long(doc, "result", "score", &score);

/* Serialise back */
char *output = hon_serialise(doc, HON_MODE_DOCUMENT, NULL);

/* Clean up */
hon_diag_free(&diags);
hon_doc_free(doc);
```

### Python Library

```python
import honest

# Parse a document
doc = honest.parse(text)
print(doc['result'].summary)    # dict access on doc, attribute access on values

# Access fields
score = doc['result'].score      # auto-unwrapped to int
tags  = list(doc['tags'])        # sequence → iterable of native types

# Serialise
text = honest.serialise(doc)                     # document mode (human-readable)
wire = honest.serialise(doc, wire=True)          # wire mode (minimal, canonical)

# Extract Honest from mixed text (e.g. AI output)
doc = honest.extract(ai_response_text)
```

Install from the project directory:

```bash
pip install python/
```

### Command-Line Tools

```bash
# Parse and validate a document
honest-parse message.honest

# Parse from stdin
cat message.honest | honest-parse -

# Reformat a document (pretty-print or wire mode)
honest-fmt message.honest
honest-fmt -w message.honest

# Extract field values (raw output, one per line)
honest-get message.honest result verdict reasoning
honest-get - result verdict < message.honest

# Build a document from shell variables
honest-build --type SessionMeta --rulebook types.honest \
  session_id="$UUID" handle="$HANDLE" pid=$$

# Build with inline type definition and nested records
honest-build --type Report \
  --typedef "type Report = record title : String; score : Integer; end;" \
  title="My Report" score=42

# Extract Honest document from mixed text (e.g. AI output)
honest-extract ai-response.txt
echo "$AI_OUTPUT" | honest-extract -
```

## Project Structure

```
honest/
  specs/
    honest-spec.md              # Formal specification
    honest-addendum.md          # Design decision rationale
    honest-grammar.ebnf         # Standalone EBNF grammar
  docs/
    user-guide.md               # Tutorial for users
    implementors-guide.md       # Guide for parser authors
  lib/
    nbs-standard.honest-rulebook  # NBS standard type library
  examples/
    01-simple-record.honest     # Basic record types
    02-enumerations-and-aliases.honest  # Enums and type aliases
    03-variant-records.honest   # Discriminated unions
    04-sequences.honest         # Ordered collections
    05-nbs-agent-message.honest # Full NBS agent message
  src/                          # C reference implementation
  tools/                        # CLI tools (honest-parse, honest-fmt, honest-get,
                                #            honest-build, honest-extract)
  tests/                        # C test suite (637 tests, 7 suites)
  python/
    honest/                     # Python library (parse, serialise, extract)
    tests/                      # Python test suite (34 tests)
  scripts/                      # Cross-implementation conformance tests
```

## Design Principles

- **Self-describing.** Every document carries its own type declarations.
- **Antifragile.** Unknown types extend the receiver's knowledge, never cause errors.
- **Meaning-carrying.** Enumerations, variant records, and semantic type aliases encode intent.
- **Totemic.** Pascal-derived syntax reads as a declaration to any intelligent reader.
- **Honest provenance.** Claims carry epistemic metadata (confidence, provenance) as typed values.

## Status

The format specification and both reference implementations are complete.

| Component | Status |
|-----------|--------|
| Format specification | Complete |
| User guide | Complete |
| Implementor's guide | Complete |
| NBS standard types | Complete |
| Examples | Complete |
| C library (libhonest) | Complete — 637 tests, ASAN clean |
| Python library | Complete — 34 tests |
| CLI tools | Complete |
| Cross-impl conformance | Complete — 6/6 byte-identical wire output |

## License

Honest is a component of the NBS (No Bull Shit) framework. Released under the [MIT License](LICENSE).
