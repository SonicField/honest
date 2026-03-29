/*
 * Honest serialiser — produces document-mode and wire-mode output.
 *
 * Document mode: human-readable with indentation.
 * Wire mode: minimal whitespace, no comments.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

/* ---- Forward Declarations ---- */

/* ---- Output Buffer ---- */

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
    int     indent;
    hon_serial_mode mode;
} outbuf;

static void buf_init(outbuf *b, hon_serial_mode mode) {
    b->cap    = 4096;
    b->data   = malloc(b->cap);
    if (!b->data) { b->cap = 0; }
    b->len    = 0;
    b->indent = 0;
    b->mode   = mode;
}

static void buf_grow(outbuf *b, size_t needed) {
    while (b->len + needed >= b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return; /* OOM — subsequent writes will be truncated */
        b->data = new_data;
        b->cap = new_cap;
    }
}

static void buf_append(outbuf *b, const char *s, size_t len) {
    buf_grow(b, len);
    memcpy(b->data + b->len, s, len);
    b->len += len;
}

static void buf_str(outbuf *b, const char *s) {
    buf_append(b, s, strlen(s));
}

static void buf_char(outbuf *b, char c) {
    buf_grow(b, 1);
    b->data[b->len++] = c;
}

static void buf_nl(outbuf *b) {
    if (b->mode == HON_MODE_DOCUMENT) {
        buf_char(b, '\n');
        for (int i = 0; i < b->indent; i++) {
            buf_str(b, "  ");
        }
    } else {
        buf_char(b, ' ');
    }
}

static void buf_printf(outbuf *b, const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0) buf_append(b, tmp, (size_t)n);
}

/* ---- Escape String ---- */

static void buf_escaped_string(outbuf *b, hon_str s) {
    buf_char(b, '\'');
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '\'') {
            buf_str(b, "''");
        } else if (c == '\\') {
            buf_str(b, "\\\\");
        } else if (c == '\n') {
            buf_str(b, "\\n");
        } else if (c == '\r') {
            buf_str(b, "\\r");
        } else if (c == '\t') {
            buf_str(b, "\\t");
        } else if (c < 0x20 && c != '\t') {
            buf_printf(b, "\\u%04X", c);
        } else {
            buf_char(b, (char)c);
        }
    }
    buf_char(b, '\'');
}

/* ---- Serialise Types ---- */

static void serialise_type(outbuf *b, const hon_type *t);

static void serialise_field_list(outbuf *b, const hon_field *fields, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        buf_nl(b);
        buf_append(b, fields[i].name.data, fields[i].name.len);
        buf_str(b, " : ");
        buf_append(b, fields[i].type->name.data, fields[i].type->name.len);
        buf_char(b, ';');
    }
}

static void serialise_type(outbuf *b, const hon_type *t) {
    buf_append(b, t->name.data, t->name.len);
    buf_str(b, " = ");

    switch (t->kind) {
    case HON_TYPE_ENUM:
        buf_char(b, '(');
        for (uint32_t i = 0; i < t->u.enumeration.value_count; i++) {
            if (i > 0) buf_str(b, ", ");
            buf_append(b, t->u.enumeration.values[i].data,
                       t->u.enumeration.values[i].len);
        }
        buf_char(b, ')');
        break;

    case HON_TYPE_RECORD: {
        buf_str(b, "record");
        b->indent++;
        serialise_field_list(b, t->u.record.fields, t->u.record.field_count);

        if (t->u.record.has_variant) {
            buf_nl(b);
            buf_str(b, "case ");
            buf_append(b, t->u.record.tag_name.data, t->u.record.tag_name.len);
            buf_str(b, " : ");
            buf_append(b, t->u.record.tag_type->name.data,
                       t->u.record.tag_type->name.len);
            buf_str(b, " of");

            b->indent++;
            for (uint32_t i = 0; i < t->u.record.arm_count; i++) {
                const hon_variant *arm = &t->u.record.arms[i];
                buf_nl(b);
                buf_append(b, arm->label.data, arm->label.len);
                buf_str(b, " : (");
                for (uint32_t j = 0; j < arm->field_count; j++) {
                    if (j > 0) buf_str(b, "; ");
                    buf_append(b, arm->fields[j].name.data,
                               arm->fields[j].name.len);
                    buf_str(b, " : ");
                    buf_append(b, arm->fields[j].type->name.data,
                               arm->fields[j].type->name.len);
                }
                buf_str(b, ");");
            }
            b->indent--;
        }

        b->indent--;
        buf_nl(b);
        buf_str(b, "end");
        break;
    }

    case HON_TYPE_SEQUENCE:
        buf_str(b, "sequence of ");
        buf_append(b, t->u.sequence.element_type->name.data,
                   t->u.sequence.element_type->name.len);
        break;

    case HON_TYPE_ALIAS:
        buf_append(b, t->u.alias.target->name.data,
                   t->u.alias.target->name.len);
        break;

    case HON_TYPE_PRIMITIVE:
        buf_append(b, t->name.data, t->name.len);
        break;
    }
}

/* ---- Serialise Values ---- */

static void serialise_value(outbuf *b, const hon_value *v);

static void serialise_value(outbuf *b, const hon_value *v) {
    if (!v) {
        buf_str(b, "{ ERROR: null value }");
        return;
    }

    switch (v->kind) {
    case HON_VAL_BOOL:
        buf_str(b, v->u.boolean ? "True" : "False");
        break;

    case HON_VAL_INT:
        buf_printf(b, "%" PRId64, v->u.integer);
        break;

    case HON_VAL_REAL:
        buf_printf(b, "%.17g", v->u.real);
        break;

    case HON_VAL_CHAR:
        buf_char(b, '\'');
        if (v->u.character < 0x80) {
            char c = (char)v->u.character;
            if (c == '\'') buf_str(b, "''");
            else if (c == '\\') buf_str(b, "\\\\");
            else if (c == '\n') buf_str(b, "\\n");
            else if (c == '\r') buf_str(b, "\\r");
            else if (c == '\t') buf_str(b, "\\t");
            else buf_char(b, c);
        } else {
            /* Encode as UTF-8 */
            uint32_t cp = v->u.character;
            char utf8[4];
            int len = 0;
            if (cp < 0x800) {
                utf8[0] = (char)(0xC0 | (cp >> 6));
                utf8[1] = (char)(0x80 | (cp & 0x3F));
                len = 2;
            } else if (cp < 0x10000) {
                utf8[0] = (char)(0xE0 | (cp >> 12));
                utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[2] = (char)(0x80 | (cp & 0x3F));
                len = 3;
            } else {
                utf8[0] = (char)(0xF0 | (cp >> 18));
                utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[3] = (char)(0x80 | (cp & 0x3F));
                len = 4;
            }
            buf_append(b, utf8, (size_t)len);
        }
        buf_char(b, '\'');
        break;

    case HON_VAL_STRING:
        buf_escaped_string(b, v->u.string);
        break;

    case HON_VAL_ENUM:
        buf_append(b, v->u.enum_val.data, v->u.enum_val.len);
        break;

    case HON_VAL_RECORD: {
        buf_char(b, '(');
        b->indent++;

        for (uint32_t i = 0; i < v->u.record.field_count; i++) {
            if (i > 0) buf_char(b, ';');
            buf_nl(b);
            buf_append(b, v->u.record.fields[i].name.data,
                       v->u.record.fields[i].name.len);
            buf_str(b, " : ");
            serialise_value(b, v->u.record.fields[i].value);
        }

        /* Tag value */
        if (v->u.record.tag_value.data) {
            if (v->u.record.field_count > 0) buf_char(b, ';');
            buf_nl(b);
            /* We need the tag field name from the type */
            hon_type *resolved = hon_resolve_alias(v->type);
            if (resolved && resolved->kind == HON_TYPE_RECORD &&
                resolved->u.record.has_variant) {
                buf_append(b, resolved->u.record.tag_name.data,
                           resolved->u.record.tag_name.len);
            } else {
                buf_str(b, "tag");
            }
            buf_str(b, " : ");
            buf_append(b, v->u.record.tag_value.data,
                       v->u.record.tag_value.len);

            /* Variant fields */
            for (uint32_t i = 0; i < v->u.record.variant_field_count; i++) {
                buf_char(b, ';');
                buf_nl(b);
                buf_append(b, v->u.record.variant_fields[i].name.data,
                           v->u.record.variant_fields[i].name.len);
                buf_str(b, " : ");
                serialise_value(b, v->u.record.variant_fields[i].value);
            }
        }

        buf_char(b, ';');
        b->indent--;
        buf_nl(b);
        buf_char(b, ')');
        break;
    }

    case HON_VAL_SEQUENCE:
        buf_char(b, '(');
        if (v->u.sequence.count > 0) {
            b->indent++;
            for (uint32_t i = 0; i < v->u.sequence.count; i++) {
                if (i > 0) buf_char(b, ';');
                buf_nl(b);
                serialise_value(b, &v->u.sequence.elements[i]);
            }
            b->indent--;
        }
        buf_char(b, ')');
        break;
    }
}

/* ---- Public API ---- */

char *hon_serialise(const hon_document *doc, hon_serial_mode mode, size_t *out_len) {
    outbuf b;
    buf_init(&b, mode);

    /* Declarations section */
    buf_str(&b, "type");
    b.indent++;

    for (uint32_t i = 0; i < doc->type_count; i++) {
        buf_nl(&b);
        serialise_type(&b, doc->types[i]);
        buf_char(&b, ';');
        if (mode == HON_MODE_DOCUMENT && i < doc->type_count - 1) {
            buf_char(&b, '\n'); /* extra blank line between types in doc mode */
        }
    }

    b.indent--;
    buf_char(&b, '\n');
    buf_char(&b, '\n');

    /* Data section */
    for (uint32_t i = 0; i < doc->var_count; i++) {
        buf_str(&b, "var ");
        buf_append(&b, doc->vars[i].name.data, doc->vars[i].name.len);
        buf_str(&b, " : ");
        buf_append(&b, doc->vars[i].type->name.data,
                   doc->vars[i].type->name.len);
        buf_str(&b, " = ");
        serialise_value(&b, doc->vars[i].value);
        buf_str(&b, ";\n");
    }

    buf_char(&b, '\0');

    if (out_len) *out_len = b.len - 1; /* exclude null terminator */
    return b.data;
}

int hon_serialise_to(const hon_document *doc, hon_serial_mode mode, FILE *out) {
    size_t len;
    char *s = hon_serialise(doc, mode, &len);
    if (!s) return -1;
    fwrite(s, 1, len, out);
    free(s);
    return 0;
}
