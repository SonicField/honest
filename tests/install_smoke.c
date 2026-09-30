#include "honest.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    static const char source[] =
        "type N = Integer; var answer : N = 42;";
    hon_diag_list diagnostics = {0};
    hon_document *document = hon_parse(source, strlen(source), &diagnostics);
    if (!document) {
        hon_diag_free(&diagnostics);
        return 1;
    }
    for (uint32_t i = 0; i < diagnostics.count; i++) {
        if (diagnostics.items[i].level == HON_DIAG_ERROR) {
            hon_doc_free(document);
            return 1;
        }
    }

    size_t output_length = 0;
    char *output = hon_serialise(document, HON_MODE_WIRE, &output_length);
    int status = !output || output_length == 0;
    free(output);
    hon_doc_free(document);
    return status;
}
