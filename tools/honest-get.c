#define _POSIX_C_SOURCE 200809L

/*
 * honest-get — Extract field values from an Honest document.
 *
 * Usage: honest-get <file | -> <var-name> <field> [field ...]
 *
 * Prints the raw value of each named field, one per line.
 * Strings and enums are unquoted. Booleans are lowercase (true/false).
 * Use - as the file argument to read from stdin.
 *
 * Exit codes: 0 = all fields found, 1 = parse error or missing field, 2 = usage error.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void print_raw_value(const hon_value *v) {
    if (!v) return;

    switch (v->kind) {
    case HON_VAL_BOOL:
        printf("%s", v->u.boolean ? "true" : "false");
        break;
    case HON_VAL_INT:
        printf("%" PRId64, v->u.integer);
        break;
    case HON_VAL_REAL:
        printf("%.17g", v->u.real);
        break;
    case HON_VAL_CHAR:
        if (v->u.character < 128)
            putchar((char)v->u.character);
        else
            printf("\\u%04X", v->u.character);
        break;
    case HON_VAL_STRING:
        fwrite(v->u.string.data, 1, v->u.string.len, stdout);
        break;
    case HON_VAL_ENUM:
        fwrite(v->u.enum_val.data, 1, v->u.enum_val.len, stdout);
        break;
    case HON_VAL_RECORD:
    case HON_VAL_SEQUENCE:
        /* For compound values, fall back to Honest syntax */
        fprintf(stderr, "warning: compound value, printing type name\n");
        if (v->type)
            printf("%.*s", (int)v->type->name.len, v->type->name.data);
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

static const hon_value *navigate_path(const hon_value *val, const char *field_path) {
    char *pathbuf = strdup(field_path);
    if (!pathbuf) return NULL;

    char *tok = strtok(pathbuf, ".");
    while (tok && val) {
        /* Numeric index for sequences */
        char *endp;
        long idx = strtol(tok, &endp, 10);
        if (*endp == '\0' && val->kind == HON_VAL_SEQUENCE) {
            if (idx >= 0 && (uint32_t)idx < val->u.sequence.count)
                val = &val->u.sequence.elements[idx];
            else
                val = NULL;
        } else {
            hon_str fname = {tok, strlen(tok)};
            val = find_field(val, fname);
        }
        tok = strtok(NULL, ".");
    }
    free(pathbuf);
    return val;
}

static char *read_stdin(size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { fprintf(stderr, "Out of memory\n"); return NULL; }

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { fprintf(stderr, "Out of memory\n"); free(buf); return NULL; }
            buf = tmp;
        }
    }
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <file | -> <var-name> <field> [field ...]\n",
            argv[0]);
        return 2;
    }

    const char *path     = argv[1];
    const char *var_name = argv[2];

    /* Parse document */
    hon_diag_list diags = {0};
    hon_document *doc = NULL;

    if (strcmp(path, "-") == 0) {
        size_t len;
        char *src = read_stdin(&len);
        if (!src) {
            fprintf(stderr, "Failed to read stdin\n");
            return 1;
        }
        doc = hon_parse(src, len, &diags);
        free(src);
    } else {
        doc = hon_parse_file(path, &diags);
    }

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
    const hon_value *var_val = NULL;
    for (uint32_t i = 0; i < doc->var_count; i++) {
        if (hon_str_eq(doc->vars[i].name, vname)) {
            var_val = doc->vars[i].value;
            break;
        }
    }

    if (!var_val) {
        fprintf(stderr, "Variable '%s' not found\n", var_name);
        hon_doc_free(doc);
        return 1;
    }

    /* Extract each requested field */
    int any_missing = 0;
    for (int i = 3; i < argc; i++) {
        const hon_value *val = navigate_path(var_val, argv[i]);
        if (!val) {
            fprintf(stderr, "Field '%s' not found\n", argv[i]);
            any_missing = 1;
            printf("\n"); /* empty line for missing fields */
            continue;
        }
        print_raw_value(val);
        printf("\n");
    }

    hon_doc_free(doc);
    return any_missing ? 1 : 0;
}
