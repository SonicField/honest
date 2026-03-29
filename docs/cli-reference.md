# Honest CLI Reference

Five command-line tools for working with Honest documents. All tools are built with `make` and placed in `build/`.

---

## honest-parse

Validate and inspect an Honest document.

```
honest-parse [options] <file | ->
```

### Options

```pascal
type
  ParseOption = (Quiet, Verbose, Help);

  ParseCli = record
    file    : String;     { path to .honest file, or '-' for stdin }
    quiet   : Boolean;    { -q, --quiet: suppress diagnostics }
    verbose : Boolean;    { -v, --verbose: print parsed structure }
    help    : Boolean;    { -h, --help: show usage }
  end;
```

### Behaviour

- Parses the input document and reports any errors to stderr.
- On success, prints `OK: N type(s), M variable(s)` to stdout.
- With `--verbose`, prints detailed type listings (kind, fields) and variable declarations.
- With `--quiet`, suppresses all diagnostic output.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Valid document, no errors |
| 1 | Parse errors or invalid document |
| 2 | Usage error (missing argument) |

### Examples

```bash
honest-parse message.honest
honest-parse -v examples/03-variant-records.honest
cat message.honest | honest-parse -q -
```

---

## honest-fmt

Reformat an Honest document in document mode (human-readable) or wire mode (compact).

```
honest-fmt [options] <file | ->
```

### Options

```pascal
type
  FmtMode = (Document, Wire);

  FmtCli = record
    file : String;   { path to .honest file, or '-' for stdin }
    mode : FmtMode;  { -d/--document (default) or -w/--wire }
    help : Boolean;  { -h, --help: show usage }
  end;
```

### Behaviour

- Parses the input, then re-serialises in the requested mode to stdout.
- Document mode (default): indented, human-readable.
- Wire mode: minimal whitespace, canonical form suitable for content hashing.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Parse error |
| 2 | Usage error |

### Examples

```bash
# Pretty-print a document
honest-fmt message.honest

# Convert to wire mode
honest-fmt -w message.honest > message.wire

# Reformat from stdin
cat message.honest | honest-fmt -w -
```

---

## honest-get

Extract field values from an Honest document by name.

```
honest-get <file | -> <var-name> <field> [field ...]
```

### Options

```pascal
type
  GetCli = record
    file     : String;               { path to .honest file, or '-' for stdin }
    var_name : String;               { variable name to read from }
    fields   : sequence of String;   { dot-path field names to extract }
  end;
```

No flags — all arguments are positional.

### Field Navigation

Fields support dot-path notation for nested access:

- `result.name` — field `name` inside record variable `result`
- `result.scores.0` — first element of sequence field `scores`
- `result.scores.2.label` — field `label` of third element

### Output Format

One value per line, raw (unquoted):

| Type | Output |
|------|--------|
| String | Unquoted text |
| Integer | Signed decimal |
| Real | Double precision (`.17g` format) |
| Boolean | `true` or `false` |
| Char | ASCII character, or `\uXXXX` for non-ASCII |
| Enumeration | Unquoted value name |
| Record/Sequence | Type name (warning to stderr) |

Missing fields print an empty line and set exit code to 1.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All requested fields found |
| 1 | Parse error or missing field |
| 2 | Usage error (insufficient arguments) |

### Examples

```bash
# Extract two fields from a file
honest-get message.honest result verdict reasoning

# Extract from stdin with dot-path
honest-get - result scores.0.value < message.honest

# Use in shell scripts
VERDICT=$(honest-get message.honest result verdict)
```

---

## honest-build

Construct an Honest document from shell values.

```
honest-build --type <TypeName> [options] field=value [field.subfield=value ...]
```

### Options

```pascal
type
  BuildCli = record
    type_name : String;   { --type: required, type to construct }
    var_name  : String;   { --var-name: variable name in output (default: 'result') }
    rulebook  : String;   { --rulebook: type definitions file }
    typedef   : String;   { --typedef: inline type definition string }
    help      : Boolean;  { -h, --help: show usage }
  end;
```

Either `--rulebook` or `--typedef` is required (not both).

### Field Assignment

Values are assigned via `field=value` arguments:

- `name=Alice` — string field
- `score=42` — integer field
- `rating=3.7` — real field
- `active=True` — boolean field
- `status=Approved` — enumeration field
- `addr.city=London` — nested record field (dot-path)
- `items.0.name=First` — sequence element field (numeric index)

### Constraints

- All record fields must be provided (no optional fields).
- Sequence indices must be contiguous (0, 1, 2, ...).
- All values must be valid UTF-8.
- Maximum 256 field assignments, maximum 32 levels of nesting.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (missing type, invalid value, UTF-8 failure) |

### Examples

```bash
# Build from a rulebook
honest-build --type SessionMeta --rulebook lib/nbs-standard.honest-rulebook \
  session_id="$UUID" handle="$HANDLE" pid=$$

# Build with inline type definition
honest-build --type Report \
  --typedef "type Report = record title : String; score : Integer; end;" \
  title="My Report" score=42

# Build with custom variable name
honest-build --type Point --var-name origin \
  --typedef "type Point = record x : Integer; y : Integer; end;" \
  x=0 y=0
```

---

## honest-extract

Find and extract a valid Honest document from mixed text (e.g. AI output).

```
honest-extract [file | -]
```

### Options

```pascal
type
  ExtractCli = record
    file : String;   { path to input file, or '-' for stdin (default: stdin) }
  end;
```

No flags. Reads from stdin by default.

### Behaviour

- Scans input for Honest documents by looking for the `type` keyword.
- Recognises Markdown code fences (`` ``` ``) and searches within them first.
- On finding a valid document, outputs it in wire mode to stdout.
- Returns the first valid document found — does not extract multiple documents.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Valid Honest document found and extracted |
| 1 | No valid document found or read error |

### Examples

```bash
# Extract from a file containing mixed text
honest-extract ai-response.txt

# Extract from piped AI output
echo "$AI_OUTPUT" | honest-extract

# Chain: extract then get a field
echo "$AI_OUTPUT" | honest-extract | honest-get - result verdict
```

---

## Common Patterns

### Stdin Piping

All tools accept stdin via `-` (or implicitly for `honest-extract`):

```bash
cat doc.honest | honest-parse -
cat doc.honest | honest-fmt -w -
cat doc.honest | honest-get - result field_name
echo "$TEXT" | honest-extract
```

### Tool Chaining

```bash
# Build a document, validate it, extract a field
MSG=$(honest-build --type Msg --typedef "..." body=hello)
echo "$MSG" | honest-parse -q - && echo "$MSG" | honest-get - result body

# Extract from AI output, reformat, and inspect
echo "$AI_OUTPUT" | honest-extract | honest-fmt - | honest-parse -v -
```
