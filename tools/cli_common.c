#include "cli_common.h"

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

int hon_cli_init(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fprintf(stderr, "error: failed to configure output handling: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

char *hon_cli_read_all(FILE *input, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf;

    if (!input || !out_len) return NULL;

    buf = malloc(cap);
    if (!buf) return NULL;

    for (;;) {
        size_t n;

        if (len == cap) {
            char *larger;
            if (cap > SIZE_MAX / 2) {
                free(buf);
                errno = EOVERFLOW;
                return NULL;
            }
            cap *= 2;
            larger = realloc(buf, cap);
            if (!larger) {
                free(buf);
                return NULL;
            }
            buf = larger;
        }

        n = fread(buf + len, 1, cap - len, input);
        len += n;
        if (n == 0) break;
    }

    if (ferror(input)) {
        free(buf);
        return NULL;
    }

    *out_len = len;
    return buf;
}

bool hon_cli_has_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

void hon_cli_print_diags(const hon_diag_list *diags) {
    if (!diags) return;
    for (uint32_t i = 0; i < diags->count; i++) {
        const char *level;
        switch (diags->items[i].level) {
        case HON_DIAG_ERROR:   level = "error"; break;
        case HON_DIAG_WARNING: level = "warning"; break;
        case HON_DIAG_INFO:    level = "info"; break;
        default:               level = "unknown"; break;
        }
        fprintf(stderr, "%s:%u:%u: %s\n", level,
                diags->items[i].line, diags->items[i].col,
                diags->items[i].message);
    }
}

int hon_cli_finish_stdout(void) {
    if (fflush(stdout) == 0 && !ferror(stdout)) return 0;
    fprintf(stderr, "error: failed to write stdout: %s\n",
            strerror(errno ? errno : EIO));
    return -1;
}

int hon_cli_write_stdout(const char *data, size_t len) {
    if (len > 0 && fwrite(data, 1, len, stdout) != len) {
        fprintf(stderr, "error: failed to write stdout: %s\n",
                strerror(errno ? errno : EIO));
        return -1;
    }
    return hon_cli_finish_stdout();
}
