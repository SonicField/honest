/*
 * Edge case and boundary tests for the field accessor API (G2).
 *
 * Complements the basic accessor tests in test_conformance.c.
 * Focuses on:
 * - Top-level scalar access (empty field_name)
 * - Buffer boundary behavior
 * - Alias chains deeper than one level
 * - Empty string values
 * - Integer range boundaries
 * - Special characters in extracted values
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

/* ================================================================
 * Top-level scalar access
 *
 * A document may contain top-level scalar variables using built-in
 * types. Users need to extract these without specifying a field path.
 * The navigate() function handles empty field_name by returning the
 * value directly.
 * ================================================================ */

static void test_get_string_top_level_empty_path(void) {
    const char *src =
        "type\n"
        "  Dummy = Integer;\n"
        "var greeting : String = 'hello world';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "top-level string: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "greeting", "", buf, sizeof(buf));
    ASSERT(rc == 0, "top-level string: empty field_name returns 0");
    ASSERT(strcmp(buf, "hello world") == 0, "top-level string: correct value");

    hon_doc_free(doc);
}

static void test_get_long_top_level_empty_path(void) {
    const char *src =
        "type\n"
        "  Dummy = Integer;\n"
        "var count : Integer = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "top-level long: parse succeeds");

    long val = 0;
    int rc = hon_get_long(doc, "count", "", &val);
    ASSERT(rc == 0, "top-level long: empty field_name returns 0");
    ASSERT(val == 42, "top-level long: correct value 42");

    hon_doc_free(doc);
}

/* ================================================================
 * Buffer boundary tests for hon_get_string
 * ================================================================ */

static void test_get_string_exact_fit(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'abc');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "exact fit: parse succeeds");

    char buf[4]; /* "abc" + NUL = 4 bytes */
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    ASSERT(rc == 0, "exact fit: returns 0");
    ASSERT(strcmp(buf, "abc") == 0, "exact fit: value correct");

    hon_doc_free(doc);
}

static void test_get_string_truncation(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'hello world');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "truncation: parse succeeds");

    char buf[6]; /* "hello" + NUL = 6, value is "hello world" (11 chars) */
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    /* New contract: returns required buffer size (>0) on truncation */
    ASSERT(rc == 12, "truncation: returns required bufsz (11 chars + NUL = 12)");
    ASSERT(strcmp(buf, "hello") == 0, "truncation: truncated to buffer size");
    ASSERT(buf[5] == '\0', "truncation: NUL-terminated");

    hon_doc_free(doc);
}

static void test_get_string_one_byte_buffer(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'test');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "one-byte buf: parse succeeds");

    char buf[1];
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    /* "test" needs 5 bytes (4 chars + NUL) */
    ASSERT(rc == 5, "one-byte buf: returns required bufsz (4 chars + NUL = 5)");
    ASSERT(buf[0] == '\0', "one-byte buf: NUL-terminated empty string");

    hon_doc_free(doc);
}

/* ================================================================
 * Empty string value
 * ================================================================ */

static void test_get_string_empty_value(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : '');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "empty string: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    ASSERT(rc == 0, "empty string: returns 0");
    ASSERT(strcmp(buf, "") == 0, "empty string: value is empty");
    ASSERT(buf[0] == '\0', "empty string: NUL at position 0");

    hon_doc_free(doc);
}

/* ================================================================
 * Deep alias chain
 * ================================================================ */

static void test_get_string_deep_alias(void) {
    const char *src =
        "type\n"
        "  Base = String;\n"
        "  Mid  = Base;\n"
        "  Deep = Mid;\n"
        "  R = record\n"
        "    val : Deep;\n"
        "  end;\n"
        "var r : R = (val : 'resolved');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "deep alias: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "r", "val", buf, sizeof(buf));
    ASSERT(rc == 0, "deep alias: three-level alias chain resolved");
    ASSERT(strcmp(buf, "resolved") == 0, "deep alias: correct value");

    hon_doc_free(doc);
}

/* ================================================================
 * Integer boundaries
 * ================================================================ */

static void test_get_long_zero(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    n : LongInt;\n"
        "  end;\n"
        "var r : R = (n : 0);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "long zero: parse succeeds");

    long val = -1;
    int rc = hon_get_long(doc, "r", "n", &val);
    ASSERT(rc == 0, "long zero: returns 0");
    ASSERT(val == 0, "long zero: value is 0");

    hon_doc_free(doc);
}

static void test_get_long_negative(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    n : LongInt;\n"
        "  end;\n"
        "var r : R = (n : -999);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "long negative: parse succeeds");

    long val = 0;
    int rc = hon_get_long(doc, "r", "n", &val);
    ASSERT(rc == 0, "long negative: returns 0");
    ASSERT(val == -999, "long negative: correct negative value");

    hon_doc_free(doc);
}

/* ================================================================
 * Cross-type convenience: hon_get_string on enum
 * ================================================================ */

static void test_get_string_reads_enum(void) {
    const char *src =
        "type\n"
        "  Status = (Active, Inactive, Pending);\n"
        "  R = record\n"
        "    state : Status;\n"
        "  end;\n"
        "var r : R = (state : Pending);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "string reads enum: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "r", "state", buf, sizeof(buf));
    ASSERT(rc == 0, "string reads enum: returns 0 (convenience behavior)");
    ASSERT(strcmp(buf, "Pending") == 0, "string reads enum: correct enum label");

    hon_doc_free(doc);
}

/* ================================================================
 * Type mismatch: hon_get_long on Boolean
 * ================================================================ */

static void test_get_long_rejects_bool(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    flag : Boolean;\n"
        "  end;\n"
        "var r : R = (flag : True);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "long rejects bool: parse succeeds");

    long val = 0;
    int rc = hon_get_long(doc, "r", "flag", &val);
    ASSERT(rc == -2, "long rejects bool: returns -2 (type mismatch)");

    hon_doc_free(doc);
}

/* ================================================================
 * Special characters — escaping round-trip via accessor
 * ================================================================ */

static void test_get_string_special_chars(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'line1\\nline2\\ttab');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "special chars: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    ASSERT(rc == 0, "special chars: returns 0");
    ASSERT(strchr(buf, '\n') != NULL, "special chars: contains literal newline");
    ASSERT(strchr(buf, '\t') != NULL, "special chars: contains literal tab");

    hon_doc_free(doc);
}

static void test_get_string_embedded_quotes(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'it''s a test');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "embedded quotes: parse succeeds");

    char buf[256];
    int rc = hon_get_string(doc, "r", "s", buf, sizeof(buf));
    ASSERT(rc == 0, "embedded quotes: returns 0");
    ASSERT(strcmp(buf, "it's a test") == 0, "embedded quotes: correct unescaped value");

    hon_doc_free(doc);
}

/* ================================================================
 * Multiple variables in one document
 * ================================================================ */

static void test_get_multiple_vars(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    val : String;\n"
        "  end;\n"
        "var first  : R = (val : 'one');\n"
        "var second : R = (val : 'two');\n"
        "var third  : R = (val : 'three');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "multi vars: parse succeeds");

    char buf[256];
    int rc;

    rc = hon_get_string(doc, "first", "val", buf, sizeof(buf));
    ASSERT(rc == 0 && strcmp(buf, "one") == 0, "multi vars: first correct");

    rc = hon_get_string(doc, "second", "val", buf, sizeof(buf));
    ASSERT(rc == 0 && strcmp(buf, "two") == 0, "multi vars: second correct");

    rc = hon_get_string(doc, "third", "val", buf, sizeof(buf));
    ASSERT(rc == 0 && strcmp(buf, "three") == 0, "multi vars: third correct");

    hon_doc_free(doc);
}

/* ================================================================
 * hon_get_bool tests
 *
 * Contract: returns 0 on success, -1 not found, -2 type mismatch.
 * Writes true/false to *out.
 * ================================================================ */

static void test_get_bool_true(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    flag : Boolean;\n"
        "  end;\n"
        "var r : R = (flag : True);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_bool true: parse succeeds");

    bool val = false;
    int rc = hon_get_bool(doc, "r", "flag", &val);
    ASSERT(rc == 0, "get_bool true: returns 0");
    ASSERT(val == true, "get_bool true: value is true");

    hon_doc_free(doc);
}

static void test_get_bool_false(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    flag : Boolean;\n"
        "  end;\n"
        "var r : R = (flag : False);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_bool false: parse succeeds");

    bool val = true;
    int rc = hon_get_bool(doc, "r", "flag", &val);
    ASSERT(rc == 0, "get_bool false: returns 0");
    ASSERT(val == false, "get_bool false: value is false");

    hon_doc_free(doc);
}

static void test_get_bool_missing(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    flag : Boolean;\n"
        "  end;\n"
        "var r : R = (flag : True);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_bool missing: parse succeeds");

    bool val = false;
    int rc = hon_get_bool(doc, "r", "nonexistent", &val);
    ASSERT(rc == -1, "get_bool missing: returns -1");

    hon_doc_free(doc);
}

static void test_get_bool_type_mismatch(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    n : Integer;\n"
        "  end;\n"
        "var r : R = (n : 42);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_bool mismatch: parse succeeds");

    bool val = false;
    int rc = hon_get_bool(doc, "r", "n", &val);
    ASSERT(rc == -2, "get_bool mismatch: returns -2 for integer field");

    hon_doc_free(doc);
}

/* ================================================================
 * hon_get_real tests
 *
 * Contract: returns 0 on success, -1 not found, -2 type mismatch.
 * Writes double to *out.
 * ================================================================ */

static void test_get_real_positive(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    x : Real;\n"
        "  end;\n"
        "var r : R = (x : 3.14);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_real positive: parse succeeds");

    double val = 0.0;
    int rc = hon_get_real(doc, "r", "x", &val);
    ASSERT(rc == 0, "get_real positive: returns 0");
    /* Compare with tolerance for floating-point */
    ASSERT(val > 3.13 && val < 3.15, "get_real positive: value is ~3.14");

    hon_doc_free(doc);
}

static void test_get_real_negative(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    x : Real;\n"
        "  end;\n"
        "var r : R = (x : -0.5);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_real negative: parse succeeds");

    double val = 0.0;
    int rc = hon_get_real(doc, "r", "x", &val);
    ASSERT(rc == 0, "get_real negative: returns 0");
    ASSERT(val > -0.51 && val < -0.49, "get_real negative: value is ~-0.5");

    hon_doc_free(doc);
}

static void test_get_real_zero(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    x : Real;\n"
        "  end;\n"
        "var r : R = (x : 0.0);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_real zero: parse succeeds");

    double val = 1.0;
    int rc = hon_get_real(doc, "r", "x", &val);
    ASSERT(rc == 0, "get_real zero: returns 0");
    ASSERT(val == 0.0, "get_real zero: value is 0.0");

    hon_doc_free(doc);
}

static void test_get_real_type_mismatch(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'hello');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_real mismatch: parse succeeds");

    double val = 0.0;
    int rc = hon_get_real(doc, "r", "s", &val);
    ASSERT(rc == -2, "get_real mismatch: returns -2 for string field");

    hon_doc_free(doc);
}

/* ================================================================
 * hon_get_char tests
 *
 * Contract: returns 0 on success, -1 not found, -2 type mismatch.
 * Writes uint32_t Unicode code point to *out.
 * ================================================================ */

static void test_get_char_ascii(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    c : Char;\n"
        "  end;\n"
        "var r : R = (c : 'A');\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_char ascii: parse succeeds");

    uint32_t val = 0;
    int rc = hon_get_char(doc, "r", "c", &val);
    ASSERT(rc == 0, "get_char ascii: returns 0");
    ASSERT(val == 'A', "get_char ascii: value is 'A' (65)");

    hon_doc_free(doc);
}

static void test_get_char_type_mismatch(void) {
    const char *src =
        "type\n"
        "  R = record\n"
        "    n : Integer;\n"
        "  end;\n"
        "var r : R = (n : 42);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc != NULL, "get_char mismatch: parse succeeds");

    uint32_t val = 0;
    int rc = hon_get_char(doc, "r", "n", &val);
    ASSERT(rc == -2, "get_char mismatch: returns -2 for integer field");

    hon_doc_free(doc);
}

int main(void) {
    /* Top-level scalar access */
    test_get_string_top_level_empty_path();
    test_get_long_top_level_empty_path();

    /* Buffer boundaries */
    test_get_string_exact_fit();
    test_get_string_truncation();
    test_get_string_one_byte_buffer();

    /* Empty string value */
    test_get_string_empty_value();

    /* Deep alias chain */
    test_get_string_deep_alias();

    /* Integer boundaries */
    test_get_long_zero();
    test_get_long_negative();

    /* Cross-type convenience */
    test_get_string_reads_enum();

    /* Type mismatch */
    test_get_long_rejects_bool();

    /* Special characters */
    test_get_string_special_chars();
    test_get_string_embedded_quotes();

    /* Multiple variables */
    test_get_multiple_vars();

    /* hon_get_bool */
    test_get_bool_true();
    test_get_bool_false();
    test_get_bool_missing();
    test_get_bool_type_mismatch();

    /* hon_get_real */
    test_get_real_positive();
    test_get_real_negative();
    test_get_real_zero();
    test_get_real_type_mismatch();

    /* hon_get_char */
    test_get_char_ascii();
    test_get_char_type_mismatch();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
