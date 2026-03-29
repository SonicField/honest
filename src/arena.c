/*
 * Arena allocator for libhonest.
 *
 * All document memory is allocated from an arena and freed in one shot.
 * No individual frees — the arena owns everything.
 */

#include "honest.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_CAP (64 * 1024)

typedef struct hon_arena_block {
    struct hon_arena_block *next;
    size_t                  cap;
    size_t                  used;
    char                    data[];
} hon_arena_block;

struct hon_arena {
    hon_arena_block *head;
    hon_arena_block *current;
};

static hon_arena_block *block_new(size_t cap) {
    hon_arena_block *b = malloc(sizeof(hon_arena_block) + cap);
    if (!b) return NULL;
    b->next = NULL;
    b->cap  = cap;
    b->used = 0;
    return b;
}

hon_arena *hon_arena_new(size_t initial_cap) {
    if (initial_cap == 0) initial_cap = ARENA_DEFAULT_CAP;
    hon_arena *a = malloc(sizeof(hon_arena));
    if (!a) return NULL;
    a->head = block_new(initial_cap);
    if (!a->head) { free(a); return NULL; }
    a->current = a->head;
    return a;
}

void *hon_arena_alloc(hon_arena *a, size_t size, size_t align) {
    if (!a || size == 0) return NULL;
    if (align == 0) align = 8;

    hon_arena_block *b = a->current;
    size_t padding = (align - (b->used % align)) % align;
    size_t needed  = padding + size;

    if (b->used + needed > b->cap) {
        size_t new_cap = b->cap * 2;
        if (new_cap < needed) new_cap = needed + ARENA_DEFAULT_CAP;
        hon_arena_block *nb = block_new(new_cap);
        if (!nb) return NULL;
        b->next    = nb;
        a->current = nb;
        b = nb;
        padding = (align - (b->used % align)) % align;
    }

    void *ptr = b->data + b->used + padding;
    b->used += padding + size;
    memset(ptr, 0, size);
    return ptr;
}

void hon_arena_free(hon_arena *a) {
    if (!a) return;
    hon_arena_block *b = a->head;
    while (b) {
        hon_arena_block *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

/* ---- String utilities ---- */

bool hon_str_eq(hon_str a, hon_str b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.data, b.data, a.len) == 0;
}

hon_str hon_str_dup(hon_arena *a, hon_str s) {
    /* Always allocate, even for empty strings, so .data is never NULL.
     * This prevents UB from memcmp(NULL, NULL, 0) in hon_str_eq
     * and NULL dereference in printf("%.*s", ...) format strings. */
    char *copy = hon_arena_alloc(a, s.len + 1, 1);
    if (!copy) return (hon_str){"", 0};
    if (s.len > 0 && s.data) memcpy(copy, s.data, s.len);
    copy[s.len] = '\0';
    return (hon_str){copy, s.len};
}

const char *hon_primitive_name(hon_primitive p) {
    static const char *names[] = {
        [HON_PRIM_BOOLEAN] = "Boolean",
        [HON_PRIM_BYTE]    = "Byte",
        [HON_PRIM_WORD]    = "Word",
        [HON_PRIM_INTEGER] = "Integer",
        [HON_PRIM_LONGINT] = "LongInt",
        [HON_PRIM_REAL]    = "Real",
        [HON_PRIM_CHAR]    = "Char",
        [HON_PRIM_STRING]  = "String",
    };
    if (p < HON_PRIM_COUNT) return names[p];
    return "unknown";
}

hon_type *hon_resolve_alias(hon_type *t) {
    /* Guard against alias cycles — limit chain depth to 256 */
    int depth = 0;
    while (t && t->kind == HON_TYPE_ALIAS && depth < 256) {
        t = t->u.alias.target;
        depth++;
    }
    return t;
}

static bool fields_equal(const hon_field *a, const hon_field *b, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!hon_str_eq(a[i].name, b[i].name)) return false;
        if (!hon_str_eq(a[i].type->name, b[i].type->name)) return false;
    }
    return true;
}

bool hon_types_equal(const hon_type *a, const hon_type *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
    case HON_TYPE_PRIMITIVE:
        return a->u.prim == b->u.prim;

    case HON_TYPE_ENUM:
        if (a->u.enumeration.value_count != b->u.enumeration.value_count)
            return false;
        for (uint32_t i = 0; i < a->u.enumeration.value_count; i++) {
            if (!hon_str_eq(a->u.enumeration.values[i],
                            b->u.enumeration.values[i]))
                return false;
        }
        return true;

    case HON_TYPE_RECORD:
        if (a->u.record.field_count != b->u.record.field_count) return false;
        if (!fields_equal(a->u.record.fields, b->u.record.fields,
                          a->u.record.field_count))
            return false;
        if (a->u.record.has_variant != b->u.record.has_variant) return false;
        if (a->u.record.has_variant) {
            if (!hon_str_eq(a->u.record.tag_name, b->u.record.tag_name))
                return false;
            if (!hon_str_eq(a->u.record.tag_type->name,
                            b->u.record.tag_type->name))
                return false;
            if (a->u.record.arm_count != b->u.record.arm_count) return false;
            for (uint32_t i = 0; i < a->u.record.arm_count; i++) {
                const hon_variant *va = &a->u.record.arms[i];
                const hon_variant *vb = &b->u.record.arms[i];
                if (!hon_str_eq(va->label, vb->label)) return false;
                if (va->field_count != vb->field_count) return false;
                if (!fields_equal(va->fields, vb->fields, va->field_count))
                    return false;
            }
        }
        return true;

    case HON_TYPE_SEQUENCE:
        return hon_str_eq(a->u.sequence.element_type->name,
                          b->u.sequence.element_type->name);

    case HON_TYPE_ALIAS:
        return hon_str_eq(a->u.alias.target->name,
                          b->u.alias.target->name);
    }
    return false;
}
