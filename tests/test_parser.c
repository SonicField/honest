/*
 * Parser tests for libhonest.
 */

#include "honest.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

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

static void print_diags(const hon_diag_list *diags) {
    if (!diags) return;
    for (uint32_t i = 0; i < diags->count; i++) {
        const char *level = "???";
        switch (diags->items[i].level) {
        case HON_DIAG_ERROR:   level = "ERROR"; break;
        case HON_DIAG_WARNING: level = "WARN";  break;
        case HON_DIAG_INFO:    level = "INFO";  break;
        }
        fprintf(stderr, "  %s [%u:%u]: %s\n", level,
                diags->items[i].line, diags->items[i].col,
                diags->items[i].message);
    }
}

static bool has_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

static void assert_rejected(const char *src, size_t len, const char *message) {
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, len, &diags);
    ASSERT(!doc || has_errors(&diags), message);
    if (doc) hon_doc_free(doc);
    else hon_diag_free(&diags);
}

static void appendf(char *buf, size_t cap, size_t *len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *len, cap - *len, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= cap - *len) {
        fprintf(stderr, "test fixture buffer exhausted\n");
        exit(2);
    }
    *len += (size_t)written;
}

static void test_simple_enum(void) {
    const char *src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "\n"
        "var c : Confidence = High;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse simple enum");
    ASSERT(doc->type_count == 1, "one type declared");
    ASSERT(doc->types[0]->kind == HON_TYPE_ENUM, "type is enum");
    ASSERT(doc->types[0]->u.enumeration.value_count == 5, "5 enum values");
    ASSERT(hon_str_eq(doc->types[0]->u.enumeration.values[1],
                      HON_STR_LIT("High")), "second value is High");

    ASSERT(doc->var_count == 1, "one variable");
    ASSERT(doc->vars[0].value != NULL, "variable has value");
    ASSERT(doc->vars[0].value->kind == HON_VAL_ENUM, "value is enum");
    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val,
                      HON_STR_LIT("High")), "value is High");

    hon_doc_free(doc);
}

static void test_simple_record(void) {
    const char *src =
        "type\n"
        "  Point = record\n"
        "    x : Integer;\n"
        "    y : Integer;\n"
        "  end;\n"
        "\n"
        "var origin : Point = (x : 0; y : 0);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse simple record");
    ASSERT(doc->type_count == 1, "one type");
    ASSERT(doc->types[0]->kind == HON_TYPE_RECORD, "type is record");
    ASSERT(doc->types[0]->u.record.field_count == 2, "2 fields");

    ASSERT(doc->var_count == 1, "one variable");
    ASSERT(doc->vars[0].value->kind == HON_VAL_RECORD, "value is record");
    ASSERT(doc->vars[0].value->u.record.field_count == 2, "2 field values");

    hon_value *xval = doc->vars[0].value->u.record.fields[0].value;
    ASSERT(xval->kind == HON_VAL_INT, "x is int");
    ASSERT(xval->u.integer == 0, "x is 0");

    hon_doc_free(doc);
}

static void test_type_alias(void) {
    const char *src =
        "type\n"
        "  Uri = String;\n"
        "  Timestamp = LongInt;\n"
        "\n"
        "var link : Uri = 'https://example.com';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse type alias");
    ASSERT(doc->type_count == 2, "two types");
    ASSERT(doc->types[0]->kind == HON_TYPE_ALIAS, "Uri is alias");
    ASSERT(doc->types[1]->kind == HON_TYPE_ALIAS, "Timestamp is alias");

    ASSERT(doc->vars[0].value->kind == HON_VAL_STRING, "value is string");
    ASSERT(hon_str_eq(doc->vars[0].value->u.string,
                      HON_STR_LIT("https://example.com")), "correct URL");

    hon_doc_free(doc);
}

static void test_sequence(void) {
    const char *src =
        "type\n"
        "  IntList = sequence of Integer;\n"
        "\n"
        "var nums : IntList = (1; 2; 3; 4; 5);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse sequence");
    ASSERT(doc->types[0]->kind == HON_TYPE_SEQUENCE, "type is sequence");

    ASSERT(doc->vars[0].value->kind == HON_VAL_SEQUENCE, "value is sequence");
    ASSERT(doc->vars[0].value->u.sequence.count == 5, "5 elements");
    ASSERT(doc->vars[0].value->u.sequence.elements[2].u.integer == 3,
           "third element is 3");

    hon_doc_free(doc);
}

static void test_empty_sequence(void) {
    const char *src =
        "type\n"
        "  IntList = sequence of Integer;\n"
        "\n"
        "var empty : IntList = ();\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse empty sequence");
    ASSERT(doc->vars[0].value->u.sequence.count == 0, "empty sequence");

    hon_doc_free(doc);
}

static void test_variant_record(void) {
    const char *src =
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
        ");\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse variant record");
    ASSERT(doc->types[1]->kind == HON_TYPE_RECORD, "Shape is record");
    ASSERT(doc->types[1]->u.record.has_variant, "has variant");
    ASSERT(doc->types[1]->u.record.arm_count == 2, "2 arms");

    ASSERT(doc->vars[0].value->kind == HON_VAL_RECORD, "value is record");
    ASSERT(hon_str_eq(doc->vars[0].value->u.record.tag_value,
                      HON_STR_LIT("A")), "tag is A");
    ASSERT(doc->vars[0].value->u.record.variant_field_count == 2,
           "2 variant fields");

    hon_value *width = doc->vars[0].value->u.record.variant_fields[0].value;
    ASSERT(width->u.integer == 10, "width is 10");

    hon_doc_free(doc);
}

static void test_full_nbs_message(void) {
    const char *src =
        "{ An NBS agent message in Honest format }\n"
        "\n"
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
        ");\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) {
        fprintf(stderr, "Errors in full NBS message test:\n");
        print_diags(&diags);
    }

    ASSERT(doc != NULL, "parse full NBS message");
    ASSERT(doc->type_count == 8, "8 types declared");
    ASSERT(doc->var_count == 1, "1 variable");

    /* Check Message structure */
    hon_value *msg = doc->vars[0].value;
    ASSERT(msg->kind == HON_VAL_RECORD, "message is record");
    ASSERT(msg->u.record.field_count == 3, "3 fixed fields");

    /* Check id */
    hon_value *id = msg->u.record.fields[0].value;
    ASSERT(id->kind == HON_VAL_INT, "id is int");
    ASSERT(id->u.integer == 4721, "id is 4721");

    /* Check sender */
    hon_value *sender = msg->u.record.fields[1].value;
    ASSERT(sender->kind == HON_VAL_ENUM, "sender is enum");
    ASSERT(hon_str_eq(sender->u.enum_val, HON_STR_LIT("Theologian")),
           "sender is Theologian");

    /* Check payload (nested record with variant) */
    hon_value *payload = msg->u.record.fields[2].value;
    ASSERT(payload->kind == HON_VAL_RECORD, "payload is record");
    ASSERT(payload->u.record.field_count == 3, "3 fixed fields in claim");
    ASSERT(hon_str_eq(payload->u.record.tag_value, HON_STR_LIT("Assertion")),
           "claim kind is Assertion");
    ASSERT(payload->u.record.variant_field_count == 1,
           "1 variant field for Assertion");

    hon_value *content = payload->u.record.variant_fields[0].value;
    ASSERT(content->kind == HON_VAL_STRING, "content is string");
    ASSERT(hon_str_eq(content->u.string,
                      HON_STR_LIT("The compilation target is ARM Thumb2")),
           "correct content");

    hon_doc_free(doc);
}

static void test_string_escapes(void) {
    const char *src =
        "type\n"
        "  T = String;\n"
        "\n"
        "var s : T = 'hello\\nworld';\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse string escapes");
    ASSERT(doc->vars[0].value->kind == HON_VAL_STRING, "value is string");
    ASSERT(doc->vars[0].value->u.string.len == 11, "length includes newline");
    ASSERT(doc->vars[0].value->u.string.data[5] == '\n', "newline at pos 5");

    hon_doc_free(doc);
}

static void test_comments_preserved(void) {
    const char *src =
        "{ This is a document comment }\n"
        "type\n"
        "  (* Inline comment *)\n"
        "  Count = Integer;\n"
        "\n"
        "var n : Count = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse with comments");
    ASSERT(doc->type_count == 1, "one type despite comments");
    ASSERT(doc->vars[0].value->u.integer == 42, "correct value");

    size_t document_len = 0;
    char *document = hon_serialise(doc, HON_MODE_DOCUMENT, &document_len);
    const char *first = document ? strstr(document, "{ This is a document comment }") : NULL;
    const char *second = document ? strstr(document, "(* Inline comment *)") : NULL;
    ASSERT(first && second && first < second,
           "document mode preserves comment text and encounter order");

    size_t wire_len = 0;
    char *wire = hon_serialise(doc, HON_MODE_WIRE, &wire_len);
    ASSERT(wire && strstr(wire, "document comment") == NULL &&
           strstr(wire, "Inline comment") == NULL,
           "wire mode strips preserved comments");

    free(document);
    free(wire);
    hon_doc_free(doc);
}

static void test_trailing_semicolons(void) {
    const char *src =
        "type\n"
        "  Point = record\n"
        "    x : Integer;\n"
        "    y : Integer;\n"  /* trailing ; after last field */
        "  end;\n"
        "\n"
        "var p : Point = (x : 10; y : 20;);\n";  /* trailing ; in value */

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse trailing semicolons");
    ASSERT(doc->vars[0].value->u.record.fields[1].value->u.integer == 20,
           "y is 20");

    hon_doc_free(doc);
}

static void test_multiple_vars(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "\n"
        "var a : Color = Red;\n"
        "var b : Color = Blue;\n"
        "var c : Integer = 42;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse multiple vars");
    ASSERT(doc->var_count == 3, "3 variables");
    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val, HON_STR_LIT("Red")),
           "a is Red");
    ASSERT(hon_str_eq(doc->vars[1].value->u.enum_val, HON_STR_LIT("Blue")),
           "b is Blue");
    ASSERT(doc->vars[2].value->u.integer == 42, "c is 42");

    hon_doc_free(doc);
}

static void test_boolean_variant(void) {
    const char *src =
        "type\n"
        "  OptString = record\n"
        "    case present : Boolean of\n"
        "      True  : (value : String);\n"
        "      False : ();\n"
        "  end;\n"
        "\n"
        "var yes : OptString = (present : True; value : 'hello');\n"
        "var no  : OptString = (present : False);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse boolean variant");
    ASSERT(doc->var_count == 2, "2 variables");

    ASSERT(hon_str_eq(doc->vars[0].value->u.record.tag_value,
                      HON_STR_LIT("True")), "yes is True");
    ASSERT(doc->vars[0].value->u.record.variant_field_count == 1,
           "yes has 1 variant field");

    ASSERT(hon_str_eq(doc->vars[1].value->u.record.tag_value,
                      HON_STR_LIT("False")), "no is False");
    ASSERT(doc->vars[1].value->u.record.variant_field_count == 0,
           "no has 0 variant fields");

    hon_doc_free(doc);
}

static void test_real_literal(void) {
    const char *src =
        "type\n"
        "  T = Real;\n"
        "\n"
        "var pi : T = 3.14159;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    if (has_errors(&diags)) print_diags(&diags);

    ASSERT(doc != NULL, "parse real literal");
    ASSERT(doc->vars[0].value->kind == HON_VAL_REAL, "value is real");
    double diff = doc->vars[0].value->u.real - 3.14159;
    ASSERT(diff > -0.00001 && diff < 0.00001, "pi approx correct");

    hon_doc_free(doc);
}

/* ---- Validation tests (negative cases) ---- */

static void test_invalid_enum_value(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "\n"
        "var c : Color = Yellow;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "invalid enum value detected");

    hon_doc_free(doc);
}

static void test_byte_out_of_range(void) {
    const char *src =
        "type\n"
        "  B = Byte;\n"
        "\n"
        "var b : B = 256;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "byte out of range detected");

    hon_doc_free(doc);
}

static void test_word_out_of_range(void) {
    const char *src =
        "type\n"
        "  W = Word;\n"
        "\n"
        "var w : W = 70000;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "word out of range detected");

    hon_doc_free(doc);
}

static void test_negative_byte(void) {
    const char *src =
        "type\n"
        "  B = Byte;\n"
        "\n"
        "var b : B = -1;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "negative byte detected");

    hon_doc_free(doc);
}

static void test_duplicate_enum_value(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Red);\n"
        "\n"
        "var c : Color = Red;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "duplicate enum value detected");

    hon_doc_free(doc);
}

static void test_duplicate_field_name(void) {
    const char *src =
        "type\n"
        "  Bad = record\n"
        "    x : Integer;\n"
        "    x : Integer;\n"
        "  end;\n"
        "\n"
        "var b : Bad = (x : 1; x : 2);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse completes (with errors)");
    ASSERT(has_errors(&diags), "duplicate field name detected");

    hon_doc_free(doc);
}

static void test_valid_byte_range(void) {
    const char *src =
        "type\n"
        "  B = Byte;\n"
        "\n"
        "var b : B = 255;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);

    ASSERT(doc != NULL, "parse valid byte");
    ASSERT(!has_errors(&diags), "255 is valid Byte");
    ASSERT(doc->vars[0].value->u.integer == 255, "value is 255");

    hon_doc_free(doc);
}

static void test_record_value_schema_validation(void) {
    assert_rejected(
        "type R = record x : Integer; end; var r : R = (wrong : 7);",
        strlen("type R = record x : Integer; end; var r : R = (wrong : 7);"),
        "record rejects undeclared field name");
    assert_rejected(
        "type R = record x : Integer; y : Integer; end; "
        "var r : R = (x : 1; x : 2);",
        strlen("type R = record x : Integer; y : Integer; end; "
               "var r : R = (x : 1; x : 2);"),
        "record rejects duplicate field in place of required field");
}

static void test_variant_schema_validation(void) {
    const char *wrong_tag =
        "type K = (A, B); R = record case kind : K of "
        "A : (x : Integer); B : (); end; "
        "var r : R = (wrong : A; x : 1);";
    const char *unknown_tag =
        "type K = (A, B); R = record case kind : K of "
        "A : (x : Integer); B : (); end; "
        "var r : R = (kind : Z);";
    const char *wrong_arm_field =
        "type K = (A, B); R = record case kind : K of "
        "A : (x : Integer); B : (); end; "
        "var r : R = (kind : A; wrong : 1);";
    const char *integer_tag =
        "type R = record case kind : Integer of A : (); end; "
        "var r : R = (kind : 1);";
    const char *unknown_arm =
        "type K = (A, B); R = record case kind : K of Z : (); end;";
    const char *duplicate_arm =
        "type K = (A, B); R = record case kind : K of "
        "A : (); A : (); end;";

    assert_rejected(wrong_tag, strlen(wrong_tag),
                    "variant rejects incorrect tag field name");
    assert_rejected(unknown_tag, strlen(unknown_tag),
                    "variant rejects value outside discriminant enumeration");
    assert_rejected(wrong_arm_field, strlen(wrong_arm_field),
                    "variant rejects undeclared arm field name");
    assert_rejected(integer_tag, strlen(integer_tag),
                    "variant rejects non-enum, non-Boolean discriminant type");
    assert_rejected(unknown_arm, strlen(unknown_arm),
                    "variant rejects arm outside discriminant enumeration");
    assert_rejected(duplicate_arm, strlen(duplicate_arm),
                    "variant rejects duplicate arm label");
}

static void test_primitive_rejections(void) {
    static const char *const malformed_reals[] = {"1e", "1e+", "1e-"};
    const char *char_src = "type C = Char; var c : C = 'ab';";
    const char *long_src =
        "type L = LongInt; var n : L = 9223372036854775808;";
    const char invalid_utf8[] =
        "type S = String; var s : S = 'bad\xFF';";

    assert_rejected(char_src, strlen(char_src),
                    "Char rejects more than one Unicode scalar");
    assert_rejected(long_src, strlen(long_src),
                    "LongInt rejects values above INT64_MAX");
    assert_rejected(invalid_utf8, sizeof(invalid_utf8) - 1,
                    "String rejects invalid UTF-8 bytes");

    for (size_t i = 0; i < sizeof(malformed_reals) / sizeof(malformed_reals[0]); i++) {
        char src[128];
        int n = snprintf(src, sizeof(src),
                         "type R = Real; var r : R = %s;", malformed_reals[i]);
        ASSERT(n > 0 && (size_t)n < sizeof(src), "real rejection fixture fits");
        assert_rejected(src, (size_t)n, "malformed exponent is rejected");
    }
}

static void test_declarations_are_required(void) {
    const char *src = "type";
    assert_rejected(src, strlen(src),
                    "declarations section requires at least one declaration");
}

static void test_required_semicolons(void) {
    static const char *const malformed[] = {
        "type R = record x : Integer y : Integer end; "
        "var r : R = (x : 1; y : 2);",
        "type R = record x : Integer; y : Integer; end; "
        "var r : R = (x : 1 y : 2);",
        "type K = (A, B); R = record case kind : K of "
        "A : () B : (); end; var r : R = (kind : B);",
        "type K = (A); R = record case kind : K of "
        "A : (x : Integer y : Integer); end; "
        "var r : R = (kind : A; x : 1; y : 2);",
        "type K = (A); R = record case kind : K of "
        "A : (x : Integer); end; var r : R = (kind : A x : 1);",
    };

    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        assert_rejected(malformed[i], strlen(malformed[i]),
                        "missing required semicolon is rejected");
    }
}

static void test_trailing_tokens_rejected(void) {
    const char *src =
        "type X = Integer; var x : X = 1; trailing";
    assert_rejected(src, strlen(src),
                    "tokens after a complete document are rejected");
}

static void test_strict_document_and_rulebook_entry_points(void) {
    const char *rulebook = "type N = Integer;";
    const char *document = "type N = Integer; var n : N = 1;";
    const char *invalid = "type N = Integer; var n : N = nope;";
    hon_diag_list diags = {0};

    hon_document *doc = hon_parse_document(
        rulebook, strlen(rulebook), &diags);
    ASSERT(doc == NULL, "strict document parser requires data");
    ASSERT(has_errors(&diags), "missing document data reports an error");
    hon_diag_free(&diags);

    diags = (hon_diag_list){0};
    doc = hon_parse_rulebook(rulebook, strlen(rulebook), &diags);
    ASSERT(doc != NULL && !has_errors(&diags),
           "strict rulebook parser accepts declarations only");
    if (doc) hon_doc_free(doc);

    diags = (hon_diag_list){0};
    doc = hon_parse_rulebook(document, strlen(document), &diags);
    ASSERT(doc == NULL, "strict rulebook parser rejects data");
    ASSERT(has_errors(&diags), "rulebook data reports an error");
    hon_diag_free(&diags);

    diags = (hon_diag_list){0};
    doc = hon_parse_document(document, strlen(document), &diags);
    ASSERT(doc != NULL && !has_errors(&diags),
           "strict document parser accepts declarations and data");
    if (doc) hon_doc_free(doc);

    diags = (hon_diag_list){0};
    doc = hon_parse_document(invalid, strlen(invalid), &diags);
    ASSERT(doc == NULL, "strict document parser never returns partial errors");
    ASSERT(has_errors(&diags), "strict parse retains error diagnostics");
    hon_diag_free(&diags);
}

static void test_reserved_boolean_type_rejected(void) {
    const char *src = "type Boolean = (Yes, No);";
    assert_rejected(src, strlen(src),
                    "reserved Boolean name cannot be redeclared");
}

static void test_large_declarations_are_not_truncated(void) {
    char src[65536];
    size_t len = 0;
    hon_diag_list diags = {0};

    appendf(src, sizeof(src), &len, "type E = (");
    for (int i = 0; i < 257; i++)
        appendf(src, sizeof(src), &len, "%sV%d", i ? ", " : "", i);
    appendf(src, sizeof(src), &len, ");");

    hon_document *doc = hon_parse(src, len, &diags);
    ASSERT(doc != NULL, "large enum: parse succeeds");
    ASSERT(!has_errors(&diags), "large enum: no parse errors");
    ASSERT(doc && doc->types[0]->u.enumeration.value_count == 257,
           "large enum: all 257 values retained");
    ASSERT(doc && hon_str_eq(doc->types[0]->u.enumeration.values[256],
                            HON_STR_LIT("V256")),
           "large enum: final value retained");
    hon_doc_free(doc);

    len = 0;
    diags = (hon_diag_list){0};
    appendf(src, sizeof(src), &len, "type R = record ");
    for (int i = 0; i < 129; i++)
        appendf(src, sizeof(src), &len, "f%d : Integer; ", i);
    appendf(src, sizeof(src), &len, "end;");

    doc = hon_parse(src, len, &diags);
    ASSERT(doc != NULL, "large record: parse succeeds");
    ASSERT(!has_errors(&diags), "large record: no parse errors");
    ASSERT(doc && doc->types[0]->u.record.field_count == 129,
           "large record: all 129 fields retained");
    ASSERT(doc && hon_str_eq(doc->types[0]->u.record.fields[128].name,
                            HON_STR_LIT("f128")),
           "large record: final field retained");
    hon_doc_free(doc);

    len = 0;
    diags = (hon_diag_list){0};
    appendf(src, sizeof(src), &len, "type K = (");
    for (int i = 0; i < 65; i++)
        appendf(src, sizeof(src), &len, "%sK%d", i ? ", " : "", i);
    appendf(src, sizeof(src), &len,
            "); R = record case kind : K of K0 : (");
    for (int i = 0; i < 65; i++)
        appendf(src, sizeof(src), &len, "f%d : Integer; ", i);
    appendf(src, sizeof(src), &len, "); ");
    for (int i = 1; i < 65; i++)
        appendf(src, sizeof(src), &len, "K%d : (); ", i);
    appendf(src, sizeof(src), &len, "end;");

    doc = hon_parse(src, len, &diags);
    ASSERT(doc != NULL, "large variant: parse succeeds");
    ASSERT(!has_errors(&diags), "large variant: no parse errors");
    ASSERT(doc && doc->types[1]->u.record.arm_count == 65,
           "large variant: all 65 arms retained");
    ASSERT(doc && doc->types[1]->u.record.arms[0].field_count == 65,
           "large variant: all 65 arm fields retained");
    ASSERT(doc && hon_str_eq(doc->types[1]->u.record.arms[64].label,
                            HON_STR_LIT("K64")),
           "large variant: final arm retained");
    ASSERT(doc && hon_str_eq(doc->types[1]->u.record.arms[0].fields[64].name,
                            HON_STR_LIT("f64")),
           "large variant: final arm field retained");
    hon_doc_free(doc);
}

int main(void) {
    test_simple_enum();
    test_simple_record();
    test_type_alias();
    test_sequence();
    test_empty_sequence();
    test_variant_record();
    test_full_nbs_message();
    test_string_escapes();
    test_comments_preserved();
    test_trailing_semicolons();
    test_multiple_vars();
    test_boolean_variant();
    test_real_literal();

    /* Validation tests */
    test_invalid_enum_value();
    test_byte_out_of_range();
    test_word_out_of_range();
    test_negative_byte();
    test_duplicate_enum_value();
    test_duplicate_field_name();
    test_valid_byte_range();
    test_record_value_schema_validation();
    test_variant_schema_validation();
    test_primitive_rejections();
    test_declarations_are_required();
    test_required_semicolons();
    test_trailing_tokens_rejected();
    test_strict_document_and_rulebook_entry_points();
    test_reserved_boolean_type_rejected();
    test_large_declarations_are_not_truncated();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
