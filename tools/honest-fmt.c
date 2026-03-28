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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    hon_serial_mode mode = HON_MODE_DOCUMENT;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wire") == 0 || strcmp(argv[i], "-w") == 0) {
            mode = HON_MODE_WIRE;
        } else if (strcmp(argv[i], "--document") == 0 || strcmp(argv[i], "-d") == 0) {
            mode = HON_MODE_DOCUMENT;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [--wire|--document] <file.honest | ->\n",
                    argv[0]);
            return 0;
        } else {
            path = argv[i];
        }
    }

    if (!path) {
        fprintf(stderr, "Usage: %s [--wire|--document] <file.honest | ->\n",
                argv[0]);
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

    if (!doc) {
        for (uint32_t i = 0; i < diags.count; i++) {
            fprintf(stderr, "%u:%u: %s\n",
                    diags.items[i].line, diags.items[i].col,
                    diags.items[i].message);
        }
        return 1;
    }

    hon_serialise_to(doc, mode, stdout);

    hon_doc_free(doc);
    return 0;
}
