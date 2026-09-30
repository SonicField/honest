"""Tests for the Python Honest parser.

Uses the same test inputs as the C conformance suite where applicable.
Values auto-unwrap: leaf types return native Python (str, int, float, bool),
compound types (records, sequences) return HonValue for further navigation.
"""

import pytest
import honest


# ---- Basic Parsing ----

def test_simple_record():
    doc = honest.parse("""
type
  Point = record
    x : Real;
    y : Real;
    label : String;
  end;
var origin : Point = (
  x : 0.0;
  y : 0.0;
  label : 'Origin';
);
""")
    assert len(doc.types) == 1
    assert len(doc.vars) == 1
    assert doc.vars[0].name == "origin"
    val = doc["origin"]  # record → stays wrapped
    assert val["x"] == 0.0
    assert val["label"] == "Origin"


def test_enum():
    doc = honest.parse("""
type
  Color = (Red, Green, Blue);
var c : Color = Red;
""")
    assert doc["c"] == "Red"


def test_alias():
    doc = honest.parse("""
type
  Uri = String;
var link : Uri = 'https://example.com';
""")
    assert doc["link"] == "https://example.com"


def test_sequence():
    doc = honest.parse("""
type
  Numbers = sequence of Integer;
var nums : Numbers = (1; 2; 3);
""")
    seq = doc["nums"]  # sequence → stays wrapped
    assert len(seq.elements) == 3
    assert seq[0] == 1
    assert seq[2] == 3


def test_nested_record():
    doc = honest.parse("""
type
  Inner = record
    x : Integer;
  end;
  Outer = record
    name : String;
    data : Inner;
  end;
var obj : Outer = (
  name : 'test';
  data : (x : 42);
);
""")
    assert doc["obj"]["name"] == "test"
    assert doc["obj"]["data"]["x"] == 42


def test_variant_record():
    doc = honest.parse("""
type
  Kind = (A, B);
  Tagged = record
    case kind : Kind of
      A : (x : Integer);
      B : (s : String);
  end;
var t : Tagged = (kind : A; x : 10);
""")
    val = doc["t"]
    assert val.tag_value == "A"
    assert val.variant_fields[0].value.integer == 10


def test_boolean_values():
    doc = honest.parse("""
type
  Flags = record
    active : Boolean;
    hidden : Boolean;
  end;
var f : Flags = (active : True; hidden : False);
""")
    assert doc["f"]["active"] is True
    assert doc["f"]["hidden"] is False


def test_string_escapes():
    doc = honest.parse(r"""
type
  S = String;
var s : S = 'it''s a \"test\"\n';
""")
    val = doc["s"]
    assert "'" in val
    assert isinstance(val, str)


def test_negative_integer():
    doc = honest.parse("""
type
  N = Integer;
var n : N = -42;
""")
    assert doc["n"] == -42


def test_real_with_exponent():
    doc = honest.parse("""
type
  R = Real;
var r : R = 1.5e10;
""")
    assert doc["r"] == 1.5e10


def test_declarations_only():
    doc = honest.parse("type\n  Foo = Integer;\n")
    assert len(doc.types) == 1
    assert len(doc.vars) == 0


def test_multiple_vars():
    doc = honest.parse("""
type
  N = Integer;
var a : N = 1;
var b : N = 2;
var c : N = 3;
""")
    assert len(doc.vars) == 3
    assert doc["a"] == 1
    assert doc["c"] == 3


def test_sequence_of_records():
    doc = honest.parse("""
type
  Item = record
    name : String;
    qty  : Integer;
  end;
  Items = sequence of Item;
var items : Items = (
  (name : 'widget'; qty : 5);
  (name : 'gadget'; qty : 3);
);
""")
    seq = doc["items"]
    assert len(seq.elements) == 2
    assert seq[0]["name"] == "widget"
    assert seq[1]["qty"] == 3


# ---- Error Cases ----

def test_bom_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("\ufefftype\n  X = Integer;\nvar x : X = 1;\n")


def test_forward_reference_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("""
type
  A = record
    b : B;
  end;
  B = Integer;
var a : A = (b : 1);
""")


def test_duplicate_type_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("""
type
  X = Integer;
  X = String;
var x : X = 1;
""")


def test_primitive_name_shadowing_is_valid_but_warned():
    source = "type Integer = String; var value : Integer = 'ok';"
    with pytest.warns(UserWarning, match="shadows a primitive type"):
        doc = honest.parse(source)
    assert doc["value"] == "ok"


def test_invalid_enum_value():
    with pytest.raises(honest.ParseError):
        honest.parse("""
type
  Color = (Red, Green, Blue);
var c : Color = Yellow;
""")


def test_duplicate_enum_value_rejected():
    with pytest.raises(honest.ParseError, match="duplicate enumeration value"):
        honest.parse("type E = (A, A); var e : E = A;")


def test_missing_type_keyword():
    with pytest.raises(honest.ParseError):
        honest.parse("var x : Integer = 1;\n")


def test_cyclic_alias_depth_limit():
    """Z7-F1: alias resolve() raises ValueError on cyclic/deep chains."""
    from honest.types import HonType, TypeKind
    a = HonType(kind=TypeKind.ALIAS, name="A")
    b = HonType(kind=TypeKind.ALIAS, name="B", target=a)
    a.target = b  # create cycle: A -> B -> A -> ...
    with pytest.raises(ValueError, match="alias chain exceeds maximum depth"):
        a.resolve()


def test_control_char_in_string_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("type\n  S = String;\nvar s : S = 'hello\x01world';\n")


def test_control_char_outside_string_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("type\n  S = String;\n\x02var s : S = 'hello';\n")


# ---- Round-Trip ----

def test_roundtrip_simple():
    src = """type
  Color = (Red, Green, Blue);
  Info = record
    name : String;
    color : Color;
  end;
var item : Info = (
  name : 'test';
  color : Blue;
);
"""
    doc = honest.parse(src)
    output = honest.serialise(doc)
    doc2 = honest.parse(output)
    assert len(doc2.types) == len(doc.types)
    assert len(doc2.vars) == len(doc.vars)
    assert doc2["item"]["name"] == "test"
    assert doc2["item"]["color"] == "Blue"


def test_roundtrip_wire():
    src = """type
  N = Integer;
var n : N = 42;
"""
    doc = honest.parse(src)
    wire = honest.serialise(doc, wire=True)
    doc2 = honest.parse(wire)
    assert doc2["n"] == 42


# ---- File Parsing ----

def test_parse_file(tmp_path):
    p = tmp_path / "test.honest"
    p.write_text("type\n  X = Integer;\nvar x : X = 99;\n")
    doc = honest.parse_file(str(p))
    assert doc["x"] == 99


# ---- Document Access ----

def test_document_get():
    doc = honest.parse("type\n  N = Integer;\nvar x : N = 1;\n")
    assert doc.get("x") == 1
    assert doc.get("nonexistent") is None


def test_value_get():
    doc = honest.parse("""
type
  R = record
    a : Integer;
    b : String;
  end;
var r : R = (a : 1; b : 'hello');
""")
    val = doc["r"]
    assert val.get("a") == 1
    assert val.get("missing") is None


# ---- Auto-Unwrap ----

def test_unwrap_leaf_types():
    """Leaf values auto-unwrap to native Python types."""
    doc = honest.parse("""
type
  R = record
    s : String;
    i : Integer;
    r : Real;
    b : Boolean;
    c : Char;
  end;
  Color = (Red, Green, Blue);
var rec : R = (s : 'hello'; i : 42; r : 3.14; b : True; c : 'X');
var col : Color = Green;
""")
    rec = doc["rec"]
    assert rec["s"] == "hello"
    assert isinstance(rec["s"], str)
    assert rec["i"] == 42
    assert isinstance(rec["i"], int)
    assert abs(rec["r"] - 3.14) < 0.001
    assert isinstance(rec["r"], float)
    assert rec["b"] is True
    assert isinstance(rec["b"], bool)
    assert rec["c"] == "X"
    assert doc["col"] == "Green"
    assert isinstance(doc["col"], str)


def test_attribute_access():
    """Record fields accessible via attribute syntax."""
    doc = honest.parse("""
type
  Task = record
    title : String;
    priority : Integer;
    done : Boolean;
  end;
var task : Task = (title : 'Build parser'; priority : 1; done : False);
""")
    task = doc["task"]
    assert task.title == "Build parser"
    assert task.priority == 1
    assert task.done is False
    with pytest.raises(AttributeError):
        _ = task.nonexistent


def test_unwrap_nested_navigation():
    """Records and sequences stay wrapped for chained navigation."""
    doc = honest.parse("""
type
  Inner = record
    value : String;
  end;
  Outer = record
    title : String;
    data : Inner;
  end;
var obj : Outer = (title : 'test'; data : (value : 'deep'));
""")
    assert doc["obj"]["title"] == "test"
    assert doc["obj"]["data"]["value"] == "deep"


# ---- Extract ----

def test_extract_from_code_fence():
    text = """Here is my evaluation:

```pascal
type
  Verdict = (Pass, Fail);
  Result = record
    verdict : Verdict;
    reasoning : String;
  end;
var result : Result = (
  verdict : Pass;
  reasoning : 'All criteria met';
);
```

The test passes because...
"""
    doc = honest.extract(text)
    assert doc["result"]["verdict"] == "Pass"
    assert doc["result"]["reasoning"] == "All criteria met"


def test_extract_from_plain_text():
    text = """I have evaluated the submission.

type
  Verdict = (Pass, Fail, Unknown);
  EvalResult = record
    verdict : Verdict;
    reasoning : String;
  end;
var result : EvalResult = (
  verdict : Fail;
  reasoning : 'Missing docs';
);

That is my assessment.
"""
    doc = honest.extract(text)
    assert doc["result"]["verdict"] == "Fail"


def test_extract_no_document():
    with pytest.raises(ValueError, match="No valid Honest document found"):
        honest.extract("This is just plain text with no Honest content.")


def test_sequence_trailing_semicolon():
    """Z7-F2: trailing semicolons in sequences are accepted (optional separator)."""
    # With trailing semicolon
    doc1 = honest.parse("""
type
  Items = sequence of Integer;
var items : Items = (1; 2; 3;);
""")
    seq1 = doc1["items"]
    assert len(seq1.elements) == 3
    assert seq1[0] == 1
    assert seq1[2] == 3

    # Without trailing semicolon
    doc2 = honest.parse("""
type
  Items = sequence of Integer;
var items : Items = (1; 2; 3);
""")
    seq2 = doc2["items"]
    assert len(seq2.elements) == 3
    assert seq2[0] == 1
    assert seq2[2] == 3


@pytest.mark.parametrize(
    "source",
    [
        "type X = Integer var x : X = 1;",
        "type R = record x : Integer y : Integer end; "
        "var r : R = (x : 1; y : 2);",
        "type R = record x : Integer; y : Integer; end; "
        "var r : R = (x : 1 y : 2);",
        "type K = (A, B); R = record case kind : K of "
        "A : () B : (); end; var r : R = (kind : B);",
        "type K = (A); R = record case kind : K of "
        "A : (x : Integer y : Integer); end; "
        "var r : R = (kind : A; x : 1; y : 2);",
        "type K = (A); R = record case kind : K of "
        "A : (x : Integer); end; var r : R = (kind : A x : 1);",
    ],
)
def test_required_semicolons(source):
    with pytest.raises(honest.ParseError):
        honest.parse(source)


def test_trailing_tokens_rejected():
    with pytest.raises(honest.ParseError):
        honest.parse("type X = Integer; var x : X = 1; trailing")


def test_extract_bare_fence():
    text = """```
type
  N = Integer;
var n : N = 42;
```"""
    doc = honest.extract(text)
    assert doc["n"] == 42
