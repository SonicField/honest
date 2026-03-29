/*
 * Diagnostic reporting for libhonest.
 *
 * Parse errors are values, not crashes. This module collects
 * diagnostics (errors, warnings, info) during parsing.
 */

#include "honest.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define DIAG_INITIAL_CAP 16

void hon_diag_init(hon_diag_list *dl, hon_arena *a) {
    dl->count = 0;
    dl->cap   = DIAG_INITIAL_CAP;
    dl->items = hon_arena_alloc(a, sizeof(hon_diagnostic) * dl->cap,
                                _Alignof(hon_diagnostic));
}

void hon_diag_free(hon_diag_list *dl) {
    if (!dl || !dl->items || !dl->owns_memory) return;
    for (uint32_t i = 0; i < dl->count; i++) {
        free((void *)dl->items[i].message);
    }
    free(dl->items);
    dl->items = NULL;
    dl->count = 0;
    dl->cap = 0;
    dl->owns_memory = false;
}

void hon_diag_push(hon_diag_list *dl, hon_arena *a,
                   hon_diag_level level, uint32_t line, uint32_t col,
                   const char *fmt, ...) {
    if (!dl || !dl->items) return;
    if (dl->count >= dl->cap) {
        uint32_t new_cap = dl->cap * 2;
        hon_diagnostic *new_items = hon_arena_alloc(
            a, sizeof(hon_diagnostic) * new_cap, _Alignof(hon_diagnostic));
        if (!new_items) {
            /* Arena exhausted — cannot grow. Mark the last diagnostic
             * as a truncation sentinel so callers can detect the drop. */
            if (dl->count > 0) {
                dl->items[dl->count - 1].message =
                    "(further diagnostics truncated — arena exhausted)";
            }
            return;
        }
        memcpy(new_items, dl->items, sizeof(hon_diagnostic) * dl->count);
        dl->items = new_items;
        dl->cap   = new_cap;
    }

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t msg_len = strlen(buf);
    char *msg = hon_arena_alloc(a, msg_len + 1, 1);
    if (msg) {
        memcpy(msg, buf, msg_len + 1);
    } else {
        /* Arena exhausted — use static fallback so .message is never NULL */
        msg = "(diagnostic message lost — arena exhausted)";
    }

    dl->items[dl->count++] = (hon_diagnostic){
        .level   = level,
        .line    = line,
        .col     = col,
        .message = msg,
    };
}
