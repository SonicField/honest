"""Honest — Python implementation of the Honest interchange format.

Usage:
    import honest

    doc = honest.parse(text)
    doc = honest.parse_file("message.honest")

    # Access variables
    value = doc["result"]
    name = value["name"].string

    # Serialise
    text = honest.serialise(doc)
    wire = honest.serialise(doc, wire=True)
"""

from .parser import parse, parse_file, ParseError
from .serialise import serialise
from .types import (
    Document, Field, FieldValue, HonType, HonValue,
    Primitive, TypeKind, ValueKind, Variable, Variant,
)

__all__ = [
    "parse", "parse_file", "serialise", "ParseError",
    "Document", "Field", "FieldValue", "HonType", "HonValue",
    "Primitive", "TypeKind", "ValueKind", "Variable", "Variant",
]

__version__ = "0.1.0"
