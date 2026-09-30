# Parser Conformance Matrix

This matrix tracks executable evidence for the Honest v1 parsing requirements.
Both implementations are tested through their strict document entry points;
rulebooks are tested separately so declarations-only input cannot accidentally
pass document validation.

| Requirement | C evidence | Python evidence | Cross-implementation evidence |
|---|---|---|---|
| UTF-8 only; no BOM or surrogate scalars | `test_strict_values.c`, `test_lexer.c` | `test_primitive_hardening.py`, `test_parse.py` | boundary corpus and generated Unicode cases |
| Exact ASCII identifier grammar and reserved names | `test_strict_values.c`, `test_parser.c` | `test_primitive_hardening.py` | boundary corpus |
| Integer ranges and finite, grammatical reals | `test_strict_values.c` | `test_primitive_hardening.py` | boundary corpus and generated values |
| String escapes, raw-control rejection, scalar `Char` | `test_strict_values.c`, `test_lexer.c` | `test_primitive_hardening.py` | valid corpus and boundary corpus |
| Nonempty, distinct enum members | `test_parser.c`, `test_strict_values.c` | `test_parse.py` | boundary and generated invalid corpora |
| Topological declarations; no forward or recursive types | `test_conformance.c`, `test_parser.c` | `test_parse.py` | generated invalid corpus |
| Exact record field presence and declaration order | `test_strict_values.c` | `test_record_hardening.py` | boundary and generated corpora |
| Valid, unique variant arms and unambiguous field namespace | `test_strict_values.c` | `test_record_hardening.py` | boundary and generated corpora |
| Homogeneous, semicolon-separated sequences | `test_strict_values.c`, `test_roundtrip.c` | `test_parse.py` | generated corpora |
| Unique type and variable bindings | `test_parser.c`, `test_conformance.c` | `test_parse.py` | boundary and generated invalid corpora |
| No trailing tokens or missing required separators | `test_parser.c` | `test_parse.py` | boundary and mutation checks |
| Document requires data; rulebook forbids data | `test_parser.c`, `test_cli.sh` | `test_parse.py` | generated strict-mode checks |
| Nesting depth of at least 32 | `test_conformance.c` | generated conformance | generated 32/65-depth boundaries |
| Strings of at least 65,535 UTF-8 bytes | generated conformance | generated conformance | generated required boundary |
| Parse/serialise semantic round trip | `test_roundtrip.c` | `test_parse.py` | L1–L3 and generated conformance |
| Unknown/divergent rulebook declarations remain parseable | `test_antifragile.c` | not applicable (no Python registry API) | document declarations remain self-contained |

## Defence in depth

- `make test` runs fixed examples, negative boundaries, and 968 deterministic
  generated acceptance, rejection, cross-parser, and idempotence checks.
- `make sanitize` runs the C and CLI suites under AddressSanitizer and UBSan.
- `make fuzz-random` runs 20,000 deterministic input mutations under those
  sanitizers.
- CI runs a further 25,000 coverage-guided libFuzzer iterations on Linux.
- `make analyze` requires a warning-clean GCC `-fanalyzer` build.

This evidence establishes strong conformance for the documented grammar. It
does not replace ongoing fuzzing or make an absolute claim that defects are
impossible.
