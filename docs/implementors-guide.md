# Honest Implementor's Guide

How to build a conformant Honest parser and serialiser.

---

## Audience

This guide is for developers building an Honest implementation. It assumes you have read the [Honest Format Specification](../specs/honest-spec.md). The specification defines *what* is correct; this guide explains *how* to achieve it and what to watch out for.

---

## 1. Architecture Overview

A complete Honest implementation has four components:

```
┌──────────────────────────────────────────┐
│              Application                 │
├──────────────────────────────────────────┤
│  Serialiser (document mode / wire mode)  │
├──────────────────────────────────────────┤
│  Type Registry (in-memory + rulebook)    │
├──────────────────────────────────────────┤
│  Parser (lexer → type parser → data      │
│          parser)                         │
└──────────────────────────────────────────┘
```

The parser and serialiser are independent — you can implement one without the other. The type registry is shared between them.

---

## 2. Parsing Strategy

### 2.1 Single-Pass Guarantee

The Honest format is designed for single-pass parsing. Two properties make this possible:

1. **Topological ordering of declarations.** Every type is defined before it is referenced. When the parser encounters a type reference, it is guaranteed to already exist in the symbol table.
2. **Declarations before data.** The entire type registry is populated before any data values are parsed.

Your parser should process the document in one forward pass:

```
Input stream → Lexer → Declaration parser → Data parser → Output AST
                              │                   │
                              ▼                   ▼
                        Type Registry ──────► Type Registry
                        (building)            (reading)
```

Do not build a two-pass parser. The format does not require it, and a two-pass parser hides bugs in your ordering validation.

### 2.2 Lexer

The lexer converts a byte stream into tokens. Honest has a small token set:

| Token type | Examples |
|-----------|----------|
| Keyword | `type`, `record`, `end`, `case`, `of`, `sequence`, `var`, `True`, `False` |
| Identifier | `Confidence`, `my_field`, `x` |
| Integer literal | `42`, `-1`, `0` |
| Real literal | `3.14`, `-0.5e3` |
| String literal | `'hello'`, `'it''s'` |
| Punctuation | `(`, `)`, `;`, `:`, `=`, `,`, `.` |
| Comment | `{ text }`, `(* text *)` |
| End of input | |

**Keyword vs identifier.** Keywords and identifiers have the same lexical form. Distinguish them by checking against the keyword list after lexing. Keywords are case-sensitive: `type` is a keyword, `Type` is an identifier.

**Comments.** In wire mode, discard comments during lexing. In document mode, preserve them as tokens attached to a position in the stream — you will need them for round-trip serialisation.

**String escapes.** Process escape sequences during lexing. The parser should receive the decoded string value, not the raw escape text. This means converting `''` to `'`, `\n` to a newline character, `\uXXXX` to the corresponding Unicode code point, etc.

**Lookahead.** The lexer needs at most two characters of lookahead:
- `(` could be the start of a paren-star comment `(*` or a regular open parenthesis
- `-` could be a negative number prefix or (in future extensions) an operator

### 2.3 Declaration Parser

The declaration parser processes the `type` section and populates the type registry.

```
"type" → loop:
    identifier "=" type_definition ";"

    type_definition =
      | "(" identifier {"," identifier} ")"           → enumeration
      | "record" fields [variant_part] "end"           → record
      | "sequence" "of" type_reference                 → sequence
      | type_reference                                 → alias
```

**Symbol table.** Maintain a map from identifier to type definition. Before adding each new type, verify that its name is not already in the table (duplicate type declarations are an error). When resolving a type reference, check the symbol table first, then the primitive type list.

**Validation during parsing.** Validate as you parse:
- Every type reference must resolve to an existing entry (no forward references).
- Enumeration values within a single enumeration must be unique.
- Field names within a single record must be unique.
- The discriminant type in a variant part must be an enumeration or Boolean.

### 2.4 Data Parser — Type-Directed Parsing

This is the most important architectural decision in the parser. **The data parser is type-directed**: the parser must know the expected type of each value before parsing it.

Why? Because Honest has syntactic ambiguities that can only be resolved by type context:

| Ambiguity | Resolution |
|-----------|------------|
| `()` — empty record or empty sequence? | Check if the expected type is a record or a sequence. |
| `'A'` — Char or one-character String? | Check if the expected type is Char or String. |
| `Green` — which enumeration? | Check the expected type's enumeration values. |
| `(x : 1; y : 2)` — record value or ...? | Always a record value (field names are syntactically distinct). |

The data parser works like this:

```
"var" identifier ":" type_reference "=" value ";"

To parse a value, given expected type T:
  if T is a primitive type:
      parse and return the appropriate literal
  if T is an enumeration:
      parse an identifier, verify it is a valid value of T
  if T is a record:
      parse "(" field_value {";" field_value} ")"
      for each field, recursively parse the value using the field's declared type
  if T is a variant record:
      parse fixed fields, then the discriminant, then the case arm fields
      (the discriminant value determines which fields to expect)
  if T is a sequence:
      parse "(" [value {";" value}] ")"
      for each element, recursively parse using the element type
  if T is an alias:
      resolve to the underlying type and recurse
```

**Variant record parsing.** When parsing a variant record value, you must:
1. Parse all fixed fields first (in declared order).
2. Parse the discriminant field.
3. Look up the discriminant value in the variant part to determine which case arm applies.
4. Parse the case arm's fields (in declared order).

The discriminant value is a runtime value, not a compile-time constant. Your parser reads it, then uses it to select the field list for the remainder of the record.

---

## 3. Type Registry

The type registry is the central data structure shared between the parser and serialiser. It maps type names to type definitions.

### 3.1 Representation

Each type definition should be represented as a tagged union (or equivalent in your language):

```
TypeDef =
  | Primitive { kind: PrimitiveKind }
  | Enumeration { values: [String] }
  | Alias { target: TypeRef }
  | Record { fields: [Field], variant: Option<VariantPart> }
  | Sequence { element: TypeRef }

Field = { name: String, type: TypeRef }
VariantPart = { tag_name: String, tag_type: TypeRef, arms: [VariantArm] }
VariantArm = { label: String, fields: [Field] }
```

### 3.2 Primitive Types

Pre-populate the type registry with the primitive types before parsing any declarations. The primitives are: `Boolean`, `Byte`, `Word`, `Integer`, `LongInt`, `Real`, `Char`, `String`.

### 3.3 Alias Resolution

When the parser encounters a type reference, resolve aliases to their underlying types. For type-directed parsing, you need the concrete type (enumeration, record, or sequence), not the alias name. However, preserve the alias name in the AST for serialisation — a field declared as `Uri` should be serialised with the type name `Uri`, not `String`.

---

## 4. Value Representation

Parsed values should be represented as a tagged union mirroring the type system:

```
Value =
  | BooleanVal { value: bool }
  | IntegerVal { value: int64 }
  | RealVal { value: float64 }
  | StringVal { value: String, kind: StringKind }
  | CharVal { value: Unicode codepoint }
  | EnumVal { type_name: String, value: String, ordinal: int }
  | RecordVal { type_name: String, fields: [(String, Value)], variant: Option<VariantData> }
  | SequenceVal { element_type: String, elements: [Value] }

VariantData = { tag_name: String, tag_value: String, arm_fields: [(String, Value)] }
```

### 4.1 Memory Management

Honest documents have a tree structure with no cycles (recursive types are banned). This makes arena allocation the natural choice in languages without garbage collection:

1. Allocate an arena when you begin parsing a document.
2. All AST nodes, strings, and type registry entries for that document are allocated from the arena.
3. Free the entire arena when the document is no longer needed.

This avoids reference counting, individual frees, and use-after-free bugs. The trade-off: you cannot free individual nodes within a document's lifetime. In practice this is rarely needed — documents are parsed, used, and discarded as a unit.

If you need to extract a sub-value from a document and keep it after the document is freed, copy it to a new arena or to individually managed memory.

---

## 5. Error Handling

### 5.1 Errors as Values

The parser should return a result structure containing both the parsed document (possibly partial) and a list of diagnostics:

```
ParseResult = {
    document: Option<Document>,    -- null only if completely unparseable
    diagnostics: [Diagnostic]
}

Diagnostic = {
    severity: Error | Warning | Info,
    position: { line: int, column: int, offset: int },
    message: String
}
```

Do not use exceptions, panics, or aborts for parse errors. The Honest design philosophy is that parsers reason about problems rather than rejecting input. A partially valid document should produce a partial result plus diagnostics.

### 5.2 Error Categories

| Category | Severity | Example |
|----------|----------|---------|
| Lexical error | Error | Unterminated string literal |
| Structural error | Error | Missing `end` for a record |
| Type error | Error | Field value does not match declared type |
| Ordering violation | Error | Type referenced before declaration |
| Unknown type in rulebook comparison | Info | Document declares a type not in the rulebook |
| Divergent type in rulebook comparison | Warning | Document's type differs from rulebook's |
| Deprecated pattern | Warning | Identifier differs from keyword only in case |

### 5.3 Recovery Strategy

When the parser encounters an error, it should attempt to recover and continue parsing to find additional errors. Common recovery strategies:

- **Skip to next semicolon.** After a malformed field or type declaration, skip tokens until a semicolon is found, then resume parsing the next field or declaration.
- **Skip to next `end`.** After a malformed record body, skip to `end` and resume.
- **Skip to next `var`.** After a malformed variable assignment, skip to the next `var` keyword.

Do not attempt to guess the user's intent. Report what is wrong and move on.

---

## 6. Serialisation

### 6.1 Document Mode

Document mode produces human-readable output:

- Indent nested structures (2 or 4 spaces per level — your choice, but be consistent).
- Place each field on its own line.
- Place each type declaration on its own line (multi-line for records).
- Preserve comments in their original positions.
- Preserve the original declaration order.

### 6.2 Wire Mode

Wire mode produces minimal output:

- Strip all comments.
- Use single spaces between tokens.
- No indentation, no extra whitespace.
- All content on minimal lines (or a single line — both are valid).

### 6.3 Round-Trip Invariant

The specification requires: `parse(serialise(document))` must yield a semantically identical document. This means:

- Field order in records must be preserved.
- Declaration order must be preserved.
- Enumeration value order must be preserved.
- String content must be preserved exactly (including escape sequences — serialise what was parsed, not the raw input form).
- Numeric values must round-trip without precision loss for integers. Floating-point values should round-trip within IEEE 754 double precision.

Test round-tripping extensively. It is the single most effective way to find bugs in both your parser and serialiser.

### 6.4 Canonical Form

For content-hash computation (rulebook versioning), produce canonical form:

1. Strip all comments.
2. Normalise all whitespace to single spaces.
3. No trailing whitespace.
4. Single line feed (LF, not CRLF) as line terminator.
5. No trailing line feed after the last token.
6. For rulebooks: sort type declarations alphabetically by name.

---

## 7. Rulebook Loading

### 7.1 Loading

A rulebook is a file containing only a `type` section (no `var` section). Parse it using the same declaration parser. The result is a pre-populated type registry.

### 7.2 Registry Ownership Model

The registry must own its types independently of any document's lifetime. When a type is added to the registry (from a parsed document or rulebook), the registry must **deep-copy** the type into its own memory.

This is critical: a parsed document's types live in the document's arena. If the document is freed, any pointers into its arena become dangling. The registry cannot hold borrowed pointers — it must own copies.

The deep copy must handle all type kinds recursively:
- Enumeration values (copy the string array)
- Record fields and variant arms (copy field lists, tag type, arm field lists)
- Sequence element types (copy the referenced type)
- Alias targets (copy the target type)
- Primitives are static singletons and need not be copied

After extending the registry, the caller is free to discard the source document.

### 7.3 Registry-Aware Parsing

To detect rulebook divergence during parsing, provide the registry to the parser. The parser compares each incoming type declaration against the registry:

- **Match:** The document's declaration is structurally identical to the registry entry. Use the registry entry (fast path).
- **Divergence:** The document's declaration differs from the registry entry. Parse from the document. Emit an Info-level diagnostic logging the divergence. The document's declaration governs.
- **Unknown:** The document declares a type not in the registry. Parse the declaration normally. Optionally extend the registry.

Both halves of the antifragility invariant must hold: unknown/divergent declarations never produce errors, and divergence is logged as information.

### 7.4 Matching

When parsing a document with a loaded rulebook:

```
for each type declaration in document:
    if name exists in rulebook:
        if structurally identical:
            use rulebook entry (fast path)
        else:
            parse from document, log divergence (slow path)
    else:
        parse from document, optionally extend registry
```

"Structurally identical" means the type definitions produce the same canonical form. Compare canonical text, not AST structures — this avoids subtle comparison bugs.

### 7.5 Content-Hash Versioning

To compute a rulebook's version hash:

1. Produce canonical form of the declarations (sorted alphabetically by type name).
2. Compute SHA-256 of the canonical text as UTF-8 bytes.
3. The hash is the version identifier.

Use a standard SHA-256 implementation. Do not substitute a non-cryptographic hash — the hash is a contract that downstream consumers store, compare, and pin against. A weak hash that is later replaced invalidates every stored reference.

---

## 8. Common Pitfalls

### 8.1 Registry Lifetime and Ownership

The most dangerous bug in a registry implementation: storing borrowed pointers to types from a parsed document. When the document is freed, the registry holds dangling pointers. This is a use-after-free that may not manifest in tests (if you happen to dereference before freeing) but will crash in real-world usage.

Always deep-copy types into the registry's own arena. The registry must be self-contained — its correctness must not depend on any other allocation's lifetime.

### 8.2 Forgetting Type-Directed Parsing

The most common implementor mistake is trying to parse data values without type context. This leads to ambiguity bugs. Always thread the expected type through your value parser.

### 8.3 Alias Chains

A type alias can reference another alias: `A = B`, `B = C`, `C = Integer`. Your alias resolution must handle chains. Set a reasonable limit (e.g., 64 steps) to detect accidental cycles (which should be caught by the no-forward-reference rule, but defense in depth is wise).

### 8.4 Variant Record Field Ordering

When parsing a variant record value, the fields appear in this order:
1. Fixed fields (in declaration order)
2. Discriminant field
3. Case arm fields (in declaration order for that arm)

The discriminant is syntactically just another field — it appears at the position where the variant part is declared in the type. Since the variant part must be last, the discriminant appears after all fixed fields.

### 8.5 Trailing Semicolons

The grammar permits an optional trailing semicolon in field lists and sequence values. Your parser must accept both forms:

```pascal
{ Both valid }
var a : Point = (x : 1; y : 2);
var b : Point = (x : 1; y : 2;);
```

### 8.6 Empty Constructs

Handle all empty cases:

| Construct | Syntax | Notes |
|-----------|--------|-------|
| Empty sequence | `()` | Valid, requires type context to distinguish from empty record |
| Empty record | `()` | Valid (for a record with no fields and no variant) |
| Empty case arm | `False : ()` | Valid, the arm has no fields |
| Empty string | `''` | Valid |

### 8.7 Enumeration Value Scoping

Enumeration values are resolved by the expected type. The identifier `Red` could belong to any enumeration that has a `Red` value. Your parser must look up the expected type's enumeration to resolve the value — do not maintain a global enumeration value namespace.

### 8.8 Unicode and String Handling

- All strings are UTF-8.
- String length in the spec is defined in bytes, not characters.
- Escape sequences (`\uXXXX`) encode Unicode code points, which must be converted to UTF-8 bytes.
- Surrogate code points (U+D800 through U+DFFF) are invalid in Honest strings — reject them.
- The null character (U+0000) is technically valid Unicode but unusual in interchange. Your implementation should decide whether to accept or reject it and document the choice.

---

## 9. Testing Your Implementation

### 9.1 Round-Trip Testing

The highest-value test: generate a document, serialise it, parse the output, serialise again. The two serialised forms must be identical (byte-for-byte in canonical form).

### 9.2 Property-Based Testing

If your language has a property-based testing framework, generate random valid type registries and random conformant values, then test:

- Round-trip: `serialise(parse(text)) == text` (for canonical form)
- Type safety: parsed value conforms to declared type
- Ordering: field order preserved across parse/serialise cycles

### 9.3 Negative Testing

For every grammar rule, test rejection of invalid input:

- Forward references (type used before declared)
- Missing fields in record values
- Wrong discriminant value
- Duplicate type names
- Duplicate field names within a record
- Unterminated strings, comments
- Integer overflow for typed fields (e.g., 300 in a Byte field)

### 9.4 Conformance Test Suite

The Honest project provides a test suite (see `tests/`). Run the full suite against your implementation before claiming conformance.

---

## 10. Performance Considerations

### 10.1 Lexer Performance

The lexer is the hot path. Optimise for the common case:
- Identifiers and keywords: scan alphanumeric runs, then check keyword table.
- String literals: scan to closing quote, handle escapes only when backslash or double-quote is encountered.
- Whitespace: skip in a tight loop.

### 10.2 Type Registry Lookup

The type registry is consulted for every type reference in both declarations and data. Use a hash map, not a linear scan.

### 10.3 Rulebook Acceleration

When a rulebook is loaded and declarations match, skip re-parsing the type definition entirely. Compare the declaration text (or a hash of it) against the rulebook's canonical form for that type. If they match, reuse the pre-parsed structure.

### 10.4 Large Sequences

For sequences with many elements, avoid reallocating on every append. Use a growable array with geometric growth (double the capacity when full). Pre-scan for element count if possible (count semicolons at the current nesting depth).

---

## Summary

Building a conformant Honest parser is straightforward if you follow the format's design:

1. **Single pass.** Lex forward, parse forward, never backtrack.
2. **Type-directed data parsing.** Always know the expected type before parsing a value.
3. **Errors as values.** Return diagnostics, not exceptions.
4. **Round-trip as the primary test.** If it round-trips, it's probably correct.
5. **Arena allocation.** Documents are trees with no cycles. Allocate together, free together.

The format was designed to be easy to implement correctly. If your parser is becoming complex, you may be solving a problem the format does not have.

## Reference Implementations

Two reference implementations are provided:

- **C** (`src/`, `include/honest.h`) — arena-allocated, single-pass, ~2500 lines. Includes typed field accessors (`hon_get_string`, `hon_get_long`, `hon_get_bool`, `hon_get_real`, `hon_get_char`) and 627 tests with AddressSanitizer coverage.

- **Python** (`python/honest/`) — pure Python, ~600 lines. Demonstrates Pythonic API design with auto-unwrapping (leaf values return native types) and attribute-style access. Includes `extract()` for finding Honest documents in mixed text. 32 pytest tests.

Both implementations accept the same inputs and produce the same outputs. Use the C test suite's inputs as the conformance baseline for any new implementation.
