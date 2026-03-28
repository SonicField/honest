/*
 * Round-trip tests: parse(serialise(doc)) == doc semantically.
 */

#include "honest.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
    } else { \
        tests_passed++; \
    } \
} while(0)

static void test_roundtrip(const char *name, const char *src) {
    hon_diag_list diags1 = {0};
    hon_document *doc1 = hon_parse(src, strlen(src), &diags1);
    ASSERT(doc1 != NULL, name);
    if (!doc1) return;

    /* Serialise */
    size_t slen;
    char *s = hon_serialise(doc1, HON_MODE_DOCUMENT, &slen);
    ASSERT(s != NULL, "serialise succeeded");
    if (!s) { hon_doc_free(doc1); return; }

    /* Parse the serialised output */
    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(s, slen, &diags2);
    ASSERT(doc2 != NULL, "re-parse succeeded");

    if (doc2) {
        /* Check structural equality */
        ASSERT(doc1->type_count == doc2->type_count, "same type count");
        ASSERT(doc1->var_count == doc2->var_count, "same var count");

        for (uint32_t i = 0; i < doc1->type_count && i < doc2->type_count; i++) {
            ASSERT(doc1->types[i]->kind == doc2->types[i]->kind, "same type kind");
            ASSERT(hon_str_eq(doc1->types[i]->name, doc2->types[i]->name),
                   "same type name");
        }

        for (uint32_t i = 0; i < doc1->var_count && i < doc2->var_count; i++) {
            ASSERT(hon_str_eq(doc1->vars[i].name, doc2->vars[i].name),
                   "same var name");
            ASSERT(doc1->vars[i].value->kind == doc2->vars[i].value->kind,
                   "same value kind");
        }

        hon_doc_free(doc2);
    }

    free(s);
    hon_doc_free(doc1);
}

static void test_enum_roundtrip(void) {
    test_roundtrip("enum roundtrip",
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "\n"
        "var c : Color = Green;\n");
}

static void test_record_roundtrip(void) {
    test_roundtrip("record roundtrip",
        "type\n"
        "  Point = record\n"
        "    x : Integer;\n"
        "    y : Integer;\n"
        "  end;\n"
        "\n"
        "var p : Point = (x : 10; y : 20);\n");
}

static void test_sequence_roundtrip(void) {
    test_roundtrip("sequence roundtrip",
        "type\n"
        "  IntList = sequence of Integer;\n"
        "\n"
        "var nums : IntList = (1; 2; 3);\n");
}

static void test_variant_roundtrip(void) {
    test_roundtrip("variant roundtrip",
        "type\n"
        "  Kind = (A, B);\n"
        "  Shape = record\n"
        "    name : String;\n"
        "    case kind : Kind of\n"
        "      A : (width : Integer; height : Integer);\n"
        "      B : (radius : Integer);\n"
        "  end;\n"
        "\n"
        "var s : Shape = (\n"
        "  name : 'rect';\n"
        "  kind : A;\n"
        "  width : 10;\n"
        "  height : 20;\n"
        ");\n");
}

static void test_full_nbs_roundtrip(void) {
    test_roundtrip("full NBS roundtrip",
        "type\n"
        "  Confidence  = (Certain, High, Moderate, Low, Speculative);\n"
        "  Provenance  = (Observed, Inferred, Recalled, Generated);\n"
        "  AgentRole   = (Supervisor, Gatekeeper, Scribe, Theologian,\n"
        "                 Generalist, Testkeeper);\n"
        "  ClaimKind   = (Assertion, Query, Correction, Error);\n"
        "  Timestamp   = LongInt;\n"
        "  NaturalText = String;\n"
        "\n"
        "  Claim = record\n"
        "    confidence : Confidence;\n"
        "    provenance : Provenance;\n"
        "    as_of      : Timestamp;\n"
        "    case kind : ClaimKind of\n"
        "      Assertion  : (content : NaturalText);\n"
        "      Query      : (intent : NaturalText; scope : NaturalText);\n"
        "      Correction : (replaces : LongInt; content : NaturalText);\n"
        "      Error      : (code : Word; reason : NaturalText);\n"
        "  end;\n"
        "\n"
        "  Message = record\n"
        "    id      : LongInt;\n"
        "    sender  : AgentRole;\n"
        "    payload : Claim;\n"
        "  end;\n"
        "\n"
        "var message : Message = (\n"
        "  id      : 4721;\n"
        "  sender  : Theologian;\n"
        "  payload : (\n"
        "    confidence : High;\n"
        "    provenance : Inferred;\n"
        "    as_of      : 1774856400;\n"
        "    kind       : Assertion;\n"
        "    content    : 'The compilation target is ARM Thumb2';\n"
        "  );\n"
        ");\n");
}

static void test_wire_mode(void) {
    const char *src =
        "{ comment } type\n"
        "  Color = (Red, Green, Blue);\n"
        "var c : Color = Green;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "parse for wire mode");
    if (!doc) return;

    size_t wlen;
    char *wire = hon_serialise(doc, HON_MODE_WIRE, &wlen);
    ASSERT(wire != NULL, "wire serialise succeeded");

    /* Wire mode should not contain newlines (except at very end) */
    /* and should still be parseable */
    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(wire, wlen, &diags2);
    ASSERT(doc2 != NULL, "re-parse wire mode");

    if (doc2) {
        ASSERT(doc2->type_count == 1, "wire mode: 1 type");
        ASSERT(doc2->var_count == 1, "wire mode: 1 var");
        hon_doc_free(doc2);
    }

    free(wire);
    hon_doc_free(doc);
}

int main(void) {
    test_enum_roundtrip();
    test_record_roundtrip();
    test_sequence_roundtrip();
    test_variant_roundtrip();
    test_full_nbs_roundtrip();
    test_wire_mode();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
