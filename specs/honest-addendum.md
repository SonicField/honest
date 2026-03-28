# Honest Specification Addendum

Goal Document Gap Resolution · v0.1
Author: theologian
Date: 2026-03-28

This document resolves every open question and structural gap identified in the Honest goal document (honest.md §8) and the theologian's initial architectural analysis. For each gap: the problem is stated, a concrete solution with syntax is proposed, and a falsifier is provided that would invalidate the solution.

Success criterion: an engineer reading this addendum alongside the goal document has zero ambiguity about what the Honest syntax permits.

---

## Gap 1: String Subtype Syntax

**Problem.** The goal document asks whether semantic string tags should appear as type qualifiers (`string:uri`) or as distinct named types (`Uri = string`). The choice affects parser complexity, readability, and extensibility.

**Solution: Named types.**

String subtypes are declared as type aliases of a base string type. No new syntax is introduced. The Pascal type alias mechanism handles this natively.

```pascal
type
  Uri        = String;
  Identifier = String;
  NaturalText = String;
  MimeType   = String;
```

In the data section, values of these types are syntactically identical to plain strings — single-quoted literals. The type information is carried by the field declaration, not by the literal.

```pascal
  Endpoint = record
    url  : Uri;
    name : NaturalText;
  end;

var endpoint : Endpoint = (
  url  : 'https://api.example.com/v1';
  name : 'Production API';
);
```

The C library represents all string subtypes as:

```c
typedef struct {
    HonestStringKind kind;  /* enum: String, Uri, Identifier, NaturalText, ... */
    size_t len;
    const char *data;       /* length-prefixed; null-terminated as convenience */
} HonestString;
```

User-defined string subtypes extend the `HonestStringKind` enum dynamically via the type registry. The parser maps the declared type alias to the appropriate kind tag.

**Falsifier.** If the number of semantic string subtypes in real-world NBS usage exceeds ~30, the named-type approach produces declaration bloat that harms the human-readability consumer. Monitor declaration section length relative to data section length. If declarations routinely exceed 50% of total document size due to string subtype proliferation, revisit.

---

## Gap 2: Temporal Scope — as_of Convention vs Mandatory

**Problem.** The goal document includes `as_of : LongInt` in the Claim record. Is this a format-level requirement (every record must carry temporal context) or a domain convention (NBS Claim records carry it; arbitrary user types need not)?

**Solution: Convention, not mandatory.**

`as_of` is a field in the NBS `Claim` record type. It is not a format-level construct. Honest imposes no temporal metadata requirements on arbitrary user-defined types.

The NBS standard library of types (shipped as a default rulebook) defines:

```pascal
type
  Timestamp = LongInt;  { Unix epoch seconds }

  Claim = record
    confidence : Confidence;
    provenance : Provenance;
    as_of      : Timestamp;
    { ... variant part ... }
  end;
```

Users defining their own record types are not required to include `as_of` or any temporal field. The format is domain-agnostic; the NBS rulebook is domain-specific.

**Falsifier.** If a use case requires temporal metadata on every record type (e.g., an audit log format), the convention approach forces manual repetition across all types. If this pattern emerges in practice, a format-level `temporal` annotation could be reconsidered. Current evidence does not support this.

---

## Gap 3: Nesting Depth Limits

**Problem.** What is the practical limit on recursive record nesting for wire efficiency and parser safety?

**Solution: No format-level limit. Implementation-level configurable maximum.**

The Honest format specification imposes no nesting depth limit. Limiting nesting in the format would be an arbitrary constraint that restricts expressiveness without a clear semantic justification.

The C parser implementation enforces a compile-time configurable maximum recursion depth:

```c
#define HONEST_MAX_NESTING_DEPTH 64
```

When the parser exceeds this depth, it emits a diagnostic (not a hard error) and stops descending. The partial parse up to that depth is still returned.

The specification documents this as: "Implementations SHOULD support nesting depths of at least 32. Implementations MAY impose a configurable maximum depth for resource safety. The depth limit is a property of the implementation, not the format."

**Falsifier.** If real-world agent messages require nesting depths exceeding 32, the default is too conservative. Monitor actual nesting depths in NBS traffic. If the 95th percentile exceeds 16, the default should be raised. If it never exceeds 8, the concern is academic.

---

## Gap 4: Comments and Documentation

**Problem.** Should Honest support inline documentation as a first-class syntax element, distinct from code comments?

**Solution: Yes. Pascal-style comments, preserved in document mode, stripped in wire mode.**

Honest supports two comment syntaxes, both from Pascal:

```pascal
{ This is a brace comment }
(* This is a paren-star comment *)
```

Comments may appear:
- Between type declarations
- Between field declarations within a record
- Between variable assignments in the data section
- At the end of a line after a semicolon

Comments MUST NOT appear:
- Inside string literals (they are literal text there)
- Inside enumeration value lists (between comma-separated values)

**Semantic distinction:**

- **Document mode** (the on-disk `.honest` file): comments are preserved. They are part of the document and carry human-directed information.
- **Wire mode** (transmitted between agents): comments are stripped. They are not part of the semantic content. The receiver's parser never sees them.

The C library represents comments as metadata attached to the nearest declaration or data node:

```c
typedef struct {
    const char *text;
    size_t len;
    HonestCommentPosition position;  /* Before, After, EndOfLine */
} HonestComment;
```

When serialising in document mode, comments are emitted. When serialising in wire mode, comments are omitted.

**Falsifier.** If agents need to transmit human-readable annotations through the wire (e.g., a supervisor sending an explanation alongside structured data), stripping comments in wire mode loses information. In that case, annotations should be structured data fields (e.g., `note : NaturalText`), not comments. If this pattern is common, add an `Annotated` wrapper type to the NBS standard library. Do not make comments semantic in wire mode — that conflates documentation with data.

---

## Gap 5: Binary Derivation

**Problem.** Should the specification define a canonical binary representation, or leave this to implementors?

**Solution: Out of scope for v1. Deferred explicitly.**

The Honest v1 specification defines only the text format. A canonical binary derivation may be specified in a future version, but is not required for the initial implementation.

Rationale:
- The three-consumer priority (human, AI, machine) places human and AI readability first. Binary serves only the machine consumer.
- A stable text canonical form must exist before a binary form can be derived from it. Specifying both simultaneously creates a consistency maintenance burden.
- The goal document explicitly lists binary wire efficiency as a non-goal.

The specification includes this forward-looking constraint: "The text format MUST be designed such that a mechanical, information-preserving transformation to a binary representation is possible. No syntax construct may be introduced that would prevent binary derivation."

This means: no syntax that relies on whitespace significance for semantics (indentation-sensitive grammars are excluded), no constructs whose meaning depends on comment content, no features that require the full document text to be preserved verbatim.

**Falsifier.** If NBS agent communication volume reaches a point where text parsing latency is a measurable bottleneck (>5% of message handling time), binary derivation becomes urgent rather than deferred. Measure parse time as a fraction of total message handling time in production.

---

## Gap 6: Rulebook Format and Versioning

**Problem.** How is the receiver's rulebook represented, stored, and versioned?

**Solution: A rulebook is a declarations-only Honest document, versioned by content hash.**

A rulebook file has the extension `.honest-rulebook` and contains only a DECLARATIONS section — no DATA section.

```pascal
{ Rulebook: NBS Standard Types }
{ Hash: sha256:a3f8c9... }

type
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Provenance = (Observed, Inferred, Recalled, Generated);
  AgentRole  = (Supervisor, Gatekeeper, Scribe, Theologian,
                Generalist, Testkeeper);
  { ... }
```

**Versioning.** Rulebooks are versioned by the SHA-256 hash of their canonical text form (comments stripped, whitespace normalised, types sorted alphabetically). This eliminates version-number coordination problems — two rulebooks with identical types have identical hashes regardless of authorship or ordering.

**Loading.** The C library loads rulebooks at initialisation:

```c
HonestRulebook *honest_rulebook_load(const char *path);
HonestRulebook *honest_rulebook_merge(HonestRulebook *base, HonestRulebook *extension);
```

When parsing a document, the parser compares incoming declarations against the loaded rulebook:
- **Match:** use the pre-parsed type from the rulebook (fast path)
- **Divergence:** parse from the wire declarations (slow path), log the divergence, extend the in-memory rulebook

**Rulebook extension.** After parsing a document with unknown declarations, the receiver can persist the extended rulebook:

```c
honest_rulebook_save(rulebook, "updated.honest-rulebook");
```

The new file has a new hash. The old file is not modified.

**Falsifier.** Content-hash versioning assumes type identity is structural (two types with identical definitions are the same type). If Honest needs nominal typing (two structurally identical types declared independently are different types), content-hash versioning produces false matches. The goal document's antifragile posture implies structural typing. If nominal typing is ever required, the versioning scheme must change to include provenance (who declared the type, when).

---

## Gap 7: Forward References in Declarations

**Problem.** Can type A reference type B before B is defined in the declarations section? Pascal allows forward declarations for pointers to records. Honest must decide whether declarations require topological ordering.

**Solution: Topological ordering required. No forward references.**

Types in the DECLARATIONS section MUST be ordered such that every type is defined before it is referenced. A type T1 that has a field of type T2 MUST appear after T2 in the declarations.

```pascal
{ VALID — Confidence defined before Claim references it }
type
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Claim = record
    confidence : Confidence;
  end;

{ INVALID — Claim references Confidence before it is defined }
type
  Claim = record
    confidence : Confidence;
  end;
  Confidence = (Certain, High, Moderate, Low, Speculative);
```

**Rationale.** Topological ordering enables single-pass parsing. The parser maintains a symbol table; every type reference resolves to an already-parsed entry. This is critical for streaming parsers that process declarations as they arrive, and it simplifies the C implementation (no second pass, no fixup phase).

**Mutual recursion.** Honest does not support mutually recursive types. A type cannot reference itself, directly or indirectly. This is a deliberate constraint: interchange formats describe data shapes, not recursive data structures. If a message needs to represent a tree, it should use a flat sequence of nodes with explicit parent references (index-based), not recursive nesting.

```pascal
{ Tree representation via flat sequence + indices }
type
  NodeIndex = Word;
  TreeNode = record
    value  : String;
    parent : NodeIndex;  { 0 = root }
  end;
  Tree = record
    nodes : sequence of TreeNode;
  end;
```

**Falsifier.** If a real NBS use case requires genuinely recursive types (e.g., a nested comment thread where each comment contains replies of the same type), the flat-sequence-with-indices approach becomes awkward and error-prone. If this pattern appears more than twice in the NBS type library, reconsider introducing a `forward` declaration mechanism for records only (not enumerations), at the cost of requiring two-pass parsing.

---

## Gap 8: Collections / Sequences

**Problem.** The goal document does not address arrays, lists, or sets. Agent communication requires variable-length ordered collections.

**Solution: Introduce `sequence of T` as a variable-length ordered collection type.**

**Declaration syntax:**

```pascal
type
  IntList = sequence of Integer;
  Claims  = sequence of Claim;
```

**Data syntax — parenthesised, semicolon-separated:**

```pascal
var numbers : IntList = (1; 2; 3; 4; 5);

var claims : Claims = (
  (confidence : High; provenance : Observed; kind : Assertion;
   content : 'First claim');
  (confidence : Moderate; provenance : Inferred; kind : Assertion;
   content : 'Second claim')
);
```

Semicolons separate elements, consistent with Pascal record field separators. The last element does not require a trailing semicolon (but one is permitted for ease of generation).

**Empty sequence:**

```pascal
var empty : IntList = ();
```

**Nesting.** Sequences of sequences are permitted only when the inner sequence is a named type:

```pascal
{ VALID }
type
  Row    = sequence of Integer;
  Matrix = sequence of Row;

{ INVALID — anonymous nested sequence }
type
  Matrix = sequence of sequence of Integer;
```

This constraint exists because anonymous nested sequences create parsing ambiguity: in `(1; 2; 3; 4)`, it is unclear where inner sequence boundaries fall without explicit delimiters. Named types resolve this — each `Row` is a complete parenthesised group.

**C representation:**

```c
typedef struct {
    HonestType *element_type;
    size_t count;
    HonestValue *elements;  /* array of count elements */
} HonestSequence;
```

**Falsifier.** If real-world usage requires set semantics (uniqueness constraint) or map semantics (key-value pairs), sequences are insufficient. Monitor whether agents routinely construct sequences and then deduplicate or index them. If so, introduce `set of T` and `map of K to V` in a future version. For v1, sequences cover the common case (ordered lists of events, batches of messages).

---

## Gap 9: Optionality Semantics

**Problem.** The goal document does not address missing fields. In interchange, the difference between "field absent," "field present but null," and "field present with value" is semantically significant.

**Solution: All declared fields MUST be present. Optionality is expressed via variant records.**

Honest has no null, no nil, no undefined, no optional qualifier. Every field declared in a record MUST appear in the data section with a value of the declared type. This is the "honest" in Honest — the format does not permit ambiguity about presence.

**Expressing optionality:**

```pascal
type
  OptionalString = record
    case present : Boolean of
      True  : (value : String);
      False : ();
  end;

  Contact = record
    name  : String;
    email : OptionalString;
    phone : OptionalString;
  end;

var contact : Contact = (
  name  : 'Alex Turner';
  email : (present : True; value : 'alex@example.com');
  phone : (present : False);
);
```

**Why no shorthand.** A `?` suffix or `optional T` keyword would be more concise but introduces implicit semantics. The variant record makes the optionality visible in the type declaration and explicit in the data. A human or AI reading the document sees exactly what is present and what is absent, and why.

**The Boolean variant pattern.** The `case present : Boolean of True/False` pattern is the canonical way to express optionality in Honest. It is intentionally verbose. Tooling (CLI tools, editor support) can generate and collapse this pattern, but the wire format is always explicit.

**C representation:**

```c
/* No special optional type — it's just a variant record */
typedef struct {
    bool present;
    union {
        HonestString value;  /* when present == true */
    } data;
} HonestOptionalString;
```

**Falsifier.** If the optionality pattern produces documents where the structural overhead (variant record boilerplate) exceeds the actual data content by more than 3:1, the format has failed the readability consumer. Measure the ratio of optionality boilerplate to payload in representative NBS messages. If it exceeds 3:1, introduce syntactic sugar (`field? : T` expanding to the Boolean variant internally) while preserving the explicit wire representation.

---

## Gap 10: Variant Record End-Boundary Disambiguation

**Problem.** Pascal's variant record syntax has a known ambiguity: where does the variant part end? In Pascal, the variant part must be the last part of the record. But when records are nested, it can be unclear whether a field belongs to the variant case or to the enclosing record.

**Solution: Variant part is always the last part of a record. Each case body is explicitly delimited by parentheses.**

**Rules:**
1. A record may have at most one variant part.
2. The variant part MUST be the last section of the record — no fixed fields after the variant.
3. Each case arm's field list is enclosed in parentheses.
4. The variant part is terminated by `end` (closing the record).

```pascal
type
  { VALID — variant part is last, cases are parenthesised }
  Claim = record
    confidence : Confidence;
    provenance : Provenance;
    as_of      : Timestamp;
    case kind : ClaimKind of
      Assertion  : (content : String);
      Query      : (intent : String; scope : String);
      Correction : (replaces : LongInt; content : String);
      Error      : (code : Word; reason : String);
  end;

  { INVALID — fixed field after variant part }
  BadRecord = record
    case kind : ClaimKind of
      Assertion : (content : String);
    footer : String;  { ERROR: no fixed fields after variant }
  end;
```

**Empty case arms:** A case arm with no fields uses empty parentheses:

```pascal
  case present : Boolean of
    True  : (value : String);
    False : ();
```

**Nested records with variants.** When a record field's type is itself a record with a variant, nesting is unambiguous because each record has its own `end`:

```pascal
type
  Inner = record
    case tag : Boolean of
      True  : (x : Integer);
      False : ();
  end;

  Outer = record
    label : String;
    inner : Inner;      { Inner's variant is self-contained }
    count : Integer;    { This clearly belongs to Outer }
  end;
```

The ambiguity only arises with inline/anonymous variant records, which Honest does not permit. All types are named and declared separately in the DECLARATIONS section.

**Falsifier.** If a use case requires multiple independent variant parts in a single record (e.g., a message with both a payload variant and a routing variant), the single-variant-part rule is insufficient. In that case, factor the record into composition: a record containing two named variant-record fields. If this composition pattern proves too verbose for common cases, reconsider allowing multiple variant parts with explicit `endcase` delimiters. Current evidence does not support this — NBS messages have a single discriminant.

---

## Summary of Decisions

| Gap | Decision | Key Constraint |
|-----|----------|----------------|
| 1. String subtypes | Named type aliases | No new syntax |
| 2. Temporal scope | Convention (as_of in Claim) | Not format-level |
| 3. Nesting depth | No format limit; impl default 64 | Configurable |
| 4. Comments | Pascal-style, preserved in doc, stripped on wire | Not semantic |
| 5. Binary derivation | Deferred to v2 | Must remain derivable |
| 6. Rulebook format | Declarations-only Honest doc, content-hash versioned | Structural typing |
| 7. Forward references | Topological ordering required; no mutual recursion | Single-pass parsing |
| 8. Collections | `sequence of T`; named inner types for nesting | Semicolon-separated |
| 9. Optionality | All fields present; Boolean variant for absence | No null/nil/optional |
| 10. Variant end-boundary | Last part of record; parenthesised case arms; no inline variants | One variant part per record |

## Deferred to v2

- `set of T` and `map of K to V` (contingent on real-world need)
- Syntactic sugar for optionality (`field? : T`)
- Binary wire representation
- Multiple variant parts per record

---

## Type System Summary

For reference, the complete Honest v1 type system:

```
PrimitiveType    = Boolean | Byte | Word | Integer | LongInt | Real | String | Char
EnumerationType  = '(' Identifier { ',' Identifier } ')'
TypeAlias        = Identifier '=' TypeReference
SequenceType     = 'sequence' 'of' TypeReference
RecordType       = 'record' FieldList [ VariantPart ] 'end'
VariantPart      = 'case' Identifier ':' TypeReference 'of' VariantArm { VariantArm }
VariantArm       = Identifier ':' '(' [ FieldList ] ')'
FieldList        = Field { ';' Field } [ ';' ]
Field            = Identifier ':' TypeReference
TypeReference    = Identifier  { must resolve to a previously declared type or primitive }
```

This is illustrative, not a formal EBNF. The formal grammar should be produced as a separate deliverable before implementation begins.
