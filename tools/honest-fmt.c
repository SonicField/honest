/*
 * honest-fmt — Format/reformat an Honest document.
 *
 * Usage: honest-fmt [--wire|--document] <file.honest | ->
 *
 * Parses input, then serialises in the requested mode.
 * Default mode is document (human-readable).
 * Output goes to stdout.
 */

#include "honest.h"
#include "cli_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog) {
    fprintf(out, "Usage: %s [--wire | --document] <file.honest | ->\n", prog);
}

int main(int argc, char **argv) {
    hon_serial_mode mode = HON_MODE_DOCUMENT;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wire") == 0 || strcmp(argv[i], "-w") == 0) {
            mode = HON_MODE_WIRE;
        } else if (strcmp(argv[i], "--document") == 0 || strcmp(argv[i], "-d") == 0) {
            mode = HON_MODE_DOCUMENT;
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

    if (!doc || hon_cli_has_errors(&diags)) {
        hon_cli_print_diags(&diags);
        if (doc) hon_doc_free(doc);
        else hon_diag_free(&diags);
        return 1;
    }

    int rc = hon_serialise_to(doc, mode, stdout);
    if (rc < 0) fprintf(stderr, "Failed to write output\n");

    hon_doc_free(doc);
    return rc == 0 ? 0 : 1;
}
