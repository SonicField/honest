"""Honest serialiser — produces document-mode and wire-mode output."""

from __future__ import annotations

import math

from .types import (
    Document, HonType, HonValue, TypeKind, ValueKind, Primitive, Variable,
)


def _escape_string(s: str) -> str:
    """Escape a string for Honest output (single-quoted)."""
    result = []
    for ch in s:
        if ch == "'":
            result.append("''")
        elif ch == "\\":
            result.append("\\\\")
        elif ch == "\n":
            result.append("\\n")
        elif ch == "\r":
            result.append("\\r")
        elif ch == "\t":
            result.append("\\t")
        elif ord(ch) < 0x20:
            result.append(f"\\u{ord(ch):04X}")
        elif 0xD800 <= ord(ch) <= 0xDFFF:
            raise ValueError("String value must contain only Unicode scalars")
        else:
            result.append(ch)
    return "".join(result)


def _serialise_real(value: float | None) -> str:
    if value is None or not math.isfinite(value):
        raise ValueError("Real value must be finite")
    rendered = f"{value:.17g}"
    if not any(marker in rendered for marker in ".eE"):
        rendered += ".0"
    return rendered


def _serialise_char(codepoint: int | None) -> str:
    if (codepoint is None or isinstance(codepoint, bool) or
            codepoint < 0 or codepoint > 0x10FFFF or
            0xD800 <= codepoint <= 0xDFFF):
        raise ValueError("Char value must be a valid Unicode scalar")
    return f"'{_escape_string(chr(codepoint))}'"


def serialise(doc: Document, wire: bool = False) -> str:
    """Serialise a Document to Honest text.

    Args:
        doc: The document to serialise.
        wire: If True, produce wire mode (minimal whitespace, no comments).
    """
    if wire:
        return _serialise_wire(doc)
    return _serialise_document(doc)


def _serialise_document(doc: Document) -> str:
    parts = []
    for comment in doc.comments:
        parts.append(comment)
        parts.append("\n")
    parts.append("type\n")

    for i, t in enumerate(doc.types):
        if i > 0:
            parts.append("\n")
        _serialise_type(t, parts, indent=1)

    for v in doc.vars:
        parts.append(f"\nvar {v.name} : {v.type.name} = ")
        _serialise_value(v.value, parts, indent=0)
        parts.append(";\n")

    return "".join(parts)


def _serialise_wire(doc: Document) -> str:
    parts = []
    parts.append("type")

    for t in doc.types:
        parts.append(" ")
        _serialise_type_wire(t, parts)

    if doc.vars:
        parts.append(" ")

    for i, v in enumerate(doc.vars):
        if i > 0:
            parts.append(" ")
        parts.append(f"var {v.name} : {v.type.name} = ")
        _serialise_value_wire(v.value, parts)
        parts.append(";")

    return "".join(parts)


def _indent(parts: list[str], level: int):
    parts.append("  " * level)


def _serialise_type(t: HonType, parts: list[str], indent: int):
    _indent(parts, indent)

    if t.kind == TypeKind.ENUM:
        vals = ", ".join(t.values or [])
        parts.append(f"{t.name} = ({vals});\n")

    elif t.kind == TypeKind.RECORD:
        parts.append(f"{t.name} = record\n")
        for f in (t.fields or []):
            _indent(parts, indent + 1)
            parts.append(f"{f.name} : {f.type.name};\n")
        if t.has_variant and t.tag_name:
            _indent(parts, indent + 1)
            parts.append(f"case {t.tag_name} : {t.tag_type.name} of\n")
            for arm in (t.arms or []):
                _indent(parts, indent + 2)
                if arm.fields:
                    field_strs = "; ".join(f"{f.name} : {f.type.name}" for f in arm.fields)
                    parts.append(f"{arm.label} : ({field_strs});\n")
                else:
                    parts.append(f"{arm.label} : ();\n")
        _indent(parts, indent)
        parts.append("end;\n")

    elif t.kind == TypeKind.SEQUENCE:
        parts.append(f"{t.name} = sequence of {t.element_type.name};\n")

    elif t.kind == TypeKind.ALIAS:
        parts.append(f"{t.name} = {t.target.name};\n")


def _serialise_type_wire(t: HonType, parts: list[str]):
    if t.kind == TypeKind.ENUM:
        vals = ", ".join(t.values or [])
        parts.append(f"{t.name} = ({vals});")

    elif t.kind == TypeKind.RECORD:
        parts.append(f"{t.name} = record ")
        for f in (t.fields or []):
            parts.append(f"{f.name} : {f.type.name}; ")
        if t.has_variant and t.tag_name:
            parts.append(f"case {t.tag_name} : {t.tag_type.name} of ")
            for arm in (t.arms or []):
                if arm.fields:
                    field_strs = "; ".join(f"{f.name} : {f.type.name}" for f in arm.fields)
                    parts.append(f"{arm.label} : ({field_strs}); ")
                else:
                    parts.append(f"{arm.label} : (); ")
        parts.append("end;")

    elif t.kind == TypeKind.SEQUENCE:
        parts.append(f"{t.name} = sequence of {t.element_type.name};")

    elif t.kind == TypeKind.ALIAS:
        parts.append(f"{t.name} = {t.target.name};")


def _serialise_value(v: HonValue, parts: list[str], indent: int):
    if v.kind == ValueKind.BOOL:
        parts.append("True" if v.boolean else "False")
    elif v.kind == ValueKind.INT:
        parts.append(str(v.integer))
    elif v.kind == ValueKind.REAL:
        parts.append(_serialise_real(v.real))
    elif v.kind == ValueKind.CHAR:
        parts.append(_serialise_char(v.character))
    elif v.kind == ValueKind.STRING:
        parts.append(f"'{_escape_string(v.string)}'")
    elif v.kind == ValueKind.ENUM:
        parts.append(v.enum_val)
    elif v.kind == ValueKind.RECORD:
        parts.append("(\n")
        for i, fv in enumerate(v.fields or []):
            if i > 0:
                parts.append(";\n")
            _indent(parts, indent + 1)
            parts.append(f"{fv.name} : ")
            _serialise_value(fv.value, parts, indent + 1)
        if v.tag_value:
            resolved = v.type.resolve() if v.type else None
            tag_name = resolved.tag_name if resolved else "tag"
            if v.fields:
                parts.append(";\n")
            _indent(parts, indent + 1)
            parts.append(f"{tag_name} : {v.tag_value}")
            for fv in (v.variant_fields or []):
                parts.append(";\n")
                _indent(parts, indent + 1)
                parts.append(f"{fv.name} : ")
                _serialise_value(fv.value, parts, indent + 1)
        parts.append("\n")
        _indent(parts, indent)
        parts.append(")")
    elif v.kind == ValueKind.SEQUENCE:
        if not v.elements:
            parts.append("()")
        else:
            parts.append("(\n")
            for i, elem in enumerate(v.elements):
                if i > 0:
                    parts.append(";\n")
                _indent(parts, indent + 1)
                _serialise_value(elem, parts, indent + 1)
            parts.append("\n")
            _indent(parts, indent)
            parts.append(")")


def _serialise_value_wire(v: HonValue, parts: list[str]):
    if v.kind == ValueKind.BOOL:
        parts.append("True" if v.boolean else "False")
    elif v.kind == ValueKind.INT:
        parts.append(str(v.integer))
    elif v.kind == ValueKind.REAL:
        parts.append(_serialise_real(v.real))
    elif v.kind == ValueKind.CHAR:
        parts.append(_serialise_char(v.character))
    elif v.kind == ValueKind.STRING:
        parts.append(f"'{_escape_string(v.string)}'")
    elif v.kind == ValueKind.ENUM:
        parts.append(v.enum_val)
    elif v.kind == ValueKind.RECORD:
        parts.append("( ")
        for i, fv in enumerate(v.fields or []):
            if i > 0:
                parts.append("; ")
            parts.append(f"{fv.name} : ")
            _serialise_value_wire(fv.value, parts)
        if v.tag_value:
            resolved = v.type.resolve() if v.type else None
            tag_name = resolved.tag_name if resolved else "tag"
            if v.fields:
                parts.append("; ")
            parts.append(f"{tag_name} : {v.tag_value}")
            for fv in (v.variant_fields or []):
                parts.append("; ")
                parts.append(f"{fv.name} : ")
                _serialise_value_wire(fv.value, parts)
        parts.append("; )")
    elif v.kind == ValueKind.SEQUENCE:
        if not v.elements:
            parts.append("()")
        else:
            parts.append("(")
            for i, elem in enumerate(v.elements):
                if i > 0:
                    parts.append(";")
                parts.append(" ")
                _serialise_value_wire(elem, parts)
            parts.append(")")
