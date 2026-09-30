"""Record and variant schema conformance tests for Python Honest."""

import pytest

import honest


def test_fixed_record_schema_is_exact():
    valid = (
        "type R = record x : Integer; y : String; end; "
        "var r : R = (x : 7; y : 'ok');"
    )
    assert honest.parse(valid)["r"]["y"] == "ok"

    invalid = [
        "type R = record x : Integer; y : String; end; var r : R = (wrong : 7; y : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7);",
        "type R = record x : Integer; y : String; end; var r : R = (y : 'ok'; x : 7);",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7; x : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 'bad'; y : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7; y : 'ok'; z : 1);",
    ]
    for source in invalid:
        with pytest.raises(honest.ParseError):
            honest.parse(source)


def test_variant_definition_validation():
    valid = [
        "type K = (A, B); Alias = K; R = record case kind : Alias of A : (); B : (); end; var r : R = (kind : A);",
        "type Flag = Boolean; R = record case present : Flag of True : (); False : (); end; var r : R = (present : False);",
    ]
    for source in valid:
        honest.parse(source)

    invalid = [
        "type K = (A); R = record case kind : K of end; var r : R = (kind : A);",
        "type R = record case kind : Integer of A : (); end; var r : R = (kind : 1);",
        "type R = record case kind : String of A : (); end; var r : R = (kind : A);",
        "type K = record end; R = record case kind : K of A : (); end; var r : R = (kind : A);",
        "type K = sequence of Integer; R = record case kind : K of A : (); end; var r : R = (kind : A);",
        "type K = (A, B); R = record case kind : K of Z : (); end; var r : R = (kind : A);",
        "type K = (A, B); R = record case kind : K of A : (); A : (); end; var r : R = (kind : A);",
        "type K = (A); R = record case kind : K of A : (x : Integer; x : Integer); end; var r : R = (kind : A; x : 1; x : 2);",
    ]
    for source in invalid:
        with pytest.raises(honest.ParseError):
            honest.parse(source)


def test_missing_variant_arm_warns_but_remains_valid():
    source = (
        "type K = (A, B); R = record case kind : K of A : (); end; "
        "var r : R = (kind : B);"
    )
    with pytest.warns(UserWarning, match="no variant arm"):
        doc = honest.parse(source)
    assert doc["r"].tag_value == "B"


def test_variant_value_schema_is_exact():
    prefix = (
        "type K = (A, B); R = record fixed : Integer; "
        "case kind : K of A : (x : Integer); B : (); end; "
    )
    assert honest.parse(
        prefix + "var r : R = (fixed : 1; kind : A; x : 2);"
    )["r"]["x"] == 2
    honest.parse(prefix + "var r : R = (fixed : 1; kind : B);")

    invalid_values = [
        "wrong : 1; kind : A; x : 2",
        "fixed : 1; wrong : A; x : 2",
        "fixed : 1; kind : Z",
        "fixed : 1; kind : A",
        "fixed : 1; kind : A; wrong : 2",
        "fixed : 1; kind : B; x : 2",
        "fixed : 1; fixed : 2; kind : A; x : 3",
    ]
    for value in invalid_values:
        with pytest.raises(honest.ParseError):
            honest.parse(prefix + f"var r : R = ({value});")
