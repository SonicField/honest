/*
 * Document construction API for libhonest.
 *
 * Programmatic building of Honest documents without parsing text.
 */

#include "honest.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_TYPE_CAP 16
#define INITIAL_VAR_CAP  8

hon_document *hon_doc_new(void) {
    hon_arena *arena = hon_arena_new(0);
    if (!arena) return NULL;

    hon_document *doc = hon_arena_alloc(arena, sizeof(hon_document),
                                        _Alignof(hon_document));
    if (!doc) { hon_arena_free(arena); return NULL; }

    doc->arena      = arena;
    doc->type_count = 0;
    doc->var_count  = 0;

    doc->types = hon_arena_alloc(arena, sizeof(hon_type *) * INITIAL_TYPE_CAP,
                                 _Alignof(hon_type *));
    doc->vars  = hon_arena_alloc(arena, sizeof(hon_variable) * INITIAL_VAR_CAP,
                                 _Alignof(hon_variable));

    return doc;
}

static int doc_ensure_type_cap(hon_document *doc) {
    /* Grow when type_count reaches a power of 2 at or above initial cap. */
    if (doc->type_count > 0 && (doc->type_count & (doc->type_count - 1)) == 0 &&
        doc->type_count >= INITIAL_TYPE_CAP) {
        uint32_t new_cap = doc->type_count * 2;
        hon_type **new_types = hon_arena_alloc(doc->arena,
            sizeof(hon_type *) * new_cap, _Alignof(hon_type *));
        if (!new_types) return -1; /* OOM — do not increment type_count */
        memcpy(new_types, doc->types, sizeof(hon_type *) * doc->type_count);
        doc->types = new_types;
    }
    return 0;
}

hon_type *hon_doc_add_enum(hon_document *doc, hon_str name,
                            const hon_str *values, uint32_t count) {
    if (!doc || count == 0) return NULL;
    if (doc_ensure_type_cap(doc) < 0) return NULL;

    hon_type *t = hon_arena_alloc(doc->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_ENUM;
    t->name = hon_str_dup(doc->arena, name);
    t->u.enumeration.value_count = count;
    t->u.enumeration.values = hon_arena_alloc(doc->arena,
        sizeof(hon_str) * count, _Alignof(hon_str));
    for (uint32_t i = 0; i < count; i++) {
        t->u.enumeration.values[i] = hon_str_dup(doc->arena, values[i]);
    }

    doc->types[doc->type_count++] = t;
    return t;
}

hon_type *hon_doc_add_record(hon_document *doc, hon_str name,
                              const hon_field *fields, uint32_t count) {
    if (!doc) return NULL;
    if (doc_ensure_type_cap(doc) < 0) return NULL;

    hon_type *t = hon_arena_alloc(doc->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_RECORD;
    t->name = hon_str_dup(doc->arena, name);
    t->u.record.field_count = count;
    t->u.record.has_variant = false;
    t->u.record.fields = NULL;
    if (count > 0) {
        t->u.record.fields = hon_arena_alloc(doc->arena,
            sizeof(hon_field) * count, _Alignof(hon_field));
        for (uint32_t i = 0; i < count; i++) {
            t->u.record.fields[i].name = hon_str_dup(doc->arena, fields[i].name);
            t->u.record.fields[i].type = fields[i].type;
        }
    }

    doc->types[doc->type_count++] = t;
    return t;
}

hon_type *hon_doc_add_sequence(hon_document *doc, hon_str name,
                                hon_type *elem_type) {
    if (!doc || !elem_type) return NULL;
    if (doc_ensure_type_cap(doc) < 0) return NULL;

    hon_type *t = hon_arena_alloc(doc->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_SEQUENCE;
    t->name = hon_str_dup(doc->arena, name);
    t->u.sequence.element_type = elem_type;

    doc->types[doc->type_count++] = t;
    return t;
}

hon_type *hon_doc_add_alias(hon_document *doc, hon_str name,
                             hon_type *target) {
    if (!doc || !target) return NULL;
    if (doc_ensure_type_cap(doc) < 0) return NULL;

    hon_type *t = hon_arena_alloc(doc->arena, sizeof(hon_type), _Alignof(hon_type));
    t->kind = HON_TYPE_ALIAS;
    t->name = hon_str_dup(doc->arena, name);
    t->u.alias.target = target;

    doc->types[doc->type_count++] = t;
    return t;
}

int hon_doc_add_var(hon_document *doc, hon_str name,
                    hon_type *type, hon_value *val) {
    if (!doc || !type || !val) return -1;

    /* Simple capacity check */
    uint32_t cap = INITIAL_VAR_CAP;
    while (cap <= doc->var_count) cap *= 2;
    if (doc->var_count > 0 && (doc->var_count & (doc->var_count - 1)) == 0 &&
        doc->var_count >= INITIAL_VAR_CAP) {
        uint32_t new_cap = doc->var_count * 2;
        hon_variable *new_vars = hon_arena_alloc(doc->arena,
            sizeof(hon_variable) * new_cap, _Alignof(hon_variable));
        if (new_vars) {
            memcpy(new_vars, doc->vars, sizeof(hon_variable) * doc->var_count);
            doc->vars = new_vars;
        }
    }

    doc->vars[doc->var_count++] = (hon_variable){
        .name  = hon_str_dup(doc->arena, name),
        .type  = type,
        .value = val,
    };

    return 0;
}
