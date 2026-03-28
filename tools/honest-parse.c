/*
 * honest-parse — Parse and validate an Honest document.
 *
 * Usage: honest-parse <file.honest>
 *        honest-parse -          (read from stdin)
 *
 * Exits 0 if valid, 1 if errors found. Prints diagnostics to stderr.
 * With -q (quiet), suppresses output; with -v (verbose), prints parsed structure.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_diags(const hon_diag_list *diags) {
    for (uint32_t i = 0; i < diags->count; i++) {
        const char *level;
        switch (diags->items[i].level) {
        case HON_DIAG_ERROR:   level = "error"; break;
        case HON_DIAG_WARNING: level = "warning"; break;
        case HON_DIAG_INFO:    level = "info"; break;
        default:               level = "???"; break;
        }
        fprintf(stderr, "%s:%u:%u: %s\n", level,
                diags->items[i].line, diags->items[i].col,
                diags->items[i].message);
    }
}

static char *read_stdin(size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) return NULL;
        }
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    bool quiet   = false;
    bool verbose = false;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [-q|-v] <file.honest | ->\n", argv[0]);
            return 0;
        } else {
            path = argv[i];
        }
    }

    if (!path) {
        fprintf(stderr, "Usage: %s [-q|-v] <file.honest | ->\n", argv[0]);
        return 2;
    }

    hon_diag_list diags = {0};
    hon_document *doc;

    if (strcmp(path, "-") == 0) {
        size_t len;
        char *src = read_stdin(&len);
        if (!src) { fprintf(stderr, "Failed to read stdin\n"); return 1; }
        doc = hon_parse(src, len, &diags);
        free(src);
    } else {
        doc = hon_parse_file(path, &diags);
    }

    bool has_errors = false;
    for (uint32_t i = 0; i < diags.count; i++) {
        if (diags.items[i].level == HON_DIAG_ERROR) has_errors = true;
    }

    if (!quiet) {
        print_diags(&diags);
    }

    if (doc && !has_errors) {
        if (!quiet) {
            fprintf(stdout, "OK: %u type(s), %u variable(s)\n",
                    doc->type_count, doc->var_count);
        }

        if (verbose) {
            for (uint32_t i = 0; i < doc->type_count; i++) {
                const char *kind;
                switch (doc->types[i]->kind) {
                case HON_TYPE_PRIMITIVE: kind = "primitive"; break;
                case HON_TYPE_ENUM:     kind = "enum"; break;
                case HON_TYPE_RECORD:   kind = "record"; break;
                case HON_TYPE_SEQUENCE: kind = "sequence"; break;
                case HON_TYPE_ALIAS:    kind = "alias"; break;
                default:                kind = "unknown"; break;
                }
                fprintf(stdout, "  type %.*s = %s\n",
                        (int)doc->types[i]->name.len,
                        doc->types[i]->name.data, kind);
            }
            for (uint32_t i = 0; i < doc->var_count; i++) {
                fprintf(stdout, "  var %.*s : %.*s\n",
                        (int)doc->vars[i].name.len,
                        doc->vars[i].name.data,
                        (int)doc->vars[i].type->name.len,
                        doc->vars[i].type->name.data);
            }
        }
    }

    int rc = (doc && !has_errors) ? 0 : 1;
    if (doc) hon_doc_free(doc);
    return rc;
}
