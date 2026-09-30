#include "honest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool has_errors(const hon_diag_list *diags) {
    for (uint32_t i = 0; diags && i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

static void release_result(hon_document *doc, hon_diag_list *diags) {
    if (doc) hon_doc_free(doc);
    else hon_diag_free(diags);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse((const char *)data, size, &diags);
    if (doc && !has_errors(&diags)) {
        size_t output_len = 0;
        char *document = hon_serialise(doc, HON_MODE_DOCUMENT, &output_len);
        free(document);
        char *wire = hon_serialise(doc, HON_MODE_WIRE, &output_len);
        free(wire);
    }
    release_result(doc, &diags);

    diags = (hon_diag_list){0};
    doc = hon_parse_document((const char *)data, size, &diags);
    release_result(doc, &diags);

    diags = (hon_diag_list){0};
    doc = hon_parse_rulebook((const char *)data, size, &diags);
    release_result(doc, &diags);
    return 0;
}

#ifdef HONEST_FUZZ_STANDALONE
static uint64_t fuzz_state = UINT64_C(0x484f4e455354);

static uint32_t fuzz_random(void) {
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 7;
    fuzz_state ^= fuzz_state << 17;
    return (uint32_t)fuzz_state;
}

int main(void) {
    static const char *const seeds[] = {
        "type N = Integer; var n : N = 1;",
        "type E = (A, B); var e : E = A;",
        "type S = sequence of String; var s : S = ('a'; 'b');",
        "type K = (A, B); R = record case kind : K of A : (x : Integer); "
        "B : (); end; var r : R = (kind : A; x : 1);",
    };

    for (uint32_t run = 0; run < 20000; run++) {
        const char *seed = seeds[run % (sizeof(seeds) / sizeof(seeds[0]))];
        size_t size = strlen(seed);
        uint8_t *input = malloc(size);
        if (!input) return 1;
        memcpy(input, seed, size);
        uint32_t changes = 1 + fuzz_random() % 8;
        for (uint32_t change = 0; change < changes && size > 0; change++) {
            input[fuzz_random() % size] = (uint8_t)fuzz_random();
        }
        LLVMFuzzerTestOneInput(input, size);
        free(input);
    }
    return 0;
}
#endif
