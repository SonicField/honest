"""Scalar serialisation must always produce parseable Honest text."""

import math

import pytest

import honest


@pytest.mark.parametrize(
    "character", ["'", "\\", "\x00", "\x01", "\n", "\r", "\t", "\x1f", "é", "🚀"]
)
@pytest.mark.parametrize("wire", [False, True])
def test_char_serialisation_round_trip(character, wire):
    doc = honest.parse("type C = Char; var c : C = 'X';")
    doc.vars[0].value.character = ord(character)
    output = honest.serialise(doc, wire=wire)
    assert honest.parse(output)["c"] == character


@pytest.mark.parametrize("scalar", [-1, 0xD800, 0x110000])
def test_invalid_char_scalar_is_not_serialised(scalar):
    doc = honest.parse("type C = Char; var c : C = 'X';")
    doc.vars[0].value.character = scalar
    with pytest.raises(ValueError):
        honest.serialise(doc)


@pytest.mark.parametrize("wire", [False, True])
def test_string_control_scalars_round_trip(wire):
    doc = honest.parse("type S = String; var s : S = '\\u0000\\u001F';")
    output = honest.serialise(doc, wire=wire)
    assert "\\u0000\\u001F" in output
    assert honest.parse(output)["s"] == "\x00\x1f"


@pytest.mark.parametrize("value", [math.inf, -math.inf, math.nan])
def test_non_finite_real_is_not_serialised(value):
    doc = honest.parse("type R = Real; var r : R = 1.0;")
    doc.vars[0].value.real = value
    with pytest.raises(ValueError):
        honest.serialise(doc)
