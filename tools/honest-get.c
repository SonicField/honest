#define _POSIX_C_SOURCE 200809L

/*
 * honest-get — Extract a value from an Honest document.
 *
 * Usage: honest-get <file.honest> <var-name> [field.path]
 *
 * Prints the value of the named variable, or a nested field if a
 * dotted path is provided. Output is in Honest value syntax.
 *
 * Designed for AI agents and shell scripts to query Honest documents.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void print_value(const hon_value *v, int indent);

static void print_indent(int n) {
    for (int i = 0; i < n; i++) printf("  ");
}

static void print_escaped_string(hon_str s) {
    putchar('\'');
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '\'') printf("''");
        else if (c == '\\') printf("\\\\");
        else if (c == '\n') printf("\\n");
        else if (c == '\r') printf("\\r");
        else if (c == '\t') printf("\\t");
        else putchar(c);
    }
    putchar('\'');
}

static void print_value(const hon_value *v, int indent) {
    if (!v) { printf("{ null }"); return; }

    switch (v->kind) {
    case HON_VAL_BOOL:
        printf("%s", v->u.boolean ? "True" : "False");
        break;
    case HON_VAL_INT:
        printf("%" PRId64, v->u.integer);
        break;
    case HON_VAL_REAL:
        printf("%.17g", v->u.real);
        break;
    case HON_VAL_CHAR:
        putchar('\'');
        /* simple: just print the char */
        if (v->u.character < 128) putchar((char)v->u.character);
        else printf("\\u%04X", v->u.character);
        putchar('\'');
        break;
    case HON_VAL_STRING:
        print_escaped_string(v->u.string);
        break;
    case HON_VAL_ENUM:
        printf("%.*s", (int)v->u.enum_val.len, v->u.enum_val.data);
        break;
    case HON_VAL_RECORD:
        printf("(\n");
        for (uint32_t i = 0; i < v->u.record.field_count; i++) {
            if (i > 0) printf(";\n");
            print_indent(indent + 1);
            printf("%.*s : ", (int)v->u.record.fields[i].name.len,
                   v->u.record.fields[i].name.data);
            print_value(v->u.record.fields[i].value, indent + 1);
        }
        if (v->u.record.tag_value.data) {
            if (v->u.record.field_count > 0) printf(";\n");
            print_indent(indent + 1);
            printf("{ tag } : %.*s",
                   (int)v->u.record.tag_value.len,
                   v->u.record.tag_value.data);
            for (uint32_t i = 0; i < v->u.record.variant_field_count; i++) {
                printf(";\n");
                print_indent(indent + 1);
                printf("%.*s : ", (int)v->u.record.variant_fields[i].name.len,
                       v->u.record.variant_fields[i].name.data);
                print_value(v->u.record.variant_fields[i].value, indent + 1);
            }
        }
        printf("\n");
        print_indent(indent);
        printf(")");
        break;
    case HON_VAL_SEQUENCE:
        if (v->u.sequence.count == 0) {
            printf("()");
        } else {
            printf("(\n");
            for (uint32_t i = 0; i < v->u.sequence.count; i++) {
                if (i > 0) printf(";\n");
                print_indent(indent + 1);
                print_value(&v->u.sequence.elements[i], indent + 1);
            }
            printf("\n");
            print_indent(indent);
            printf(")");
        }
        break;
    }
}

static const hon_value *find_field(const hon_value *v, hon_str name) {
    if (!v || v->kind != HON_VAL_RECORD) return NULL;

    for (uint32_t i = 0; i < v->u.record.field_count; i++) {
        if (hon_str_eq(v->u.record.fields[i].name, name))
            return v->u.record.fields[i].value;
    }
    /* Check variant fields */
    for (uint32_t i = 0; i < v->u.record.variant_field_count; i++) {
        if (hon_str_eq(v->u.record.variant_fields[i].name, name))
            return v->u.record.variant_fields[i].value;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <file.honest> <var-name> [field.path]\n",
                argv[0]);
        return 2;
    }

    const char *path     = argv[1];
    const char *var_name = argv[2];
    const char *field_path = argc > 3 ? argv[3] : NULL;

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_file(path, &diags);
    if (!doc) {
        for (uint32_t i = 0; i < diags.count; i++) {
            fprintf(stderr, "%u:%u: %s\n",
                    diags.items[i].line, diags.items[i].col,
                    diags.items[i].message);
        }
        return 1;
    }

    /* Find variable */
    hon_str vname = {var_name, strlen(var_name)};
    const hon_value *val = NULL;
    for (uint32_t i = 0; i < doc->var_count; i++) {
        if (hon_str_eq(doc->vars[i].name, vname)) {
            val = doc->vars[i].value;
            break;
        }
    }

    if (!val) {
        fprintf(stderr, "Variable '%s' not found\n", var_name);
        hon_doc_free(doc);
        return 1;
    }

    /* Navigate field path */
    if (field_path) {
        char *pathbuf = strdup(field_path);
        char *tok = strtok(pathbuf, ".");
        while (tok && val) {
            hon_str fname = {tok, strlen(tok)};
            val = find_field(val, fname);
            tok = strtok(NULL, ".");
        }
        free(pathbuf);

        if (!val) {
            fprintf(stderr, "Field path '%s' not found\n", field_path);
            hon_doc_free(doc);
            return 1;
        }
    }

    print_value(val, 0);
    printf("\n");

    hon_doc_free(doc);
    return 0;
}
