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

/* ---- Helpers ---- */

static hon_token next_skip_comments(parser *p) {
    for (;;) {
        hon_token t = hon_lexer_next(p->lex);
        if (t.kind != HON_TOK_COMMENT) return t;
    }
}

static hon_token peek_skip_comments(parser *p) {
    /* Peek but skip comments — consume comments as side effect */
    for (;;) {
        hon_token t = hon_lexer_peek(p->lex);
        if (t.kind != HON_TOK_COMMENT) return t;
        hon_lexer_next(p->lex); /* consume the comment */
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
    hon_str values[256]; /* reasonable max for enum values */
    uint32_t count = 0;

    for (;;) {
        hon_token val = expect_skip(p, HON_TOK_IDENT);
        if (val.kind == HON_TOK_ERROR) return NULL;

        /* Check for duplicate enum value */
        for (uint32_t i = 0; i < count; i++) {
            if (hon_str_eq(values[i], val.text)) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              val.line, val.col,
                              "duplicate enumeration value '%.*s'",
                              (int)val.text.len, val.text.data);
            }
        }

        if (count < 256) {
            values[count++] = val.text;
        }

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
    t->u.enumeration.values = hon_arena_alloc(p->arena,
        sizeof(hon_str) * count, _Alignof(hon_str));
    memcpy(t->u.enumeration.values, values, sizeof(hon_str) * count);

    return t;
}

/* ---- Parse Field List ---- */

/* Parse fields until we hit 'case', 'end', or ')'.
   Returns field count. Fields stored in caller-provided buffer. */
static uint32_t parse_fields(parser *p, hon_field *fields, uint32_t max_fields,
                             bool in_variant_arm) {
    uint32_t count = 0;

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
        hon_token colon = peek_skip_comments(p);
        if (colon.kind != HON_TOK_COLON) {
            /* Not a field — push back by reporting error */
            hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                          colon.line, colon.col,
                          "expected ':' after field name '%.*s'",
                          (int)name_tok.text.len, name_tok.text.data);
            return count;
        }
        next_skip_comments(p); /* consume ':' */

        hon_token type_tok = expect_skip(p, HON_TOK_IDENT);
        if (type_tok.kind == HON_TOK_ERROR) return count;

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

        if (count < max_fields) {
            fields[count].name = name_tok.text;
            fields[count].type = ftype;
            count++;
        }

        /* Optional semicolon — may be followed by more fields, 'case', 'end', or ')' */
        peek = peek_skip_comments(p);
        if (peek.kind == HON_TOK_SEMICOLON) {
            next_skip_comments(p); /* consume ';' */
        }
    }

    return count;
}

/* ---- Parse Variant Part ---- */

static bool parse_variant_part(parser *p, hon_type *rec) {
    /* 'case' already peeked but not consumed */
    next_skip_comments(p); /* consume 'case' */

    hon_token tag_name = expect_skip(p, HON_TOK_IDENT);
    if (tag_name.kind == HON_TOK_ERROR) return false;

    expect_skip(p, HON_TOK_COLON);

    hon_token tag_type_name = expect_skip(p, HON_TOK_IDENT);
    if (tag_type_name.kind == HON_TOK_ERROR) return false;

    hon_type *tag_type = resolve_type_ref(p, tag_type_name.text,
                                           tag_type_name.line, tag_type_name.col);

    expect_skip(p, HON_TOK_OF);

    rec->u.record.has_variant = true;
    rec->u.record.tag_name    = tag_name.text;
    rec->u.record.tag_type    = tag_type;

    /* Parse variant arms */
    hon_variant arms[64];
    uint32_t arm_count = 0;

    for (;;) {
        hon_token peek = peek_skip_comments(p);
        /* Variant arm labels can be identifiers, True, or False */
        if (peek.kind != HON_TOK_IDENT && peek.kind != HON_TOK_TRUE &&
            peek.kind != HON_TOK_FALSE)
            break;

        hon_token label = next_skip_comments(p); /* consume label */
        expect_skip(p, HON_TOK_COLON);
        expect_skip(p, HON_TOK_LPAREN);

        hon_field arm_fields[64];
        uint32_t fcount = 0;

        if (peek_skip_comments(p).kind != HON_TOK_RPAREN) {
            fcount = parse_fields(p, arm_fields, 64, true);
        }

        expect_skip(p, HON_TOK_RPAREN);

        /* Optional semicolon after arm */
        if (peek_skip_comments(p).kind == HON_TOK_SEMICOLON)
            next_skip_comments(p);

        if (arm_count < 64) {
            arms[arm_count].label       = label.text;
            arms[arm_count].field_count = fcount;
            arms[arm_count].fields      = NULL;
            if (fcount > 0) {
                arms[arm_count].fields = hon_arena_alloc(p->arena,
                    sizeof(hon_field) * fcount, _Alignof(hon_field));
                memcpy(arms[arm_count].fields, arm_fields, sizeof(hon_field) * fcount);
            }
            arm_count++;
        }
    }

    rec->u.record.arm_count = arm_count;
    rec->u.record.arms = hon_arena_alloc(p->arena,
        sizeof(hon_variant) * arm_count, _Alignof(hon_variant));
    memcpy(rec->u.record.arms, arms, sizeof(hon_variant) * arm_count);

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
    hon_field fields[128];
    uint32_t fcount = parse_fields(p, fields, 128, false);

    t->u.record.field_count = fcount;
    t->u.record.fields = NULL;
    if (fcount > 0) {
        t->u.record.fields = hon_arena_alloc(p->arena,
            sizeof(hon_field) * fcount, _Alignof(hon_field));
        memcpy(t->u.record.fields, fields, sizeof(hon_field) * fcount);
    }

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

    /* Check for duplicate type name */
    if (lookup_type(p, name_tok.text)) {
        hon_diag_push(p->diags, p->arena, HON_DIAG_WARNING,
                      name_tok.line, name_tok.col,
                      "type '%.*s' redeclared (shadows previous definition)",
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
                /* Type exists in rulebook — check for divergence */
                bool divergent = false;
                if (rb_type->kind != t->kind) {
                    divergent = true;
                } else if (rb_type->kind == HON_TYPE_ENUM) {
                    if (rb_type->u.enumeration.value_count != t->u.enumeration.value_count) {
                        divergent = true;
                    } else {
                        for (uint32_t i = 0; i < rb_type->u.enumeration.value_count; i++) {
                            if (!hon_str_eq(rb_type->u.enumeration.values[i],
                                            t->u.enumeration.values[i])) {
                                divergent = true;
                                break;
                            }
                        }
                    }
                } else if (rb_type->kind == HON_TYPE_RECORD) {
                    if (rb_type->u.record.field_count != t->u.record.field_count) {
                        divergent = true;
                    }
                }

                if (divergent) {
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
                size_t len = t.text.len < 31 ? t.text.len : 31;
                memcpy(buf, t.text.data, len);
                buf[len] = '\0';
                val->u.integer = strtoll(buf, NULL, 10);
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
            if (t.kind != HON_TOK_REAL && t.kind != HON_TOK_INT) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected numeric literal");
                val = NULL;
            } else {
                val->kind = HON_VAL_REAL;
                char buf[64];
                size_t len = t.text.len < 63 ? t.text.len : 63;
                memcpy(buf, t.text.data, len);
                buf[len] = '\0';
                val->u.real = strtod(buf, NULL);
            }
            break;

        case HON_PRIM_CHAR:
            if (t.kind != HON_TOK_STRING) {
                hon_diag_push(p->diags, p->arena, HON_DIAG_ERROR,
                              t.line, t.col, "expected character literal");
                val = NULL;
            } else {
                hon_str unesc = unescape_string(p, t.text);
                val->kind = HON_VAL_CHAR;
                /* Decode first UTF-8 codepoint */
                if (unesc.len > 0) {
                    const unsigned char *u = (const unsigned char *)unesc.data;
                    if (u[0] < 0x80) val->u.character = u[0];
                    else if ((u[0] & 0xE0) == 0xC0) val->u.character = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
                    else if ((u[0] & 0xF0) == 0xE0) val->u.character = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
                    else if ((u[0] & 0xF8) == 0xF0) val->u.character = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
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
            /* Expect semicolon between fields */
            hon_token sep = peek_skip_comments(p);
            if (sep.kind == HON_TOK_SEMICOLON)
                next_skip_comments(p);
        }

        hon_token fname = expect_skip(p, HON_TOK_IDENT);
        if (fname.kind == HON_TOK_ERROR) return NULL;

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
        /* Semicolon before tag field */
        if (fixed_count > 0 || peek_skip_comments(p).kind == HON_TOK_SEMICOLON) {
            hon_token sep = peek_skip_comments(p);
            if (sep.kind == HON_TOK_SEMICOLON)
                next_skip_comments(p);
        }

        /* Parse tag field: name : value */
        hon_token tag_fname = expect_skip(p, HON_TOK_IDENT);
        if (tag_fname.kind == HON_TOK_ERROR) return NULL;
        expect_skip(p, HON_TOK_COLON);

        hon_token tag_val = next_skip_comments(p);
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
                if (sep.kind == HON_TOK_SEMICOLON)
                    next_skip_comments(p);

                hon_token vfname = expect_skip(p, HON_TOK_IDENT);
                if (vfname.kind == HON_TOK_ERROR) return NULL;
                expect_skip(p, HON_TOK_COLON);

                hon_value *vfval = parse_value(p, matched_arm->fields[i].type);
                if (!vfval) return NULL;

                vfields[i].name  = vfname.text;
                vfields[i].value = vfval;
            }

            val->u.record.variant_fields      = vfields;
            val->u.record.variant_field_count = matched_arm->field_count;
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
        hon_diag_push(p->diags, p->arena, HON_DIAG_WARNING,
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

    return doc;
}

/* ---- Public API ---- */

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
        .depth     = 0,
        .max_depth = MAX_DEPTH_DEFAULT,
    };

    if (diags) hon_diag_init(diags, arena);

    p.types = hon_arena_alloc(arena, sizeof(hon_type *) * p.type_cap,
                              _Alignof(hon_type *));
    p.vars  = hon_arena_alloc(arena, sizeof(hon_variable) * p.var_cap,
                              _Alignof(hon_variable));

    hon_document *doc = parse_document(&p);
    if (!doc) {
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
        .depth     = 0,
        .max_depth = MAX_DEPTH_DEFAULT,
        .registry  = reg,
    };

    if (diags) hon_diag_init(diags, arena);

    p.types = hon_arena_alloc(arena, sizeof(hon_type *) * p.type_cap,
                              _Alignof(hon_type *));
    p.vars  = hon_arena_alloc(arena, sizeof(hon_variable) * p.var_cap,
                              _Alignof(hon_variable));

    hon_document *doc = parse_document(&p);
    if (!doc) {
        hon_arena_free(arena);
        return NULL;
    }

    return doc;
}

hon_document *hon_parse_file(const char *path, hon_diag_list *diags) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    hon_document *doc = hon_parse(buf, nread, diags);
    free(buf);
    return doc;
}

void hon_doc_free(hon_document *doc) {
    if (doc) hon_arena_free(doc->arena);
}
