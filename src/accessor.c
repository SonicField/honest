#define _POSIX_C_SOURCE 200809L

/*
 * Field accessor API for libhonest.
 *
 * Typed field extraction by variable name and field path,
 * with alias resolution and dot-path navigation.
 */

#include "honest.h"
#include <string.h>
#include <stdlib.h>

/* Find a variable by name in a parsed document. */
static const hon_variable *find_var(const hon_document *doc, const char *var_name) {
    hon_str vname = {var_name, strlen(var_name)};
    for (uint32_t i = 0; i < doc->var_count; i++) {
        if (hon_str_eq(doc->vars[i].name, vname))
            return &doc->vars[i];
    }
    return NULL;
}

/* Navigate a dot-separated field path from a value.
 * Supports record field names and numeric sequence indices. */
static const hon_value *navigate(const hon_value *val, const char *field_name) {
    if (!val || !field_name || field_name[0] == '\0')
        return val;

    char *pathbuf = strdup(field_name);
    if (!pathbuf) return NULL;

    const hon_value *cur = val;
    char *tok = strtok(pathbuf, ".");

    while (tok && cur) {
        if (cur->kind == HON_VAL_RECORD) {
            hon_str fname = {tok, strlen(tok)};
            const hon_value *found = NULL;
            for (uint32_t i = 0; i < cur->u.record.field_count; i++) {
                if (hon_str_eq(cur->u.record.fields[i].name, fname)) {
                    found = cur->u.record.fields[i].value;
                    break;
                }
            }
            if (!found) {
                for (uint32_t i = 0; i < cur->u.record.variant_field_count; i++) {
                    if (hon_str_eq(cur->u.record.variant_fields[i].name, fname)) {
                        found = cur->u.record.variant_fields[i].value;
                        break;
                    }
                }
            }
            cur = found;
        } else if (cur->kind == HON_VAL_SEQUENCE) {
            char *endp;
            long idx = strtol(tok, &endp, 10);
            if (*endp == '\0' && idx >= 0 && (uint32_t)idx < cur->u.sequence.count)
                cur = &cur->u.sequence.elements[idx];
            else
                cur = NULL;
        } else {
            cur = NULL;
        }
        tok = strtok(NULL, ".");
    }

    free(pathbuf);
    return cur;
}

int hon_get_string(const hon_document *doc,
                   const char *var_name,
                   const char *field_name,
                   char *buf, size_t bufsz) {
    if (!doc || !var_name || !field_name || !buf || bufsz == 0)
        return -1;

    const hon_variable *var = find_var(doc, var_name);
    if (!var) return -1;

    const hon_value *val = navigate(var->value, field_name);
    if (!val) return -1;

    /* Resolve alias on the value's type to check actual kind */
    hon_type *resolved = val->type ? hon_resolve_alias(val->type) : NULL;

    if (val->kind == HON_VAL_STRING) {
        size_t len = val->u.string.len;
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, val->u.string.data, len);
        buf[len] = '\0';
        return 0;
    }

    /* Also accept enum values as strings for convenience */
    if (val->kind == HON_VAL_ENUM) {
        size_t len = val->u.enum_val.len;
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, val->u.enum_val.data, len);
        buf[len] = '\0';
        return 0;
    }

    /* Check if the underlying type is String (or alias of String) */
    if (resolved && resolved->kind == HON_TYPE_PRIMITIVE &&
        resolved->u.prim == HON_PRIM_STRING) {
        /* The value should have been a string but wasn't */
        return -2;
    }

    return -2; /* not a string type */
}

int hon_get_long(const hon_document *doc,
                 const char *var_name,
                 const char *field_name,
                 long *out) {
    if (!doc || !var_name || !field_name || !out)
        return -1;

    const hon_variable *var = find_var(doc, var_name);
    if (!var) return -1;

    const hon_value *val = navigate(var->value, field_name);
    if (!val) return -1;

    if (val->kind == HON_VAL_INT) {
        *out = (long)val->u.integer;
        return 0;
    }

    return -2; /* not an integer type */
}
