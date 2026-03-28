/*
 * Antifragility tests for libhonest.
 *
 * Core invariant: unknown declarations extend the receiver's knowledge,
 * never cause errors. Divergent declarations are logged, not rejected.
 * The document's declarations are ground truth.
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

static bool has_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

static bool has_info(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_INFO) return true;
    }
    return false;
}

/*
 * Helper: parse a rulebook string into a registry.
 * Registry deep-copies types, so the source document can be freed.
 */
static hon_registry *load_rulebook_str(hon_arena *arena, const char *src) {
    hon_diag_list diags = {0};
    hon_document *doc = hon_parse(src, strlen(src), &diags);
    hon_registry *reg = hon_registry_new(arena);
    if (doc) {
        for (uint32_t i = 0; i < doc->type_count; i++) {
            hon_registry_extend(reg, doc->types[i]);
        }
        hon_doc_free(doc); /* safe — registry deep-copied */
    }
    return reg;
}

/*
 * Test 1: Unknown enum type
 * - Rulebook has Confidence only
 * - Document declares Priority (unknown)
 * - Parser must succeed and add Priority to registry
 */
static void test_unknown_enum(void) {
    const char *rulebook_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "var _dummy : Confidence = Certain;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, rulebook_src);
    ASSERT(reg != NULL, "T1: rulebook loaded");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Confidence")) != NULL,
           "T1: Confidence in rulebook");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Priority")) == NULL,
           "T1: Priority NOT in rulebook");

    const char *doc_src =
        "type\n"
        "  Priority = (Critical, High, Normal, Low);\n"
        "\n"
        "var p : Priority = Normal;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_with_registry(doc_src, strlen(doc_src),
                                                 reg, &diags);

    ASSERT(doc != NULL, "T1: parse succeeds");
    ASSERT(!has_errors(&diags), "T1: zero errors");
    ASSERT(doc->type_count == 1, "T1: Priority type parsed");
    ASSERT(hon_str_eq(doc->types[0]->name, HON_STR_LIT("Priority")),
           "T1: type is Priority");
    ASSERT(doc->vars[0].value->kind == HON_VAL_ENUM, "T1: value is enum");
    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val, HON_STR_LIT("Normal")),
           "T1: value is Normal");

    /* Unknown type — no divergence diagnostic expected */
    ASSERT(!has_info(&diags), "T1: no divergence info (type is new, not divergent)");

    /* Extend registry */
    hon_registry_extend(reg, doc->types[0]);
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Priority")) != NULL,
           "T1: Priority now in registry");

    hon_doc_free(doc);
    hon_arena_free(arena);
}

/*
 * Test 2: Unknown record type
 */
static void test_unknown_record(void) {
    const char *rulebook_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "var _dummy : Confidence = Certain;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, rulebook_src);
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Point")) == NULL,
           "T2: Point NOT in rulebook");

    const char *doc_src =
        "type\n"
        "  Point = record\n"
        "    x : Integer;\n"
        "    y : Integer;\n"
        "  end;\n"
        "\n"
        "var p : Point = (x : 1; y : 2);\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_with_registry(doc_src, strlen(doc_src),
                                                 reg, &diags);

    ASSERT(doc != NULL, "T2: parse succeeds");
    ASSERT(!has_errors(&diags), "T2: zero errors");
    ASSERT(doc->types[0]->kind == HON_TYPE_RECORD, "T2: Point is record");

    hon_value *val = doc->vars[0].value;
    ASSERT(val->kind == HON_VAL_RECORD, "T2: value is record");
    ASSERT(val->u.record.field_count == 2, "T2: 2 fields");
    ASSERT(val->u.record.fields[0].value->u.integer == 1, "T2: x is 1");
    ASSERT(val->u.record.fields[1].value->u.integer == 2, "T2: y is 2");

    hon_registry_extend(reg, doc->types[0]);
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Point")) != NULL,
           "T2: Point now in registry");

    hon_doc_free(doc);
    hon_arena_free(arena);
}

/*
 * Test 3: Divergent type (same name, different definition)
 * - Rulebook has Status = (Active, Inactive)
 * - Document declares Status = (Active, Inactive, Pending, Archived)
 * - Parser must succeed, document's definition governs
 * - Divergence MUST be logged as info diagnostic (spec §1.1, §6.3)
 */
static void test_divergent_type(void) {
    const char *rulebook_src =
        "type\n"
        "  Status = (Active, Inactive);\n"
        "var _dummy : Status = Active;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, rulebook_src);

    hon_type *old_status = hon_registry_lookup(reg, HON_STR_LIT("Status"));
    ASSERT(old_status != NULL, "T3: Status in rulebook");
    ASSERT(old_status->u.enumeration.value_count == 2, "T3: rulebook has 2 values");

    const char *doc_src =
        "type\n"
        "  Status = (Active, Inactive, Pending, Archived);\n"
        "\n"
        "var s : Status = Pending;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_with_registry(doc_src, strlen(doc_src),
                                                 reg, &diags);

    ASSERT(doc != NULL, "T3: parse succeeds");
    ASSERT(!has_errors(&diags), "T3: zero errors");

    /* Divergence must be logged as info (spec §1.1, §6.3) */
    ASSERT(has_info(&diags), "T3: divergence logged as info diagnostic");

    /* Document's definition governs — Pending is valid */
    ASSERT(doc->types[0]->u.enumeration.value_count == 4,
           "T3: document type has 4 values");
    ASSERT(doc->vars[0].value->kind == HON_VAL_ENUM, "T3: value is enum");
    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val, HON_STR_LIT("Pending")),
           "T3: Pending parses correctly");

    /* Update registry — document governs */
    hon_registry_extend(reg, doc->types[0]);
    hon_type *new_status = hon_registry_lookup(reg, HON_STR_LIT("Status"));
    ASSERT(new_status->u.enumeration.value_count == 4,
           "T3: registry updated to 4 values");

    hon_doc_free(doc);
    hon_arena_free(arena);
}

/*
 * Test 4: Matching type (rulebook acceleration)
 */
static void test_matching_type(void) {
    const char *rulebook_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "var _dummy : Confidence = Certain;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, rulebook_src);

    const char *doc_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "\n"
        "var c : Confidence = High;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_with_registry(doc_src, strlen(doc_src),
                                                 reg, &diags);

    ASSERT(doc != NULL, "T4: parse succeeds");
    ASSERT(!has_errors(&diags), "T4: zero errors");
    /* Matching type — no divergence diagnostic */
    ASSERT(!has_info(&diags), "T4: no divergence info (types match)");

    /* Structurally identical */
    hon_type *rb_type = hon_registry_lookup(reg, HON_STR_LIT("Confidence"));
    hon_type *doc_type = doc->types[0];
    ASSERT(rb_type->kind == doc_type->kind, "T4: same type kind");
    ASSERT(rb_type->u.enumeration.value_count == doc_type->u.enumeration.value_count,
           "T4: same value count");
    for (uint32_t i = 0; i < rb_type->u.enumeration.value_count; i++) {
        ASSERT(hon_str_eq(rb_type->u.enumeration.values[i],
                          doc_type->u.enumeration.values[i]),
               "T4: values match");
    }

    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val, HON_STR_LIT("High")),
           "T4: value is High");

    hon_doc_free(doc);
    hon_arena_free(arena);
}

/*
 * Test 5: Mixed — known, unknown, and divergent in same document
 */
static void test_mixed(void) {
    const char *rulebook_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "  Status = (Active, Inactive);\n"
        "var _dummy : Confidence = Certain;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, rulebook_src);
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Confidence")) != NULL,
           "T5: Confidence in rulebook");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Status")) != NULL,
           "T5: Status in rulebook");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Priority")) == NULL,
           "T5: Priority NOT in rulebook");

    const char *doc_src =
        "type\n"
        "  Confidence = (Certain, High, Moderate, Low, Speculative);\n"
        "  Status = (Active, Inactive, Pending, Archived);\n"
        "  Priority = (Critical, High, Normal, Low);\n"
        "\n"
        "var c : Confidence = High;\n"
        "var s : Status = Pending;\n"
        "var p : Priority = Normal;\n";

    hon_diag_list diags = {0};
    hon_document *doc = hon_parse_with_registry(doc_src, strlen(doc_src),
                                                 reg, &diags);

    ASSERT(doc != NULL, "T5: parse succeeds");
    ASSERT(!has_errors(&diags), "T5: zero errors");
    ASSERT(doc->type_count == 3, "T5: 3 types parsed");
    ASSERT(doc->var_count == 3, "T5: 3 variables parsed");

    /* Divergence diagnostic for Status */
    ASSERT(has_info(&diags), "T5: divergence info emitted for Status");

    /* All values correct */
    ASSERT(hon_str_eq(doc->vars[0].value->u.enum_val, HON_STR_LIT("High")),
           "T5: Confidence = High");
    ASSERT(hon_str_eq(doc->vars[1].value->u.enum_val, HON_STR_LIT("Pending")),
           "T5: Status = Pending (divergent value)");
    ASSERT(hon_str_eq(doc->vars[2].value->u.enum_val, HON_STR_LIT("Normal")),
           "T5: Priority = Normal (unknown type)");

    /* Extend registry */
    for (uint32_t i = 0; i < doc->type_count; i++) {
        hon_registry_extend(reg, doc->types[i]);
    }

    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Confidence")) != NULL,
           "T5: Confidence still in registry");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Status")) != NULL,
           "T5: Status updated in registry");
    ASSERT(hon_registry_lookup(reg, HON_STR_LIT("Priority")) != NULL,
           "T5: Priority added to registry");

    hon_type *status = hon_registry_lookup(reg, HON_STR_LIT("Status"));
    ASSERT(status->u.enumeration.value_count == 4,
           "T5: Status has 4 values (document governs)");

    hon_doc_free(doc);
    hon_arena_free(arena);
}

/*
 * Test 6: Registry hash is SHA-256 and stable
 */
static void test_registry_hash(void) {
    const char *src =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "  Size = (Small, Medium, Large);\n"
        "var _dummy : Color = Red;\n";

    hon_arena *arena = hon_arena_new(0);
    hon_registry *reg = load_rulebook_str(arena, src);

    char *hash1 = hon_registry_hash(reg);
    ASSERT(hash1 != NULL, "T6: hash returned");
    ASSERT(strlen(hash1) == 64, "T6: hash is 64 hex chars (SHA-256)");

    /* Same content produces same hash */
    char *hash2 = hon_registry_hash(reg);
    ASSERT(strcmp(hash1, hash2) == 0, "T6: hash is deterministic");

    free(hash1);
    free(hash2);
    hon_arena_free(arena);
}

int main(void) {
    test_unknown_enum();
    test_unknown_record();
    test_divergent_type();
    test_matching_type();
    test_mixed();
    test_registry_hash();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
