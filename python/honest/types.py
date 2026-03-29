"""Honest type system and value representation.

Mirrors the C implementation's type/value model using Python dataclasses.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional


# ---- Primitive Types ----

class Primitive(Enum):
    BOOLEAN = auto()
    BYTE = auto()
    WORD = auto()
    INTEGER = auto()
    LONGINT = auto()
    REAL = auto()
    CHAR = auto()
    STRING = auto()


PRIMITIVE_NAMES = {
    "Boolean": Primitive.BOOLEAN,
    "Byte": Primitive.BYTE,
    "Word": Primitive.WORD,
    "Integer": Primitive.INTEGER,
    "LongInt": Primitive.LONGINT,
    "Real": Primitive.REAL,
    "Char": Primitive.CHAR,
    "String": Primitive.STRING,
}


# ---- Type Definitions ----

class TypeKind(Enum):
    PRIMITIVE = auto()
    ENUM = auto()
    RECORD = auto()
    SEQUENCE = auto()
    ALIAS = auto()


@dataclass
class Field:
    name: str
    type: HonType


@dataclass
class Variant:
    label: str
    fields: list[Field] = field(default_factory=list)


@dataclass
class HonType:
    kind: TypeKind
    name: str

    # PRIMITIVE
    prim: Optional[Primitive] = None

    # ENUM
    values: Optional[list[str]] = None

    # RECORD
    fields: Optional[list[Field]] = None
    has_variant: bool = False
    tag_name: Optional[str] = None
    tag_type: Optional[HonType] = None
    arms: Optional[list[Variant]] = None

    # SEQUENCE
    element_type: Optional[HonType] = None

    # ALIAS
    target: Optional[HonType] = None

    def resolve(self) -> HonType:
        """Follow alias chain to the underlying type."""
        t = self
        while t.kind == TypeKind.ALIAS and t.target is not None:
            t = t.target
        return t


# ---- Values ----

class ValueKind(Enum):
    BOOL = auto()
    INT = auto()
    REAL = auto()
    CHAR = auto()
    STRING = auto()
    ENUM = auto()
    RECORD = auto()
    SEQUENCE = auto()


@dataclass
class FieldValue:
    name: str
    value: HonValue


@dataclass
class HonValue:
    kind: ValueKind
    type: Optional[HonType] = None

    # Leaf values
    boolean: Optional[bool] = None
    integer: Optional[int] = None
    real: Optional[float] = None
    character: Optional[int] = None  # Unicode code point
    string: Optional[str] = None
    enum_val: Optional[str] = None

    # Record
    fields: Optional[list[FieldValue]] = None
    tag_value: Optional[str] = None
    variant_fields: Optional[list[FieldValue]] = None

    # Sequence
    elements: Optional[list[HonValue]] = None

    def unwrap(self):
        """Return native Python value for leaves, self for compounds."""
        if self.kind == ValueKind.BOOL:
            return self.boolean
        if self.kind == ValueKind.INT:
            return self.integer
        if self.kind == ValueKind.REAL:
            return self.real
        if self.kind == ValueKind.CHAR:
            return chr(self.character) if self.character is not None else None
        if self.kind == ValueKind.STRING:
            return self.string
        if self.kind == ValueKind.ENUM:
            return self.enum_val
        return self  # records and sequences stay wrapped

    def _lookup(self, key):
        """Find a child value by key (field name or sequence index)."""
        if self.kind == ValueKind.RECORD and self.fields:
            for fv in self.fields:
                if fv.name == key:
                    return fv.value
            if self.variant_fields:
                for fv in self.variant_fields:
                    if fv.name == key:
                        return fv.value
        if self.kind == ValueKind.SEQUENCE and self.elements:
            if isinstance(key, int):
                return self.elements[key]
        return None

    def __getitem__(self, key):
        """Dict-style access with auto-unwrapping of leaf values."""
        val = self._lookup(key)
        if val is None:
            raise KeyError(key)
        return val.unwrap()

    def get(self, key, default=None):
        val = self._lookup(key)
        if val is None:
            return default
        return val.unwrap()


# ---- Variable ----

@dataclass
class Variable:
    name: str
    type: HonType
    value: HonValue


# ---- Document ----

@dataclass
class Document:
    types: list[HonType] = field(default_factory=list)
    vars: list[Variable] = field(default_factory=list)

    def __getitem__(self, key):
        """Dict-style access for variables with auto-unwrapping."""
        for v in self.vars:
            if v.name == key:
                return v.value.unwrap()
        raise KeyError(key)

    def get(self, key, default=None):
        for v in self.vars:
            if v.name == key:
                return v.value.unwrap()
        return default

    def get_type(self, name: str) -> Optional[HonType]:
        for t in self.types:
            if t.name == name:
                return t
        return None
