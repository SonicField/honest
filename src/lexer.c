/*
 * Honest lexer — tokenises Honest source text.
 *
 * Single-pass, no backtracking. Produces tokens for the parser.
 * Comments are emitted as HON_TOK_COMMENT (parser can skip or preserve).
 */

#include "honest.h"
#include <string.h>
#include <ctype.h>

struct hon_lexer {
    const char *src;
    size_t      len;
    size_t      pos;
    uint32_t    line;
    uint32_t    col;
    hon_arena  *arena;
    bool        has_peek;
    hon_token   peeked;
};

hon_lexer *hon_lexer_new(hon_arena *a, const char *src, size_t len) {
    hon_lexer *lex = hon_arena_alloc(a, sizeof(hon_lexer), _Alignof(hon_lexer));
    if (!lex) return NULL;
    lex->src      = src;
    lex->len      = len;
    lex->pos      = 0;
    lex->line     = 1;
    lex->col      = 1;
    lex->arena    = a;
    lex->has_peek = false;
    return lex;
}

static inline bool at_end(const hon_lexer *lex) {
    return lex->pos >= lex->len;
}

static inline char peek_char(const hon_lexer *lex) {
    if (at_end(lex)) return '\0';
    return lex->src[lex->pos];
}

static inline char peek_char_at(const hon_lexer *lex, size_t offset) {
    size_t p = lex->pos + offset;
    if (p >= lex->len) return '\0';
    return lex->src[p];
}

static inline char advance(hon_lexer *lex) {
    if (at_end(lex)) return '\0';
    char c = lex->src[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static hon_token make_token(hon_tok_kind kind, const char *start, size_t len,
                            uint32_t line, uint32_t col) {
    return (hon_token){
        .kind = kind,
        .text = {start, len},
        .line = line,
        .col  = col,
    };
}

static void skip_whitespace(hon_lexer *lex) {
    while (!at_end(lex)) {
        char c = peek_char(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else {
            break;
        }
    }
}

static hon_token lex_brace_comment(hon_lexer *lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col  = lex->col;
    const char *start   = lex->src + lex->pos;

    advance(lex); /* skip { */
    while (!at_end(lex) && peek_char(lex) != '}') {
        advance(lex);
    }
    if (!at_end(lex)) {
        advance(lex); /* skip } */
    } else {
        size_t len = (size_t)(lex->src + lex->pos - start);
        return make_token(HON_TOK_ERROR, start, len, start_line, start_col);
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(HON_TOK_COMMENT, start, len, start_line, start_col);
}

static hon_token lex_paren_star_comment(hon_lexer *lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col  = lex->col;
    const char *start   = lex->src + lex->pos;

    advance(lex); /* skip ( */
    advance(lex); /* skip * */

    bool terminated = false;
    while (!at_end(lex)) {
        if (peek_char(lex) == '*' && peek_char_at(lex, 1) == ')') {
            advance(lex); /* skip * */
            advance(lex); /* skip ) */
            terminated = true;
            break;
        }
        advance(lex);
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(terminated ? HON_TOK_COMMENT : HON_TOK_ERROR,
                      start, len, start_line, start_col);
}

static hon_token lex_string(hon_lexer *lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col  = lex->col;
    const char *start   = lex->src + lex->pos;

    advance(lex); /* skip opening ' */

    bool terminated = false;
    while (!at_end(lex)) {
        char c = peek_char(lex);
        if (c == '\'') {
            if (peek_char_at(lex, 1) == '\'') {
                advance(lex); /* first ' */
                advance(lex); /* second ' */
                continue;
            }
            advance(lex); /* closing ' */
            terminated = true;
            break;
        }
        if (c == '\\') {
            advance(lex); /* skip \ */
            if (!at_end(lex)) advance(lex); /* skip escaped char */
            continue;
        }
        /* Reject control characters inside strings (except tab) */
        if ((unsigned char)c < 0x20 && c != '\t') {
            size_t len = (size_t)(lex->src + lex->pos - start);
            return make_token(HON_TOK_ERROR, start, len, start_line, start_col);
        }
        advance(lex);
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(terminated ? HON_TOK_STRING : HON_TOK_ERROR,
                      start, len, start_line, start_col);
}

static hon_token lex_number(hon_lexer *lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col  = lex->col;
    const char *start   = lex->src + lex->pos;
    hon_tok_kind kind    = HON_TOK_INT;

    if (peek_char(lex) == '-') advance(lex);

    while (!at_end(lex) && isdigit((unsigned char)peek_char(lex))) {
        advance(lex);
    }

    /* Check for real literal */
    if (peek_char(lex) == '.' && isdigit((unsigned char)peek_char_at(lex, 1))) {
        kind = HON_TOK_REAL;
        advance(lex); /* skip . */
        while (!at_end(lex) && isdigit((unsigned char)peek_char(lex))) {
            advance(lex);
        }
    }

    /* Check for exponent */
    if (peek_char(lex) == 'e' || peek_char(lex) == 'E') {
        kind = HON_TOK_REAL;
        advance(lex);
        if (peek_char(lex) == '+' || peek_char(lex) == '-') advance(lex);
        while (!at_end(lex) && isdigit((unsigned char)peek_char(lex))) {
            advance(lex);
        }
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(kind, start, len, start_line, start_col);
}

/* Keyword lookup table */
typedef struct {
    const char   *text;
    size_t        len;
    hon_tok_kind  kind;
} keyword_entry;

static const keyword_entry keywords[] = {
    {"type",     4, HON_TOK_TYPE},
    {"record",   6, HON_TOK_RECORD},
    {"end",      3, HON_TOK_END},
    {"case",     4, HON_TOK_CASE},
    {"of",       2, HON_TOK_OF},
    {"sequence", 8, HON_TOK_SEQUENCE},
    {"var",      3, HON_TOK_VAR},
    {"True",     4, HON_TOK_TRUE},
    {"False",    5, HON_TOK_FALSE},
    {NULL, 0, HON_TOK_IDENT},
};

static hon_tok_kind classify_ident(const char *text, size_t len) {
    for (const keyword_entry *kw = keywords; kw->text; kw++) {
        if (kw->len == len && memcmp(kw->text, text, len) == 0) {
            return kw->kind;
        }
    }
    return HON_TOK_IDENT;
}

static hon_token lex_identifier(hon_lexer *lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col  = lex->col;
    const char *start   = lex->src + lex->pos;

    while (!at_end(lex)) {
        char c = peek_char(lex);
        if (isalpha((unsigned char)c) || isdigit((unsigned char)c) || c == '_') {
            advance(lex);
        } else {
            break;
        }
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    hon_tok_kind kind = classify_ident(start, len);
    return make_token(kind, start, len, start_line, start_col);
}

hon_token hon_lexer_next(hon_lexer *lex) {
    if (lex->has_peek) {
        lex->has_peek = false;
        return lex->peeked;
    }

    for (;;) {
        skip_whitespace(lex);
        if (at_end(lex)) {
            return make_token(HON_TOK_EOF, lex->src + lex->pos, 0,
                              lex->line, lex->col);
        }

        uint32_t line = lex->line;
        uint32_t col  = lex->col;
        char c = peek_char(lex);

        /* Comments */
        if (c == '{') {
            return lex_brace_comment(lex);
        }
        if (c == '(' && peek_char_at(lex, 1) == '*') {
            return lex_paren_star_comment(lex);
        }

        /* Single-char tokens */
        switch (c) {
        case '(':
            advance(lex);
            return make_token(HON_TOK_LPAREN, lex->src + lex->pos - 1, 1, line, col);
        case ')':
            advance(lex);
            return make_token(HON_TOK_RPAREN, lex->src + lex->pos - 1, 1, line, col);
        case ':':
            advance(lex);
            return make_token(HON_TOK_COLON, lex->src + lex->pos - 1, 1, line, col);
        case ';':
            advance(lex);
            return make_token(HON_TOK_SEMICOLON, lex->src + lex->pos - 1, 1, line, col);
        case '=':
            advance(lex);
            return make_token(HON_TOK_EQUALS, lex->src + lex->pos - 1, 1, line, col);
        case ',':
            advance(lex);
            return make_token(HON_TOK_COMMA, lex->src + lex->pos - 1, 1, line, col);
        case '.':
            advance(lex);
            return make_token(HON_TOK_DOT, lex->src + lex->pos - 1, 1, line, col);
        default:
            break;
        }

        /* String literal */
        if (c == '\'') {
            return lex_string(lex);
        }

        /* Number literal (including negative) */
        if (isdigit((unsigned char)c) ||
            (c == '-' && isdigit((unsigned char)peek_char_at(lex, 1)))) {
            return lex_number(lex);
        }

        /* Identifier or keyword */
        if (isalpha((unsigned char)c)) {
            return lex_identifier(lex);
        }

        /* Unknown character */
        advance(lex);
        return make_token(HON_TOK_ERROR, lex->src + lex->pos - 1, 1, line, col);
    }
}

hon_token hon_lexer_peek(hon_lexer *lex) {
    if (!lex->has_peek) {
        lex->peeked  = hon_lexer_next(lex);
        lex->has_peek = true;
    }
    return lex->peeked;
}

const char *hon_tok_kind_name(hon_tok_kind kind) {
    static const char *names[] = {
        [HON_TOK_LPAREN]    = "(",
        [HON_TOK_RPAREN]    = ")",
        [HON_TOK_COLON]     = ":",
        [HON_TOK_SEMICOLON] = ";",
        [HON_TOK_EQUALS]    = "=",
        [HON_TOK_COMMA]     = ",",
        [HON_TOK_DOT]       = ".",
        [HON_TOK_TYPE]      = "type",
        [HON_TOK_RECORD]    = "record",
        [HON_TOK_END]       = "end",
        [HON_TOK_CASE]      = "case",
        [HON_TOK_OF]        = "of",
        [HON_TOK_SEQUENCE]  = "sequence",
        [HON_TOK_VAR]       = "var",
        [HON_TOK_IDENT]     = "identifier",
        [HON_TOK_INT]       = "integer",
        [HON_TOK_REAL]      = "real",
        [HON_TOK_STRING]    = "string",
        [HON_TOK_TRUE]      = "True",
        [HON_TOK_FALSE]     = "False",
        [HON_TOK_COMMENT]   = "comment",
        [HON_TOK_EOF]       = "EOF",
        [HON_TOK_ERROR]     = "error",
    };
    if (kind <= HON_TOK_ERROR) return names[kind];
    return "unknown";
}
