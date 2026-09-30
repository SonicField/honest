# Honest User Guide

A tutorial introduction to reading, writing, and understanding Honest documents.

---

## What is Honest?

Honest is an interchange format — a way to represent structured data so that it can be passed between programs, agents, and people. If you have used JSON, YAML, or Protobuf, Honest fills a similar role but with different priorities.

Honest is designed for three consumers equally:

- **Humans** can read an Honest document and understand it without a parser or documentation.
- **AI agents** can interpret an Honest document cold, using only what the document itself contains.
- **Machines** can parse an Honest document efficiently, optionally accelerating with a pre-loaded type registry.

Every Honest document carries its own type definitions. You never need an external schema file, a documentation website, or prior knowledge to understand what a document contains.

---

## Your First Honest Document

Here is a complete Honest document:

```pascal
type
  Colour = (Red, Green, Blue);

  Pixel = record
    x      : Integer;
    y      : Integer;
    colour : Colour;
  end;

var pixel : Pixel = (
  x      : 120;
  y      : 45;
  colour : Green;
);
```

Read it from top to bottom. By the time you reach the data, you already know what every field means:

1. `Colour` is one of three values: Red, Green, or Blue.
2. A `Pixel` has an x coordinate, a y coordinate, and a colour.
3. The variable `pixel` is a Pixel at position (120, 45), coloured Green.

No external documentation was needed. The document explained itself.

---

## Document Structure

Every Honest document has exactly two sections, in this order:

### 1. Declarations

The declarations section defines all the types used in the document. It starts with the keyword `type` and contains one or more type definitions.

```pascal
type
  Status = (Active, Inactive, Pending);

  Account = record
    id     : LongInt;
    name   : String;
    status : Status;
  end;
```

Think of declarations as the vocabulary of the document. They tell you what kinds of things exist and what shape they have.

### 2. Data

The data section contains one or more variable assignments. Each assignment gives a name, a type, and a value.

```pascal
var account : Account = (
  id     : 42;
  name   : 'Alice';
  status : Active;
);
```

Both sections must be present in every document. A document without declarations is not Honest — even if the types seem obvious, they must be stated explicitly. This is the self-description guarantee.

---

## Types

Honest has a small, fixed set of types. Everything you need to represent structured data, nothing more.

### Primitive Types

These are built in. You do not need to declare them.

| Type | What it holds | Example values |
|------|--------------|----------------|
| `Boolean` | True or false | `True`, `False` |
| `Byte` | Small number (0–255) | `0`, `128`, `255` |
| `Word` | Medium number (0–65535) | `1024`, `65535` |
| `Integer` | Signed whole number (32-bit) | `-1`, `0`, `2147483647` |
| `LongInt` | Large signed whole number (64-bit) | `1774856400` |
| `Real` | Decimal number | `3.14`, `-0.001`, `2.5e10` |
| `Char` | Single character | `'A'`, `'!'` |
| `String` | Text | `'Hello, world'` |

### Enumerations

An enumeration is a type with a fixed set of named values:

```pascal
type
  Direction = (North, South, East, West);
  Priority  = (Critical, High, Normal, Low);
```

Enumeration values are self-explanatory. When you see `direction : North` in the data, you know exactly what it means.

### Records

A record groups named fields together:

```pascal
type
  Point = record
    x : Real;
    y : Real;
  end;
```

Record values list each field with its value, enclosed in parentheses, separated by semicolons:

```pascal
var origin : Point = (
  x : 0.0;
  y : 0.0;
);
```

Every field declared in the record must be present in the value. There is no "optional" or "null" — if a field is declared, it must have a value. (See [Expressing Optionality](#expressing-optionality) for how to handle fields that might be absent.)

### Type Aliases

A type alias gives a new name to an existing type:

```pascal
type
  Uri         = String;
  Timestamp   = LongInt;
  NaturalText = String;
```

The alias is a distinct type for documentation purposes. A field of type `Uri` tells you the string is a web address. A field of type `Timestamp` tells you the number is a Unix epoch time. The names carry meaning that the underlying types do not.

### Sequences

A sequence is an ordered list of values of the same type:

```pascal
type
  Scores = sequence of Integer;
```

Sequence values are enclosed in parentheses, elements separated by semicolons:

```pascal
var scores : Scores = (95; 87; 62; 100; 73);
```

An empty sequence:

```pascal
var empty : Scores = ();
```

Sequences can contain records:

```pascal
type
  Point  = record
    x : Real;
    y : Real;
  end;
  Path = sequence of Point;

var triangle : Path = (
  (x : 0.0; y : 0.0);
  (x : 1.0; y : 0.0);
  (x : 0.5; y : 0.866);
);
```

### Variant Records (Discriminated Unions)

This is the most powerful feature of Honest. A variant record is a record where some fields depend on a tag value.

Consider a shape that can be either a circle or a rectangle:

```pascal
type
  ShapeKind = (Circle, Rectangle);

  Shape = record
    name : String;
    case kind : ShapeKind of
      Circle    : (radius : Real);
      Rectangle : (width : Real; height : Real);
  end;
```

A circle has a radius. A rectangle has a width and height. The `kind` field tells you which set of fields is present:

```pascal
var myCircle : Shape = (
  name   : 'Big circle';
  kind   : Circle;
  radius : 5.0;
);

var myRect : Shape = (
  name   : 'Small box';
  kind   : Rectangle;
  width  : 2.0;
  height : 3.0;
);
```

The variant part must be the last section of a record. You can have ordinary fields before it (like `name` above), but nothing after it.

---

## Common Patterns

### Expressing Optionality

Honest has no null, nil, or optional keyword. If a value might be absent, you express this explicitly with a Boolean variant:

```pascal
type
  OptionalString = record
    case present : Boolean of
      True  : (value : String);
      False : ();
  end;

  Person = record
    name  : String;
    email : OptionalString;
  end;

var alice : Person = (
  name  : 'Alice';
  email : (present : True; value : 'alice@example.com');
);

var bob : Person = (
  name  : 'Bob';
  email : (present : False);
);
```

This is intentionally verbose. You can see exactly what is present and what is absent. There is no ambiguity about whether a field was omitted by accident or on purpose.

### Multiple Values in One Document

A document can contain multiple variable assignments:

```pascal
type
  Colour = (Red, Green, Blue);

var primary : Colour = Red;
var secondary : Colour = Blue;
var accent : Colour = Green;
```

### Nested Records

Records can contain other records:

```pascal
type
  Address = record
    street : String;
    city   : String;
    zip    : String;
  end;

  Company = record
    name    : String;
    address : Address;
  end;

var acme : Company = (
  name    : 'Acme Corp';
  address : (
    street : '123 Main St';
    city   : 'Springfield';
    zip    : '62704';
  );
);
```

### Sequences of Variant Records

Combining sequences and variants is how you represent heterogeneous collections:

```pascal
type
  EventKind = (Login, Logout, Error);

  Event = record
    timestamp : LongInt;
    case kind : EventKind of
      Login  : (user : String; ip : String);
      Logout : (user : String);
      Error  : (code : Integer; message : String);
  end;

  EventLog = sequence of Event;

var log : EventLog = (
  (timestamp : 1774856400; kind : Login;
   user : 'alice'; ip : '192.168.1.1');
  (timestamp : 1774856460; kind : Error;
   code : 500; message : 'Internal server error');
  (timestamp : 1774856520; kind : Logout;
   user : 'alice');
);
```

---

## Comments

Honest supports comments for human readers:

```pascal
{ This is a brace comment }
(* This is a paren-star comment *)

type
  { Confidence level for epistemic claims }
  Confidence = (Certain, High, Moderate, Low, Speculative);
```

Comment text and encounter order are preserved in document mode, although a
formatter may normalise placement. Comments are stripped when messages are sent
between agents. They are for humans, not machines. Do not put information in
comments that a receiver needs to interpret the data — use typed fields instead.

---

## Declaration Ordering

Types must be declared before they are used. If type A references type B, then B must appear first:

```pascal
{ CORRECT — Colour declared before Pixel uses it }
type
  Colour = (Red, Green, Blue);
  Pixel = record
    colour : Colour;
  end;
```

```pascal
{ WRONG — Pixel references Colour before it exists }
type
  Pixel = record
    colour : Colour;
  end;
  Colour = (Red, Green, Blue);
```

Primitive types (Boolean, Integer, String, etc.) are always available — you do not need to declare them.

---

## Strings

Strings are enclosed in single quotes:

```pascal
var greeting : String = 'Hello, world';
```

A single quote within a string is written as two single quotes:

```pascal
var message : String = 'It''s a lovely day';
```

Special characters use backslash escapes:

| Escape | Meaning |
|--------|---------|
| `''` | Single quote |
| `\n` | New line |
| `\t` | Tab |
| `\\` | Backslash |
| `\uXXXX` | Unicode character (4 hex digits) |

---

## Rulebooks

A rulebook is a file containing only type declarations — no data section. It acts as a pre-loaded vocabulary that lets a parser work faster.

```pascal
{ File: standard.honest-rulebook }

type
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Provenance = (Observed, Inferred, Recalled, Generated);
  { ... more types ... }
```

When a parser has a rulebook loaded, it compares each document's declarations against the rulebook. Where they match, parsing is faster. Where they differ, the document's own declarations are used.

You do not need a rulebook to use Honest. Every document is self-contained. Rulebooks are a performance optimisation, not a requirement.

---

## Key Principles

**Every document carries its own types.** Even if the receiver has a rulebook with the same types, the document includes them. This means any document can be understood in isolation.

**No nulls, no defaults, no implicit values.** If a field is declared, it must be present with a value. If something might be absent, the type system says so explicitly (via a Boolean variant).

**Types declare before data uses.** Read a document top to bottom. By the time you reach the data, you have already seen every type definition you need.

**Semicolons separate values. Commas list names.** In the declarations section, commas separate enumeration values: `(Red, Green, Blue)`. In the data section, semicolons separate field assignments and sequence elements: `(x : 1; y : 2)`.

**Comments are for humans, not machines.** Put documentation in comments. Put data in typed fields. Comments are stripped from wire transmissions.

---

## A Complete Example

An NBS agent message demonstrating all major features:

```pascal
{ An agent reporting analysis results }

type
  { Epistemic metadata }
  Confidence = (Certain, High, Moderate, Low, Speculative);
  Provenance = (Observed, Inferred, Recalled, Generated);

  { Agent identity }
  AgentRole = (Supervisor, Gatekeeper, Scribe, Theologian,
               Generalist, Testkeeper);

  { Semantic string types }
  NaturalText = String;
  Timestamp   = LongInt;
  FilePath    = String;

  { A reference to a source file }
  SourceRef = record
    file : FilePath;
    line : Integer;
  end;

  { A finding from code analysis }
  FindingKind = (Risk, Invariant, Assumption);

  Finding = record
    kind       : FindingKind;
    confidence : Confidence;
    summary    : NaturalText;
    location   : SourceRef;
  end;

  { The analysis report }
  Findings = sequence of Finding;

  Report = record
    analyst   : AgentRole;
    timestamp : Timestamp;
    subject   : NaturalText;
    findings  : Findings;
  end;

var report : Report = (
  analyst   : Theologian;
  timestamp : 1774856400;
  subject   : 'Architecture review of auth module';
  findings  : (
    (kind : Risk; confidence : High;
     summary : 'Session tokens are not rotated after privilege escalation';
     location : (file : 'src/auth/session.c'; line : 142));
    (kind : Invariant; confidence : Certain;
     summary : 'All password hashes use bcrypt with cost factor 12';
     location : (file : 'src/auth/password.c'; line : 58));
    (kind : Assumption; confidence : Moderate;
     summary : 'Rate limiting assumes single-process deployment';
     location : (file : 'src/auth/ratelimit.c'; line : 23));
  );
);
```

A human can read this and understand the analysis. An AI can interpret the structure and act on the findings. A program can parse the typed data and route, store, or transform it. All three consumers served equally, from a single document.

---

## Tooling

### Command-Line Tools

Honest includes five CLI tools:

- **`honest-parse`** — parse and validate a document (`honest-parse file.honest` or `honest-parse -`)
- **`honest-fmt`** — reformat a document (pretty-print or wire mode with `-w`)
- **`honest-get`** — extract field values from a document (`honest-get file.honest var field1 field2`)
- **`honest-build`** — construct a document from shell key=value arguments
- **`honest-extract`** — extract the first valid Honest document from mixed text (e.g. AI output)

### Building Documents from Shell

```bash
honest-build --type SessionMeta --rulebook types.honest \
  session_id="$UUID" handle="$HANDLE" pid=$$
```

Nested records use dot-paths: `data.x=42 data.y=world`. Sequences use numeric indices: `items.0.name=widget items.1.name=gadget`.

### Python Library

```python
import honest

# Parse
doc = honest.parse_file("message.honest")

# Access values (auto-unwraps to native Python types)
doc["result"]["name"]      # → 'Alice' (str)
doc["result"]["score"]     # → 42 (int)
doc["result"]["active"]    # → True (bool)

# Attribute-style access
task = doc["task"]
task.title                 # → 'Build parser' (str)

# Extract from AI output
doc = honest.extract(ai_response_text)
verdict = doc["result"]["verdict"]  # → 'Pass' (str)

# Serialise
honest.serialise(doc)                # document mode
honest.serialise(doc, wire=True)     # wire mode
```

Install: `pip install -e python/`
