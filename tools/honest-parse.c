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
#include "cli_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog) {
    fprintf(out, "Usage: %s [-q | -v] <file.honest | ->\n", prog);
}

int main(int argc, char **argv) {
    if (hon_cli_init() < 0) return 1;
    bool quiet   = false;
    bool verbose = false;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return hon_cli_finish_stdout() == 0 ? 0 : 1;
        } else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(stderr, argv[0]);
            return 2;
        } else {
            if (path) {
                fprintf(stderr, "Only one input path may be specified\n");
                usage(stderr, argv[0]);
                return 2;
            }
            path = argv[i];
        }
    }

    if (quiet && verbose) {
        fprintf(stderr, "--quiet and --verbose are mutually exclusive\n");
        usage(stderr, argv[0]);
        return 2;
    }

    if (!path) {
        usage(stderr, argv[0]);
        return 2;
    }

    hon_diag_list diags = {0};
    hon_document *doc;

    if (strcmp(path, "-") == 0) {
        size_t len;
        char *src = hon_cli_read_all(stdin, &len);
        if (!src) { fprintf(stderr, "Failed to read stdin\n"); return 1; }
        doc = hon_parse(src, len, &diags);
        free(src);
    } else {
        doc = hon_parse_file(path, &diags);
    }

    bool has_errors = hon_cli_has_errors(&diags);

    if (!quiet) {
        hon_cli_print_diags(&diags);
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
    if (rc == 0 && !quiet && hon_cli_finish_stdout() < 0) rc = 1;
    if (doc) hon_doc_free(doc);
    else hon_diag_free(&diags);
    return rc;
}
