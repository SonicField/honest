# Honest command-line contract

This document defines the observable interface of the five Honest command-line
programs. The [format specification](../specs/honest-spec.md) remains the
authority for Honest syntax and data semantics.

## Common rules

- Data is written to standard output. Diagnostics and usage errors are written
  to standard error.
- `-h` and `--help` write usage to standard output and return 0.
- A successful exit means all output was flushed successfully. Read,
  allocation, parse, serialisation, and output failures return non-zero.
- `-` means standard input wherever an input path is accepted.
- Inputs are read in full before parsing; these tools are not streaming
  parsers and do not impose a fixed document-size limit.
- Error diagnostics are intended for people and are not a stable
  machine-readable interface.

## `honest-parse`

```text
honest-parse [-q | -v] <file.honest | ->
```

The command validates one document. Normal success prints its type and variable
counts. `--verbose` also lists declarations. `--quiet` suppresses parse
diagnostics and successful output; operational failures may still be reported.
The two modes are mutually exclusive.

- 0: valid document
- 1: input, parse, or output failure
- 2: invalid command line

## `honest-fmt`

```text
honest-fmt [--wire | --document] <file.honest | ->
```

The command parses one document and writes document mode by default. `--wire`
writes the canonical compact representation. A parse containing an error never
produces formatted output. Formatting is idempotent for a selected mode.

- 0: document formatted and written
- 1: input, parse, serialisation, or output failure
- 2: invalid command line

## `honest-get`

```text
honest-get <file | -> <var-name> <field-path> [field-path ...]
```

Each field path is relative to `var-name`; for example, `result address.city`
selects `result.address.city`. Decimal path components index sequences. Values
are written raw, one requested value after another, with a newline appended to
each. A string value can itself contain newlines, so output is not necessarily
one physical line per value. Missing values produce an empty line and make the
overall command fail after processing every requested path.

Non-ASCII `Char` values use `\u` followed by the hexadecimal scalar value.
Compound values print their type name and a warning rather than a nested value.

- 0: every value found and written
- 1: input, parse, lookup, or output failure
- 2: invalid command line

## `honest-build`

```text
honest-build --type TypeName \
  (--rulebook file | --typedef 'type ...') \
  [--var-name name] field=value ...
```

Exactly one declaration source is required. Every assignment path must resolve
against the selected type and every required record field must be supplied.
Unknown, duplicate, malformed, or unused assignments are errors. Sequence
indices must be decimal, contiguous from zero, and below 256.

The command accepts at most 256 assignments and 32 path components. It builds
primitive, enumeration, ordinary record, and sequence values. Construction of
variant-record arms is not currently supported. A `Char` value must contain
exactly one Unicode scalar. All assignment arguments must be valid UTF-8.

The output variable is named `result` unless `--var-name` supplies a valid
Honest identifier. Existing variables in a rulebook are retained, so
declaration-only rulebooks are recommended.

- 0: document built and written
- 1: any command-line, declaration, value, construction, serialisation, or
  output error

## `honest-extract`

```text
honest-extract [file | -]
```

Standard input is the default. The command scans in input order for `type` at
the beginning of a line, allowing indentation. Markdown triple-backtick fences
are recognised when encountered. The first candidate that parses without an
error is written in wire mode; malformed candidates are skipped. At most one
document is produced.

This is deliberately a conservative recovery tool, not a general Markdown
parser. It does not recognise tilded fences or `type` embedded mid-line.

- 0: a valid document was found and written
- 1: no valid document, input failure, serialisation failure, or output failure
- 2: invalid command line

## Compatibility boundary

The tools target POSIX-like systems and are tested on Linux and macOS. Windows
is not currently supported. Neither the CLIs nor the libraries require NBS, a
service, or an AI provider.
