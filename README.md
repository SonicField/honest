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
make test       # run the test suite
make install    # install to /usr/local (or PREFIX=...)
```

### C Library

```c
#include <honest.h>

/* Parse a document */
HonestResult result = honest_parse(input, length);
if (result.diagnostics_count > 0) {
    /* handle warnings/errors */
}

/* Access typed values */
HonestValue *msg = honest_get_var(result.document, "message");

/* Serialise back */
char *output = honest_serialise(result.document, HONEST_MODE_DOCUMENT);

/* Clean up */
honest_free(result.document);
```

### Command-Line Tools

```bash
# Parse and validate a document
honest-parse message.honest

# Pretty-print a document
honest-inspect message.honest

# Emit a document from structured input
honest-emit --type Message --data '...'
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
  tests/                        # Conformance test suite
```

## Design Principles

- **Self-describing.** Every document carries its own type declarations.
- **Antifragile.** Unknown types extend the receiver's knowledge, never cause errors.
- **Meaning-carrying.** Enumerations, variant records, and semantic type aliases encode intent.
- **Totemic.** Pascal-derived syntax reads as a declaration to any intelligent reader.
- **Honest provenance.** Claims carry epistemic metadata (confidence, provenance) as typed values.

## Status

The format specification is complete. The C reference implementation is in progress.

| Component | Status |
|-----------|--------|
| Format specification | Complete |
| User guide | Complete |
| Implementor's guide | Complete |
| NBS standard types | Complete |
| Examples | Complete |
| C library (libhonest) | In progress |
| CLI tools | In progress |
| Conformance test suite | In progress |

## License

Honest is a component of the NBS (No Bull Shit) framework. Open source.
