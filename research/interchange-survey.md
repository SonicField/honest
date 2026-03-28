# Interchange Format Survey

How existing formats handle collections, optionality, and discriminated unions —
and what Honest should learn from them.

## 1. Protobuf (Protocol Buffers)

### Collections
- `repeated` field modifier: `repeated string tags = 3;`
- Always variable-length. Wire format is simply the field repeated N times.
- No fixed-size arrays. No sets or maps in proto2; proto3 added `map<K,V>` as syntactic sugar over `repeated` pairs.
- Nested repeated fields (repeated message containing repeated fields) are fully supported.

### Optionality
- Proto2: all fields are optional by default, with explicit `required` and `optional` keywords. `has_field()` distinguishes "absent" from "present with default value."
- Proto3: dropped `required` and `optional` (initially). All fields are implicitly optional. A field set to its default value is indistinguishable from an absent field on the wire. `optional` was re-added in proto3 to restore presence tracking.
- No null concept. Absence and default are conflated unless `optional` is used.

### Discriminated Unions
- `oneof` construct: fields in a oneof share memory; exactly one can be set.
- The discriminant is implicit — determined by which field is present on the wire. No explicit tag field.
- Cannot contain `repeated` or `map` fields.

### Lessons for Honest
- **Adopt**: Variable-length collections are essential. Protobuf's `repeated` proves fixed-size is a non-starter for interchange.
- **Reject**: Implicit optionality (proto3 default conflation) is exactly the kind of dishonesty Honest exists to prevent. Absence must be explicit.
- **Reject**: Implicit discriminant in `oneof`. Honest's variant record tag should be explicit and typed — the Pascal tradition is correct here.

---

## 2. Cap'n Proto

### Collections
- `List(T)` type: variable-length, typed. `List(List(T))` for nested lists.
- No maps natively — represented as `List(Entry)` where Entry is a struct with key/value.
- Lists are random-access in the zero-copy representation (each element has known offset).

### Optionality
- All fields have default values. A field is always "present" with either its explicit or default value.
- Unions can represent absence: define a union variant with `void` to mean "not set."
- No null. No three-state (absent/null/value) distinction.

### Discriminated Unions
- Unnamed unions within structs. Each variant has an implicit 16-bit discriminant.
- Named groups possible via `union { ... }` syntax.
- The discriminant tag exists on the wire but is not user-named — it's positional.

### Lessons for Honest
- **Adopt**: `List(T)` as a named type constructor is clean. Honest's `sequence of T` should work similarly.
- **Note**: Cap'n Proto's zero-copy design drives many syntax decisions. Honest doesn't need this (text-first), so we have more freedom.
- **Reject**: Unnamed discriminants. Honest should follow Pascal: the tag is a named, typed field.

---

## 3. FlatBuffers

### Collections
- `[T]` vector syntax: `[string]`, `[Monster]`.
- Variable-length. Random-access via offset table.
- No nested vectors directly — must wrap inner vector in a table.

### Optionality
- All fields are optional by default (the opposite of Protobuf's proto2 `required`).
- `required` attribute marks mandatory fields: `name:string (required);`
- Default values specified inline: `hp:int = 100;`
- Absent fields return default value. `has_field()` available in some language bindings.

### Discriminated Unions
- `union` keyword with explicit enum of types: `union Equipment { Weapon, Armor, Potion }`
- Wire representation: type enum + offset. The discriminant is an explicit typed enum.
- Only tables (not scalars or structs) can be union members.

### Lessons for Honest
- **Adopt**: FlatBuffers' explicit union discriminant enum is close to Pascal's variant record approach. Validates the design direction.
- **Reject**: Default optionality. Honest should require all declared fields to be present. Optionality should be expressed through the type system (variant records), not through implicit field absence.
- **Note**: FlatBuffers' restriction on nested vectors (must wrap in table) parallels the recommendation to require named types for nested sequences.

---

## 4. ASN.1 (Abstract Syntax Notation One)

### Collections
- `SEQUENCE OF T` — ordered, variable-length. Directly analogous to the proposed `sequence of T`.
- `SET OF T` — unordered collection.
- `SEQUENCE { ... }` (without OF) — a record/struct with named fields.
- Rich collection model. Possibly the most expressive of all surveyed formats.

### Optionality
- Explicit `OPTIONAL` keyword on fields: `middle-name UTF8String OPTIONAL`
- `DEFAULT` keyword for default values: `status INTEGER DEFAULT 0`
- Three-state model: absent, present-with-default, present-with-value. All distinguishable.
- Also supports `NULL` as a distinct type.

### Discriminated Unions
- `CHOICE` type: `CHOICE { email UTF8String, phone NumericString, fax NumericString }`
- No explicit tag — the discriminant is determined by the encoding (tag-length-value).
- With `AUTOMATIC TAGS`, each variant gets an implicit context tag.
- Tagging can be made explicit with `[0]`, `[1]`, etc.

### Lessons for Honest
- **Adopt**: `SEQUENCE OF T` syntax. Honest's `sequence of T` is directly inspired by this and it's proven over 40 years.
- **Adopt**: Explicit `OPTIONAL` keyword demonstrates that optionality can be declared in the type system without ambiguity. However, Honest should prefer variant records over an OPTIONAL keyword to stay consistent with its Pascal foundation.
- **Reject**: ASN.1's complexity. The full standard (with its multiple encoding rules — BER, DER, PER, XER, OER) is notoriously hard to implement correctly. Honest must have exactly one encoding.
- **Note**: ASN.1 is the strongest precedent for "self-describing type system for interchange." Its longevity (1984–present) validates the core premise of Honest.

---

## 5. CBOR (Concise Binary Object Representation)

### Collections
- Arrays (major type 4): ordered, variable-length. Can be definite-length (count prefix) or indefinite-length (break code terminated).
- Maps (major type 5): key-value pairs. Keys can be any CBOR type.
- Heterogeneous — arrays can contain mixed types. No type constraint on elements.

### Optionality
- Implicit. Fields are key-value pairs in maps. If a key is absent, the field is absent.
- Explicit `null` (simple value 22) and `undefined` (simple value 23) are distinct values.
- Four-state model: absent, null, undefined, present-with-value. More states than most formats.

### Discriminated Unions
- No native support. Must be represented by convention (e.g., single-key map, or tag + value).
- CBOR tags (major type 6) can annotate values with semantic type information (e.g., tag 0 = date-time string, tag 32 = URI).

### Lessons for Honest
- **Reject**: Schemaless design. CBOR is JSON-equivalent in that it encodes shape but not meaning. Exactly the problem Honest exists to solve.
- **Reject**: Heterogeneous collections. Honest sequences must be homogeneously typed.
- **Note**: CBOR's semantic tags are interesting — they're essentially type annotations on values. Honest's named string subtypes (Uri, Identifier) serve the same purpose but are declared in the type system rather than annotated inline.
- **Note**: CBOR's distinction between null and undefined is over-specified for interchange. Honest's "all fields must be present" rule eliminates this entire class of ambiguity.

---

## 6. Summary Matrix

| Feature | Protobuf | Cap'n Proto | FlatBuffers | ASN.1 | CBOR |
|---------|----------|-------------|-------------|-------|------|
| Variable-length collections | `repeated` | `List(T)` | `[T]` | `SEQUENCE OF` | Array (type 4) |
| Nested collections | Yes | `List(List(T))` | No (wrap in table) | Yes | Yes (heterogeneous) |
| Typed collections | Yes | Yes | Yes | Yes | No |
| Explicit optionality | Proto2 yes, proto3 no | No (defaults) | `required` attr | `OPTIONAL` keyword | No (implicit absence) |
| Null concept | No | `void` variant | No | `NULL` type | `null` + `undefined` |
| Discriminated unions | `oneof` (implicit tag) | `union` (implicit tag) | `union` (explicit enum) | `CHOICE` (encoding tags) | Convention only |
| Self-describing | No (needs .proto) | No (needs schema) | No (needs schema) | BER/XER yes, PER no | Yes (schemaless) |
| Human-readable | .proto files yes, wire no | Schema yes, wire no | Schema yes, wire no | ASN.1 notation yes, wire no | No |

---

## 7. Recommendations for Honest

### Collections
Adopt `sequence of T` following ASN.1's proven `SEQUENCE OF` model. Variable-length, homogeneously typed, ordered. Require named type aliases for nested sequences to avoid parsing ambiguity (following FlatBuffers' pattern). No sets or maps in v1 — maps can be represented as `sequence of` a key-value record.

### Optionality
All declared fields must be present in the data section. No implicit absence, no null, no defaults. Optionality is expressed through variant records:
```pascal
case has_notes : Boolean of
  True  : (notes : ShortString);
  False : ();
```
This is more verbose than ASN.1's `OPTIONAL` keyword but is structurally honest — every possible state is declared and tagged. The verbosity is proportional to the semantic complexity, which is correct.

### Discriminated Unions
Follow FlatBuffers' pattern of explicit typed discriminant enums, mapped onto Pascal's variant record syntax. The discriminant tag is a named field of an enumerated type. This is the strongest consensus position across all surveyed formats and aligns with the Pascal foundation.

### Self-Description
No surveyed format achieves Honest's goal of being simultaneously self-describing, typed, and human-readable. ASN.1 comes closest (its notation is readable, its wire format can be self-describing in BER) but separates schema from encoding. CBOR is self-describing but untyped. Honest's innovation — declarations in every document — is genuinely novel in this landscape and addresses a real gap.

### What to Avoid
- Protobuf's default-value conflation (dishonest about presence)
- CBOR's schemaless flexibility (encodes shape, not meaning)
- ASN.1's encoding proliferation (one format, one encoding)
- Implicit discriminant tags (all formats except FlatBuffers get this wrong)
