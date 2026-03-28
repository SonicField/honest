/*
 * Parser tests for libhonest.
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

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
