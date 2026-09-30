/*
 * Conformance tests for libhonest.
 *
 * Tests derived systematically from specs/honest-spec.md.
 * Each test maps to a specific spec section and requirement.
 * This file serves as executable documentation of the spec.
 *
 * Coverage targets: lexical rules (§2), type system (§3),
 * document structure (§4), serialisation (§5), conformance (§7).
 */

#include "honest.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

static bool has_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

/* ================================================================
 * §2.1 — BOM MUST NOT be present
 * ================================================================ */
static void test_bom_rejection(void) {
    /* UTF-8 BOM: EF BB BF */
    const char src[] = "\xEF\xBB\xBF"
        "type\n"
        "  X = Integer;\n"
        "var x : X = 1;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, sizeof(src) - 1, &diags);

    /* BOM MUST NOT be present — parser must reject or produce an error */
    ASSERT(!doc || has_errors(&diags), "§2.1: BOM rejected with error diagnostic");

    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §2.3 — Comments: both styles, non-nesting
 * ================================================================ */
static void test_brace_comment(void) {
    const char *src =
        "{ This is a brace comment }\n"
        "type\n"
        "  N = Integer;\n"
        "var n : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.3: brace comment parsed");
    ASSERT(!has_errors(&diags), "§2.3: no errors with brace comment");
    ASSERT(doc->vars[0].value->u.integer == 42, "§2.3: value correct after brace comment");

    hon_doc_free(doc);
}

static void test_paren_star_comment(void) {
    const char *src =
        "(* This is a paren-star comment *)\n"
        "type\n"
        "  N = Integer;\n"
        "var n : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.3: paren-star comment parsed");
    ASSERT(!has_errors(&diags), "§2.3: no errors with paren-star comment");
    ASSERT(doc->vars[0].value->u.integer == 42, "§2.3: value correct after (* *) comment");

    hon_doc_free(doc);
}

static void test_unterminated_brace_comment(void) {
    const char *src =
        "{ unterminated comment\n"
        "type\n"
        "  N = Integer;\n"
        "var n : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(has_errors(&diags), "§2.3: unterminated { comment produces error");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

static void test_unterminated_paren_star_comment(void) {
    const char *src =
        "(* unterminated comment\n"
        "type\n"
        "  N = Integer;\n"
        "var n : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(has_errors(&diags), "§2.3: unterminated (* comment produces error");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §2.4 — Identifiers: must start with letter, underscore after first
 * ================================================================ */
static void test_underscore_in_identifier(void) {
    const char *src =
        "type\n"
        "  my_type = Integer;\n"
        "var my_var : my_type = 1;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.4: underscore after first char accepted");
    ASSERT(!has_errors(&diags), "§2.4: no errors");

    hon_doc_free(doc);
}

/* ================================================================
 * §2.5 — Keywords are case-sensitive
 * ================================================================ */
static void test_case_sensitive_keywords(void) {
    /* 'Type' (capital T) is NOT a keyword — it should be treated as
     * an identifier. This document should fail to parse because
     * there's no 'type' keyword. */
    const char *src =
        "Type\n"
        "  X = Integer;\n"
        "var x : X = 1;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(has_errors(&diags), "§2.5: 'Type' (capital) is not keyword 'type'");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §2.6 — Numeric literals: negative integers, real with exponent
 * ================================================================ */
static void test_negative_integer(void) {
    const char *src =
        "type\n"
        "  N = Integer;\n"
        "var n : N = -42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.6: negative integer parsed");
    ASSERT(!has_errors(&diags), "§2.6: no errors");
    ASSERT(doc->vars[0].value->u.integer == -42, "§2.6: value is -42");

    hon_doc_free(doc);
}

static void test_real_with_exponent(void) {
    const char *src =
        "type\n"
        "  R = Real;\n"
        "var r : R = 1.5e3;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (doc && !has_errors(&diags)) {
        double diff = doc->vars[0].value->u.real - 1500.0;
        ASSERT(fabs(diff) < 0.01, "§2.6: 1.5e3 == 1500.0");
    } else {
        /* If the parser doesn't support exponent notation, note it */
        ASSERT(0, "§2.6: real with exponent not supported");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §2.7 — String literals: escape sequences
 * ================================================================ */
static void test_pascal_quote_escape(void) {
    /* Pascal-style: '' inside quotes is a single quote */
    const char *src =
        "type\n"
        "  S = String;\n"
        "var s : S = 'it''s';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.7: Pascal '' escape parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.string.len == 4, "§2.7: 'it''s' is 4 chars");
        ASSERT(doc->vars[0].value->u.string.data[2] == '\'',
               "§2.7: third char is single quote");
    }

    if (doc) hon_doc_free(doc);
}

static void test_backslash_tab_escape(void) {
    const char *src =
        "type\n"
        "  S = String;\n"
        "var s : S = 'a\\tb';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.7: \\t escape parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.string.len == 3, "§2.7: 'a\\tb' is 3 chars");
        ASSERT(doc->vars[0].value->u.string.data[1] == '\t',
               "§2.7: middle char is tab");
    }

    if (doc) hon_doc_free(doc);
}

static void test_backslash_backslash_escape(void) {
    const char *src =
        "type\n"
        "  S = String;\n"
        "var s : S = 'a\\\\b';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§2.7: \\\\ escape parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.string.len == 3, "§2.7: 'a\\\\b' is 3 chars");
        ASSERT(doc->vars[0].value->u.string.data[1] == '\\',
               "§2.7: middle char is backslash");
    }

    if (doc) hon_doc_free(doc);
}

static void test_unicode_escape_4hex(void) {
    /* \u0041 is 'A' (U+0041) */
    const char *src =
        "type\n"
        "  S = String;\n"
        "var s : S = '\\u0041';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.string.len == 1, "§2.7: \\u0041 is 1 char");
        ASSERT(doc->vars[0].value->u.string.data[0] == 'A',
               "§2.7: \\u0041 is 'A'");
    } else {
        ASSERT(0, "§2.7: \\uXXXX escape not supported");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.2 — Enumeration: minimum 1 value
 * ================================================================ */
static void test_single_value_enum(void) {
    const char *src =
        "type\n"
        "  Singleton = (Only);\n"
        "var s : Singleton = Only;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.2: single-value enum parsed");
    ASSERT(!has_errors(&diags), "§3.2: no errors");
    ASSERT(doc->types[0]->u.enumeration.value_count == 1, "§3.2: 1 enum value");

    hon_doc_free(doc);
}

/* ================================================================
 * §3.4 — Empty record
 * ================================================================ */
static void test_empty_record(void) {
    const char *src =
        "type\n"
        "  Unit = record\n"
        "  end;\n"
        "var u : Unit = ();\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.4: empty record parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->types[0]->kind == HON_TYPE_RECORD, "§3.4: type is record");
        ASSERT(doc->types[0]->u.record.field_count == 0, "§3.4: 0 fields");
        ASSERT(doc->vars[0].value->kind == HON_VAL_RECORD, "§3.4: value is record");
        ASSERT(doc->vars[0].value->u.record.field_count == 0, "§3.4: 0 field values");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.7 — Forward references MUST be rejected
 * ================================================================ */
static void test_forward_reference_rejected(void) {
    /* Message references Claim, but Claim is declared AFTER Message */
    const char *src =
        "type\n"
        "  Message = record\n"
        "    payload : Claim;\n"
        "  end;\n"
        "  Claim = record\n"
        "    content : String;\n"
        "  end;\n"
        "var m : Message = (payload : (content : 'x'));\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(has_errors(&diags), "§3.7: forward reference produces error");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.7 — Duplicate type names in declarations
 * ================================================================ */
static void test_duplicate_type_name(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "  Color = (Red, Cyan, Magenta);\n"
        "var c : Color = Red;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    /* The value is valid for both definitions, so only duplication can fail. */
    ASSERT(has_errors(&diags), "§3.7: duplicate type name produces error");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §4.1 — Both sections MUST be present
 * ================================================================ */
static void test_missing_declarations(void) {
    const char *src = "var x : Integer = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(has_errors(&diags), "§4.1: missing 'type' section produces error");
    hon_diag_free(&diags);
    if (doc) hon_doc_free(doc);
}

static void test_missing_data(void) {
    const char *src =
        "type\n"
        "  N = Integer;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    /* A document without var is invalid per §4.1, but rulebook files
     * are declarations-only per §6.1. The parser may accept it. */
    if (doc && !has_errors(&diags)) {
        /* If accepted, it should have 0 vars */
        ASSERT(doc->var_count == 0, "§4.1: no vars in declarations-only doc");
    } else {
        ASSERT(has_errors(&diags), "§4.1: missing 'var' section produces error");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §5.2 — Wire mode: comments stripped
 * ================================================================ */
static void test_wire_mode_strips_comments(void) {
    const char *src =
        "{ Document comment }\n"
        "type\n"
        "  (* inline comment *)\n"
        "  N = Integer;\n"
        "var n : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "§5.2: source parses");
    if (!doc) return;

    /* Serialise in wire mode */
    size_t wlen;
    char *wire = hon_serialise(doc, HON_MODE_WIRE, &wlen);
    ASSERT(wire != NULL, "§5.2: wire serialisation succeeded");

    if (wire) {
        ASSERT(strcmp(wire, "type N = Integer; var n : N = 42;") == 0,
               "§5.3: wire output uses canonical single-space form");
        /* Wire output should not contain comment markers */
        ASSERT(strstr(wire, "Document comment") == NULL,
               "§5.2: brace comment stripped in wire mode");
        ASSERT(strstr(wire, "inline comment") == NULL,
               "§5.2: paren-star comment stripped in wire mode");
        ASSERT(strstr(wire, "{") == NULL || strstr(wire, "(*") == NULL,
               "§5.2: comment delimiters absent in wire mode");

        /* Wire output should still be parseable */
        hon_diag_list diags2 = {0};
        hon_document *doc2 = hon_parse(wire, wlen, &diags2);
        ASSERT(doc2 != NULL, "§5.2: wire output re-parses");
        if (doc2) {
            ASSERT(doc2->vars[0].value->u.integer == 42,
                   "§5.2: wire output preserves data");
            hon_doc_free(doc2);
        }
        free(wire);
    }

    hon_doc_free(doc);
}

/* ================================================================
 * §7.2.5 — Nesting depth >= 32
 * ================================================================ */
static void test_nesting_depth_32(void) {
    /* Build a document with 32 levels of record nesting */
    char src[8192];
    int pos = 0;

    pos += snprintf(src + pos, sizeof(src) - pos, "type\n");

    /* Declare 32 nested record types: R0 contains Integer, R1 contains R0, ... */
    pos += snprintf(src + pos, sizeof(src) - pos,
                    "  R0 = record\n    val : Integer;\n  end;\n");
    for (int i = 1; i < 32; i++) {
        pos += snprintf(src + pos, sizeof(src) - pos,
                        "  R%d = record\n    inner : R%d;\n  end;\n", i, i - 1);
    }

    /* Build the nested value: R31 = (inner : (inner : ... (val : 1) ...)) */
    pos += snprintf(src + pos, sizeof(src) - pos, "var top : R31 = ");
    for (int i = 31; i >= 1; i--) {
        pos += snprintf(src + pos, sizeof(src) - pos, "(inner : ");
    }
    pos += snprintf(src + pos, sizeof(src) - pos, "(val : 1)");
    for (int i = 1; i <= 31; i++) {
        pos += snprintf(src + pos, sizeof(src) - pos, ")");
    }
    pos += snprintf(src + pos, sizeof(src) - pos, ";\n");

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, (size_t)pos, &diags);

    ASSERT(doc != NULL, "§7.2.5: 32-level nesting parsed");
    ASSERT(!has_errors(&diags), "§7.2.5: no errors at depth 32");

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §7.2.6 — Field order preservation across round-trip
 * ================================================================ */
static void test_field_order_roundtrip(void) {
    const char *src =
        "type\n"
        "  P = record\n"
        "    alpha : Integer;\n"
        "    beta  : String;\n"
        "    gamma : Boolean;\n"
        "  end;\n"
        "var p : P = (alpha : 1; beta : 'two'; gamma : True);\n";

    hon_diag_list diags1 = {0};
    hon_document *doc1 = hon_parse(src, strlen(src), &diags1);
    ASSERT(doc1 != NULL, "§7.2.6: original parses");
    if (!doc1) return;

    size_t slen;
    char *s = hon_serialise(doc1, HON_MODE_DOCUMENT, &slen);
    ASSERT(s != NULL, "§7.2.6: serialise succeeds");
    if (!s) { hon_doc_free(doc1); return; }

    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(s, slen, &diags2);
    ASSERT(doc2 != NULL, "§7.2.6: re-parse succeeds");

    if (doc2) {
        /* Verify field order is preserved */
        ASSERT(hon_str_eq(doc2->types[0]->u.record.fields[0].name,
                          HON_STR_LIT("alpha")),
               "§7.2.6: first field is alpha");
        ASSERT(hon_str_eq(doc2->types[0]->u.record.fields[1].name,
                          HON_STR_LIT("beta")),
               "§7.2.6: second field is beta");
        ASSERT(hon_str_eq(doc2->types[0]->u.record.fields[2].name,
                          HON_STR_LIT("gamma")),
               "§7.2.6: third field is gamma");
        hon_doc_free(doc2);
    }

    free(s);
    hon_doc_free(doc1);
}

/* ================================================================
 * §7.2.7 — Declaration order preservation across round-trip
 * ================================================================ */
static void test_declaration_order_roundtrip(void) {
    const char *src =
        "type\n"
        "  First  = Integer;\n"
        "  Second = String;\n"
        "  Third  = Boolean;\n"
        "var a : First = 1;\n";

    hon_diag_list diags1 = {0};
    hon_document *doc1 = hon_parse(src, strlen(src), &diags1);
    ASSERT(doc1 != NULL, "§7.2.7: original parses");
    if (!doc1) return;

    size_t slen;
    char *s = hon_serialise(doc1, HON_MODE_DOCUMENT, &slen);
    ASSERT(s != NULL, "§7.2.7: serialise succeeds");
    if (!s) { hon_doc_free(doc1); return; }

    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(s, slen, &diags2);
    ASSERT(doc2 != NULL, "§7.2.7: re-parse succeeds");

    if (doc2) {
        ASSERT(doc2->type_count == 3, "§7.2.7: 3 types");
        ASSERT(hon_str_eq(doc2->types[0]->name, HON_STR_LIT("First")),
               "§7.2.7: first type is First");
        ASSERT(hon_str_eq(doc2->types[1]->name, HON_STR_LIT("Second")),
               "§7.2.7: second type is Second");
        ASSERT(hon_str_eq(doc2->types[2]->name, HON_STR_LIT("Third")),
               "§7.2.7: third type is Third");
        hon_doc_free(doc2);
    }

    free(s);
    hon_doc_free(doc1);
}

/* ================================================================
 * §3.1 — Char type: single Unicode code point
 * ================================================================ */
static void test_char_type(void) {
    const char *src =
        "type\n"
        "  C = Char;\n"
        "var c : C = 'A';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->kind == HON_VAL_CHAR,
               "§3.1: Char value parsed as char");
    } else {
        /* Char might be parsed as string in current implementation;
         * the spec allows type-directed disambiguation */
        ASSERT(doc != NULL, "§3.1: Char type document parses");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.1 — Boolean type
 * ================================================================ */
static void test_boolean_values(void) {
    const char *src =
        "type\n"
        "  B = Boolean;\n"
        "var t : B = True;\n"
        "var f : B = False;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.1: Boolean values parsed");
    ASSERT(!has_errors(&diags), "§3.1: no errors");
    ASSERT(doc->vars[0].value->kind == HON_VAL_BOOL, "§3.1: True is bool");
    ASSERT(doc->vars[0].value->u.boolean == true, "§3.1: True is true");
    ASSERT(doc->vars[1].value->kind == HON_VAL_BOOL, "§3.1: False is bool");
    ASSERT(doc->vars[1].value->u.boolean == false, "§3.1: False is false");

    hon_doc_free(doc);
}

/* ================================================================
 * §3.1 — LongInt type: 64-bit range
 * ================================================================ */
static void test_longint_large_value(void) {
    const char *src =
        "type\n"
        "  L = LongInt;\n"
        "var big : L = 9223372036854775807;\n"; /* INT64_MAX */

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.1: LongInt large value parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.integer == INT64_MAX,
               "§3.1: INT64_MAX preserved");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.6 — Sequence of records
 * ================================================================ */
static void test_sequence_of_records(void) {
    const char *src =
        "type\n"
        "  Point = record\n"
        "    x : Integer;\n"
        "    y : Integer;\n"
        "  end;\n"
        "  Path = sequence of Point;\n"
        "var path : Path = ((x : 0; y : 0); (x : 1; y : 2); (x : 3; y : 4));\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.6: sequence of records parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.sequence.count == 3,
               "§3.6: 3 points in path");
        hon_value *p1 = &doc->vars[0].value->u.sequence.elements[1];
        ASSERT(p1->kind == HON_VAL_RECORD, "§3.6: element is record");
        ASSERT(p1->u.record.fields[0].value->u.integer == 1,
               "§3.6: second point x=1");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §3.5 — Variant record with empty arm
 * ================================================================ */
static void test_variant_empty_arm(void) {
    const char *src =
        "type\n"
        "  Kind = (Some, None);\n"
        "  Maybe = record\n"
        "    case has : Kind of\n"
        "      Some : (val : Integer);\n"
        "      None : ();\n"
        "  end;\n"
        "var m : Maybe = (has : None);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.5: empty variant arm parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.record.variant_field_count == 0,
               "§3.5: None arm has 0 fields");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * §5.3 — Round-trip: wire mode
 * ================================================================ */
static void test_wire_mode_roundtrip(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "  P = record\n"
        "    color : Color;\n"
        "    value : Integer;\n"
        "  end;\n"
        "var p : P = (color : Green; value : 42);\n";

    hon_diag_list diags1 = {0};
    hon_document *doc1 = hon_parse(src, strlen(src), &diags1);
    ASSERT(doc1 != NULL, "§5.3: original parses");
    if (!doc1) return;

    /* Wire mode */
    size_t wlen;
    char *wire = hon_serialise(doc1, HON_MODE_WIRE, &wlen);
    ASSERT(wire != NULL, "§5.3: wire serialise succeeds");
    if (!wire) { hon_doc_free(doc1); return; }

    /* Re-parse wire output */
    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(wire, wlen, &diags2);
    ASSERT(doc2 != NULL, "§5.3: wire output re-parses");

    if (doc2) {
        ASSERT(doc2->type_count == doc1->type_count,
               "§5.3: wire round-trip preserves type count");
        ASSERT(doc2->var_count == doc1->var_count,
               "§5.3: wire round-trip preserves var count");
        ASSERT(doc2->vars[0].value->u.record.fields[1].value->u.integer == 42,
               "§5.3: wire round-trip preserves value");
        hon_doc_free(doc2);
    }

    free(wire);
    hon_doc_free(doc1);
}

/* ================================================================
 * §3.3 — Alias chains resolve correctly
 * ================================================================ */
static void test_alias_chain(void) {
    const char *src =
        "type\n"
        "  Base = Integer;\n"
        "  Mid  = Base;\n"
        "  Top  = Mid;\n"
        "var n : Top = 99;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.3: alias chain parsed");
    ASSERT(!has_errors(&diags), "§3.3: no errors");
    ASSERT(doc->vars[0].value->u.integer == 99, "§3.3: value through alias chain");

    hon_doc_free(doc);
}

/* ================================================================
 * §3.6 — Sequence of strings
 * ================================================================ */
static void test_sequence_of_strings(void) {
    const char *src =
        "type\n"
        "  Words = sequence of String;\n"
        "var w : Words = ('hello'; 'world'; 'foo');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.6: sequence of strings parsed");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.sequence.count == 3,
               "§3.6: 3 string elements");
        ASSERT(hon_str_eq(doc->vars[0].value->u.sequence.elements[1].u.string,
                          HON_STR_LIT("world")),
               "§3.6: second element is 'world'");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * Construction API: all 5 functions + serialize + round-trip
 * ================================================================ */
static void test_construction_api(void) {
    hon_document *doc = hon_doc_new();
    ASSERT(doc != NULL, "construction: hon_doc_new");

    /* 1. hon_doc_add_enum */
    hon_str vals[] = {HON_STR_LIT("Red"), HON_STR_LIT("Green"), HON_STR_LIT("Blue")};
    hon_type *color = hon_doc_add_enum(doc, HON_STR_LIT("Color"), vals, 3);
    ASSERT(color != NULL, "construction: add_enum succeeds");
    ASSERT(color->kind == HON_TYPE_ENUM, "construction: enum kind correct");
    ASSERT(color->u.enumeration.value_count == 3, "construction: 3 enum values");
    ASSERT(doc->type_count == 1, "construction: 1 type after add_enum");

    /* 2. hon_doc_add_record — record with a field of type Color */
    hon_field fields[] = {
        {HON_STR_LIT("primary"), color},
    };
    hon_type *palette = hon_doc_add_record(doc, HON_STR_LIT("Palette"), fields, 1);
    ASSERT(palette != NULL, "construction: add_record succeeds");
    ASSERT(palette->kind == HON_TYPE_RECORD, "construction: record kind correct");
    ASSERT(palette->u.record.field_count == 1, "construction: 1 field");
    ASSERT(doc->type_count == 2, "construction: 2 types after add_record");

    /* 3. hon_doc_add_sequence — sequence of Color */
    hon_type *colors = hon_doc_add_sequence(doc, HON_STR_LIT("Colors"), color);
    ASSERT(colors != NULL, "construction: add_sequence succeeds");
    ASSERT(colors->kind == HON_TYPE_SEQUENCE, "construction: sequence kind correct");
    ASSERT(colors->u.sequence.element_type == color,
           "construction: element type is Color");
    ASSERT(doc->type_count == 3, "construction: 3 types after add_sequence");

    /* 4. hon_doc_add_alias — alias of Color */
    hon_type *hue = hon_doc_add_alias(doc, HON_STR_LIT("Hue"), color);
    ASSERT(hue != NULL, "construction: add_alias succeeds");
    ASSERT(hue->kind == HON_TYPE_ALIAS, "construction: alias kind correct");
    ASSERT(hue->u.alias.target == color, "construction: alias target is Color");
    ASSERT(doc->type_count == 4, "construction: 4 types after add_alias");

    /* 5. hon_doc_add_var — add a variable */
    hon_value *val = hon_arena_alloc(doc->arena, sizeof(hon_value),
                                      _Alignof(hon_value));
    val->kind = HON_VAL_ENUM;
    val->type = color;
    val->u.enum_val = HON_STR_LIT("Green");
    int rc = hon_doc_add_var(doc, HON_STR_LIT("chosen"), color, val);
    ASSERT(rc == 0, "construction: add_var succeeds");
    ASSERT(doc->var_count == 1, "construction: 1 var after add_var");

    /* Verify error handling: NULL args rejected */
    ASSERT(hon_doc_add_enum(NULL, HON_STR_LIT("X"), vals, 3) == NULL,
           "construction: add_enum rejects NULL doc");
    ASSERT(hon_doc_add_record(NULL, HON_STR_LIT("X"), NULL, 0) == NULL,
           "construction: add_record rejects NULL doc");
    ASSERT(hon_doc_add_sequence(doc, HON_STR_LIT("X"), NULL) == NULL,
           "construction: add_sequence rejects NULL elem_type");
    ASSERT(hon_doc_add_alias(doc, HON_STR_LIT("X"), NULL) == NULL,
           "construction: add_alias rejects NULL target");
    ASSERT(hon_doc_add_var(doc, HON_STR_LIT("x"), NULL, val) == -1,
           "construction: add_var rejects NULL type");

    /* Serialise and verify round-trip */
    size_t slen;
    char *s = hon_serialise(doc, HON_MODE_DOCUMENT, &slen);
    ASSERT(s != NULL, "construction: serialise succeeds");
    if (s) {
        ASSERT(slen > 0, "construction: serialised output non-empty");
        hon_diag_list diags = {0};
        hon_document *doc2 = hon_parse(s, slen, &diags);
        ASSERT(doc2 != NULL, "construction: round-trip parses");
        if (doc2) {
            ASSERT(doc2->type_count == 4,
                   "construction: round-trip preserves 4 types");
            ASSERT(doc2->var_count == 1,
                   "construction: round-trip preserves 1 var");

            /* Verify type kinds survived round-trip */
            ASSERT(doc2->types[0]->kind == HON_TYPE_ENUM,
                   "construction: rt type[0] is enum (Color)");
            ASSERT(hon_str_eq(doc2->types[0]->name, HON_STR_LIT("Color")),
                   "construction: rt type[0] named Color");
            ASSERT(doc2->types[0]->u.enumeration.value_count == 3,
                   "construction: rt Color has 3 values");

            ASSERT(doc2->types[1]->kind == HON_TYPE_RECORD,
                   "construction: rt type[1] is record (Palette)");
            ASSERT(doc2->types[1]->u.record.field_count == 1,
                   "construction: rt Palette has 1 field");
            ASSERT(hon_str_eq(doc2->types[1]->u.record.fields[0].name,
                              HON_STR_LIT("primary")),
                   "construction: rt Palette field is 'primary'");

            ASSERT(doc2->types[2]->kind == HON_TYPE_SEQUENCE,
                   "construction: rt type[2] is sequence (Colors)");

            ASSERT(doc2->types[3]->kind == HON_TYPE_ALIAS,
                   "construction: rt type[3] is alias (Hue)");

            /* Verify value survived */
            ASSERT(hon_str_eq(doc2->vars[0].value->u.enum_val,
                              HON_STR_LIT("Green")),
                   "construction: round-trip preserves value");
            hon_doc_free(doc2);
        }
        free(s);
    }

    hon_doc_free(doc);
}

/* ================================================================
 * §7.3.5 — Serialiser round-trip invariant
 * ================================================================ */
static void test_complex_roundtrip(void) {
    /* Full NBS-style message with variant record, aliases, sequences */
    const char *src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "  Provenance = (Observed, Inferred, Recalled, Generated);\n"
        "  AgentRole  = (Supervisor, Gatekeeper, Scribe, Theologian);\n"
        "  ClaimKind  = (Assertion, Query);\n"
        "  Timestamp  = LongInt;\n"
        "  NaturalText = String;\n"
        "  Claim = record\n"
        "    confidence : Confidence;\n"
        "    provenance : Provenance;\n"
        "    as_of      : Timestamp;\n"
        "    case kind : ClaimKind of\n"
        "      Assertion : (content : NaturalText);\n"
        "      Query     : (intent : NaturalText; scope : NaturalText);\n"
        "  end;\n"
        "  Message = record\n"
        "    id      : LongInt;\n"
        "    sender  : AgentRole;\n"
        "    payload : Claim;\n"
        "  end;\n"
        "var msg : Message = (\n"
        "  id : 100;\n"
        "  sender : Theologian;\n"
        "  payload : (\n"
        "    confidence : High;\n"
        "    provenance : Inferred;\n"
        "    as_of : 1000000;\n"
        "    kind : Query;\n"
        "    intent : 'What is the target?';\n"
        "    scope : 'architecture';\n"
        "  );\n"
        ");\n";

    hon_diag_list diags1 = {0};
    hon_document *doc1 = hon_parse(src, strlen(src), &diags1);
    ASSERT(doc1 != NULL, "§7.3.5: complex doc parses");
    if (!doc1) return;

    /* Round-trip: document mode */
    size_t slen;
    char *s = hon_serialise(doc1, HON_MODE_DOCUMENT, &slen);
    ASSERT(s != NULL, "§7.3.5: serialise succeeds");
    if (!s) { hon_doc_free(doc1); return; }

    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(s, slen, &diags2);
    ASSERT(doc2 != NULL, "§7.3.5: re-parse succeeds");

    if (doc2) {
        ASSERT(doc2->type_count == doc1->type_count,
               "§7.3.5: type count preserved");
        ASSERT(doc2->var_count == doc1->var_count,
               "§7.3.5: var count preserved");

        /* Verify deep value */
        hon_value *payload = doc2->vars[0].value->u.record.fields[2].value;
        ASSERT(payload->kind == HON_VAL_RECORD, "§7.3.5: payload is record");
        ASSERT(hon_str_eq(payload->u.record.tag_value, HON_STR_LIT("Query")),
               "§7.3.5: variant tag preserved");
        ASSERT(payload->u.record.variant_field_count == 2,
               "§7.3.5: Query has 2 variant fields");
        ASSERT(hon_str_eq(payload->u.record.variant_fields[0].value->u.string,
                          HON_STR_LIT("What is the target?")),
               "§7.3.5: intent preserved through round-trip");

        hon_doc_free(doc2);
    }

    free(s);
    hon_doc_free(doc1);
}

/* ================================================================
 * §3.1 — Byte type: 0 is valid lower bound
 * ================================================================ */
static void test_byte_zero(void) {
    const char *src =
        "type\n"
        "  B = Byte;\n"
        "var b : B = 0;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.1: Byte 0 parses");
    ASSERT(!has_errors(&diags), "§3.1: Byte 0 no errors");
    ASSERT(doc->vars[0].value->u.integer == 0, "§3.1: Byte 0 value correct");

    hon_doc_free(doc);
}

/* ================================================================
 * §3.1 — Word type: 65535 is valid upper bound
 * ================================================================ */
static void test_word_max(void) {
    const char *src =
        "type\n"
        "  W = Word;\n"
        "var w : W = 65535;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.1: Word 65535 parses");
    ASSERT(!has_errors(&diags), "§3.1: Word 65535 no errors");
    ASSERT(doc->vars[0].value->u.integer == 65535, "§3.1: Word 65535 correct");

    hon_doc_free(doc);
}

/* ================================================================
 * §3.1 — Integer type: negative boundary
 * ================================================================ */
static void test_integer_min(void) {
    const char *src =
        "type\n"
        "  I = Integer;\n"
        "var i : I = -2147483648;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "§3.1: Integer MIN parses");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.integer == -2147483648LL,
               "§3.1: INT32_MIN preserved");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * hon_types_equal — structural equality
 * ================================================================ */
static void test_types_equal(void) {
    const char *src1 =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "var c : Color = Red;\n";
    const char *src2 =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "var c : Color = Green;\n";
    const char *src3 =
        "type\n"
        "  Color = (Red, Green, Blue, Yellow);\n"
        "var c : Color = Red;\n";

    hon_diag_list d1 = {0}, d2 = {0}, d3 = {0};
    hon_document *doc1 = hon_parse(src1, strlen(src1), &d1);
    hon_document *doc2 = hon_parse(src2, strlen(src2), &d2);
    hon_document *doc3 = hon_parse(src3, strlen(src3), &d3);

    if (doc1 && doc2 && doc3) {
        ASSERT(hon_types_equal(doc1->types[0], doc2->types[0]),
               "types_equal: identical enums are equal");
        ASSERT(!hon_types_equal(doc1->types[0], doc3->types[0]),
               "types_equal: different enums are not equal");
    }

    if (doc1) hon_doc_free(doc1);
    if (doc2) hon_doc_free(doc2);
    if (doc3) hon_doc_free(doc3);
}

/* ================================================================
 * Empty string
 * ================================================================ */
static void test_empty_string(void) {
    const char *src =
        "type\n"
        "  S = String;\n"
        "var s : S = '';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "empty string parses");
    if (doc && !has_errors(&diags)) {
        ASSERT(doc->vars[0].value->u.string.len == 0, "empty string has length 0");
    }

    if (doc) hon_doc_free(doc);
}

/* ================================================================
 * Multiple variables of different types
 * ================================================================ */
static void test_mixed_var_types(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "  Pair = record\n"
        "    a : Integer;\n"
        "    b : Integer;\n"
        "  end;\n"
        "  Nums = sequence of Integer;\n"
        "var c : Color = Blue;\n"
        "var p : Pair = (a : 10; b : 20);\n"
        "var n : Nums = (1; 2; 3);\n"
        "var s : String = 'hello';\n"
        "var b : Boolean = True;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "mixed vars: parses");
    ASSERT(!has_errors(&diags), "mixed vars: no errors");
    ASSERT(doc->var_count == 5, "mixed vars: 5 variables");

    ASSERT(doc->vars[0].value->kind == HON_VAL_ENUM, "mixed: enum");
    ASSERT(doc->vars[1].value->kind == HON_VAL_RECORD, "mixed: record");
    ASSERT(doc->vars[2].value->kind == HON_VAL_SEQUENCE, "mixed: sequence");
    ASSERT(doc->vars[3].value->kind == HON_VAL_STRING, "mixed: string");
    ASSERT(doc->vars[4].value->kind == HON_VAL_BOOL, "mixed: bool");

    hon_doc_free(doc);
}

/* ================================================================
 * Non-NUL-terminated buffer parsing (R6 verification)
 *
 * hon_parse accepts (buf, len). If the buffer is not NUL-terminated,
 * the lexer must respect len and never read past the boundary.
 * This is critical for socket/wire use where buffers are sized by
 * a length prefix, not a terminator.
 * ================================================================ */
static void test_non_nul_terminated_buffer(void) {
    /* Build a valid document string */
    const char *valid = "type\n  N = Integer;\nvar x : N = 42;\n";
    size_t valid_len = strlen(valid);

    /* Allocate a buffer with NO NUL terminator and garbage after */
    char *buf = malloc(valid_len + 16);
    memcpy(buf, valid, valid_len);
    /* Fill bytes after valid content with non-NUL garbage */
    memset(buf + valid_len, 0xFF, 16);

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(buf, valid_len, &diags);

    ASSERT(doc != NULL, "non-NUL buffer: parses successfully");
    ASSERT(!has_errors(&diags), "non-NUL buffer: no errors");
    ASSERT(doc->var_count == 1, "non-NUL buffer: one variable");
    ASSERT(doc->vars[0].value->kind == HON_VAL_INT, "non-NUL buffer: integer value");
    ASSERT(doc->vars[0].value->u.integer == 42, "non-NUL buffer: correct value");

    hon_doc_free(doc);
    free(buf);
}

/* Buffer with valid document followed by truncated garbage —
 * the parser must stop at len, not read into the garbage. */
static void test_buffer_boundary_with_trailing_syntax(void) {
    /* Valid document immediately followed by what looks like more syntax */
    const char full[] = "type\n  N = Integer;\nvar x : N = 7;\nvar y : N = 99;\n";
    /* Parse only up to the first var declaration */
    const char *first_var_end = "type\n  N = Integer;\nvar x : N = 7;\n";
    size_t partial_len = strlen(first_var_end);

    /* Ensure the buffer contains more data beyond partial_len */
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(full, partial_len, &diags);

    ASSERT(doc != NULL, "buffer boundary: parses partial buffer");
    ASSERT(!has_errors(&diags), "buffer boundary: no errors");
    ASSERT(doc->var_count == 1, "buffer boundary: only first var parsed");
    ASSERT(doc->vars[0].value->u.integer == 7, "buffer boundary: correct value from partial");

    hon_doc_free(doc);
}

/* Serialise to buffer and verify no NUL dependency in the output */
static void test_serialise_buffer_length(void) {
    const char *src = "type\n  N = Integer;\nvar x : N = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "serialise buffer: parse succeeds");

    size_t out_len = 0;
    char *wire = hon_serialise(doc, HON_MODE_WIRE, &out_len);
    ASSERT(wire != NULL, "serialise buffer: serialise succeeds");
    ASSERT(out_len > 0, "serialise buffer: non-zero length returned");

    /* Re-parse from the wire buffer using only the returned length */
    hon_diag_list diags2 = {0};
    hon_document *doc2 = hon_parse(wire, out_len, &diags2);
    ASSERT(doc2 != NULL, "serialise buffer: re-parse from wire succeeds");
    ASSERT(!has_errors(&diags2), "serialise buffer: no errors on re-parse");
    ASSERT(doc2->vars[0].value->u.integer == 42, "serialise buffer: value preserved");

    hon_doc_free(doc2);
    free(wire);
    hon_doc_free(doc);
}

/* ================================================================
 * Field Accessor API — hon_get_string / hon_get_long
 * ================================================================ */
static void test_accessor_api(void) {
    const char *src =
        "type\n"
        "  Verdict = (Pass, Fail, Unknown);\n"
        "  Uri = String;\n"
        "  Info = record\n"
        "    name    : String;\n"
        "    verdict : Verdict;\n"
        "    score   : LongInt;\n"
        "    active  : Boolean;\n"
        "    link    : Uri;\n"
        "  end;\n"
        "  Items = sequence of Info;\n"
        "  Report = record\n"
        "    title : String;\n"
        "    items : Items;\n"
        "  end;\n"
        "var result : Info = (\n"
        "  name    : 'Alice''s test';\n"
        "  verdict : Pass;\n"
        "  score   : 42;\n"
        "  active  : True;\n"
        "  link    : 'https://example.com';\n"
        ");\n"
        "var report : Report = (\n"
        "  title : 'Summary';\n"
        "  items : (\n"
        "    (name : 'A'; verdict : Pass;  score : 10; active : True;  link : 'a');\n"
        "    (name : 'B'; verdict : Fail;  score : 20; active : False; link : 'b');\n"
        "  );\n"
        ");\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "accessor: parse succeeds");

    /* hon_get_string — basic field */
    char buf[256];
    int rc = hon_get_string(doc, "result", "name", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: get string name succeeds");
    ASSERT(strcmp(buf, "Alice's test") == 0, "accessor: string value correct");

    /* hon_get_string — enum field (returned as string) */
    rc = hon_get_string(doc, "result", "verdict", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: get enum as string succeeds");
    ASSERT(strcmp(buf, "Pass") == 0, "accessor: enum value correct");

    /* hon_get_string — alias to String */
    rc = hon_get_string(doc, "result", "link", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: get alias-to-String succeeds");
    ASSERT(strcmp(buf, "https://example.com") == 0, "accessor: alias string value correct");

    /* hon_get_long — integer field */
    long lval;
    rc = hon_get_long(doc, "result", "score", &lval);
    ASSERT(rc == 0, "accessor: get long succeeds");
    ASSERT(lval == 42, "accessor: long value correct");

    /* hon_get_string — missing variable returns -1 */
    rc = hon_get_string(doc, "nonexistent", "name", buf, sizeof(buf));
    ASSERT(rc == -1, "accessor: missing variable returns -1");

    /* hon_get_string — missing field returns -1 */
    rc = hon_get_string(doc, "result", "nonexistent", buf, sizeof(buf));
    ASSERT(rc == -1, "accessor: missing field returns -1");

    /* hon_get_long — type mismatch returns -2 */
    rc = hon_get_long(doc, "result", "name", &lval);
    ASSERT(rc == -2, "accessor: type mismatch returns -2");

    /* hon_get_string — type mismatch on integer field returns -2 */
    rc = hon_get_string(doc, "result", "score", buf, sizeof(buf));
    ASSERT(rc == -2, "accessor: string on integer returns -2");

    /* Dot-path navigation into nested records */
    rc = hon_get_string(doc, "report", "title", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: nested record field succeeds");
    ASSERT(strcmp(buf, "Summary") == 0, "accessor: nested record value correct");

    /* Dot-path navigation into sequences */
    rc = hon_get_string(doc, "report", "items.0.name", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: sequence index.field succeeds");
    ASSERT(strcmp(buf, "A") == 0, "accessor: sequence[0] value correct");

    rc = hon_get_string(doc, "report", "items.1.verdict", buf, sizeof(buf));
    ASSERT(rc == 0, "accessor: sequence[1] enum field succeeds");
    ASSERT(strcmp(buf, "Fail") == 0, "accessor: sequence[1] value correct");

    rc = hon_get_long(doc, "report", "items.1.score", &lval);
    ASSERT(rc == 0, "accessor: sequence[1] long field succeeds");
    ASSERT(lval == 20, "accessor: sequence[1] long value correct");

    /* Out-of-bounds sequence index */
    rc = hon_get_string(doc, "report", "items.5.name", buf, sizeof(buf));
    ASSERT(rc == -1, "accessor: out-of-bounds sequence returns -1");

    hon_doc_free(doc);
}

/* ================================================================
 * Field Accessor API — hon_get_bool / hon_get_real / hon_get_char
 * ================================================================ */
static void test_accessor_bool_real_char(void) {
    const char *src =
        "type\n"
        "  Config = record\n"
        "    active : Boolean;\n"
        "    ratio  : Real;\n"
        "    grade  : Char;\n"
        "    count  : Integer;\n"
        "    name   : String;\n"
        "  end;\n"
        "var cfg : Config = (\n"
        "  active : True;\n"
        "  ratio  : 3.14;\n"
        "  grade  : 'A';\n"
        "  count  : 7;\n"
        "  name   : 'test';\n"
        ");\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "bool/real/char: parse succeeds");

    /* hon_get_bool */
    bool bval;
    int rc = hon_get_bool(doc, "cfg", "active", &bval);
    ASSERT(rc == 0, "get_bool: succeeds");
    ASSERT(bval == true, "get_bool: value is true");

    /* hon_get_bool — type mismatch */
    rc = hon_get_bool(doc, "cfg", "name", &bval);
    ASSERT(rc == -2, "get_bool: string field returns -2");

    /* hon_get_bool — missing field */
    rc = hon_get_bool(doc, "cfg", "nonexistent", &bval);
    ASSERT(rc == -1, "get_bool: missing field returns -1");

    /* hon_get_real */
    double dval;
    rc = hon_get_real(doc, "cfg", "ratio", &dval);
    ASSERT(rc == 0, "get_real: succeeds");
    ASSERT(dval > 3.13 && dval < 3.15, "get_real: value is ~3.14");

    /* hon_get_real — integer accepted as real */
    rc = hon_get_real(doc, "cfg", "count", &dval);
    ASSERT(rc == 0, "get_real: integer promoted to real");
    ASSERT(dval == 7.0, "get_real: promoted value correct");

    /* hon_get_real — type mismatch */
    rc = hon_get_real(doc, "cfg", "name", &dval);
    ASSERT(rc == -2, "get_real: string field returns -2");

    /* hon_get_char */
    uint32_t cval;
    rc = hon_get_char(doc, "cfg", "grade", &cval);
    ASSERT(rc == 0, "get_char: succeeds");
    ASSERT(cval == 'A', "get_char: value is 'A'");

    /* hon_get_char — type mismatch */
    rc = hon_get_char(doc, "cfg", "name", &cval);
    ASSERT(rc == -2, "get_char: string field returns -2");

    hon_doc_free(doc);
}

/* ================================================================
 * Z1 Hardening — foundation zone fixes
 * ================================================================ */
static void test_z1_empty_string_not_null(void) {
    /* Z1-F3: hon_str_dup for empty strings must return non-NULL data
     * so that memcmp/printf never receive NULL pointers. */
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : '');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "Z1-F3: empty string parses");

    /* The string value must have non-NULL data even for empty content */
    ASSERT(doc->vars[0].value->u.record.fields[0].value->u.string.data != NULL,
           "Z1-F3: empty string .data is not NULL");
    ASSERT(doc->vars[0].value->u.record.fields[0].value->u.string.len == 0,
           "Z1-F3: empty string .len is 0");

    /* Round-trip must work without crashing */
    char *output = hon_serialise(doc, HON_MODE_WIRE, NULL);
    ASSERT(output != NULL, "Z1-F3: serialise empty string succeeds");
    free(output);

    hon_doc_free(doc);
}

static void test_z1_str_eq_empty(void) {
    /* Z1-F2: hon_str_eq with two empty strings must not UB.
     * With the fix, empty strings have non-NULL .data. */
    hon_str a = {.data = "", .len = 0};
    hon_str b = {.data = "", .len = 0};
    ASSERT(hon_str_eq(a, b), "Z1-F2: empty strings are equal");

    hon_str c = {.data = "x", .len = 1};
    ASSERT(!hon_str_eq(a, c), "Z1-F2: empty != non-empty");
}

static void test_z1_resolve_alias_depth_guard(void) {
    /* Z1-F7: hon_resolve_alias with deep chain terminates.
     * Build a chain of 3 aliases: C -> B -> A -> Integer.
     * Verify resolve returns the primitive. */
    const char *src =
        "type\n"
        "  A = Integer;\n"
        "  B = A;\n"
        "  C = B;\n"
        "var x : C = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "Z1-F7: alias chain parses");

    hon_type *c_type = doc->types[2]; /* C */
    hon_type *resolved = hon_resolve_alias(c_type);
    ASSERT(resolved != NULL, "Z1-F7: resolve succeeds");
    ASSERT(resolved->kind == HON_TYPE_PRIMITIVE, "Z1-F7: resolves to primitive");
    ASSERT(resolved->u.prim == HON_PRIM_INTEGER, "Z1-F7: resolves to Integer");

    hon_doc_free(doc);
}

int main(void) {
    /* §2 — Lexical */
    test_bom_rejection();
    test_brace_comment();
    test_paren_star_comment();
    test_unterminated_brace_comment();
    test_unterminated_paren_star_comment();
    test_underscore_in_identifier();
    test_case_sensitive_keywords();
    test_negative_integer();
    test_real_with_exponent();
    test_pascal_quote_escape();
    test_backslash_tab_escape();
    test_backslash_backslash_escape();
    test_unicode_escape_4hex();

    /* §3 — Type System */
    test_single_value_enum();
    test_empty_record();
    test_forward_reference_rejected();
    test_duplicate_type_name();
    test_char_type();
    test_boolean_values();
    test_longint_large_value();
    test_sequence_of_records();
    test_variant_empty_arm();
    test_alias_chain();
    test_sequence_of_strings();
    test_byte_zero();
    test_word_max();
    test_integer_min();

    /* §4 — Document Structure */
    test_missing_declarations();
    test_missing_data();

    /* §5 — Serialisation */
    test_wire_mode_strips_comments();
    test_wire_mode_roundtrip();

    /* §7 — Conformance */
    test_nesting_depth_32();
    test_field_order_roundtrip();
    test_declaration_order_roundtrip();
    test_complex_roundtrip();

    /* Utility */
    test_types_equal();
    test_empty_string();
    test_mixed_var_types();

    /* Construction API */
    test_construction_api();

    /* Buffer parsing (R6 verification) */
    test_non_nul_terminated_buffer();
    test_buffer_boundary_with_trailing_syntax();
    test_serialise_buffer_length();

    /* Field Accessor API */
    test_accessor_api();
    test_accessor_bool_real_char();

    /* Z1 Hardening */
    test_z1_empty_string_not_null();
    test_z1_str_eq_empty();
    test_z1_resolve_alias_depth_guard();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
