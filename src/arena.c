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
    return memcmp(a.data, b.data, a.len) == 0;
}

hon_str hon_str_dup(hon_arena *a, hon_str s) {
    if (s.len == 0) return (hon_str){NULL, 0};
    char *copy = hon_arena_alloc(a, s.len + 1, 1);
    if (!copy) return (hon_str){NULL, 0};
    memcpy(copy, s.data, s.len);
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
    while (t && t->kind == HON_TYPE_ALIAS)
        t = t->u.alias.target;
    return t;
}
