#define _POSIX_C_SOURCE 200809L

/*
 * honest-build — Construct an Honest document from CLI key=value arguments.
 *
 * Usage:
 *   honest-build --type TypeName [--rulebook file | --typedef 'type ...'] \
 *                field=value [field.subfield=value] ...
 *
 * Produces a self-describing Honest document on stdout.
 * Supports nested records via dot-paths and sequences via numeric indices.
 * Handles escaping of single quotes, backslashes, and special characters.
 *
 * Exit codes: 0 = success, 1 = error.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>

/* ---- Value Builder ---- */

/*
 * We build a tree of assignments from the CLI arguments, then
 * walk it against the type tree to produce hon_value nodes.
 *
 * An assignment is: path -> raw_value_string
 * A path is: ["field", "subfield", "0", ...]
 */

#define MAX_PATH_DEPTH 32
#define MAX_ASSIGNMENTS 256

typedef struct {
    char *segments[MAX_PATH_DEPTH];
    int   depth;
    char *raw_value;
} assignment;

static int parse_assignment(const char *arg, assignment *out) {
    const char *eq = strchr(arg, '=');
    if (!eq || eq == arg) return -1;

    /* Extract key part */
    size_t key_len = (size_t)(eq - arg);
    char *key = strndup(arg, key_len);
    if (!key) return -1;

    /* Split key on dots */
    out->depth = 0;
    char *tok = strtok(key, ".");
    while (tok && out->depth < MAX_PATH_DEPTH) {
        out->segments[out->depth++] = strdup(tok);
        tok = strtok(NULL, ".");
    }
    free(key);

    if (out->depth == 0) return -1;

    /* Value is everything after the = */
    out->raw_value = strdup(eq + 1);
    return 0;
}

static void free_assignment(assignment *a) {
    for (int i = 0; i < a->depth; i++)
        free(a->segments[i]);
    free(a->raw_value);
}

/* ---- Type-directed value construction ---- */

static hon_type *resolve_type(hon_type *t) {
    while (t && t->kind == HON_TYPE_ALIAS)
        t = t->u.alias.target;
    return t;
}

/* Check if a string is a valid integer. */
static int is_integer(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

/* Check if a string is a valid real number. */
static int is_real(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    int has_dot = 0, has_digit = 0;
    while (*s) {
        if (*s == '.') {
            if (has_dot) return 0;
            has_dot = 1;
        } else if (*s == 'e' || *s == 'E') {
            if (!has_digit) return 0;
            s++;
            if (*s == '+' || *s == '-') s++;
            if (!*s || !isdigit((unsigned char)*s)) return 0;
            while (*s && isdigit((unsigned char)*s)) s++;
            return *s == '\0';
        } else if (isdigit((unsigned char)*s)) {
            has_digit = 1;
        } else {
            return 0;
        }
        s++;
    }
    return has_digit && has_dot;
}

/* Build a primitive value from a raw string. */
static hon_value *build_primitive_value(hon_arena *a, hon_type *type,
                                         const char *raw) {
    hon_type *resolved = resolve_type(type);
    if (!resolved || resolved->kind != HON_TYPE_PRIMITIVE) return NULL;

    hon_value *v = hon_arena_alloc(a, sizeof(hon_value), _Alignof(hon_value));
    if (!v) return NULL;
    v->type = type;

    switch (resolved->u.prim) {
    case HON_PRIM_BOOLEAN:
        if (strcmp(raw, "True") == 0 || strcmp(raw, "true") == 0) {
            v->kind = HON_VAL_BOOL;
            v->u.boolean = true;
        } else if (strcmp(raw, "False") == 0 || strcmp(raw, "false") == 0) {
            v->kind = HON_VAL_BOOL;
            v->u.boolean = false;
        } else {
            fprintf(stderr, "Invalid Boolean value: '%s'\n", raw);
            return NULL;
        }
        return v;

    case HON_PRIM_BYTE:
    case HON_PRIM_WORD:
    case HON_PRIM_INTEGER:
    case HON_PRIM_LONGINT: {
        if (!is_integer(raw)) {
            fprintf(stderr, "Invalid integer value: '%s'\n", raw);
            return NULL;
        }
        errno = 0;
        int64_t ival = strtoll(raw, NULL, 10);
        if (errno == ERANGE) {
            fprintf(stderr, "Integer overflow: '%s'\n", raw);
            return NULL;
        }
        v->kind = HON_VAL_INT;
        v->u.integer = ival;
        return v;
    }

    case HON_PRIM_REAL: {
        if (!is_real(raw) && !is_integer(raw)) {
            fprintf(stderr, "Invalid real value: '%s'\n", raw);
            return NULL;
        }
        v->kind = HON_VAL_REAL;
        v->u.real = strtod(raw, NULL);
        return v;
    }

    case HON_PRIM_CHAR:
        if (strlen(raw) == 0) {
            fprintf(stderr, "Empty Char value\n");
            return NULL;
        }
        v->kind = HON_VAL_CHAR;
        v->u.character = (uint32_t)(unsigned char)raw[0];
        return v;

    case HON_PRIM_STRING:
        v->kind = HON_VAL_STRING;
        v->u.string = hon_str_dup(a, (hon_str){raw, strlen(raw)});
        return v;

    default:
        return NULL;
    }
}

/* Build an enum value. */
static hon_value *build_enum_value(hon_arena *a, hon_type *type,
                                    const char *raw) {
    hon_type *resolved = resolve_type(type);
    if (!resolved || resolved->kind != HON_TYPE_ENUM) return NULL;

    /* Validate against declared values */
    hon_str raw_str = {raw, strlen(raw)};
    int found = 0;
    for (uint32_t i = 0; i < resolved->u.enumeration.value_count; i++) {
        if (hon_str_eq(resolved->u.enumeration.values[i], raw_str)) {
            found = 1;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "Invalid enum value '%s' for type '%.*s'\n",
                raw, (int)type->name.len, type->name.data);
        return NULL;
    }

    hon_value *v = hon_arena_alloc(a, sizeof(hon_value), _Alignof(hon_value));
    if (!v) return NULL;
    v->kind = HON_VAL_ENUM;
    v->type = type;
    v->u.enum_val = hon_str_dup(a, raw_str);
    return v;
}

/*
 * Build a value for a given type from a set of assignments whose first
 * path segment has already been consumed.
 *
 * For leaf types: expects exactly one assignment with depth==0.
 * For records: expects assignments with depth>=1, first segment is field name.
 * For sequences: expects assignments with depth>=1, first segment is numeric index.
 */
static hon_value *build_value(hon_arena *a, hon_type *type,
                               assignment *assigns, int nassign,
                               int seg_offset);

/* Forward declaration for recursive building. */
static hon_value *build_record_value(hon_arena *a, hon_type *type,
                                      assignment *assigns, int nassign,
                                      int seg_offset) {
    hon_type *resolved = resolve_type(type);
    if (!resolved || resolved->kind != HON_TYPE_RECORD) return NULL;

    uint32_t nfields = resolved->u.record.field_count;
    hon_value *v = hon_arena_alloc(a, sizeof(hon_value), _Alignof(hon_value));
    if (!v) return NULL;
    v->kind = HON_VAL_RECORD;
    v->type = type;
    v->u.record.field_count = nfields;
    v->u.record.fields = hon_arena_alloc(a, sizeof(hon_field_val) * nfields,
                                          _Alignof(hon_field_val));
    v->u.record.tag_value = (hon_str){0};
    v->u.record.variant_fields = NULL;
    v->u.record.variant_field_count = 0;

    for (uint32_t fi = 0; fi < nfields; fi++) {
        hon_str fname = resolved->u.record.fields[fi].name;
        hon_type *ftype = resolved->u.record.fields[fi].type;

        v->u.record.fields[fi].name = fname;

        /* Collect assignments for this field */
        assignment sub_assigns[MAX_ASSIGNMENTS];
        int nsub = 0;
        for (int ai = 0; ai < nassign; ai++) {
            if (assigns[ai].depth > seg_offset &&
                strlen(assigns[ai].segments[seg_offset]) == fname.len &&
                memcmp(assigns[ai].segments[seg_offset], fname.data, fname.len) == 0) {
                sub_assigns[nsub++] = assigns[ai];
            }
        }

        if (nsub == 0) {
            fprintf(stderr, "Missing field '%.*s' in type '%.*s'\n",
                    (int)fname.len, fname.data,
                    (int)type->name.len, type->name.data);
            return NULL;
        }

        v->u.record.fields[fi].value = build_value(a, ftype, sub_assigns, nsub,
                                                     seg_offset + 1);
        if (!v->u.record.fields[fi].value) return NULL;
    }

    return v;
}

static hon_value *build_sequence_value(hon_arena *a, hon_type *type,
                                        assignment *assigns, int nassign,
                                        int seg_offset) {
    hon_type *resolved = resolve_type(type);
    if (!resolved || resolved->kind != HON_TYPE_SEQUENCE) return NULL;

    hon_type *elem_type = resolved->u.sequence.element_type;

    /* Determine the maximum index */
    int max_idx = -1;
    for (int ai = 0; ai < nassign; ai++) {
        if (assigns[ai].depth <= seg_offset) continue;
        char *seg = assigns[ai].segments[seg_offset];
        char *endp;
        long idx = strtol(seg, &endp, 10);
        if (*endp != '\0' || idx < 0) {
            fprintf(stderr, "Invalid sequence index '%s'\n", seg);
            return NULL;
        }
        if (idx > max_idx) max_idx = (int)idx;
    }

    if (max_idx < 0) {
        fprintf(stderr, "No elements for sequence type '%.*s'\n",
                (int)type->name.len, type->name.data);
        return NULL;
    }

    uint32_t count = (uint32_t)(max_idx + 1);

    hon_value *v = hon_arena_alloc(a, sizeof(hon_value), _Alignof(hon_value));
    if (!v) return NULL;
    v->kind = HON_VAL_SEQUENCE;
    v->type = type;
    v->u.sequence.count = count;
    v->u.sequence.elements = hon_arena_alloc(a, sizeof(hon_value) * count,
                                              _Alignof(hon_value));
    if (!v->u.sequence.elements) return NULL;

    for (uint32_t ei = 0; ei < count; ei++) {
        char idx_str[16];
        snprintf(idx_str, sizeof(idx_str), "%u", ei);
        size_t idx_len = strlen(idx_str);

        assignment sub_assigns[MAX_ASSIGNMENTS];
        int nsub = 0;
        for (int ai = 0; ai < nassign; ai++) {
            if (assigns[ai].depth > seg_offset &&
                strlen(assigns[ai].segments[seg_offset]) == idx_len &&
                memcmp(assigns[ai].segments[seg_offset], idx_str, idx_len) == 0) {
                sub_assigns[nsub++] = assigns[ai];
            }
        }

        if (nsub == 0) {
            fprintf(stderr, "Missing sequence element %u (sparse indices not allowed)\n", ei);
            return NULL;
        }

        hon_value *elem = build_value(a, elem_type, sub_assigns, nsub,
                                       seg_offset + 1);
        if (!elem) return NULL;
        v->u.sequence.elements[ei] = *elem;
    }

    return v;
}

static hon_value *build_value(hon_arena *a, hon_type *type,
                               assignment *assigns, int nassign,
                               int seg_offset) {
    hon_type *resolved = resolve_type(type);
    if (!resolved) return NULL;

    switch (resolved->kind) {
    case HON_TYPE_PRIMITIVE:
        /* Leaf: expect exactly one assignment at this depth */
        if (nassign != 1 || assigns[0].depth != seg_offset) {
            if (nassign > 1) {
                fprintf(stderr, "Multiple values for primitive field\n");
            } else {
                fprintf(stderr, "Path too deep for primitive type\n");
            }
            return NULL;
        }
        return build_primitive_value(a, type, assigns[0].raw_value);

    case HON_TYPE_ENUM:
        if (nassign != 1 || assigns[0].depth != seg_offset) {
            fprintf(stderr, "Path too deep for enum type\n");
            return NULL;
        }
        return build_enum_value(a, type, assigns[0].raw_value);

    case HON_TYPE_RECORD:
        return build_record_value(a, type, assigns, nassign, seg_offset);

    case HON_TYPE_SEQUENCE:
        return build_sequence_value(a, type, assigns, nassign, seg_offset);

    case HON_TYPE_ALIAS:
        /* Should have been resolved already */
        return NULL;
    }

    return NULL;
}

/* ---- Validate UTF-8 ---- */

static int is_valid_utf8(const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        size_t seqlen;
        if (c < 0x80) seqlen = 1;
        else if ((c & 0xE0) == 0xC0) seqlen = 2;
        else if ((c & 0xF0) == 0xE0) seqlen = 3;
        else if ((c & 0xF8) == 0xF0) seqlen = 4;
        else return 0;

        if (i + seqlen > len) return 0;
        for (size_t j = 1; j < seqlen; j++) {
            if (((unsigned char)s[i+j] & 0xC0) != 0x80) return 0;
        }
        i += seqlen;
    }
    return 1;
}

/* ---- Main ---- */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --type TypeName [--rulebook file | --typedef 'type ...'] "
        "field=value ...\n", prog);
}

int main(int argc, char **argv) {
    const char *type_name = NULL;
    const char *rulebook_path = NULL;
    const char *typedef_text = NULL;

    /* Parse flags */
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--type") == 0 && argi + 1 < argc) {
            type_name = argv[++argi];
        } else if (strcmp(argv[argi], "--rulebook") == 0 && argi + 1 < argc) {
            rulebook_path = argv[++argi];
        } else if (strcmp(argv[argi], "--typedef") == 0 && argi + 1 < argc) {
            typedef_text = argv[++argi];
        } else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 1;
        }
        argi++;
    }

    if (!type_name) {
        fprintf(stderr, "Error: --type is required\n");
        usage(argv[0]);
        return 1;
    }

    if (argi >= argc) {
        fprintf(stderr, "Error: no field=value arguments provided\n");
        usage(argv[0]);
        return 1;
    }

    /* Load type definitions */
    hon_document *type_doc = NULL;
    hon_diag_list diags = {0};

    if (rulebook_path) {
        type_doc = hon_parse_file(rulebook_path, &diags);
        if (!type_doc) {
            fprintf(stderr, "Failed to parse rulebook '%s'\n", rulebook_path);
            for (uint32_t i = 0; i < diags.count; i++)
                fprintf(stderr, "  %u:%u: %s\n",
                        diags.items[i].line, diags.items[i].col,
                        diags.items[i].message);
            return 1;
        }
    } else if (typedef_text) {
        /* Parse inline type definition. Add a dummy var to make it a valid doc. */
        size_t tlen = strlen(typedef_text);
        /* We need a complete document: type defs + at least one var.
         * But we just want the types. Parse as-is and check if it works,
         * or add a dummy var. */
        const char *suffix = "\nvar _dummy : Boolean = True;\n";
        size_t slen = strlen(suffix);
        char *full = malloc(tlen + slen + 1);
        if (!full) { fprintf(stderr, "Out of memory\n"); return 1; }
        memcpy(full, typedef_text, tlen);
        memcpy(full + tlen, suffix, slen + 1);
        type_doc = hon_parse(full, tlen + slen, &diags);
        free(full);
        if (!type_doc) {
            fprintf(stderr, "Failed to parse type definition\n");
            for (uint32_t i = 0; i < diags.count; i++)
                fprintf(stderr, "  %u:%u: %s\n",
                        diags.items[i].line, diags.items[i].col,
                        diags.items[i].message);
            return 1;
        }
    } else {
        fprintf(stderr, "Error: --rulebook or --typedef required\n");
        usage(argv[0]);
        return 1;
    }

    /* Find the target type */
    hon_str tname = {type_name, strlen(type_name)};
    hon_type *target_type = NULL;
    for (uint32_t i = 0; i < type_doc->type_count; i++) {
        if (hon_str_eq(type_doc->types[i]->name, tname)) {
            target_type = type_doc->types[i];
            break;
        }
    }

    if (!target_type) {
        fprintf(stderr, "Type '%s' not found in declarations\n", type_name);
        hon_doc_free(type_doc);
        return 1;
    }

    /* Parse CLI assignments */
    assignment assigns[MAX_ASSIGNMENTS];
    int nassign = 0;

    for (int i = argi; i < argc && nassign < MAX_ASSIGNMENTS; i++) {
        /* Validate UTF-8 */
        if (!is_valid_utf8(argv[i], strlen(argv[i]))) {
            fprintf(stderr, "Invalid UTF-8 in argument: '%s'\n", argv[i]);
            hon_doc_free(type_doc);
            return 1;
        }

        if (parse_assignment(argv[i], &assigns[nassign]) < 0) {
            fprintf(stderr, "Invalid argument (expected key=value): '%s'\n", argv[i]);
            hon_doc_free(type_doc);
            return 1;
        }
        nassign++;
    }

    /* Build the value tree */
    hon_value *val = build_value(type_doc->arena, target_type,
                                  assigns, nassign, 0);
    if (!val) {
        fprintf(stderr, "Failed to build value\n");
        for (int i = 0; i < nassign; i++) free_assignment(&assigns[i]);
        hon_doc_free(type_doc);
        return 1;
    }

    /* Add the variable to type_doc directly and serialise.
     * type_doc already has all type declarations; we just add the data. */
    hon_doc_add_var(type_doc, HON_STR_LIT("result"), target_type, val);

    /* If we used a --typedef with dummy var, remove the dummy by adjusting var_count.
     * The dummy is the first var if typedef was used. */
    if (typedef_text && type_doc->var_count > 1) {
        /* Shift vars to remove the dummy */
        type_doc->vars[0] = type_doc->vars[type_doc->var_count - 1];
        type_doc->var_count = 1;
    }

    /* Serialise */
    size_t out_len;
    char *output = hon_serialise(type_doc, HON_MODE_DOCUMENT, &out_len);
    if (output) {
        fwrite(output, 1, out_len, stdout);
        if (out_len > 0 && output[out_len - 1] != '\n')
            putchar('\n');
        free(output);
    }

    for (int i = 0; i < nassign; i++) free_assignment(&assigns[i]);
    hon_doc_free(type_doc);
    return 0;
}
