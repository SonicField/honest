#define _POSIX_C_SOURCE 200809L

/*
 * Honest parser — parses declarations and data sections.
 *
 * Single-pass, type-directed. The parser builds the type registry
 * from declarations, then uses type context to disambiguate values
 * in the data section.
 */

#include "honest.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

/* ---- Parser State ---- */

typedef struct {
    hon_lexer     *lex;
    hon_arena     *arena;
    hon_diag_list *diags;
    /* Type registry — simple array, linear scan (sufficient for typical sizes) */
    hon_type     **types;
    uint32_t       type_count;
    uint32_t       type_cap;
    /* Variables */
    hon_variable  *vars;
    uint32_t       var_count;
    uint32_t       var_cap;
    /* Comments retained for document-mode serialisation. */
    hon_str        *comments;
    uint32_t       comment_count;
    uint32_t       comment_cap;
    /* Nesting depth tracking */
    uint32_t       depth;
    uint32_t       max_depth;
    /* Optional rulebook for divergence detection */
    const hon_registry *registry;
} parser;

#define MAX_DEPTH_DEFAULT 64

/* ---- Forward Declarations ---- */

static hon_type  *parse_type_definition(parser *p);
static hon_value *parse_value(parser *p, hon_type *type);

static int utf8_decode_one(const unsigned char *s, size_t len,
                           uint32_t *codepoint, size_t *used) {
    uint32_t cp;
    size_t width;

    if (len == 0) return -1;
    if (s[0] < 0x80) {
        cp = s[0];
        width = 1;
    } else if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        cp = s[0] & 0x1F;
        width = 2;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        cp = s[0] & 0x0F;
        width = 3;
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        cp = s[0] & 0x07;
        width = 4;
    } else {
        return -1;
    }

    if (width > len) return -1;
    for (size_t i = 1; i < width; i++) {
        if ((s[i] & 0xC0) != 0x80) return -1;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if ((width == 3 && cp < 0x800) ||
        (width == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        return -1;
    }

    *codepoint = cp;
    *used = width;
    return 0;
}

static bool valid_utf8(const char *src, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        uint32_t codepoint;
        size_t used;
        if (utf8_decode_one((const unsigned char *)src + offset, len - offset,
                            &codepoint, &used) < 0) {
            return false;
        }
        (void)codepoint;
        offset += used;
    }
    return true;
}

/* ---- Helpers ---- */

static void preserve_comment(parser *p, hon_str comment) {
    if (p->comment_count >= p->comment_cap) {
        uint32_t new_cap = p->comment_cap * 2;
        hon_str *new_comments = hon_arena_alloc(
            p->arena, sizeof(hon_str) * new_cap, _Alignof(hon_str));
        if (!new_comments) return;
        memcpy(new_comments, p->comments, sizeof(hon_str) * p->comment_count);
        p->comments = new_comments;
        p->comment_cap = new_cap;
    }
    p->comments[p->comment_count++] = comment;
}

static hon_token next_skip_comments(parser *p) {
    for (;;) {
        hon_token t = hon_lexer_next(p->lex);
        if (t.kind != HON_TOK_COMMENT) return t;
        preserve_comment(p, t.text);
    }
}

static hon_token peek_skip_comments(parser *p) {
    /* Peek but skip comments — consume comments as side effect */
    for (;;) {
        hon_token t = hon_lexer_peek(p->lex);
        if (t.kind != HON_TOK_COMMENT) return t;
        hon_lexer_next(p->lex); /* consume the comment */
        preserve_comment(p, t.text);
    }
}

static hon_token expect_skip(parser *p, hon_tok_kind kind) {
    hon_token t = next_skip_comments(p);
    if (t.kind != kind) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, t.line, t.col,
                      "expected %s, got %s", hon_tok_kind_name(kind),
                      hon_tok_kind_name(t.kind));
        return (hon_token){.kind = HON_TOK_ERROR};
    }
    return t;
}

/* Other reserved words have dedicated token kinds. Boolean remains an
 * identifier token because it is also a legal type reference. */
static void reject_reserved_boolean_name(parser *p, hon_token token,
                                         const char *context) {
    if (hon_str_eq(token.text, HON_STR_LIT("Boolean"))) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      token.line, token.col,
                      "reserved name 'Boolean' cannot be used as %s", context);
    }
}

static bool field_name_exists(const hon_field *fields, uint32_t count,
                              hon_str name) {
    for (uint32_t i = 0; i < count; i++) {
        if (hon_str_eq(fields[i].name, name)) return true;
    }
    return false;
}

/* ---- Primitive Type Registry ---- */

static hon_type primitive_types[HON_PRIM_COUNT];

static void init_primitives(void) {
    static bool done = false;
    if (done) return;
    done = true;

    static const struct { hon_primitive p; const char *name; } prims[] = {
        {HON_PRIM_BOOLEAN, "Boolean"},
        {HON_PRIM_BYTE,    "Byte"},
        {HON_PRIM_WORD,    "Word"},
        {HON_PRIM_INTEGER, "Integer"},
        {HON_PRIM_LONGINT, "LongInt"},
        {HON_PRIM_REAL,    "Real"},
        {HON_PRIM_CHAR,    "Char"},
        {HON_PRIM_STRING,  "String"},
    };

    for (size_t i = 0; i < HON_PRIM_COUNT; i++) {
        primitive_types[i].kind   = HON_TYPE_PRIMITIVE;
        primitive_types[i].name   = (hon_str){prims[i].name, strlen(prims[i].name)};
        primitive_types[i].u.prim = prims[i].p;
    }
}

/* ---- Type Lookup ---- */

static hon_type *lookup_type(parser *p, hon_str name) {
    /* Check user-declared types (reverse order for most recent) */
    for (uint32_t i = p->type_count; i > 0; i--) {
        if (hon_str_eq(p->types[i - 1]->name, name))
            return p->types[i - 1];
    }
    /* Check primitives */
    for (int i = 0; i < HON_PRIM_COUNT; i++) {
        if (hon_str_eq(primitive_types[i].name, name))
            return &primitive_types[i];
    }
    return NULL;
}

static bool type_was_declared(const parser *p, hon_str name) {
    for (uint32_t i = 0; i < p->type_count; i++) {
        if (hon_str_eq(p->types[i]->name, name)) return true;
    }
    return false;
}

static void register_type(parser *p, hon_type *t) {
    if (p->type_count >= p->type_cap) {
        uint32_t new_cap = p->type_cap * 2;
        hon_type **new_types = hon_arena_alloc(p->arena,
            sizeof(hon_type *) * new_cap, _Alignof(hon_type *));
        if (!new_types) return;
        memcpy(new_types, p->types, sizeof(hon_type *) * p->type_count);
        p->types   = new_types;
        p->type_cap = new_cap;
    }
    p->types[p->type_count++] = t;
}

/* ---- Resolve Type Reference ---- */

static hon_type *resolve_type_ref(parser *p, hon_str name, uint32_t line, uint32_t col) {
    hon_type *t = lookup_type(p, name);
    if (!t) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, line, col,
                      "unknown type '%.*s'", (int)name.len, name.data);
    }
    return t;
}

/* ---- Parse Enumeration ---- */

static hon_type *parse_enum(parser *p, hon_str name) {
    /* Already consumed '(' */
    uint32_t cap = 16;
    uint32_t count = 0;
    hon_str *values = hon_arena_alloc(p->arena, sizeof(hon_str) * cap,
                                      _Alignof(hon_str));
    if (!values) return NULL;

    for (;;) {
        hon_token val = expect_skip(p, HON_TOK_IDENT);
        if (val.kind == HON_TOK_ERROR) return NULL;
        reject_reserved_boolean_name(p, val, "an enumeration value");

        /* Check for duplicate enum value */
        for (uint32_t i = 0; i < count; i++) {
            if (hon_str_eq(values[i], val.text)) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              val.line, val.col,
                              "duplicate enumeration value '%.*s'",
                              (int)val.text.len, val.text.data);
            }
        }

        if (count == cap) {
            uint32_t new_cap = cap * 2;
            hon_str *new_values = hon_arena_alloc(
                p->arena, sizeof(hon_str) * new_cap, _Alignof(hon_str));
            if (!new_values) return NULL;
            memcpy(new_values, values, sizeof(hon_str) * count);
            values = new_values;
            cap = new_cap;
        }
        values[count++] = val.text;

        hon_token next = peek_skip_comments(p);
        if (next.kind == HON_TOK_COMMA) {
            next_skip_comments(p); /* consume comma */
        } else if (next.kind == HON_TOK_RPAREN) {
            next_skip_comments(p); /* consume ) */
            break;
        } else {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, next.line, next.col,
                          "expected ',' or ')' in enumeration");
            return NULL;
        }
    }

    hon_type *t = hon_arena_alloc(p->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_ENUM;
    t->name = name;
    t->u.enumeration.value_count = count;
    t->u.enumeration.values = values;

    return t;
}

/* ---- Parse Field List ---- */

/* Parse fields until we hit 'case', 'end', or ')'. */
static bool parse_fields(parser *p, hon_field **out_fields,
                         uint32_t *out_count, bool in_variant_arm) {
    uint32_t cap = 16;
    uint32_t count = 0;
    hon_field *fields = hon_arena_alloc(p->arena, sizeof(hon_field) * cap,
                                        _Alignof(hon_field));
    if (!fields) return false;

    for (;;) {
        hon_token peek = peek_skip_comments(p);

        /* Stop conditions */
        if (peek.kind == HON_TOK_CASE || peek.kind == HON_TOK_END)
            break;
        if (in_variant_arm && peek.kind == HON_TOK_RPAREN)
            break;
        if (peek.kind == HON_TOK_EOF || peek.kind == HON_TOK_ERROR)
            break;

        /* Disambiguate: is this a field (IDENT ':' ...) or something else? */
        if (peek.kind != HON_TOK_IDENT)
            break;

        hon_token name_tok = next_skip_comments(p);
        reject_reserved_boolean_name(p, name_tok, "a field name");
        hon_token colon = peek_skip_comments(p);
        if (colon.kind != HON_TOK_COLON) {
            /* Not a field — push back by reporting error */
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          colon.line, colon.col,
                          "expected ':' after field name '%.*s'",
                          (int)name_tok.text.len, name_tok.text.data);
            break;
        }
        next_skip_comments(p); /* consume ':' */

        hon_token type_tok = expect_skip(p, HON_TOK_IDENT);
        if (type_tok.kind == HON_TOK_ERROR) break;

        hon_type *ftype = resolve_type_ref(p, type_tok.text,
                                            type_tok.line, type_tok.col);

        /* Check for duplicate field name */
        for (uint32_t i = 0; i < count; i++) {
            if (hon_str_eq(fields[i].name, name_tok.text)) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              name_tok.line, name_tok.col,
                              "duplicate field name '%.*s'",
                              (int)name_tok.text.len, name_tok.text.data);
            }
        }

        if (count == cap) {
            uint32_t new_cap = cap * 2;
            hon_field *new_fields = hon_arena_alloc(
                p->arena, sizeof(hon_field) * new_cap, _Alignof(hon_field));
            if (!new_fields) return false;
            memcpy(new_fields, fields, sizeof(hon_field) * count);
            fields = new_fields;
            cap = new_cap;
        }
        fields[count].name = name_tok.text;
        fields[count].type = ftype;
        count++;

        /* Optional semicolon — may be followed by more fields, 'case', 'end', or ')' */
        peek = peek_skip_comments(p);
        if (peek.kind == HON_TOK_SEMICOLON) {
            next_skip_comments(p); /* consume ';' */
        } else if (peek.kind == HON_TOK_IDENT) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          peek.line, peek.col,
                          "expected ';' between record fields");
        }
    }

    *out_fields = fields;
    *out_count = count;
    return true;
}

/* ---- Parse Variant Part ---- */

static bool parse_variant_part(parser *p, hon_type *rec) {
    /* 'case' already peeked but not consumed */
    next_skip_comments(p); /* consume 'case' */

    hon_token tag_name = expect_skip(p, HON_TOK_IDENT);
    if (tag_name.kind == HON_TOK_ERROR) return false;
    reject_reserved_boolean_name(p, tag_name, "a field name");
    if (field_name_exists(rec->u.record.fields, rec->u.record.field_count,
                          tag_name.text)) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      tag_name.line, tag_name.col,
                      "variant tag duplicates fixed field '%.*s'",
                      (int)tag_name.text.len, tag_name.text.data);
    }

    expect_skip(p, HON_TOK_COLON);

    hon_token tag_type_name = expect_skip(p, HON_TOK_IDENT);
    if (tag_type_name.kind == HON_TOK_ERROR) return false;

    hon_type *tag_type = resolve_type_ref(p, tag_type_name.text,
                                           tag_type_name.line, tag_type_name.col);
    hon_type *resolved_tag = hon_resolve_alias(tag_type);
    bool enum_tag = resolved_tag && resolved_tag->kind == HON_TYPE_ENUM;
    bool bool_tag = resolved_tag && resolved_tag->kind == HON_TYPE_PRIMITIVE &&
                    resolved_tag->u.prim == HON_PRIM_BOOLEAN;
    if (!enum_tag && !bool_tag) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      tag_type_name.line, tag_type_name.col,
                      "variant discriminant must be an enumeration or Boolean");
    }

    expect_skip(p, HON_TOK_OF);

    rec->u.record.has_variant = true;
    rec->u.record.tag_name    = tag_name.text;
    rec->u.record.tag_type    = tag_type;

    /* Parse variant arms */
    uint32_t arm_cap = 8;
    uint32_t arm_count = 0;
    hon_variant *arms = hon_arena_alloc(
        p->arena, sizeof(hon_variant) * arm_cap, _Alignof(hon_variant));
    if (!arms) return false;

    for (;;) {
        hon_token peek = peek_skip_comments(p);
        /* Variant arm labels can be identifiers, True, or False */
        if (peek.kind != HON_TOK_IDENT && peek.kind != HON_TOK_TRUE &&
            peek.kind != HON_TOK_FALSE)
            break;

        hon_token label = next_skip_comments(p); /* consume label */
        bool valid_label = false;
        if (enum_tag && label.kind == HON_TOK_IDENT) {
            for (uint32_t i = 0; i < resolved_tag->u.enumeration.value_count; i++) {
                if (hon_str_eq(resolved_tag->u.enumeration.values[i], label.text)) {
                    valid_label = true;
                    break;
                }
            }
        } else if (bool_tag &&
                   (label.kind == HON_TOK_TRUE || label.kind == HON_TOK_FALSE)) {
            valid_label = true;
        }
        if (!valid_label) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          label.line, label.col,
                          "invalid variant arm label '%.*s'",
                          (int)label.text.len, label.text.data);
        }
        for (uint32_t i = 0; i < arm_count; i++) {
            if (hon_str_eq(arms[i].label, label.text)) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              label.line, label.col,
                              "duplicate variant arm '%.*s'",
                              (int)label.text.len, label.text.data);
            }
        }
        expect_skip(p, HON_TOK_COLON);
        expect_skip(p, HON_TOK_LPAREN);

        hon_field *arm_fields = NULL;
        uint32_t fcount = 0;

        if (peek_skip_comments(p).kind != HON_TOK_RPAREN) {
            if (!parse_fields(p, &arm_fields, &fcount, true)) return false;
        }

        for (uint32_t i = 0; i < fcount; i++) {
            if (hon_str_eq(arm_fields[i].name, tag_name.text) ||
                field_name_exists(rec->u.record.fields,
                                  rec->u.record.field_count,
                                  arm_fields[i].name)) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              label.line, label.col,
                              "variant field '%.*s' conflicts with record field",
                              (int)arm_fields[i].name.len,
                              arm_fields[i].name.data);
            }
        }

        expect_skip(p, HON_TOK_RPAREN);

        /* Every variant arm ends with a semicolon. */
        hon_token arm_end = peek_skip_comments(p);
        if (arm_end.kind == HON_TOK_SEMICOLON) {
            next_skip_comments(p);
        } else {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          arm_end.line, arm_end.col,
                          "expected ';' after variant arm");
        }

        if (arm_count == arm_cap) {
            uint32_t new_cap = arm_cap * 2;
            hon_variant *new_arms = hon_arena_alloc(
                p->arena, sizeof(hon_variant) * new_cap,
                _Alignof(hon_variant));
            if (!new_arms) return false;
            memcpy(new_arms, arms, sizeof(hon_variant) * arm_count);
            arms = new_arms;
            arm_cap = new_cap;
        }
        arms[arm_count].label       = label.text;
        arms[arm_count].field_count = fcount;
        arms[arm_count].fields      = arm_fields;
        arm_count++;
    }

    rec->u.record.arm_count = arm_count;
    rec->u.record.arms = arms;

    if (arm_count == 0) {
        hon_token end = peek_skip_comments(p);
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      end.line, end.col,
                      "variant part requires at least one arm");
    }

    uint32_t legal_value_count = bool_tag ? 2u :
        (enum_tag ? resolved_tag->u.enumeration.value_count : 0u);
    if (arm_count < legal_value_count) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_WARNING,
                      tag_name.line, tag_name.col,
                      "variant does not provide an arm for every legal discriminant value");
    }

    return true;
}

/* ---- Parse Record ---- */

static hon_type *parse_record(parser *p, hon_str name) {
    /* 'record' already consumed */
    hon_type *t = hon_arena_alloc(p->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_RECORD;
    t->name = name;
    t->u.record.has_variant = false;

    /* Parse fixed fields */
    hon_field *fields = NULL;
    uint32_t fcount = 0;
    if (!parse_fields(p, &fields, &fcount, false)) return NULL;

    t->u.record.field_count = fcount;
    t->u.record.fields = fields;

    /* Check for variant part */
    if (peek_skip_comments(p).kind == HON_TOK_CASE) {
        parse_variant_part(p, t);
    }

    expect_skip(p, HON_TOK_END);

    return t;
}

/* ---- Parse Type Definition ---- */

static hon_type *parse_type_definition(parser *p) {
    hon_token name_tok = expect_skip(p, HON_TOK_IDENT);
    if (name_tok.kind == HON_TOK_ERROR) return NULL;

    /* Duplicate declarations are ambiguous. Boolean is also a reserved word. */
    if (type_was_declared(p, name_tok.text)) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      name_tok.line, name_tok.col,
                      "duplicate type declaration '%.*s'",
                      (int)name_tok.text.len, name_tok.text.data);
    } else if (hon_str_eq(name_tok.text, HON_STR_LIT("Boolean"))) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      name_tok.line, name_tok.col,
                      "reserved type name 'Boolean' cannot be redeclared");
    } else if (lookup_type(p, name_tok.text)) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_WARNING,
                      name_tok.line, name_tok.col,
                      "type '%.*s' shadows a primitive type",
                      (int)name_tok.text.len, name_tok.text.data);
    }

    expect_skip(p, HON_TOK_EQUALS);

    hon_token peek = peek_skip_comments(p);
    hon_type *t = NULL;

    if (peek.kind == HON_TOK_LPAREN) {
        /* Enumeration: (Value1, Value2, ...) */
        next_skip_comments(p); /* consume '(' */
        t = parse_enum(p, name_tok.text);
    } else if (peek.kind == HON_TOK_RECORD) {
        /* Record */
        next_skip_comments(p); /* consume 'record' */
        t = parse_record(p, name_tok.text);
    } else if (peek.kind == HON_TOK_SEQUENCE) {
        /* Sequence */
        next_skip_comments(p); /* consume 'sequence' */
        expect_skip(p, HON_TOK_OF);
        hon_token elem_tok = expect_skip(p, HON_TOK_IDENT);
        if (elem_tok.kind == HON_TOK_ERROR) return NULL;

        hon_type *elem = resolve_type_ref(p, elem_tok.text,
                                           elem_tok.line, elem_tok.col);

        t = hon_arena_alloc(p->arena, sizeof(hon_type), _Alignof(hon_type));
        t->kind = HON_TYPE_SEQUENCE;
        t->name = name_tok.text;
        t->u.sequence.element_type = elem;
    } else if (peek.kind == HON_TOK_IDENT) {
        /* Type alias */
        hon_token target_tok = next_skip_comments(p);
        hon_type *target = resolve_type_ref(p, target_tok.text,
                                             target_tok.line, target_tok.col);

        t = hon_arena_alloc(p->arena, sizeof(hon_type), _Alignof(hon_type));
        t->kind = HON_TYPE_ALIAS;
        t->name = name_tok.text;
        t->u.alias.target = target;
    } else {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, peek.line, peek.col,
                      "expected type definition, got %s",
                      hon_tok_kind_name(peek.kind));
        return NULL;
    }

    expect_skip(p, HON_TOK_SEMICOLON);

    if (t) {
        /* Check for divergence against loaded rulebook */
        if (p->registry) {
            hon_type *rb_type = hon_registry_lookup(p->registry, t->name);
            if (rb_type) {
                if (!hon_types_equal(rb_type, t)) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_INFO,
                                  name_tok.line, name_tok.col,
                                  "type '%.*s' diverges from rulebook",
                                  (int)t->name.len, t->name.data);
                }
            }
        }
        register_type(p, t);
    }
    return t;
}

/* ---- Parse Values (Type-Directed) ---- */

/* Unescape a string literal token, stripping quotes */
static hon_str unescape_string(parser *p, hon_str raw) {
    /* raw includes surrounding quotes */
    if (raw.len < 2) return (hon_str){NULL, 0};

    const char *src = raw.data + 1;       /* skip opening ' */
    size_t slen     = raw.len - 2;        /* strip both quotes */

    char *buf = hon_arena_alloc(p->arena, slen + 1, 1);
    if (!buf) return (hon_str){NULL, 0};

    size_t out = 0;
    for (size_t i = 0; i < slen; i++) {
        if (src[i] == '\'' && i + 1 < slen && src[i + 1] == '\'') {
            buf[out++] = '\'';
            i++;
        } else if (src[i] == '\\' && i + 1 < slen) {
            i++;
            switch (src[i]) {
            case 'n':  buf[out++] = '\n'; break;
            case 'r':  buf[out++] = '\r'; break;
            case 't':  buf[out++] = '\t'; break;
            case '\\': buf[out++] = '\\'; break;
            case 'u': case 'U': {
                int hex_len = (src[i] == 'u') ? 4 : 8;
                if (i + hex_len >= slen) {
                    buf[out++] = '\\';
                    buf[out++] = src[i];
                    break;
                }
                uint32_t cp = 0;
                for (int j = 0; j < hex_len; j++) {
                    char hc = src[i + 1 + j];
                    cp <<= 4;
                    if (hc >= '0' && hc <= '9') cp |= (uint32_t)(hc - '0');
                    else if (hc >= 'a' && hc <= 'f') cp |= (uint32_t)(hc - 'a' + 10);
                    else if (hc >= 'A' && hc <= 'F') cp |= (uint32_t)(hc - 'A' + 10);
                }
                i += hex_len;
                /* Encode as UTF-8 */
                if (cp < 0x80) {
                    buf[out++] = (char)cp;
                } else if (cp < 0x800) {
                    buf[out++] = (char)(0xC0 | (cp >> 6));
                    buf[out++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    buf[out++] = (char)(0xE0 | (cp >> 12));
                    buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[out++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x110000) {
                    buf[out++] = (char)(0xF0 | (cp >> 18));
                    buf[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[out++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                buf[out++] = '\\';
                buf[out++] = src[i];
                break;
            }
        } else {
            buf[out++] = src[i];
        }
    }

    buf[out] = '\0';
    return (hon_str){buf, out};
}

static hon_value *parse_record_value(parser *p, hon_type *type);
static hon_value *parse_sequence_value(parser *p, hon_type *type);

static hon_value *parse_value(parser *p, hon_type *type) {
    if (p->depth >= p->max_depth) {
        hon_token t = peek_skip_comments(p);
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, t.line, t.col,
                      "maximum nesting depth (%u) exceeded", p->max_depth);
        return NULL;
    }
    p->depth++;

    hon_type *resolved = hon_resolve_alias(type);
    hon_value *val = hon_arena_alloc(p->arena, sizeof(hon_value), _Alignof(hon_value));
    val->type = type;

    if (!resolved) {
        hon_token t = peek_skip_comments(p);
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, t.line, t.col,
                      "cannot resolve type for value");
        p->depth--;
        return NULL;
    }

    hon_token peek = peek_skip_comments(p);

    switch (resolved->kind) {
    case HON_TYPE_PRIMITIVE: {
        hon_token t = next_skip_comments(p);
        switch (resolved->u.prim) {
        case HON_PRIM_BOOLEAN:
            if (t.kind == HON_TOK_TRUE) {
                val->kind = HON_VAL_BOOL;
                val->u.boolean = true;
            } else if (t.kind == HON_TOK_FALSE) {
                val->kind = HON_VAL_BOOL;
                val->u.boolean = false;
            } else {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected True or False");
                val = NULL;
            }
            break;

        case HON_PRIM_BYTE:
        case HON_PRIM_WORD:
        case HON_PRIM_INTEGER:
        case HON_PRIM_LONGINT:
            if (t.kind != HON_TOK_INT) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected integer literal");
                val = NULL;
            } else {
                val->kind = HON_VAL_INT;
                char buf[32];
                char *end;
                if (t.text.len >= sizeof(buf)) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  t.line, t.col, "integer literal is too long");
                    val = NULL;
                    break;
                }
                memcpy(buf, t.text.data, t.text.len);
                buf[t.text.len] = '\0';
                errno = 0;
                int64_t parsed = strtoll(buf, &end, 10);
                if (errno == ERANGE || *end != '\0') {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  t.line, t.col, "integer literal is out of range");
                    val = NULL;
                    break;
                }
                val->u.integer = parsed;
                /* Range validation */
                int64_t v = val->u.integer;
                switch (resolved->u.prim) {
                case HON_PRIM_BYTE:
                    if (v < 0 || v > 255)
                        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                      t.line, t.col,
                                      "value %" PRId64 " out of range for Byte (0..255)", v);
                    break;
                case HON_PRIM_WORD:
                    if (v < 0 || v > 65535)
                        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                      t.line, t.col,
                                      "value %" PRId64 " out of range for Word (0..65535)", v);
                    break;
                case HON_PRIM_INTEGER:
                    if (v < -2147483648LL || v > 2147483647LL)
                        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                      t.line, t.col,
                                      "value %" PRId64 " out of range for Integer", v);
                    break;
                default:
                    break;
                }
            }
            break;

        case HON_PRIM_REAL:
            if (t.kind != HON_TOK_REAL) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected real literal");
                val = NULL;
            } else {
                val->kind = HON_VAL_REAL;
                char buf[64];
                char *end;
                if (t.text.len >= sizeof(buf)) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  t.line, t.col, "real literal is too long");
                    val = NULL;
                    break;
                }
                memcpy(buf, t.text.data, t.text.len);
                buf[t.text.len] = '\0';
                errno = 0;
                double parsed = strtod(buf, &end);
                if (errno == ERANGE || *end != '\0' || !isfinite(parsed)) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  t.line, t.col, "real literal is out of range");
                    val = NULL;
                    break;
                }
                val->u.real = parsed;
            }
            break;

        case HON_PRIM_CHAR:
            if (t.kind != HON_TOK_STRING) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected character literal");
                val = NULL;
            } else {
                hon_str unesc = unescape_string(p, t.text);
                uint32_t codepoint;
                size_t used;
                if (utf8_decode_one((const unsigned char *)unesc.data,
                                    unesc.len, &codepoint, &used) < 0 ||
                    used != unesc.len) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  t.line, t.col,
                                  "Char must contain exactly one Unicode scalar");
                    val = NULL;
                } else {
                    val->kind = HON_VAL_CHAR;
                    val->u.character = codepoint;
                }
            }
            break;

        case HON_PRIM_STRING:
            if (t.kind != HON_TOK_STRING) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected string literal");
                val = NULL;
            } else {
                val->kind = HON_VAL_STRING;
                val->u.string = unescape_string(p, t.text);
            }
            break;

        default:
            val = NULL;
            break;
        }
        break;
    }

    case HON_TYPE_ENUM:
        if (peek.kind == HON_TOK_TRUE || peek.kind == HON_TOK_FALSE ||
            peek.kind == HON_TOK_IDENT) {
            hon_token t = next_skip_comments(p);
            val->kind = HON_VAL_ENUM;
            val->u.enum_val = t.text;
            /* Validate enum value is a member of the type */
            bool valid_enum = false;
            for (uint32_t i = 0; i < resolved->u.enumeration.value_count; i++) {
                if (hon_str_eq(resolved->u.enumeration.values[i], t.text)) {
                    valid_enum = true;
                    break;
                }
            }
            if (!valid_enum) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col,
                              "'%.*s' is not a value of enumeration '%.*s'",
                              (int)t.text.len, t.text.data,
                              (int)resolved->name.len, resolved->name.data);
            }
        } else {
            hon_token t = next_skip_comments(p);
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          t.line, t.col, "expected enumeration value");
            val = NULL;
        }
        break;

    case HON_TYPE_RECORD:
        val = parse_record_value(p, resolved);
        if (val) val->type = type;
        break;

    case HON_TYPE_SEQUENCE:
        val = parse_sequence_value(p, resolved);
        if (val) val->type = type;
        break;

    default:
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR, peek.line, peek.col,
                      "unsupported type kind in value parsing");
        val = NULL;
        break;
    }

    p->depth--;
    return val;
}

static hon_value *parse_record_value(parser *p, hon_type *type) {
    expect_skip(p, HON_TOK_LPAREN);

    hon_value *val = hon_arena_alloc(p->arena, sizeof(hon_value), _Alignof(hon_value));
    val->kind = HON_VAL_RECORD;
    val->type = type;

    uint32_t fixed_count = type->u.record.field_count;
    hon_field_val *fvals = NULL;
    if (fixed_count > 0) {
        fvals = hon_arena_alloc(p->arena,
            sizeof(hon_field_val) * fixed_count, _Alignof(hon_field_val));
    }

    for (uint32_t i = 0; i < fixed_count; i++) {
        if (i > 0) {
            /* Require a semicolon between fields. */
            hon_token sep = peek_skip_comments(p);
            if (sep.kind == HON_TOK_SEMICOLON) {
                next_skip_comments(p);
            } else {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              sep.line, sep.col,
                              "expected ';' between record values");
            }
        }

        hon_token fname = expect_skip(p, HON_TOK_IDENT);
        if (fname.kind == HON_TOK_ERROR) return NULL;
        if (!hon_str_eq(fname.text, type->u.record.fields[i].name)) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          fname.line, fname.col,
                          "expected field '%.*s', got '%.*s'",
                          (int)type->u.record.fields[i].name.len,
                          type->u.record.fields[i].name.data,
                          (int)fname.text.len, fname.text.data);
            return NULL;
        }

        expect_skip(p, HON_TOK_COLON);

        hon_value *fval = parse_value(p, type->u.record.fields[i].type);
        if (!fval) return NULL;

        fvals[i].name  = fname.text;
        fvals[i].value = fval;
    }

    val->u.record.fields      = fvals;
    val->u.record.field_count = fixed_count;

    /* Variant part */
    val->u.record.tag_value           = (hon_str){NULL, 0};
    val->u.record.variant_fields      = NULL;
    val->u.record.variant_field_count = 0;

    if (type->u.record.has_variant) {
        /* A fixed field and the tag field require a separator. */
        if (fixed_count > 0) {
            hon_token sep = peek_skip_comments(p);
            if (sep.kind == HON_TOK_SEMICOLON) {
                next_skip_comments(p);
            } else {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              sep.line, sep.col,
                              "expected ';' before variant tag");
            }
        }

        /* Parse tag field: name : value */
        hon_token tag_fname = expect_skip(p, HON_TOK_IDENT);
        if (tag_fname.kind == HON_TOK_ERROR) return NULL;
        if (!hon_str_eq(tag_fname.text, type->u.record.tag_name)) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          tag_fname.line, tag_fname.col,
                          "expected variant tag '%.*s', got '%.*s'",
                          (int)type->u.record.tag_name.len,
                          type->u.record.tag_name.data,
                          (int)tag_fname.text.len, tag_fname.text.data);
            return NULL;
        }
        expect_skip(p, HON_TOK_COLON);

        hon_token tag_val = next_skip_comments(p);
        hon_type *tag_type = hon_resolve_alias(type->u.record.tag_type);
        bool valid_tag = false;
        if (tag_type && tag_type->kind == HON_TYPE_ENUM &&
            tag_val.kind == HON_TOK_IDENT) {
            for (uint32_t i = 0; i < tag_type->u.enumeration.value_count; i++) {
                if (hon_str_eq(tag_type->u.enumeration.values[i], tag_val.text)) {
                    valid_tag = true;
                    break;
                }
            }
        } else if (tag_type && tag_type->kind == HON_TYPE_PRIMITIVE &&
                   tag_type->u.prim == HON_PRIM_BOOLEAN &&
                   (tag_val.kind == HON_TOK_TRUE || tag_val.kind == HON_TOK_FALSE)) {
            valid_tag = true;
        }
        if (!valid_tag) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          tag_val.line, tag_val.col,
                          "invalid value for variant discriminant '%.*s'",
                          (int)type->u.record.tag_name.len,
                          type->u.record.tag_name.data);
            return NULL;
        }
        val->u.record.tag_value = tag_val.text;

        /* Find matching arm */
        hon_variant *matched_arm = NULL;
        for (uint32_t i = 0; i < type->u.record.arm_count; i++) {
            if (hon_str_eq(type->u.record.arms[i].label, tag_val.text)) {
                matched_arm = &type->u.record.arms[i];
                break;
            }
        }

        if (matched_arm && matched_arm->field_count > 0) {
            hon_field_val *vfields = hon_arena_alloc(p->arena,
                sizeof(hon_field_val) * matched_arm->field_count,
                _Alignof(hon_field_val));

            for (uint32_t i = 0; i < matched_arm->field_count; i++) {
                hon_token sep = peek_skip_comments(p);
                if (sep.kind == HON_TOK_SEMICOLON) {
                    next_skip_comments(p);
                } else {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  sep.line, sep.col,
                                  "expected ';' before variant field");
                }

                hon_token vfname = expect_skip(p, HON_TOK_IDENT);
                if (vfname.kind == HON_TOK_ERROR) return NULL;
                if (!hon_str_eq(vfname.text, matched_arm->fields[i].name)) {
                    hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                                  vfname.line, vfname.col,
                                  "expected variant field '%.*s', got '%.*s'",
                                  (int)matched_arm->fields[i].name.len,
                                  matched_arm->fields[i].name.data,
                                  (int)vfname.text.len, vfname.text.data);
                    return NULL;
                }
                expect_skip(p, HON_TOK_COLON);

                hon_value *vfval = parse_value(p, matched_arm->fields[i].type);
                if (!vfval) return NULL;

                vfields[i].name  = vfname.text;
                vfields[i].value = vfval;
            }

            val->u.record.variant_fields      = vfields;
            val->u.record.variant_field_count = matched_arm->field_count;
        } else if (!matched_arm) {
            hon_diag_push(p->diags, p->arena, HON_DIAG_WARNING,
                          tag_val.line, tag_val.col,
                          "no variant arm for discriminant value '%.*s'",
                          (int)tag_val.text.len, tag_val.text.data);
        }
    }

    /* Optional trailing semicolon before ')' */
    if (peek_skip_comments(p).kind == HON_TOK_SEMICOLON)
        next_skip_comments(p);

    expect_skip(p, HON_TOK_RPAREN);
    return val;
}

static hon_value *parse_sequence_value(parser *p, hon_type *type) {
    expect_skip(p, HON_TOK_LPAREN);

    hon_value *val = hon_arena_alloc(p->arena, sizeof(hon_value), _Alignof(hon_value));
    val->kind = HON_VAL_SEQUENCE;
    val->type = type;

    /* Empty sequence */
    if (peek_skip_comments(p).kind == HON_TOK_RPAREN) {
        next_skip_comments(p);
        val->u.sequence.elements = NULL;
        val->u.sequence.count    = 0;
        return val;
    }

    /* Dynamic array — start with reasonable capacity */
    uint32_t cap = 16;
    hon_value *elements = hon_arena_alloc(p->arena,
        sizeof(hon_value) * cap, _Alignof(hon_value));
    uint32_t count = 0;

    hon_type *elem_type = type->u.sequence.element_type;

    for (;;) {
        if (count >= cap) {
            uint32_t new_cap = cap * 2;
            hon_value *new_elems = hon_arena_alloc(p->arena,
                sizeof(hon_value) * new_cap, _Alignof(hon_value));
            memcpy(new_elems, elements, sizeof(hon_value) * count);
            elements = new_elems;
            cap = new_cap;
        }

        hon_value *elem = parse_value(p, elem_type);
        if (!elem) return NULL;

        elements[count++] = *elem;

        hon_token peek = peek_skip_comments(p);
        if (peek.kind == HON_TOK_SEMICOLON) {
            next_skip_comments(p);
            /* Check for trailing semicolon before ')' */
            if (peek_skip_comments(p).kind == HON_TOK_RPAREN) {
                next_skip_comments(p);
                break;
            }
        } else if (peek.kind == HON_TOK_RPAREN) {
            next_skip_comments(p);
            break;
        } else {
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          peek.line, peek.col,
                          "expected ';' or ')' in sequence");
            return NULL;
        }
    }

    val->u.sequence.elements = elements;
    val->u.sequence.count    = count;
    return val;
}

/* ---- Parse Document ---- */

static hon_document *parse_document(parser *p) {
    /* Declarations section: expect 'type' keyword */
    hon_token type_kw = next_skip_comments(p);
    if (type_kw.kind != HON_TOK_TYPE) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      type_kw.line, type_kw.col,
                      "expected 'type' keyword at start of declarations");
        return NULL;
    }

    /* Parse type declarations until we see 'var' or EOF */
    while (peek_skip_comments(p).kind == HON_TOK_IDENT) {
        if (!parse_type_definition(p)) {
            /* Try to recover: skip to next semicolon */
            hon_token t;
            do {
                t = next_skip_comments(p);
            } while (t.kind != HON_TOK_SEMICOLON && t.kind != HON_TOK_EOF);
        }
    }
    if (p->type_count == 0) {
        hon_token t = peek_skip_comments(p);
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      t.line, t.col,
                      "declarations section requires at least one type");
    }

    /* Data section: parse var declarations */
    while (peek_skip_comments(p).kind == HON_TOK_VAR) {
        next_skip_comments(p); /* consume 'var' */

        hon_token var_name = expect_skip(p, HON_TOK_IDENT);
        if (var_name.kind == HON_TOK_ERROR) continue;

        expect_skip(p, HON_TOK_COLON);

        hon_token var_type_name = expect_skip(p, HON_TOK_IDENT);
        if (var_type_name.kind == HON_TOK_ERROR) continue;

        hon_type *var_type = resolve_type_ref(p, var_type_name.text,
                                               var_type_name.line,
                                               var_type_name.col);

        expect_skip(p, HON_TOK_EQUALS);

        hon_value *val = NULL;
        if (var_type) {
            val = parse_value(p, var_type);
        }

        expect_skip(p, HON_TOK_SEMICOLON);

        /* Store variable */
        if (p->var_count >= p->var_cap) {
            uint32_t new_cap = p->var_cap * 2;
            hon_variable *new_vars = hon_arena_alloc(p->arena,
                sizeof(hon_variable) * new_cap, _Alignof(hon_variable));
            memcpy(new_vars, p->vars, sizeof(hon_variable) * p->var_count);
            p->vars    = new_vars;
            p->var_cap = new_cap;
        }

        p->vars[p->var_count++] = (hon_variable){
            .name  = var_name.text,
            .type  = var_type,
            .value = val,
        };
    }

    /* Check for trailing tokens */
    hon_token end = peek_skip_comments(p);
    if (end.kind != HON_TOK_EOF) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                      end.line, end.col,
                      "unexpected tokens after end of document");
    }

    /* Build document */
    hon_document *doc = hon_arena_alloc(p->arena, sizeof(hon_document),
                                        _Alignof(hon_document));
    doc->arena      = p->arena;
    doc->types      = p->types;
    doc->type_count = p->type_count;
    doc->vars       = p->vars;
    doc->var_count  = p->var_count;
    doc->comments   = p->comments;
    doc->comment_count = p->comment_count;

    return doc;
}

/* ---- Diagnostic Detach ---- */

/* Copy diagnostics from arena to malloc so they survive arena_free. */
static void diag_detach_from_arena(hon_diag_list *diags) {
    if (!diags || diags->count == 0) return;
    hon_diagnostic *safe = malloc(sizeof(hon_diagnostic) * diags->count);
    if (safe) {
        for (uint32_t di = 0; di < diags->count; di++) {
            safe[di] = diags->items[di];
            if (diags->items[di].message)
                safe[di].message = strdup(diags->items[di].message);
        }
        diags->items = safe;
        diags->owns_memory = true;
    } else {
        diags->items = NULL;
        diags->count = 0;
    }
}

/* ---- Public API ---- */

static hon_diagnostic invalid_utf8_diagnostic = {
    .level = HON_DIAG_ERROR,
    .line = 1,
    .col = 1,
    .message = "input is not valid UTF-8",
};

static void set_invalid_utf8_diagnostic(hon_diag_list *diags) {
    if (!diags) return;
    diags->items = &invalid_utf8_diagnostic;
    diags->count = 1;
    diags->cap = 1;
    diags->owns_memory = false;
}

hon_document *hon_parse(const char *src, size_t len, hon_diag_list *diags) {
    init_primitives();

    hon_arena *arena = hon_arena_new(0);
    if (!arena) return NULL;

    /* Copy source into arena so all hon_str pointers remain valid
     * for the lifetime of the document. */
    char *arena_src = hon_arena_alloc(arena, len + 1, 1);
    if (!arena_src) { hon_arena_free(arena); return NULL; }
    memcpy(arena_src, src, len);
    arena_src[len] = '\0';

    parser p = {
        .lex       = hon_lexer_new(arena, arena_src, len),
        .arena     = arena,
        .diags     = diags,
        .type_cap  = 64,
        .type_count = 0,
        .var_cap   = 16,
        .var_count = 0,
        .comment_cap = 16,
        .comment_count = 0,
        .depth     = 0,
        .max_depth = MAX_DEPTH_DEFAULT,
    };

    if (diags) hon_diag_init(diags, arena);

    if (!valid_utf8(arena_src, len)) {
        set_invalid_utf8_diagnostic(diags);
        hon_arena_free(arena);
        return NULL;
    }

    p.types = hon_arena_alloc(arena, sizeof(hon_type *) * p.type_cap,
                              _Alignof(hon_type *));
    p.vars  = hon_arena_alloc(arena, sizeof(hon_variable) * p.var_cap,
                              _Alignof(hon_variable));
    p.comments = hon_arena_alloc(arena, sizeof(hon_str) * p.comment_cap,
                                 _Alignof(hon_str));

    hon_document *doc = parse_document(&p);
    if (!doc) {
        diag_detach_from_arena(diags);
        hon_arena_free(arena);
        return NULL;
    }

    return doc;
}

hon_document *hon_parse_with_registry(const char *src, size_t len,
                                       const hon_registry *reg,
                                       hon_diag_list *diags) {
    init_primitives();

    hon_arena *arena = hon_arena_new(0);
    if (!arena) return NULL;

    char *arena_src = hon_arena_alloc(arena, len + 1, 1);
    if (!arena_src) { hon_arena_free(arena); return NULL; }
    memcpy(arena_src, src, len);
    arena_src[len] = '\0';

    parser p = {
        .lex       = hon_lexer_new(arena, arena_src, len),
        .arena     = arena,
        .diags     = diags,
        .type_cap  = 64,
        .type_count = 0,
        .var_cap   = 16,
        .var_count = 0,
        .comment_cap = 16,
        .comment_count = 0,
        .depth     = 0,
        .max_depth = MAX_DEPTH_DEFAULT,
        .registry  = reg,
    };

    if (diags) hon_diag_init(diags, arena);

    if (!valid_utf8(arena_src, len)) {
        set_invalid_utf8_diagnostic(diags);
        hon_arena_free(arena);
        return NULL;
    }

    p.types = hon_arena_alloc(arena, sizeof(hon_type *) * p.type_cap,
                              _Alignof(hon_type *));
    p.vars  = hon_arena_alloc(arena, sizeof(hon_variable) * p.var_cap,
                              _Alignof(hon_variable));
    p.comments = hon_arena_alloc(arena, sizeof(hon_str) * p.comment_cap,
                                 _Alignof(hon_str));

    hon_document *doc = parse_document(&p);
    if (!doc) {
        diag_detach_from_arena(diags);
        hon_arena_free(arena);
        return NULL;
    }

    return doc;
}

static bool diagnostics_have_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

static hon_document *validate_strict_result(hon_document *doc,
                                            hon_diag_list *working_diags,
                                            hon_diag_list *caller_diags,
                                            bool require_data) {
    if (!doc) {
        if (!caller_diags) hon_diag_free(working_diags);
        return NULL;
    }

    bool wrong_shape = require_data ? doc->var_count == 0 : doc->var_count > 0;
    if (wrong_shape) {
        hon_diag_push(working_diags, doc->arena, HON_DIAG_ERROR, 1, 1,
                      require_data
                          ? "document requires a data section"
                          : "rulebook must not contain variable assignments");
    }

    if (wrong_shape || diagnostics_have_errors(working_diags)) {
        if (caller_diags) diag_detach_from_arena(caller_diags);
        hon_doc_free(doc);
        return NULL;
    }
    return doc;
}

hon_document *hon_parse_document(const char *src, size_t len,
                                 hon_diag_list *diags) {
    hon_diag_list local_diags = {0};
    hon_diag_list *working_diags = diags ? diags : &local_diags;
    hon_document *doc = hon_parse(src, len, working_diags);
    return validate_strict_result(doc, working_diags, diags, true);
}

hon_document *hon_parse_document_with_registry(const char *src, size_t len,
                                                const hon_registry *reg,
                                                hon_diag_list *diags) {
    hon_diag_list local_diags = {0};
    hon_diag_list *working_diags = diags ? diags : &local_diags;
    hon_document *doc = hon_parse_with_registry(src, len, reg, working_diags);
    return validate_strict_result(doc, working_diags, diags, true);
}

hon_document *hon_parse_rulebook(const char *src, size_t len,
                                 hon_diag_list *diags) {
    hon_diag_list local_diags = {0};
    hon_diag_list *working_diags = diags ? diags : &local_diags;
    hon_document *doc = hon_parse(src, len, working_diags);
    return validate_strict_result(doc, working_diags, diags, false);
}

typedef hon_document *(*file_parser)(const char *, size_t, hon_diag_list *);

static hon_document *parse_file_using(const char *path, hon_diag_list *diags,
                                      file_parser parse_source) {
    if (!path || !parse_source) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long fsize = ftell(f);
    if (fsize < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (uintmax_t)fsize >= SIZE_MAX) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    if (ferror(f) || fclose(f) != 0) {
        free(buf);
        return NULL;
    }
    buf[nread] = '\0';

    hon_document *doc = parse_source(buf, nread, diags);
    free(buf);
    return doc;
}

hon_document *hon_parse_file(const char *path, hon_diag_list *diags) {
    return parse_file_using(path, diags, hon_parse);
}

hon_document *hon_parse_document_file(const char *path, hon_diag_list *diags) {
    return parse_file_using(path, diags, hon_parse_document);
}

hon_document *hon_parse_rulebook_file(const char *path, hon_diag_list *diags) {
    return parse_file_using(path, diags, hon_parse_rulebook);
}

void hon_doc_free(hon_document *doc) {
    if (doc) hon_arena_free(doc->arena);
}
