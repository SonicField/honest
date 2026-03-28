/*
 * Type registry (rulebook) for libhonest.
 *
 * A registry is a collection of named types pre-loaded from a
 * rulebook file. Used to accelerate parsing by providing known
 * type definitions.
 */

#include "honest.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Simple hash table with linear probing */
#define REGISTRY_INITIAL_CAP 64

typedef struct {
    hon_str    name;
    hon_type  *type;
    bool       occupied;
} reg_entry;

struct hon_registry {
    hon_arena  *arena;
    reg_entry  *entries;
    uint32_t    cap;
    uint32_t    count;
};

static uint32_t hash_str(hon_str s) {
    uint32_t h = 5381;
    for (size_t i = 0; i < s.len; i++) {
        h = ((h << 5) + h) + (unsigned char)s.data[i];
    }
    return h;
}

hon_registry *hon_registry_new(hon_arena *a) {
    hon_registry *reg = hon_arena_alloc(a, sizeof(hon_registry),
                                         _Alignof(hon_registry));
    if (!reg) return NULL;
    reg->arena   = a;
    reg->cap     = REGISTRY_INITIAL_CAP;
    reg->count   = 0;
    reg->entries = hon_arena_alloc(a, sizeof(reg_entry) * reg->cap,
                                   _Alignof(reg_entry));
    return reg;
}

static void registry_grow(hon_registry *reg) {
    uint32_t new_cap = reg->cap * 2;
    reg_entry *new_entries = hon_arena_alloc(reg->arena,
        sizeof(reg_entry) * new_cap, _Alignof(reg_entry));
    if (!new_entries) return;

    /* Rehash */
    for (uint32_t i = 0; i < reg->cap; i++) {
        if (!reg->entries[i].occupied) continue;
        uint32_t idx = hash_str(reg->entries[i].name) % new_cap;
        while (new_entries[idx].occupied) {
            idx = (idx + 1) % new_cap;
        }
        new_entries[idx] = reg->entries[i];
    }
    reg->entries = new_entries;
    reg->cap     = new_cap;
}

void hon_registry_extend(hon_registry *reg, hon_type *type) {
    if (!reg || !type) return;
    if (reg->count * 2 >= reg->cap) registry_grow(reg);

    uint32_t idx = hash_str(type->name) % reg->cap;
    while (reg->entries[idx].occupied) {
        if (hon_str_eq(reg->entries[idx].name, type->name)) {
            /* Update existing entry */
            reg->entries[idx].type = type;
            return;
        }
        idx = (idx + 1) % reg->cap;
    }

    reg->entries[idx].name     = type->name;
    reg->entries[idx].type     = type;
    reg->entries[idx].occupied = true;
    reg->count++;
}

hon_type *hon_registry_lookup(const hon_registry *reg, hon_str name) {
    if (!reg || reg->count == 0) return NULL;

    uint32_t idx = hash_str(name) % reg->cap;
    while (reg->entries[idx].occupied) {
        if (hon_str_eq(reg->entries[idx].name, name)) {
            return reg->entries[idx].type;
        }
        idx = (idx + 1) % reg->cap;
    }
    return NULL;
}

int hon_registry_load(hon_registry *reg, const char *path, hon_diag_list *diags) {
    hon_document *doc = hon_parse_file(path, diags);
    if (!doc) return -1;

    for (uint32_t i = 0; i < doc->type_count; i++) {
        hon_registry_extend(reg, doc->types[i]);
    }

    /* Note: we intentionally do NOT free doc here because the types
     * in the registry point into the document's arena. The registry's
     * arena should outlive or be the same as the document's arena. */
    return 0;
}

char *hon_registry_hash(const hon_registry *reg) {
    /* Simple content hash — collect all type names sorted, hash them.
     * For a proper implementation this would be SHA-256 of canonical form.
     * This is a placeholder that provides unique-enough identification. */
    if (!reg) return NULL;

    /* Collect names */
    hon_str *names = malloc(sizeof(hon_str) * reg->count);
    if (!names) return NULL;
    uint32_t n = 0;
    for (uint32_t i = 0; i < reg->cap; i++) {
        if (reg->entries[i].occupied) {
            names[n++] = reg->entries[i].name;
        }
    }

    /* Sort by name (insertion sort, small N) */
    for (uint32_t i = 1; i < n; i++) {
        hon_str key = names[i];
        int j = (int)i - 1;
        while (j >= 0) {
            int cmp = memcmp(names[j].data, key.data,
                             names[j].len < key.len ? names[j].len : key.len);
            if (cmp <= 0) break;
            names[j + 1] = names[j];
            j--;
        }
        names[j + 1] = key;
    }

    /* djb2 hash over sorted names */
    uint32_t h = 5381;
    for (uint32_t i = 0; i < n; i++) {
        for (size_t j = 0; j < names[i].len; j++) {
            h = ((h << 5) + h) + (unsigned char)names[i].data[j];
        }
        h = ((h << 5) + h) + 0xFF; /* separator */
    }

    free(names);

    char *result = malloc(17);
    if (result) snprintf(result, 17, "%08x", h);
    return result;
}
