# The Honest Format Specification

Version 1.0 Draft
Date: 2026-03-28

---

## 1. Introduction

Honest is a self-describing, declarative interchange format. It is designed for three equal consumers: humans reading without tooling, AI agents interpreting without prior context, and machine parsers deserialising efficiently. It is the native wire format of the NBS multi-agent framework.

### 1.1 Design Philosophy

Honest occupies a design space between JSON (shape without meaning) and IDL systems like Protobuf and CORBA (meaning without tolerance). Its defining properties are:

**Self-describing.** Every Honest document carries its own type declarations. No external schema registry, documentation, or shared context is required to interpret a document. The declarations section IS the schema.

**Antifragile.** Unknown declarations extend the receiver's knowledge rather than causing errors. A receiver encountering a type it has never seen before gains a new type, not a parse failure. Schema divergence between sender and receiver is logged as information, never raised as an error.

**Meaning-carrying.** The type system encodes intent. Discriminated unions (variant records), enumerated types, and semantic string subtypes carry meaning that would otherwise require out-of-band documentation. A field of type `Confidence` is self-explanatory in a way that a field of type `int` is not.

**Totemic.** The syntax is grounded in Pascal. It reads as a declaration, not an instruction. An intelligent reader — human or AI — encountering Honest for the first time can understand a document without a tutorial.

**Honest provenance.** Every claim can carry its epistemic status (confidence, provenance) as typed values, not optional annotations. The format does not permit ambiguity about what is known versus what is guessed.

### 1.2 Notational Conventions

This specification uses the following conventions:

- EBNF grammar rules use `=` for definition, `|` for alternation, `{ }` for zero-or-more repetition, `[ ]` for optional elements, `( )` for grouping, and quoted strings for terminals.
- The key words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are used per RFC 2119.
- Examples are illustrative. Where an example conflicts with the grammar, the grammar governs.

---

## 2. Lexical Structure

### 2.1 Character Set

An Honest document is a sequence of Unicode characters encoded in UTF-8. Implementations MUST accept UTF-8 input. Implementations MUST produce UTF-8 output. Invalid UTF-8 anywhere in the byte stream, including within comments or between tokens, is a parse error. A byte order mark (BOM, U+FEFF) MUST NOT be present at the start of an Honest document.

### 2.2 Whitespace

Whitespace characters are: space (U+0020), horizontal tab (U+0009), carriage return (U+000D), and line feed (U+000A). Whitespace is not significant — it serves only to separate tokens and improve readability. Implementations MUST accept any amount of whitespace between tokens. Implementations SHOULD produce human-readable output with conventional indentation.

### 2.3 Comments

Honest supports two comment syntaxes:

```ebnf
Comment       = BraceComment | ParenStarComment .
BraceComment  = "{" { CommentChar } "}" .
ParenStarComment = "(*" { CommentChar | "*" } "*)" .
CommentChar   = (* any character except "}" for BraceComment,
                   or "*)" for ParenStarComment *) .
```

Comments may appear between any two tokens except within string literals.

Comments MUST NOT nest. A `{` inside a brace comment is literal, not a nested comment opener.

**Document mode vs wire mode.** In document mode (on-disk files, human-readable output), comments are preserved and associated with the nearest declaration or data node. In wire mode (agent-to-agent transmission), comments are stripped. Comments MUST NOT carry semantic information required for correct interpretation. Use typed fields for data that must survive wire transmission.

### 2.4 Identifiers

```ebnf
Identifier = Letter { Letter | Digit | "_" } .
Letter     = "A".."Z" | "a".."z" .
Digit      = "0".."9" .
```

Identifiers are case-sensitive. `Confidence` and `confidence` are distinct identifiers.

Identifiers MUST begin with a letter. The underscore character is permitted after the first character.

### 2.5 Keywords

The following identifiers are reserved keywords and MUST NOT be used as type names, field names, or enumeration values:

```
type  record  end  case  of  sequence  var  Boolean  True  False
```

Keywords are case-sensitive. `Type`, `TYPE`, and `type` are distinct; only `type` is reserved. Implementations SHOULD warn when an identifier differs from a keyword only in case.

### 2.6 Numeric Literals

```ebnf
IntegerLiteral = [ "-" ] Digit { Digit } .
RealLiteral    = [ "-" ] Digit { Digit } "." Digit { Digit } [ Exponent ] .
Exponent       = ( "e" | "E" ) [ "+" | "-" ] Digit { Digit } .
```

Integer literals represent whole numbers. The complete token MUST convert
without overflow, and the result MUST fit the declared type's range (see §3.1).

Real literals represent floating-point numbers. They MUST contain a decimal
point with at least one digit on each side. The complete token MUST convert to
a finite IEEE 754 double; infinities and NaNs are not literals in this grammar.

### 2.7 String Literals

```ebnf
StringLiteral = "'" { StringChar } "'" .
StringChar    = (* any character except "'" *) | "''" .
```

Strings are delimited by single quotes. A literal single quote within a string is represented by two consecutive single quotes (`''`).

String literals MUST NOT contain raw control characters (U+0000 through U+001F) except horizontal tab (U+0009). Line breaks within string literals are represented by their escape sequences.

**Escape sequences:**

| Sequence | Meaning |
|----------|---------|
| `''` | Literal single quote |
| `\n` | Line feed (U+000A) |
| `\r` | Carriage return (U+000D) |
| `\t` | Horizontal tab (U+0009) |
| `\\` | Literal backslash |
| `\uXXXX` | Unicode code point (4 hex digits) |
| `\UXXXXXXXX` | Unicode code point (8 hex digits) |

### 2.8 Boolean Literals

```ebnf
BooleanLiteral = "True" | "False" .
```

Boolean literals are capitalised, consistent with Pascal convention.

---

## 3. Type System

Honest has a fixed set of primitive types and four composite type constructors: enumerations, type aliases, records (with optional variant parts), and sequences.

### 3.1 Primitive Types

| Type | Meaning | Range |
|------|---------|-------|
| `Boolean` | Truth value | `True`, `False` |
| `Byte` | Unsigned 8-bit integer | 0..255 |
| `Word` | Unsigned 16-bit integer | 0..65535 |
| `Integer` | Signed 32-bit integer | -2147483648..2147483647 |
| `LongInt` | Signed 64-bit integer | -2^63..2^63-1 |
| `Real` | IEEE 754 double-precision float | per IEEE 754 |
| `Char` | Single Unicode code point | any valid Unicode scalar value |
| `String` | Variable-length Unicode text | implementation-defined max length |

Implementations MUST support `String` values of at least 65535 bytes (UTF-8 encoded). Implementations SHOULD support arbitrarily long strings.

### 3.2 Enumeration Types

```ebnf
EnumerationType = "(" Identifier { "," Identifier } ")" .
```

An enumeration defines an ordered set of named values. Each value is distinct. The order of values is significant — it defines the ordinal position (first value is ordinal 0).

```pascal
type
  Confidence = (Certain, High, Moderate, Low, Speculative);
```

Enumeration values are scoped to their type. The same identifier MAY appear in multiple enumerations if the usage context is unambiguous (i.e., the field's declared type resolves the enumeration). Implementations SHOULD warn on ambiguous enumeration values.

An enumeration MUST have at least one value.

### 3.3 Type Aliases

```ebnf
TypeAlias = Identifier "=" TypeReference .
```

A type alias introduces a new name for an existing type. The alias is a distinct type — not interchangeable with the original in the type system, but structurally identical for serialisation.

```pascal
type
  Uri         = String;
  Identifier  = String;
  NaturalText = String;
  Timestamp   = LongInt;
```

Type aliases are the mechanism for semantic string subtypes. A field of type `Uri` is serialised identically to a field of type `String`, but the type name carries semantic information for human and AI consumers.

### 3.4 Record Types

```ebnf
RecordType  = "record" FieldList [ VariantPart ] "end" .
FieldList   = Field { ";" Field } [ ";" ] .
Field       = Identifier ":" TypeReference .
```

A record is an ordered collection of named, typed fields. Field order is significant — it defines serialisation order and is preserved across parse/serialise cycles.

```pascal
type
  Message = record
    id      : LongInt;
    sender  : AgentRole;
    payload : Claim;
  end;
```

**Field presence.** Every field declared in a record MUST be present exactly
once in the data section, in declaration order, with a value of the declared
type. Undeclared fields are errors. There is no null, nil, optional, or default
value mechanism. Optionality is expressed via variant records (see §3.5).

**Empty records.** A record with no fields and no variant part is permitted but unusual:

```pascal
type
  Unit = record
  end;
```

### 3.5 Variant Records (Discriminated Unions)

```ebnf
VariantPart = "case" Identifier ":" TypeReference "of" VariantArm { VariantArm } .
VariantArm  = Identifier ":" "(" [ FieldList ] ")" ";" .
```

A variant part introduces a discriminated union within a record. The discriminant field (the `case` identifier) is a typed tag whose value determines which fields are present.

```pascal
type
  ClaimKind = (Assertion, Query, Correction, Error);

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
```

**Constraints:**

1. A record MUST have at most one variant part.
2. The variant part MUST be the last section of the record. No fixed fields may follow the variant part.
3. Each case arm is delimited by parentheses. An empty case arm uses empty parentheses: `()`.
4. The discriminant type MUST be an enumeration type or `Boolean`.
5. Every value of the discriminant type SHOULD have a corresponding case arm. Implementations MUST warn if a discriminant value has no case arm.
6. Every arm label MUST be a value of the discriminant type and MUST occur at
   most once. Field names within an arm MUST be unique.

**Optionality pattern.** The canonical way to express an optional field is a Boolean variant:

```pascal
type
  OptionalString = record
    case present : Boolean of
      True  : (value : String);
      False : ();
  end;
```

### 3.6 Sequence Types

```ebnf
SequenceType = "sequence" "of" TypeReference .
```

A sequence is a variable-length ordered collection of elements of a single type.

```pascal
type
  IntList = sequence of Integer;
  Claims  = sequence of Claim;
```

**Nesting.** Sequences of sequences are permitted only when the inner sequence is a named type:

```pascal
{ VALID }
type
  Row    = sequence of Integer;
  Matrix = sequence of Row;

{ INVALID — direct nesting without named intermediate type }
type
  Matrix = sequence of sequence of Integer;
```

This constraint prevents parsing ambiguity in the data section.

### 3.7 Type References and Ordering

```ebnf
TypeReference = Identifier .
```

A type reference is an identifier that resolves to a previously declared type or a primitive type.

**Topological ordering.** Types in the DECLARATIONS section MUST be ordered such that every type is fully defined before it is referenced. A type `T1` that has a field of type `T2` MUST appear after `T2` in the declarations.

**No forward references.** An identifier used as a type reference MUST resolve to either a primitive type (§3.1) or a type declared earlier in the same declarations section. References to types not yet declared are a parse error.

**No recursive types.** A type MUST NOT reference itself, directly or indirectly. The type dependency graph MUST be a directed acyclic graph (DAG). This constraint guarantees finite document size and enables single-pass parsing.

---

## 4. Document Structure

### 4.1 Overall Structure

```ebnf
Document         = DeclarationsSection DataSection .
DeclarationsSection = TypeDeclarations .
DataSection      = VariableAssignment { VariableAssignment } .
```

An Honest document consists of exactly two sections, in order:

1. **DECLARATIONS** — all type definitions required to interpret the data section.
2. **DATA** — one or more typed variable assignments.

Both sections MUST be present. A document without declarations is not a valid Honest document. A document without data is not a valid Honest document (but see §6 for rulebook files, which contain only declarations).

### 4.2 Declarations Section

```ebnf
TypeDeclarations = "type" TypeDeclaration { TypeDeclaration } .
TypeDeclaration  = Identifier "=" TypeDefinition ";" .
TypeDefinition   = EnumerationType | RecordType | SequenceType | TypeAlias .
```

The declarations section begins with the keyword `type` and contains one or more type declarations. Each declaration binds an identifier to a type definition.

```pascal
type
  Confidence = (Certain, High, Moderate, Low, Speculative);

  Claim = record
    confidence : Confidence;
    content    : String;
  end;
```

**Self-description invariant.** Every type referenced in the data section MUST be declared in the declarations section or be a primitive type. A document that references a type defined only in an external rulebook (not in the document's own declarations) violates the self-description invariant and is non-conformant.

**Redundancy with rulebook.** A document MAY declare types that the receiver already has in its rulebook. This is expected and correct — the declarations section is the ground truth. Where a document's declaration matches the rulebook, the parser MAY use the pre-parsed rulebook entry for performance. Where they diverge, the document's declaration governs.

### 4.3 Data Section

```ebnf
VariableAssignment = "var" Identifier ":" TypeReference "=" Value ";" .
```

The data section contains one or more variable assignments. Each assignment binds an identifier to a typed value.

```pascal
var message : Message = (
  id      : 4721;
  sender  : Theologian;
  payload : (
    confidence : High;
    provenance : Inferred;
    kind       : Assertion;
    content    : 'The compilation target is ARM Thumb2';
  );
);
```

### 4.4 Values

```ebnf
Value         = PrimitiveValue | EnumValue | RecordValue | SequenceValue .
PrimitiveValue = IntegerLiteral | RealLiteral | StringLiteral | BooleanLiteral | CharLiteral .
CharLiteral   = "'" StringChar "'" .
EnumValue     = Identifier .
RecordValue   = "(" FieldValue { ";" FieldValue } [ ";" ] ")" .
FieldValue    = Identifier ":" Value .
SequenceValue = "(" [ Value { ";" Value } [ ";" ] ] ")" .
```

**Record values.** Fields within a record value MUST appear in the same order as declared in the type definition. All declared fields MUST be present. For variant records, the discriminant field MUST appear first in the variant section, followed by the fields of the matching case arm.

**Sequence values.** Elements are separated by semicolons. An empty sequence is represented as `()`.

**Disambiguation.** An empty record value `()` and an empty sequence value `()` are syntactically identical. The parser disambiguates based on the declared type of the enclosing context.

---

## 5. Serialisation Modes

Honest defines two serialisation modes for the same logical document:

### 5.1 Document Mode

Document mode is the canonical on-disk representation. It preserves:

- Comments
- Whitespace formatting and indentation
- Declaration ordering (beyond the required topological order, the author's chosen grouping and sequence)

Document mode files use the extension `.honest`.

### 5.2 Wire Mode

Wire mode is the transmission representation used for agent-to-agent communication. It:

- Strips all comments
- Normalises whitespace to single spaces between tokens
- Preserves all declarations and data exactly

Wire mode is a strict subset of document mode — every wire-mode document is a valid document-mode document, but not vice versa (because comments are absent).

### 5.3 Canonical Form

The canonical form of an Honest document is defined for content-hash computation:

1. All comments removed.
2. All whitespace normalised to a single space between tokens.
3. No trailing whitespace on any line.
4. A single line feed (U+000A) as line terminator.
5. No trailing line feed after the last token.

Two documents with identical canonical forms are semantically identical.

---

## 6. Rulebooks

A rulebook is a persistent type registry that accelerates parsing across multiple documents.

### 6.1 Rulebook Format

A rulebook is a declarations-only Honest file. It contains a `type` section but no `var` section. Rulebook files use the extension `.honest-rulebook`.

```pascal
{ Rulebook: NBS Standard Types v1 }

type
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Provenance = (Observed, Inferred, Recalled, Generated);
  AgentRole  = (Supervisor, Gatekeeper, Scribe, Theologian,
                Generalist, Testkeeper);
  { ... }
```

### 6.2 Versioning

Rulebooks are versioned by the SHA-256 hash of their canonical form (§5.3, applied to the declarations only, types sorted alphabetically by name). Two rulebooks containing identical type definitions produce identical hashes regardless of comment content, whitespace, or original ordering.

### 6.3 Parsing Behaviour

When a parser has a loaded rulebook and encounters a document:

1. For each type declaration in the document, compare against the rulebook by type name.
2. **Match:** The document's declaration is structurally identical to the rulebook entry. Use the pre-parsed rulebook entry. This is the fast path.
3. **Divergence:** The document's declaration differs from the rulebook entry. Parse the document's declaration from scratch. Log the divergence. The document's declaration governs for this document.
4. **Unknown:** The document declares a type not in the rulebook. Parse the declaration. Optionally extend the in-memory rulebook.

The parser MUST NOT reject a document because its declarations diverge from the rulebook. Divergence is information, not error.

### 6.4 Rulebook Extension

After parsing documents with unknown or divergent declarations, a receiver MAY persist the extended rulebook. The extended rulebook is a new file with a new content hash. The original rulebook is not modified.

---

## 7. Conformance

### 7.1 Conformant Documents

A conformant Honest document MUST:

1. Be valid UTF-8.
2. Contain a declarations section followed by a data section.
3. Declare all non-primitive types referenced in the data section.
4. Order type declarations topologically (every type defined before referenced).
5. Contain no forward references or recursive types.
6. Include all declared fields in every record value.
7. Include the discriminant field in every variant record value.
8. Match variant case arm fields to the discriminant value.

### 7.2 Conformant Parsers

A conformant Honest parser MUST:

1. Accept any conformant document.
2. Accept documents with unknown type declarations (extend, not reject).
3. Report divergence between document declarations and loaded rulebook as diagnostic information, not errors.
4. Produce diagnostic messages (not hard failures) for recoverable parse issues.
5. Support nesting depths of at least 32.
6. Preserve field order in records across parse/serialise cycles.
7. Preserve declaration order across parse/serialise cycles.
8. Correctly handle both document mode and wire mode input.

A conformant parser SHOULD:

1. Support nesting depths of at least 64.
2. Warn on enumeration values unused by any case arm.
3. Warn on identifiers that differ from keywords only in case.
4. Provide partial parse results for malformed documents where possible.

### 7.3 Conformant Serialisers

A conformant Honest serialiser MUST:

1. Produce valid UTF-8 output.
2. Emit declarations in topological order.
3. Emit all declared fields in record values.
4. Emit fields in declared order.
5. Produce output that round-trips: parse(serialise(document)) yields a semantically identical document.

A conformant serialiser SHOULD:

1. Produce human-readable output with conventional indentation in document mode.
2. Produce minimal-whitespace output in wire mode.
3. Preserve comments in document mode.

---

## 8. Formal Grammar

The complete EBNF grammar for Honest v1:

```ebnf
(* Document structure *)
Document            = DeclarationsSection DataSection .
DeclarationsSection = "type" TypeDeclaration { TypeDeclaration } .
DataSection         = VariableAssignment { VariableAssignment } .

(* Type declarations *)
TypeDeclaration     = Identifier "=" TypeDefinition ";" .
TypeDefinition      = EnumerationType | RecordType | SequenceType | TypeReference .

(* Enumeration *)
EnumerationType     = "(" Identifier { "," Identifier } ")" .

(* Record *)
RecordType          = "record" [ FieldList ] [ VariantPart ] "end" .
FieldList           = Field { ";" Field } [ ";" ] .
Field               = Identifier ":" TypeReference .

(* Variant *)
VariantPart         = "case" Identifier ":" TypeReference "of"
                      VariantArm { VariantArm } .
VariantArm          = Identifier ":" "(" [ FieldList ] ")" ";" .

(* Sequence *)
SequenceType        = "sequence" "of" TypeReference .

(* Type reference *)
TypeReference       = Identifier .

(* Variable assignment *)
VariableAssignment  = "var" Identifier ":" TypeReference "=" Value ";" .

(* Values *)
Value               = PrimitiveValue | EnumValue | RecordValue | SequenceValue .
PrimitiveValue      = IntegerLiteral | RealLiteral | StringLiteral
                    | BooleanLiteral | CharLiteral .
BooleanLiteral      = "True" | "False" .
EnumValue           = Identifier .
RecordValue         = "(" FieldValue { ";" FieldValue } [ ";" ] ")" .
FieldValue          = Identifier ":" Value .
SequenceValue       = "(" [ Value { ";" Value } [ ";" ] ] ")" .

(* Lexical *)
Identifier          = Letter { Letter | Digit | "_" } .
Letter              = "A".."Z" | "a".."z" .
Digit               = "0".."9" .
IntegerLiteral      = [ "-" ] Digit { Digit } .
RealLiteral         = [ "-" ] Digit { Digit } "." Digit { Digit } [ Exponent ] .
Exponent            = ( "e" | "E" ) [ "+" | "-" ] Digit { Digit } .
StringLiteral       = "'" { StringChar } "'" .
StringChar          = (* any character except "'" and raw control chars *)
                    | "''" | EscapeSequence .
EscapeSequence      = "\n" | "\r" | "\t" | "\\" | "\u" HexDigit HexDigit HexDigit HexDigit
                    | "\U" HexDigit HexDigit HexDigit HexDigit
                           HexDigit HexDigit HexDigit HexDigit .
HexDigit            = Digit | "A".."F" | "a".."f" .
CharLiteral         = "'" StringChar "'" .

(* Comments — lexically stripped before parsing in wire mode *)
Comment             = BraceComment | ParenStarComment .
BraceComment        = "{" { (* any char except "}" *) } "}" .
ParenStarComment    = "(*" { (* any char except "*)" *) } "*)" .
```

---

## 9. NBS Standard Type Library

The following types constitute the NBS standard rulebook. They are not part of the Honest format specification — they are a domain-specific type library built on Honest.

```pascal
{ NBS Standard Types — shipped as nbs-standard.honest-rulebook }

type
  { Epistemic metadata }
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Provenance = (Observed, Inferred, Recalled, Generated);

  { Agent roles }
  AgentRole = (Supervisor, Gatekeeper, Scribe, Theologian,
               Generalist, Testkeeper, Pythia, Shepherd);

  { Speech acts }
  ClaimKind = (Assertion, Query, Correction, Partial, Error);

  { Semantic string subtypes }
  Uri         = String;
  Identifier  = String;
  NaturalText = String;
  Timestamp   = LongInt;

  { Core message types }
  Claim = record
    confidence : Confidence;
    provenance : Provenance;
    as_of      : Timestamp;
    case kind : ClaimKind of
      Assertion  : (content : NaturalText);
      Query      : (intent : NaturalText; scope : NaturalText);
      Correction : (replaces : LongInt; content : NaturalText);
      Partial    : (content : NaturalText; completeness : Real);
      Error      : (code : Word; reason : NaturalText);
  end;

  Message = record
    id      : LongInt;
    sender  : AgentRole;
    payload : Claim;
  end;

  { Collections }
  MessageBatch = sequence of Message;

  { Optionality pattern }
  OptionalString = record
    case present : Boolean of
      True  : (value : String);
      False : ();
  end;
```

---

## Appendix A: Complete Example Document

```pascal
{ An NBS agent message in Honest format }

type
  Confidence  = (Certain, High, Moderate, Low, Speculative);
  Provenance  = (Observed, Inferred, Recalled, Generated);
  AgentRole   = (Supervisor, Gatekeeper, Scribe, Theologian,
                 Generalist, Testkeeper);
  ClaimKind   = (Assertion, Query, Correction, Error);
  Timestamp   = LongInt;
  NaturalText = String;

  Claim = record
    confidence : Confidence;
    provenance : Provenance;
    as_of      : Timestamp;
    case kind : ClaimKind of
      Assertion  : (content : NaturalText);
      Query      : (intent : NaturalText; scope : NaturalText);
      Correction : (replaces : LongInt; content : NaturalText);
      Error      : (code : Word; reason : NaturalText);
  end;

  Message = record
    id      : LongInt;
    sender  : AgentRole;
    payload : Claim;
  end;

var message : Message = (
  id      : 4721;
  sender  : Theologian;
  payload : (
    confidence : High;
    provenance : Inferred;
    as_of      : 1774856400;
    kind       : Assertion;
    content    : 'The compilation target is ARM Thumb2';
  );
);
```

---

## Appendix B: Design Rationale Index

| Decision | Rationale | Reference |
|----------|-----------|-----------|
| Named string subtypes over qualifiers | Reuses existing type alias mechanism; no new syntax to parse | §3.3 |
| No null/nil/optional | Explicit presence via variant records; no ambiguity about field state | §3.4 |
| Topological declaration ordering | Enables single-pass parsing; no fixup phase needed | §3.7 |
| No recursive types | Guarantees finite documents; enables arena allocation | §3.7 |
| Semicolon-separated sequences | Consistent with record field separator; unambiguous | §3.6 |
| Comments stripped in wire mode | Comments are documentation, not data; keeps wire format minimal | §2.3, §5.2 |
| Rulebook as declarations-only file | Rulebooks are valid Honest documents; no separate format needed | §6.1 |
| Content-hash versioning for rulebooks | Avoids version-number coordination; structural identity | §6.2 |
| Variant part must be last in record | Eliminates end-boundary ambiguity; matches Pascal convention | §3.5 |
| Single variant part per record | Simplifies parser; composition via named types for complex cases | §3.5 |
