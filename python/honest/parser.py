"""Honest parser — single-pass, type-directed.

Parses declarations then data, using type context to disambiguate values.
"""

from __future__ import annotations

import math
import warnings

from .lexer import Lexer, Token, TokenKind
from .types import (
    Document, Field, FieldValue, HonType, HonValue, Primitive,
    PRIMITIVE_NAMES, TypeKind, ValueKind, Variable, Variant,
)

MAX_DEPTH = 64

INTEGER_RANGES = {
    Primitive.BYTE: (0, 255),
    Primitive.WORD: (0, 65535),
    Primitive.INTEGER: (-2147483648, 2147483647),
    Primitive.LONGINT: (-9223372036854775808, 9223372036854775807),
}


class ParseError(Exception):
    def __init__(self, message: str, line: int = 0, col: int = 0):
        self.line = line
        self.col = col
        super().__init__(f"{line}:{col}: {message}")


class Parser:
    def __init__(self, src: str, *, allow_trailing: bool = False):
        self._lex = Lexer(src)
        self._allow_trailing = allow_trailing
        self._types: dict[str, HonType] = {}
        self._type_list: list[HonType] = []
        self._comments: list[str] = []
        self._depth = 0

        # Register built-in primitives
        for name, prim in PRIMITIVE_NAMES.items():
            t = HonType(kind=TypeKind.PRIMITIVE, name=name, prim=prim)
            self._types[name] = t

    def _next(self) -> Token:
        t = self._lex.next()
        while t.kind == TokenKind.COMMENT:
            self._comments.append(t.text)
            t = self._lex.next()
        if t.kind == TokenKind.ERROR:
            raise ParseError(t.text, t.line, t.col)
        return t

    def _peek(self) -> Token:
        t = self._lex.peek()
        while t.kind == TokenKind.COMMENT:
            self._comments.append(self._lex.next().text)
            t = self._lex.peek()
        if t.kind == TokenKind.ERROR:
            raise ParseError(t.text, t.line, t.col)
        return t

    def _expect(self, kind: TokenKind, context: str = "") -> Token:
        t = self._next()
        if t.kind != kind:
            msg = f"expected {kind.name}"
            if context:
                msg += f" in {context}"
            msg += f", got {t.kind.name} '{t.text}'"
            raise ParseError(msg, t.line, t.col)
        return t

    def _error(self, msg: str, tok: Token) -> ParseError:
        return ParseError(msg, tok.line, tok.col)

    # ---- Type Declarations ----

    def _parse_enum(self, name: str) -> HonType:
        """Parse: (Value1, Value2, ...)"""
        self._expect(TokenKind.LPAREN, "enum")
        values = []
        while True:
            t = self._expect(TokenKind.IDENT, "enum value")
            if t.text in values:
                raise self._error(
                    f"duplicate enumeration value '{t.text}'", t)
            values.append(t.text)
            p = self._peek()
            if p.kind == TokenKind.COMMA:
                self._next()
            elif p.kind == TokenKind.RPAREN:
                self._next()
                break
            else:
                raise self._error("expected ',' or ')' in enum", p)

        return HonType(kind=TypeKind.ENUM, name=name, values=values)

    def _parse_fields(self) -> list[Field]:
        """Parse field declarations until 'end', 'case', or ')'."""
        fields = []
        while True:
            p = self._peek()
            if p.kind in (TokenKind.END, TokenKind.CASE):
                break
            if p.kind == TokenKind.RPAREN:
                break

            name_tok = self._expect(TokenKind.IDENT, "field name")
            self._expect(TokenKind.COLON, "field")
            type_tok = self._expect(TokenKind.IDENT, "field type")

            ftype = self._types.get(type_tok.text)
            if ftype is None:
                raise self._error(f"unknown type '{type_tok.text}'", type_tok)

            if any(field.name == name_tok.text for field in fields):
                raise self._error(
                    f"duplicate field name '{name_tok.text}'", name_tok)

            fields.append(Field(name=name_tok.text, type=ftype))

            p = self._peek()
            if p.kind == TokenKind.SEMICOLON:
                self._next()
            elif p.kind == TokenKind.IDENT:
                raise self._error("expected ';' between record fields", p)

        return fields

    def _parse_record(self, name: str) -> HonType:
        """Parse: record ... end"""
        fields = self._parse_fields()

        has_variant = False
        tag_name = None
        tag_type = None
        arms = None

        if self._peek().kind == TokenKind.CASE:
            self._next()  # consume 'case'
            has_variant = True

            tag_tok = self._expect(TokenKind.IDENT, "variant tag name")
            tag_name = tag_tok.text
            self._expect(TokenKind.COLON, "variant tag")
            type_tok = self._expect(TokenKind.IDENT, "variant tag type")
            tag_type = self._types.get(type_tok.text)
            if tag_type is None:
                raise self._error(f"unknown type '{type_tok.text}'", type_tok)
            resolved_tag = tag_type.resolve()
            enum_tag = resolved_tag.kind == TypeKind.ENUM
            bool_tag = (
                resolved_tag.kind == TypeKind.PRIMITIVE
                and resolved_tag.prim == Primitive.BOOLEAN
            )
            if not enum_tag and not bool_tag:
                raise self._error(
                    "variant discriminant must be an enumeration or Boolean",
                    type_tok,
                )

            self._expect(TokenKind.OF, "variant")

            arms = []
            while self._peek().kind != TokenKind.END:
                # Variant labels can be identifiers, True, or False
                label_tok = self._next()
                if label_tok.kind not in (TokenKind.IDENT, TokenKind.TRUE, TokenKind.FALSE):
                    raise self._error(f"expected variant label, got {label_tok.kind.name}", label_tok)
                if enum_tag:
                    if (label_tok.kind != TokenKind.IDENT or
                            label_tok.text not in (resolved_tag.values or [])):
                        raise self._error(
                            f"invalid variant arm label '{label_tok.text}'",
                            label_tok,
                        )
                elif label_tok.kind not in (TokenKind.TRUE, TokenKind.FALSE):
                    raise self._error(
                        f"invalid Boolean variant arm label '{label_tok.text}'",
                        label_tok,
                    )
                if any(arm.label == label_tok.text for arm in arms):
                    raise self._error(
                        f"duplicate variant arm '{label_tok.text}'", label_tok)
                self._expect(TokenKind.COLON, "variant arm")
                self._expect(TokenKind.LPAREN, "variant arm")

                arm_fields = self._parse_fields()
                self._expect(TokenKind.RPAREN, "variant arm")

                arms.append(Variant(label=label_tok.text, fields=arm_fields))

                self._expect(TokenKind.SEMICOLON, "variant arm")

            expected_labels = (
                set(resolved_tag.values or []) if enum_tag else {"True", "False"}
            )
            actual_labels = {arm.label for arm in arms}
            for missing in sorted(expected_labels - actual_labels):
                warnings.warn(
                    f"no variant arm for discriminant value '{missing}'",
                    UserWarning,
                    stacklevel=2,
                )

        self._expect(TokenKind.END, "record")
        return HonType(
            kind=TypeKind.RECORD, name=name, fields=fields,
            has_variant=has_variant, tag_name=tag_name,
            tag_type=tag_type, arms=arms,
        )

    def _parse_type_def(self) -> HonType:
        """Parse a single type definition: Name = ..."""
        name_tok = self._expect(TokenKind.IDENT, "type name")
        name = name_tok.text

        if name in self._types:
            raise self._error(f"duplicate type name '{name}'", name_tok)

        self._expect(TokenKind.EQUALS, "type definition")

        p = self._peek()

        if p.kind == TokenKind.LPAREN:
            # Enum
            typ = self._parse_enum(name)
        elif p.kind == TokenKind.RECORD:
            self._next()
            typ = self._parse_record(name)
        elif p.kind == TokenKind.SEQUENCE:
            self._next()
            self._expect(TokenKind.OF, "sequence")
            elem_tok = self._expect(TokenKind.IDENT, "sequence element type")
            elem_type = self._types.get(elem_tok.text)
            if elem_type is None:
                raise self._error(f"unknown type '{elem_tok.text}'", elem_tok)
            typ = HonType(kind=TypeKind.SEQUENCE, name=name, element_type=elem_type)
        elif p.kind == TokenKind.IDENT:
            # Alias
            self._next()
            target = self._types.get(p.text)
            if target is None:
                raise self._error(f"unknown type '{p.text}' (forward references not allowed)", p)
            typ = HonType(kind=TypeKind.ALIAS, name=name, target=target)
        else:
            raise self._error(f"expected type definition, got {p.kind.name}", p)

        self._types[name] = typ
        self._type_list.append(typ)
        return typ

    # ---- Values ----

    def _parse_value(self, typ: HonType) -> HonValue:
        self._depth += 1
        if self._depth > MAX_DEPTH:
            t = self._peek()
            raise self._error("nesting depth exceeded", t)
        try:
            return self._parse_value_inner(typ)
        finally:
            self._depth -= 1

    def _parse_value_inner(self, typ: HonType) -> HonValue:
        resolved = typ.resolve()

        if resolved.kind == TypeKind.PRIMITIVE:
            return self._parse_primitive_value(typ, resolved)
        elif resolved.kind == TypeKind.ENUM:
            return self._parse_enum_value(typ, resolved)
        elif resolved.kind == TypeKind.RECORD:
            return self._parse_record_value(typ, resolved)
        elif resolved.kind == TypeKind.SEQUENCE:
            return self._parse_sequence_value(typ, resolved)
        else:
            t = self._peek()
            raise self._error(f"cannot parse value for type kind {resolved.kind}", t)

    def _parse_primitive_value(self, typ: HonType, resolved: HonType) -> HonValue:
        t = self._next()
        prim = resolved.prim

        if prim == Primitive.BOOLEAN:
            if t.kind == TokenKind.TRUE:
                return HonValue(kind=ValueKind.BOOL, type=typ, boolean=True)
            elif t.kind == TokenKind.FALSE:
                return HonValue(kind=ValueKind.BOOL, type=typ, boolean=False)
            raise self._error("expected True or False", t)

        if prim in (Primitive.BYTE, Primitive.WORD, Primitive.INTEGER, Primitive.LONGINT):
            if t.kind != TokenKind.INT:
                raise self._error(f"expected integer, got {t.kind.name}", t)
            try:
                value = int(t.text)
            except ValueError as exc:
                raise self._error("invalid integer literal", t) from exc
            minimum, maximum = INTEGER_RANGES[prim]
            if value < minimum or value > maximum:
                raise self._error(
                    f"value {value} out of range for {resolved.name}", t)
            return HonValue(kind=ValueKind.INT, type=typ, integer=value)

        if prim == Primitive.REAL:
            if t.kind != TokenKind.REAL:
                raise self._error(f"expected real, got {t.kind.name}", t)
            try:
                value = float(t.text)
            except (ValueError, OverflowError) as exc:
                raise self._error("invalid real literal", t) from exc
            if not math.isfinite(value):
                raise self._error("real literal is out of range", t)
            return HonValue(kind=ValueKind.REAL, type=typ, real=value)

        if prim == Primitive.CHAR:
            if t.kind != TokenKind.STRING:
                raise self._error(f"expected char literal, got {t.kind.name}", t)
            if len(t.text) != 1:
                raise self._error(f"Char must be single character, got {len(t.text)}", t)
            return HonValue(kind=ValueKind.CHAR, type=typ, character=ord(t.text[0]))

        if prim == Primitive.STRING:
            if t.kind != TokenKind.STRING:
                raise self._error(f"expected string, got {t.kind.name}", t)
            return HonValue(kind=ValueKind.STRING, type=typ, string=t.text)

        raise self._error(f"unhandled primitive {prim}", t)

    def _parse_enum_value(self, typ: HonType, resolved: HonType) -> HonValue:
        t = self._expect(TokenKind.IDENT, "enum value")
        if resolved.values and t.text not in resolved.values:
            raise self._error(f"'{t.text}' is not a valid value for {resolved.name}", t)
        return HonValue(kind=ValueKind.ENUM, type=typ, enum_val=t.text)

    def _parse_record_value(self, typ: HonType, resolved: HonType) -> HonValue:
        self._expect(TokenKind.LPAREN, "record value")

        field_vals = []
        if resolved.fields:
            for i, f in enumerate(resolved.fields):
                if i > 0:
                    self._expect(TokenKind.SEMICOLON, "record value")

                name_tok = self._expect(TokenKind.IDENT, "field name")
                if name_tok.text != f.name:
                    raise self._error(
                        f"expected field '{f.name}', got '{name_tok.text}'", name_tok)
                self._expect(TokenKind.COLON, "field")
                val = self._parse_value(f.type)
                field_vals.append(FieldValue(name=f.name, value=val))

        tag_value = None
        variant_fields = []

        if resolved.has_variant and resolved.tag_name:
            if resolved.fields:
                self._expect(TokenKind.SEMICOLON, "variant tag")

            # Read tag field
            tag_name_tok = self._expect(TokenKind.IDENT, "variant tag")
            if tag_name_tok.text != resolved.tag_name:
                raise self._error(
                    f"expected tag '{resolved.tag_name}', got '{tag_name_tok.text}'",
                    tag_name_tok)
            self._expect(TokenKind.COLON, "variant tag")
            tag_tok = self._next()
            tag_type = resolved.tag_type.resolve()
            if tag_type.kind == TypeKind.ENUM:
                if (tag_tok.kind != TokenKind.IDENT or
                        tag_tok.text not in (tag_type.values or [])):
                    raise self._error("invalid enumeration variant tag", tag_tok)
                tag_value = tag_tok.text
            elif (tag_type.kind == TypeKind.PRIMITIVE and
                  tag_type.prim == Primitive.BOOLEAN):
                if tag_tok.kind not in (TokenKind.TRUE, TokenKind.FALSE):
                    raise self._error("invalid Boolean variant tag", tag_tok)
                tag_value = tag_tok.text
            else:
                raise self._error("invalid variant discriminant type", tag_tok)

            # Find matching arm
            if resolved.arms:
                arm = None
                for a in resolved.arms:
                    if a.label == tag_value:
                        arm = a
                        break

                if arm and arm.fields:
                    for af in arm.fields:
                        self._expect(TokenKind.SEMICOLON, "variant field")
                        fn_tok = self._expect(TokenKind.IDENT, "variant field")
                        if fn_tok.text != af.name:
                            raise self._error(
                                f"expected variant field '{af.name}', got '{fn_tok.text}'",
                                fn_tok,
                            )
                        self._expect(TokenKind.COLON, "variant field")
                        val = self._parse_value(af.type)
                        variant_fields.append(FieldValue(name=fn_tok.text, value=val))

        # Consume trailing semicolon before closing paren
        if self._peek().kind == TokenKind.SEMICOLON:
            self._next()

        self._expect(TokenKind.RPAREN, "record value")

        return HonValue(
            kind=ValueKind.RECORD, type=typ, fields=field_vals,
            tag_value=tag_value, variant_fields=variant_fields,
        )

    def _parse_sequence_value(self, typ: HonType, resolved: HonType) -> HonValue:
        self._expect(TokenKind.LPAREN, "sequence value")

        elements = []
        if self._peek().kind != TokenKind.RPAREN:
            elem_type = resolved.element_type
            while True:
                val = self._parse_value(elem_type)
                elements.append(val)
                p = self._peek()
                if p.kind == TokenKind.SEMICOLON:
                    self._next()
                    if self._peek().kind == TokenKind.RPAREN:
                        break
                elif p.kind == TokenKind.RPAREN:
                    break
                else:
                    raise self._error("expected ';' or ')' in sequence", p)

        self._expect(TokenKind.RPAREN, "sequence")
        return HonValue(kind=ValueKind.SEQUENCE, type=typ, elements=elements)

    # ---- Document ----

    def parse(self) -> Document:
        """Parse a complete Honest document."""
        # Check for BOM error from lexer
        p = self._peek()
        if p.kind == TokenKind.ERROR:
            raise self._error(p.text, p)

        # Declarations section
        tok = self._peek()
        if tok.kind != TokenKind.TYPE:
            raise self._error("expected 'type' keyword", tok)
        self._next()

        while self._peek().kind == TokenKind.IDENT:
            self._parse_type_def()
            self._expect(TokenKind.SEMICOLON, "type declaration")

        if not self._type_list:
            raise self._error(
                "declarations section requires at least one type", self._peek())

        # Data section (optional)
        variables = []
        while self._peek().kind == TokenKind.VAR:
            self._next()
            var_name_tok = self._expect(TokenKind.IDENT, "variable name")
            self._expect(TokenKind.COLON, "variable")
            type_tok = self._expect(TokenKind.IDENT, "variable type")

            var_type = self._types.get(type_tok.text)
            if var_type is None:
                raise self._error(f"unknown type '{type_tok.text}'", type_tok)

            self._expect(TokenKind.EQUALS, "variable")
            val = self._parse_value(var_type)

            variables.append(Variable(name=var_name_tok.text, type=var_type, value=val))

            self._expect(TokenKind.SEMICOLON, "variable")

        if not self._allow_trailing:
            trailing = self._peek()
            if trailing.kind != TokenKind.EOF:
                raise self._error("unexpected tokens after end of document", trailing)

        return Document(
            types=list(self._type_list), vars=variables,
            comments=list(self._comments),
        )


def parse(src: str) -> Document:
    """Parse an Honest document from a string."""
    return Parser(src).parse()


def parse_file(path: str) -> Document:
    """Parse an Honest document from a file."""
    with open(path, "r", encoding="utf-8") as f:
        return parse(f.read())
