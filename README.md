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

- **[Format Specification](specs/honest-spec.md)** — the formal language specification with complete EBNF grammar
- **[User Guide](docs/user-guide.md)** — tutorial introduction to reading and writing Honest documents
- **[Implementor's Guide](docs/implementors-guide.md)** — how to build a conformant parser
- **[Examples](examples/)** — annotated Honest documents covering all features
- **[NBS Standard Types](lib/nbs-standard.honest-rulebook)** — the canonical NBS type library

## Building

Honest includes a C reference implementation: `libhonest` (a library) and a set of command-line tools.

```bash
make            # build libhonest and CLI tools
make check      # run the test suite
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

/* Serialise back */
char *output = hon_serialise(doc, HON_MODE_DOCUMENT, NULL);

/* Clean up */
hon_doc_free(doc);
```

### Command-Line Tools

```bash
# Parse and validate a document
honest-parse message.honest

# Parse with verbose inspection
honest-parse -v message.honest

# Reformat a document (pretty-print or wire mode)
honest-fmt message.honest
honest-fmt -w message.honest

# Extract a value by name and field path
honest-get message.honest msg payload.content
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
  tools/                        # CLI tools (honest-parse, honest-fmt, honest-get)
  tests/                        # Test suite
```

## Design Principles

- **Self-describing.** Every document carries its own type declarations.
- **Antifragile.** Unknown types extend the receiver's knowledge, never cause errors.
- **Meaning-carrying.** Enumerations, variant records, and semantic type aliases encode intent.
- **Totemic.** Pascal-derived syntax reads as a declaration to any intelligent reader.
- **Honest provenance.** Claims carry epistemic metadata (confidence, provenance) as typed values.

## Status

The format specification and C reference implementation are complete.

| Component | Status |
|-----------|--------|
| Format specification | Complete |
| User guide | Complete |
| Implementor's guide | Complete |
| NBS standard types | Complete |
| Examples | Complete |
| C library (libhonest) | Complete |
| CLI tools | Complete |
| Test suite (224 tests) | Complete |

## License

Honest is a component of the NBS (No Bull Shit) framework. Released under the [MIT License](LICENSE).
