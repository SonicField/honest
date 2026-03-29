#define _POSIX_C_SOURCE 200809L

/*
 * honest-extract — Extract the first valid Honest document from mixed text.
 *
 * Scans input for the 'type' keyword at line start (or after code fences),
 * attempts to parse from that point, and outputs the extracted document
 * in wire mode.
 *
 * Exit codes: 0 = document found, 1 = no valid document found.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }
    if (ferror(f)) { free(buf); return NULL; }
    *out_len = len;
    return buf;
}

/* Check if position is at the start of a line (or start of input). */
static int at_line_start(const char *src, size_t pos) {
    return pos == 0 || src[pos - 1] == '\n';
}

/* Check if a line is a code fence opener (``` with optional language tag). */
static int is_code_fence(const char *line, size_t remaining) {
    if (remaining < 3) return 0;
    if (line[0] == '`' && line[1] == '`' && line[2] == '`') return 1;
    return 0;
}

/* Find the closing code fence from a given position. Returns offset past
 * the closing fence line, or len if not found. */
static size_t find_fence_end(const char *src, size_t start, size_t len) {
    size_t i = start;
    /* Skip the rest of the opening fence line */
    while (i < len && src[i] != '\n') i++;
    if (i < len) i++; /* skip newline */

    while (i < len) {
        if (at_line_start(src, i) && is_code_fence(src + i, len - i)) {
            /* Skip past closing fence line */
            while (i < len && src[i] != '\n') i++;
            if (i < len) i++;
            return i;
        }
        while (i < len && src[i] != '\n') i++;
        if (i < len) i++;
    }
    return len;
}

/* Check if a position starts with 'type' followed by whitespace or newline. */
static int is_type_keyword(const char *src, size_t pos, size_t len) {
    if (pos + 4 > len) return 0;
    if (memcmp(src + pos, "type", 4) != 0) return 0;
    if (pos + 4 == len) return 1; /* at end of input */
    char next = src[pos + 4];
    return next == ' ' || next == '\t' || next == '\n' || next == '\r';
}

/* Skip leading whitespace on the current line. */
static size_t skip_line_whitespace(const char *src, size_t pos, size_t len) {
    while (pos < len && (src[pos] == ' ' || src[pos] == '\t')) pos++;
    return pos;
}

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1 && strcmp(argv[1], "-") != 0) {
        f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "Cannot open '%s'\n", argv[1]);
            return 1;
        }
    }

    size_t len;
    char *src = read_all(f, &len);
    if (f != stdin) fclose(f);
    if (!src) {
        fprintf(stderr, "Failed to read input\n");
        return 1;
    }

    /* Scan for candidate Honest documents */
    size_t i = 0;
    while (i < len) {
        int in_fence = 0;
        size_t fence_end = len;
        size_t candidate = len; /* position where 'type' keyword starts */

        /* Check for code fence */
        size_t line_pos = skip_line_whitespace(src, i, len);

        if (at_line_start(src, i) && is_code_fence(src + line_pos, len - line_pos)) {
            in_fence = 1;
            fence_end = find_fence_end(src, line_pos, len);
            /* Skip the opening fence line */
            size_t fence_content = line_pos;
            while (fence_content < len && src[fence_content] != '\n') fence_content++;
            if (fence_content < len) fence_content++;

            /* Look for 'type' keyword inside the fence */
            size_t j = fence_content;
            while (j < fence_end) {
                size_t ws = skip_line_whitespace(src, j, fence_end);
                if (is_type_keyword(src, ws, fence_end)) {
                    candidate = ws;
                    break;
                }
                while (j < fence_end && src[j] != '\n') j++;
                if (j < fence_end) j++;
            }

            if (candidate < len) {
                /* Extract text between 'type' and closing fence */
                size_t extract_end = fence_end;
                /* Back up past the closing fence line */
                if (in_fence && extract_end > candidate) {
                    size_t e = extract_end;
                    if (e > 0 && src[e-1] == '\n') e--;
                    while (e > candidate && src[e-1] != '\n') e--;
                    /* Check if this line is a fence */
                    size_t check = skip_line_whitespace(src, e, extract_end);
                    if (is_code_fence(src + check, extract_end - check))
                        extract_end = e;
                }

                hon_diag_list diags = {0};
                hon_document *doc = hon_parse(src + candidate,
                                              extract_end - candidate, &diags);
                if (doc) {
                    size_t out_len;
                    char *wire = hon_serialise(doc, HON_MODE_WIRE, &out_len);
                    if (wire) {
                        fwrite(wire, 1, out_len, stdout);
                        if (out_len > 0 && wire[out_len-1] != '\n')
                            putchar('\n');
                        free(wire);
                    }
                    hon_doc_free(doc);
                    free(src);
                    return 0;
                }
            }

            i = fence_end;
            continue;
        }

        /* Not in a code fence — check for 'type' at line start */
        if (at_line_start(src, i)) {
            size_t ws = skip_line_whitespace(src, i, len);
            if (is_type_keyword(src, ws, len)) {
                /* Try parsing from here to end of input */
                hon_diag_list diags = {0};
                hon_document *doc = hon_parse(src + ws, len - ws, &diags);
                if (doc) {
                    size_t out_len;
                    char *wire = hon_serialise(doc, HON_MODE_WIRE, &out_len);
                    if (wire) {
                        fwrite(wire, 1, out_len, stdout);
                        if (out_len > 0 && wire[out_len-1] != '\n')
                            putchar('\n');
                        free(wire);
                    }
                    hon_doc_free(doc);
                    free(src);
                    return 0;
                }
            }
        }

        /* Move to next line */
        while (i < len && src[i] != '\n') i++;
        if (i < len) i++;
    }

    free(src);
    fprintf(stderr, "No valid Honest document found\n");
    return 1;
}
