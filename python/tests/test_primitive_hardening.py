"""Lexical and primitive boundary tests for the Python implementation."""

import math

import pytest

import honest


def primitive_document(name: str, literal: str) -> str:
    return f"type\n  T = {name};\nvar value : T = {literal};\n"


@pytest.mark.parametrize(
    ("name", "literal", "expected"),
    [
        ("Byte", "0", 0),
        ("Byte", "255", 255),
        ("Word", "0", 0),
        ("Word", "65535", 65535),
        ("Integer", "-2147483648", -2147483648),
        ("Integer", "2147483647", 2147483647),
        ("LongInt", "-9223372036854775808", -9223372036854775808),
        ("LongInt", "9223372036854775807", 9223372036854775807),
    ],
)
def test_integer_boundaries_round_trip(name, literal, expected):
    doc = honest.parse(primitive_document(name, literal))
    assert doc["value"] == expected
    assert honest.parse(honest.serialise(doc))["value"] == expected


@pytest.mark.parametrize(
    ("name", "literal"),
    [
        ("Byte", "-1"),
        ("Byte", "256"),
        ("Word", "-1"),
        ("Word", "65536"),
        ("Integer", "-2147483649"),
        ("Integer", "2147483648"),
        ("LongInt", "-9223372036854775809"),
        ("LongInt", "9223372036854775808"),
        ("Integer", "1x"),
    ],
)
def test_integer_out_of_range_or_malformed_rejected(name, literal):
    with pytest.raises(honest.ParseError):
        honest.parse(primitive_document(name, literal))


@pytest.mark.parametrize(
    "literal",
    [
        "1", "1e", "1e+", "1e-", "1e2", "1.", ".1", "NaN", "inf", "-inf",
        "1.0e999", "-1.0e999", "1.0x",
    ],
)
def test_invalid_real_is_parse_error(literal):
    with pytest.raises(honest.ParseError):
        honest.parse(primitive_document("Real", literal))


@pytest.mark.parametrize(
    "literal", ["0.0", "-1.5", "1.0e+10", "1.7976931348623157e308"]
)
def test_finite_real_round_trip(literal):
    doc = honest.parse(primitive_document("Real", literal))
    value = doc["value"]
    assert math.isfinite(value)
    assert honest.parse(honest.serialise(doc))["value"] == value


@pytest.mark.parametrize(
    "source",
    [
        "type Café = Integer; var x : Café = 1;",
        "type R = record café : Integer; end; var r : R = (café : 1);",
        "type E = (Café); var e : E = Café;",
        "type T = Integer; var café : T = 1;",
        "type E = (A); R = record case café : E of A : (); end; var r : R = (café : A);",
        "type E = (A); R = record case kind : E of Café : (); end; var r : R = (kind : A);",
        "type _T = Integer; var x : Integer = 1;",
        "type T = Integer; var _x : T = 1;",
        "type 1T = Integer; var x : Integer = 1;",
        "type Bad-Name = Integer; var x : Integer = 1;",
        "type Bad Name = Integer; var x : Integer = 1;",
        "type type = Integer; var x : Integer = 1;",
        "type R = record type : Integer; end; var r : R = (type : 1);",
        "type E = (True); var e : E = True;",
        "type T = Integer; var var : T = 1;",
        "type E = (A); R = record case var : E of A : (); end; var r : R = (var : A);",
    ],
)
def test_non_ascii_or_malformed_identifiers_rejected(source):
    with pytest.raises(honest.ParseError):
        honest.parse(source)


def test_declarations_section_is_nonempty():
    for source in ("type", "type\n { comment only }"):
        with pytest.raises(honest.ParseError):
            honest.parse(source)


def test_declaration_only_rulebook_and_normal_document_remain_valid():
    rulebook = honest.parse("type N = Integer;")
    assert len(rulebook.types) == 1
    assert not rulebook.vars
    document = honest.parse("type N = Integer; var n : N = 1;")
    assert document["n"] == 1


@pytest.mark.parametrize(
    "source",
    [
        "type S = String; var value : S = '\\uZZZZ';",
        "type S = String; var value : S = '\\u12';",
        "type S = String; var value : S = '\\uD800';",
        "type S = String; var value : S = '\\U00110000';",
        "type C = Char; var value : C = '\\uD800';",
    ],
)
def test_malformed_or_non_scalar_unicode_escape_rejected(source):
    with pytest.raises(honest.ParseError):
        honest.parse(source)
