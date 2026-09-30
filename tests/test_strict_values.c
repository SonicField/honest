#include "honest.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run;
static int tests_passed;

#define ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) tests_passed++; \
    else fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, message); \
} while (0)

static bool has_level(const hon_diag_list *diags, hon_diag_level level) {
    for (uint32_t i = 0; diags && i < diags->count; i++) {
        if (diags->items[i].level == level) return true;
    }
    return false;
}

static void release_parse(hon_document *doc, hon_diag_list *diags) {
    if (doc) hon_doc_free(doc);
    else hon_diag_free(diags);
}

static void expect_rejected(const char *src, size_t len, const char *message) {
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, len, &diags);
    ASSERT(!doc || has_level(&diags, HON_DIAG_ERROR), message);
    release_parse(doc, &diags);
}

static hon_document *expect_valid(const char *src, const char *message) {
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    ASSERT(doc && !has_level(&diags, HON_DIAG_ERROR), message);
    if (!doc) hon_diag_free(&diags);
    return doc;
}

static void test_integer_boundaries(void) {
    struct integer_case {
        const char *type;
        const char *literal;
        int64_t expected;
    } valid[] = {
        {"Byte", "0", 0}, {"Byte", "255", 255},
        {"Word", "0", 0}, {"Word", "65535", 65535},
        {"Integer", "-2147483648", INT32_MIN},
        {"Integer", "2147483647", INT32_MAX},
        {"LongInt", "-9223372036854775808", INT64_MIN},
        {"LongInt", "9223372036854775807", INT64_MAX},
    };
    struct invalid_integer_case {
        const char *type;
        const char *literal;
    } invalid[] = {
        {"Byte", "-1"}, {"Byte", "256"},
        {"Word", "-1"}, {"Word", "65536"},
        {"Integer", "-2147483649"}, {"Integer", "2147483648"},
        {"LongInt", "-9223372036854775809"},
        {"LongInt", "9223372036854775808"},
        {"Integer", "1x"},
    };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type T = %s; var value : T = %s;",
                         valid[i].type, valid[i].literal);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "integer fixture fits");
        hon_document *doc = expect_valid(src, "integer boundary parses");
        if (doc) {
            ASSERT(doc->vars[0].value->u.integer == valid[i].expected,
                   "integer boundary retains exact value");
            size_t wire_len = 0;
            char *wire = hon_serialise(doc, HON_MODE_WIRE, &wire_len);
            hon_diag_list again_diags = {0};
            hon_document *again = wire ? hon_parse(wire, wire_len, &again_diags) : NULL;
            ASSERT(again && !has_level(&again_diags, HON_DIAG_ERROR),
                   "integer boundary round-trips");
            release_parse(again, &again_diags);
            free(wire);
            hon_doc_free(doc);
        }
    }

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type T = %s; var value : T = %s;",
                         invalid[i].type, invalid[i].literal);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "invalid integer fixture fits");
        expect_rejected(src, (size_t)n, "out-of-range or malformed integer rejected");
    }
}

static void test_real_boundaries(void) {
    static const char *const valid[] = {
        "0.0", "-1.5", "1.0e+10", "1.7976931348623157e308",
    };
    static const char *const invalid[] = {
        "1", "1e", "1e+", "1e-", "1e2", "1.", ".1", "NaN", "inf", "-inf",
        "1.0e999", "-1.0e999", "1.0x",
    };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type R = Real; var value : R = %s;", valid[i]);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "real fixture fits");
        hon_document *doc = expect_valid(src, "finite real parses");
        if (doc) {
            ASSERT(isfinite(doc->vars[0].value->u.real), "real remains finite");
            size_t wire_len = 0;
            char *wire = hon_serialise(doc, HON_MODE_WIRE, &wire_len);
            hon_diag_list again_diags = {0};
            hon_document *again = wire ? hon_parse(wire, wire_len, &again_diags) : NULL;
            ASSERT(again && !has_level(&again_diags, HON_DIAG_ERROR),
                   "finite real round-trips");
            release_parse(again, &again_diags);
            free(wire);
            hon_doc_free(doc);
        }
    }

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type R = Real; var value : R = %s;", invalid[i]);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "invalid real fixture fits");
        expect_rejected(src, (size_t)n, "malformed or non-finite real rejected");
    }
}

static void test_char_scalars(void) {
    struct char_case { const char *literal; uint32_t expected; } valid[] = {
        {"'A'", 'A'}, {"''''", '\''}, {"'\\\\'", '\\'},
        {"'\\n'", '\n'}, {"'\\r'", '\r'}, {"'\\t'", '\t'},
        {"'\\u00E9'", 0x00E9}, {"'\\U0001F680'", 0x1F680},
    };
    static const char *const invalid[] = {
        "''", "'ab'", "'é🚀'", "'é'", "'\\uD800'", "'\\U00110000'",
    };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type C = Char; var value : C = %s;", valid[i].literal);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "Char fixture fits");
        hon_document *doc = expect_valid(src, "one Unicode scalar Char parses");
        if (doc) {
            ASSERT(doc->vars[0].value->u.character == valid[i].expected,
                   "Char retains exact scalar");
            size_t wire_len = 0;
            char *wire = hon_serialise(doc, HON_MODE_WIRE, &wire_len);
            hon_diag_list again_diags = {0};
            hon_document *again = wire ? hon_parse(wire, wire_len, &again_diags) : NULL;
            ASSERT(again && !has_level(&again_diags, HON_DIAG_ERROR),
                   "Char round-trips");
            release_parse(again, &again_diags);
            free(wire);
            hon_doc_free(doc);
        }
    }

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char src[160];
        int n = snprintf(src, sizeof(src),
                         "type C = Char; var value : C = %s;", invalid[i]);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "invalid Char fixture fits");
        expect_rejected(src, (size_t)n, "invalid Char rejected");
    }
}

static void test_string_unicode_escapes(void) {
    static const char *const invalid[] = {
        "type S = String; var value : S = '\\uZZZZ';",
        "type S = String; var value : S = '\\u12';",
        "type S = String; var value : S = '\\uD800';",
        "type S = String; var value : S = '\\U00110000';",
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        expect_rejected(invalid[i], strlen(invalid[i]),
                        "malformed or non-scalar Unicode escape rejected");

    hon_document *doc = expect_valid(
        "type S = String; var value : S = '\\u0000\\u001F';",
        "escaped control scalars parse");
    if (doc) {
        hon_str value = doc->vars[0].value->u.string;
        ASSERT(value.len == 2, "escaped controls retain their length");
        ASSERT(value.data[0] == '\0' && value.data[1] == 0x1F,
               "escaped controls retain their values");
        size_t wire_len = 0;
        char *wire = hon_serialise(doc, HON_MODE_WIRE, &wire_len);
        ASSERT(wire && strstr(wire, "\\u0000\\u001F"),
               "escaped controls remain escaped when serialised");
        hon_diag_list again_diags = {0};
        hon_document *again = wire ? hon_parse(wire, wire_len, &again_diags) : NULL;
        ASSERT(again && !has_level(&again_diags, HON_DIAG_ERROR),
               "escaped controls round-trip");
        release_parse(again, &again_diags);
        free(wire);
        hon_doc_free(doc);
    }
}

static void test_invalid_utf8_contexts(void) {
    static const unsigned char *const invalid[] = {
        (const unsigned char *)"\x80",
        (const unsigned char *)"\xFF",
        (const unsigned char *)"\xC0\x80",
        (const unsigned char *)"\xE2\x82",
        (const unsigned char *)"\xED\xA0\x80",
        (const unsigned char *)"\xF4\x90\x80\x80",
    };
    static const size_t invalid_len[] = {1, 1, 2, 2, 3, 4};
    static const char *const prefixes[] = {
        "type S = String; var s : S = '",
        "type ",
        "{",
        "(*",
        "type ",
    };
    static const char *const suffixes[] = {
        "';", " = Integer; var x : Integer = 1;", "} type N = Integer;",
        "*) type N = Integer;", "N = Integer;",
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        for (size_t j = 0; j < sizeof(prefixes) / sizeof(prefixes[0]); j++) {
            size_t prefix_len = strlen(prefixes[j]);
            size_t suffix_len = strlen(suffixes[j]);
            size_t len = prefix_len + invalid_len[i] + suffix_len;
            char *src = malloc(len);
            ASSERT(src != NULL, "UTF-8 fixture allocation succeeds");
            if (!src) continue;
            memcpy(src, prefixes[j], prefix_len);
            memcpy(src + prefix_len, invalid[i], invalid_len[i]);
            memcpy(src + prefix_len + invalid_len[i], suffixes[j], suffix_len);
            expect_rejected(src, len, "invalid UTF-8 rejected in every context");
            free(src);
        }
    }

    hon_document *doc = expect_valid(
        "{ café } (* 🚀 *) type S = String; var s : S = 'naïve';",
        "valid UTF-8 remains accepted in comments and strings");
    if (doc) hon_doc_free(doc);
}

static void test_record_and_variant_schema(void) {
    static const char *const invalid_records[] = {
        "type R = record x : Integer; y : String; end; var r : R = (wrong : 7; y : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7);",
        "type R = record x : Integer; y : String; end; var r : R = (y : 'ok'; x : 7);",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7; x : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 'bad'; y : 'ok');",
        "type R = record x : Integer; y : String; end; var r : R = (x : 7; y : 'ok'; z : 1);",
    };
    static const char *const invalid_variants[] = {
        "type R = record case kind : Integer of A : (); end; var r : R = (kind : 1);",
        "type R = record case kind : String of A : (); end; var r : R = (kind : A);",
        "type K = record end; R = record case kind : K of A : (); end; var r : R = (kind : A);",
        "type K = sequence of Integer; R = record case kind : K of A : (); end; var r : R = (kind : A);",
        "type K = (A, B); R = record case kind : K of Z : (); end; var r : R = (kind : A);",
        "type K = (A, B); R = record case kind : K of A : (); A : (); end; var r : R = (kind : A);",
        "type K = (A); R = record case kind : K of A : (x : Integer; x : Integer); end; var r : R = (kind : A; x : 1; x : 2);",
        "type K = (A, B); R = record fixed : Integer; case kind : K of A : (x : Integer); B : (); end; var r : R = (fixed : 1; wrong : A; x : 2);",
        "type K = (A, B); R = record fixed : Integer; case kind : K of A : (x : Integer); B : (); end; var r : R = (fixed : 1; kind : Z);",
        "type K = (A, B); R = record fixed : Integer; case kind : K of A : (x : Integer); B : (); end; var r : R = (fixed : 1; kind : A);",
        "type K = (A, B); R = record fixed : Integer; case kind : K of A : (x : Integer); B : (); end; var r : R = (fixed : 1; kind : A; wrong : 2);",
        "type K = (A, B); R = record fixed : Integer; case kind : K of A : (x : Integer); B : (); end; var r : R = (fixed : 1; kind : B; x : 2);",
    };

    hon_document *doc = expect_valid(
        "type R = record x : Integer; y : String; end; "
        "var r : R = (x : 7; y : 'ok');",
        "fixed record matching its schema parses");
    if (doc) hon_doc_free(doc);

    doc = expect_valid(
        "type K = (A, B); Alias = K; R = record case kind : Alias of "
        "A : (); B : (); end; var r : R = (kind : A);",
        "enum alias discriminant parses");
    if (doc) hon_doc_free(doc);

    doc = expect_valid(
        "type Flag = Boolean; R = record case present : Flag of "
        "True : (); False : (); end; var r : R = (present : False);",
        "Boolean alias discriminant parses");
    if (doc) hon_doc_free(doc);

    for (size_t i = 0; i < sizeof(invalid_records) / sizeof(invalid_records[0]); i++)
        expect_rejected(invalid_records[i], strlen(invalid_records[i]),
                        "invalid fixed record rejected");
    for (size_t i = 0; i < sizeof(invalid_variants) / sizeof(invalid_variants[0]); i++)
        expect_rejected(invalid_variants[i], strlen(invalid_variants[i]),
                        "invalid variant rejected");

    const char *missing_arm =
        "type K = (A, B); R = record case kind : K of A : (); end;";
    hon_diag_list diags = {0};
    doc = hon_parse(missing_arm, strlen(missing_arm), &diags);
    ASSERT(doc && !has_level(&diags, HON_DIAG_ERROR),
           "missing legal variant arm is not an error");
    ASSERT(has_level(&diags, HON_DIAG_WARNING),
           "missing legal variant arm emits a warning");
    if (doc) hon_doc_free(doc);
    else hon_diag_free(&diags);
}

static void test_nonempty_declarations(void) {
    expect_rejected("type", 4, "bare type is rejected");
    expect_rejected("type { comment only }", strlen("type { comment only }"),
                    "comment-only declarations are rejected");
    hon_document *doc = expect_valid("type N = Integer;",
                                     "one-declaration rulebook remains valid");
    if (doc) hon_doc_free(doc);
    doc = expect_valid("type N = Integer; var n : N = 1;",
                       "normal document remains valid");
    if (doc) hon_doc_free(doc);
}

int main(void) {
    test_integer_boundaries();
    test_real_boundaries();
    test_char_scalars();
    test_string_unicode_escapes();
    test_invalid_utf8_contexts();
    test_record_and_variant_schema();
    test_nonempty_declarations();

    printf("%d/%d strict value tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
