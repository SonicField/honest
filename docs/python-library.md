# Honest Python Library

A pure Python implementation of the Honest interchange format. Parse, serialise, and extract Honest documents with a Pythonic API.

---

## Installation

```bash
pip install python/
```

Requires Python 3.10+. No external dependencies.

---

## Quick Start

```python
import honest

# Parse a document from a string
doc = honest.parse("""
type
  Status = (Active, Inactive);
  User = record
    name : String;
    age  : Integer;
    status : Status;
  end;
var user : User = (name : 'Alice'; age : 30; status : Active);
""")

# Access fields — auto-unwrapped to native Python types
print(doc['user'].name)      # 'Alice' (str)
print(doc['user'].age)       # 30 (int)
print(doc['user'].status)    # 'Active' (str)

# Serialise back
text = honest.serialise(doc)              # human-readable
wire = honest.serialise(doc, wire=True)   # compact wire mode
```

---

## API Reference

### `honest.parse(text) -> Document`

Parse an Honest document from a string.

```python
doc = honest.parse(text)
```

Raises `honest.ParseError` on invalid input. The error includes line and column information.

### `honest.parse_file(path) -> Document`

Parse an Honest document from a file.

```python
doc = honest.parse_file("message.honest")
```

### `honest.serialise(doc, wire=False) -> str`

Serialise a `Document` back to Honest text.

```python
text = honest.serialise(doc)              # document mode (indented, readable)
wire = honest.serialise(doc, wire=True)   # wire mode (minimal, canonical)
```

Wire mode produces byte-identical output to the C reference implementation and is suitable for content hashing.

### `honest.extract(text) -> Document`

Extract the first valid Honest document from mixed text (e.g. AI output, chat logs, documentation).

```python
doc = honest.extract(ai_response)
```

Handles:
- Honest documents embedded in prose
- Markdown code fences (` ```pascal `, ` ```honest `, or bare ` ``` `)
- Multiple candidate positions (tries each until one parses)

Raises `ValueError` if no valid Honest document is found.

---

## Document Model

The parsed document structure (shown as Honest-like pseudocode — the actual types are Python dataclasses):

```
Document
  types : list of HonType
  vars  : list of Variable

Variable
  name  : String
  type  : HonType
  value : HonValue
```

The full type and value hierarchies are defined in `python/honest/types.py`.

### Accessing Variables

Documents support dict-style access to variables:

```python
doc = honest.parse(text)

# Dict-style
value = doc["result"]

# With default
value = doc.get("result", None)
```

### Accessing Fields

Record values support dict-style, attribute-style, and `.get()` access:

```python
# Dict-style (returns auto-unwrapped value)
name = doc["user"]["name"]      # 'Alice'

# Dict then attribute (most concise for nested access)
name = doc['user'].name          # 'Alice'

# With default
email = doc['user'].get("email", "unknown")
```

### Auto-Unwrapping

Leaf values are automatically unwrapped to native Python types:

| Honest Type | Python Type |
|-------------|-------------|
| Boolean | `bool` |
| Byte, Word, Integer, LongInt | `int` |
| Real | `float` |
| Char | `str` (single character) |
| String | `str` |
| Enumeration | `str` (value name) |
| Record | `HonValue` (stays wrapped for field access) |
| Sequence | `HonValue` (stays wrapped for element access) |

```python
doc = honest.parse(text)

# Leaves auto-unwrap
assert isinstance(doc['user'].name, str)
assert isinstance(doc['user'].age, int)
assert isinstance(doc['user'].active, bool)

# Compounds stay wrapped for navigation
record = doc['user']         # HonValue (record)
seq = doc['items']           # HonValue (sequence)
```

### Navigating Sequences

Sequence elements are accessed by integer index:

```python
doc = honest.parse("""
type
  Scores = sequence of Integer;
var scores : Scores = (95; 87; 62);
""")

first = doc["scores"][0]      # 95
second = doc["scores"][1]     # 87
```

### Navigating Variant Records

Variant record fields (both fixed and arm-specific) are accessed by name:

```python
# Access fixed fields
shape = doc['shape']
print(shape.label)            # fixed field

# Access discriminant
print(shape.kind)             # tag value (enum name)

# Access variant arm fields
print(shape.radius)           # field from the active arm
```

---

## Type Introspection

Access the type registry of a parsed document:

```python
doc = honest.parse(text)

# List all types
for t in doc.types:
    print(f"{t.name}: {t.kind.name}")

# Look up a type by name
user_type = doc.get_type("User")
if user_type:
    for field in user_type.fields:
        print(f"  {field.name}: {field.type.name}")

# Resolve aliases to their underlying type
uri_type = doc.get_type("Uri")
resolved = uri_type.resolve()   # follows alias chain to String
```

---

## Error Handling

```python
import honest

try:
    doc = honest.parse(invalid_text)
except honest.ParseError as e:
    print(f"Parse error: {e}")
    # Includes line and column information

try:
    doc = honest.extract(text_without_honest)
except ValueError:
    print("No Honest document found in text")
```

---

## Examples

### Parse and inspect a file

```python
import honest

doc = honest.parse_file("examples/05-nbs-agent-message.honest")
print(f"Types: {len(doc.types)}, Variables: {len(doc.vars)}")
for v in doc.vars:
    print(f"  {v.name}: {v.type.name}")
```

### Extract from AI output

```python
import honest

ai_response = """
Here is my analysis:

```pascal
type
  Verdict = (Pass, Fail, Inconclusive);
  Result = record
    verdict : Verdict;
    reasoning : String;
  end;
var result : Result = (verdict : Pass; reasoning : 'All checks passed');
```

The system is functioning correctly.
"""

doc = honest.extract(ai_response)
print(doc['result'].verdict)       # 'Pass'
print(doc['result'].reasoning)     # 'All checks passed'
```

### Round-trip serialisation

```python
import honest

doc = honest.parse_file("message.honest")

# Document mode (readable)
readable = honest.serialise(doc)

# Wire mode (compact, canonical)
wire = honest.serialise(doc, wire=True)

# Round-trip: parse(serialise(doc)) produces identical wire output
doc2 = honest.parse(readable)
wire2 = honest.serialise(doc2, wire=True)
assert wire == wire2
```
