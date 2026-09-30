"""Honest lexer — tokenises Honest source text.

Single-pass tokeniser matching the C implementation's behaviour.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto


class TokenKind(Enum):
    # Structural
    LPAREN = auto()
    RPAREN = auto()
    COLON = auto()
    SEMICOLON = auto()
    EQUALS = auto()
    COMMA = auto()
    DOT = auto()

    # Keywords
    TYPE = auto()
    RECORD = auto()
    END = auto()
    CASE = auto()
    OF = auto()
    SEQUENCE = auto()
    VAR = auto()

    # Literals
    IDENT = auto()
    INT = auto()
    REAL = auto()
    STRING = auto()
    TRUE = auto()
    FALSE = auto()

    # Special
    COMMENT = auto()
    EOF = auto()
    ERROR = auto()


KEYWORDS = {
    "type": TokenKind.TYPE,
    "record": TokenKind.RECORD,
    "end": TokenKind.END,
    "case": TokenKind.CASE,
    "of": TokenKind.OF,
    "sequence": TokenKind.SEQUENCE,
    "var": TokenKind.VAR,
    "True": TokenKind.TRUE,
    "False": TokenKind.FALSE,
}


def _is_ascii_letter(ch: str) -> bool:
    return "A" <= ch <= "Z" or "a" <= ch <= "z"


def _is_ascii_digit(ch: str) -> bool:
    return "0" <= ch <= "9"


@dataclass
class Token:
    kind: TokenKind
    text: str
    line: int
    col: int


class Lexer:
    def __init__(self, src: str):
        # BOM check
        if src.startswith("\ufeff") or src[:3] == "\xef\xbb\xbf":
            self._error_token = Token(TokenKind.ERROR, "BOM", 1, 1)
            self._src = ""
            self._pos = 0
        else:
            self._error_token = None
            self._src = src
            self._pos = 0
        self._line = 1
        self._col = 1
        self._peeked: Token | None = None

    def _ch(self) -> str:
        if self._pos >= len(self._src):
            return ""
        return self._src[self._pos]

    def _advance(self) -> str:
        ch = self._src[self._pos]
        self._pos += 1
        if ch == "\n":
            self._line += 1
            self._col = 1
        else:
            self._col += 1
        return ch

    def _skip_whitespace(self):
        while self._pos < len(self._src) and self._src[self._pos] in " \t\r\n":
            self._advance()

    def _read_brace_comment(self, line: int, col: int) -> Token:
        """Read { ... } comment."""
        text = "{"
        while self._pos < len(self._src):
            ch = self._advance()
            text += ch
            if 0xD800 <= ord(ch) <= 0xDFFF:
                return Token(TokenKind.ERROR,
                             "non-scalar Unicode character", line, col)
            if ch == "}":
                return Token(TokenKind.COMMENT, text, line, col)
        return Token(TokenKind.ERROR, "unterminated { comment", line, col)

    def _read_paren_star_comment(self, line: int, col: int) -> Token:
        """Read (* ... *) comment."""
        text = "(*"
        while self._pos < len(self._src):
            ch = self._advance()
            text += ch
            if 0xD800 <= ord(ch) <= 0xDFFF:
                return Token(TokenKind.ERROR,
                             "non-scalar Unicode character", line, col)
            if ch == "*" and self._pos < len(self._src) and self._src[self._pos] == ")":
                text += self._advance()
                return Token(TokenKind.COMMENT, text, line, col)
        return Token(TokenKind.ERROR, "unterminated (* comment", line, col)

    def _read_string(self, line: int, col: int) -> Token:
        """Read a single-quoted string literal with escape handling."""
        result = []
        while self._pos < len(self._src):
            ch = self._advance()
            if ch == "'":
                # Pascal-style '' escape
                if self._pos < len(self._src) and self._src[self._pos] == "'":
                    self._advance()
                    result.append("'")
                else:
                    return Token(TokenKind.STRING, "".join(result), line, col)
            elif ch == "\\":
                if self._pos < len(self._src):
                    esc = self._advance()
                    if esc == "n":
                        result.append("\n")
                    elif esc == "r":
                        result.append("\r")
                    elif esc == "t":
                        result.append("\t")
                    elif esc == "\\":
                        result.append("\\")
                    elif esc == "u":
                        # \uXXXX
                        hex_str = self._src[self._pos:self._pos + 4]
                        if len(hex_str) != 4 or any(
                                ch not in "0123456789abcdefABCDEF" for ch in hex_str):
                            return Token(TokenKind.ERROR, "invalid \\u escape", line, col)
                        cp = int(hex_str, 16)
                        if 0xD800 <= cp <= 0xDFFF:
                            return Token(TokenKind.ERROR,
                                         "Unicode escape is not a scalar", line, col)
                        result.append(chr(cp))
                        for _ in range(4):
                            self._advance()
                    elif esc == "U":
                        # \UXXXXXXXX
                        hex_str = self._src[self._pos:self._pos + 8]
                        if len(hex_str) != 8 or any(
                                ch not in "0123456789abcdefABCDEF" for ch in hex_str):
                            return Token(TokenKind.ERROR, "invalid \\U escape", line, col)
                        cp = int(hex_str, 16)
                        if cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF:
                            return Token(TokenKind.ERROR,
                                         "Unicode escape is not a scalar", line, col)
                        result.append(chr(cp))
                        for _ in range(8):
                            self._advance()
                    else:
                        result.append("\\" + esc)
                else:
                    result.append("\\")
            elif 0xD800 <= ord(ch) <= 0xDFFF:
                return Token(TokenKind.ERROR, "non-scalar Unicode character", line, col)
            elif ord(ch) < 0x20 and ch != '\t':
                return Token(TokenKind.ERROR,
                             f"control character U+{ord(ch):04X} in string", line, col)
            else:
                result.append(ch)
        return Token(TokenKind.ERROR, "unterminated string", line, col)

    def _read_number(self, line: int, col: int) -> Token:
        """Read an integer or real literal."""
        start = self._pos - 1  # already consumed first char
        is_real = False
        has_fraction = False

        while self._pos < len(self._src) and _is_ascii_digit(self._src[self._pos]):
            self._advance()

        # Decimal point
        if (self._pos < len(self._src) and self._src[self._pos] == "." and
                self._pos + 1 < len(self._src) and _is_ascii_digit(self._src[self._pos + 1])):
            is_real = True
            has_fraction = True
            self._advance()  # consume '.'
            while self._pos < len(self._src) and _is_ascii_digit(self._src[self._pos]):
                self._advance()

        # Exponent
        if self._pos < len(self._src) and self._src[self._pos] in "eE":
            is_real = True
            self._advance()
            if self._pos < len(self._src) and self._src[self._pos] in "+-":
                self._advance()
            if self._pos >= len(self._src) or not _is_ascii_digit(self._src[self._pos]):
                text = self._src[start:self._pos]
                return Token(TokenKind.ERROR, text, line, col)
            while self._pos < len(self._src) and _is_ascii_digit(self._src[self._pos]):
                self._advance()
            if not has_fraction:
                text = self._src[start:self._pos]
                return Token(TokenKind.ERROR, text, line, col)

        text = self._src[start:self._pos]
        return Token(TokenKind.REAL if is_real else TokenKind.INT, text, line, col)

    def _read_ident(self, line: int, col: int) -> Token:
        """Read an identifier or keyword."""
        start = self._pos - 1
        while self._pos < len(self._src) and (
                _is_ascii_letter(self._src[self._pos]) or
                _is_ascii_digit(self._src[self._pos]) or
                self._src[self._pos] == "_"):
            self._advance()

        text = self._src[start:self._pos]

        # Check for keywords (case-sensitive)
        kind = KEYWORDS.get(text, TokenKind.IDENT)
        return Token(kind, text, line, col)

    def _next_token(self) -> Token:
        if self._error_token is not None:
            t = self._error_token
            self._error_token = None
            return t

        self._skip_whitespace()

        if self._pos >= len(self._src):
            return Token(TokenKind.EOF, "", self._line, self._col)

        line, col = self._line, self._col
        ch = self._advance()

        if 0xD800 <= ord(ch) <= 0xDFFF:
            return Token(TokenKind.ERROR, "non-scalar Unicode character", line, col)

        # Single-character tokens
        if ch == "(":
            # Check for (* comment
            if self._pos < len(self._src) and self._src[self._pos] == "*":
                self._advance()
                return self._read_paren_star_comment(line, col)
            return Token(TokenKind.LPAREN, "(", line, col)
        if ch == ")":
            return Token(TokenKind.RPAREN, ")", line, col)
        if ch == ":":
            return Token(TokenKind.COLON, ":", line, col)
        if ch == ";":
            return Token(TokenKind.SEMICOLON, ";", line, col)
        if ch == "=":
            return Token(TokenKind.EQUALS, "=", line, col)
        if ch == ",":
            return Token(TokenKind.COMMA, ",", line, col)
        if ch == ".":
            return Token(TokenKind.DOT, ".", line, col)
        if ch == "{":
            return self._read_brace_comment(line, col)
        if ch == "'":
            return self._read_string(line, col)

        # Numbers
        if _is_ascii_digit(ch):
            return self._read_number(line, col)
        if (ch == "-" and self._pos < len(self._src) and
                _is_ascii_digit(self._src[self._pos])):
            return self._read_number(line, col)

        # Identifiers and keywords
        if _is_ascii_letter(ch):
            return self._read_ident(line, col)

        # Reject control characters
        if ord(ch) < 0x20 and ch not in "\t\n\r":
            return Token(TokenKind.ERROR, f"control character U+{ord(ch):04X}", line, col)

        return Token(TokenKind.ERROR, ch, line, col)

    def next(self) -> Token:
        """Return the next token, consuming it."""
        if self._peeked is not None:
            t = self._peeked
            self._peeked = None
            return t
        return self._next_token()

    def peek(self) -> Token:
        """Peek at the next token without consuming it."""
        if self._peeked is None:
            self._peeked = self._next_token()
        return self._peeked

    def next_skip_comments(self) -> Token:
        """Return the next non-comment token."""
        while True:
            t = self.next()
            if t.kind != TokenKind.COMMENT:
                return t
