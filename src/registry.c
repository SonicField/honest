/*
 * Type registry (rulebook) for libhonest.
 *
 * A registry is a collection of named types. It owns all stored types
 * by deep-copying them into its own arena. This ensures registry
 * entries remain valid regardless of the source document's lifetime.
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

/* ---- Deep Copy ---- */

#define REGISTRY_COPY_MAX_DEPTH 256

static hon_type *deep_copy_type(hon_arena *a, const hon_type *src,
                                uint32_t depth, bool *ok);

static bool array_size(uint32_t count, size_t item_size, size_t *size) {
    if (count > 0 && item_size > SIZE_MAX / count) return false;
    *size = (size_t)count * item_size;
    return true;
}

static bool deep_copy_str(hon_arena *a, hon_str src, hon_str *dst) {
    if (!dst || src.len == SIZE_MAX || (src.len > 0 && !src.data)) return false;
    char *copy = hon_arena_alloc(a, src.len + 1, 1);
    if (!copy) return false;
    if (src.len > 0) memcpy(copy, src.data, src.len);
    copy[src.len] = '\0';
    *dst = (hon_str){copy, src.len};
    return true;
}

static hon_field *deep_copy_fields(hon_arena *a, const hon_field *src,
                                   uint32_t count, uint32_t depth, bool *ok) {
    if (count == 0) return NULL;
    size_t fields_size = 0;
    if (!src || !array_size(count, sizeof(hon_field), &fields_size)) {
        *ok = false;
        return NULL;
    }
    hon_field *dst = hon_arena_alloc(a, fields_size,
                                     _Alignof(hon_field));
    if (!dst) {
        *ok = false;
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!deep_copy_str(a, src[i].name, &dst[i].name) || !src[i].type) {
            *ok = false;
            return NULL;
        }
        dst[i].type = deep_copy_type(a, src[i].type, depth + 1, ok);
        if (!*ok) return NULL;
    }
    return dst;
}

static hon_type *deep_copy_type(hon_arena *a, const hon_type *src,
                                uint32_t depth, bool *ok) {
    if (!src || depth > REGISTRY_COPY_MAX_DEPTH) {
        *ok = false;
        return NULL;
    }

    if (src->kind > HON_TYPE_ALIAS) {
        *ok = false;
        return NULL;
    }

    hon_type *dst = hon_arena_alloc(a, sizeof(hon_type), _Alignof(hon_type));
    if (!dst || !deep_copy_str(a, src->name, &dst->name)) {
        *ok = false;
        return NULL;
    }
    dst->kind = src->kind;

    switch (src->kind) {
    case HON_TYPE_ENUM:
        dst->u.enumeration.value_count = src->u.enumeration.value_count;
        if (src->u.enumeration.value_count == 0) {
            dst->u.enumeration.values = NULL;
            break;
        }
        size_t values_size = 0;
        if (!src->u.enumeration.values ||
            !array_size(src->u.enumeration.value_count, sizeof(hon_str),
                        &values_size)) {
            *ok = false;
            return NULL;
        }
        dst->u.enumeration.values = hon_arena_alloc(
            a, values_size, _Alignof(hon_str));
        if (!dst->u.enumeration.values) {
            *ok = false;
            return NULL;
        }
        for (uint32_t i = 0; i < src->u.enumeration.value_count; i++) {
            if (!deep_copy_str(a, src->u.enumeration.values[i],
                               &dst->u.enumeration.values[i])) {
                *ok = false;
                return NULL;
            }
        }
        break;

    case HON_TYPE_RECORD:
        dst->u.record.field_count = src->u.record.field_count;
        dst->u.record.fields = deep_copy_fields(
            a, src->u.record.fields, src->u.record.field_count, depth, ok);
        if (!*ok) return NULL;
        dst->u.record.has_variant = src->u.record.has_variant;
        if (src->u.record.has_variant) {
            if (!deep_copy_str(a, src->u.record.tag_name,
                               &dst->u.record.tag_name) ||
                !src->u.record.tag_type) {
                *ok = false;
                return NULL;
            }
            dst->u.record.tag_type = deep_copy_type(
                a, src->u.record.tag_type, depth + 1, ok);
            if (!*ok) return NULL;
            dst->u.record.arm_count = src->u.record.arm_count;
            if (src->u.record.arm_count == 0) {
                dst->u.record.arms = NULL;
                break;
            }
            size_t arms_size = 0;
            if (!src->u.record.arms ||
                !array_size(src->u.record.arm_count, sizeof(hon_variant),
                            &arms_size)) {
                *ok = false;
                return NULL;
            }
            dst->u.record.arms = hon_arena_alloc(
                a, arms_size, _Alignof(hon_variant));
            if (!dst->u.record.arms) {
                *ok = false;
                return NULL;
            }
            for (uint32_t i = 0; i < src->u.record.arm_count; i++) {
                if (!deep_copy_str(a, src->u.record.arms[i].label,
                                   &dst->u.record.arms[i].label)) {
                    *ok = false;
                    return NULL;
                }
                dst->u.record.arms[i].field_count =
                    src->u.record.arms[i].field_count;
                dst->u.record.arms[i].fields = deep_copy_fields(
                    a, src->u.record.arms[i].fields,
                    src->u.record.arms[i].field_count, depth, ok);
                if (!*ok) return NULL;
            }
        }
        break;

    case HON_TYPE_SEQUENCE:
        if (!src->u.sequence.element_type) {
            *ok = false;
            return NULL;
        }
        dst->u.sequence.element_type = deep_copy_type(
            a, src->u.sequence.element_type, depth + 1, ok);
        if (!*ok) return NULL;
        break;

    case HON_TYPE_ALIAS:
        if (!src->u.alias.target) {
            *ok = false;
            return NULL;
        }
        dst->u.alias.target = deep_copy_type(
            a, src->u.alias.target, depth + 1, ok);
        if (!*ok) return NULL;
        break;

    case HON_TYPE_PRIMITIVE:
        if (src->u.prim >= HON_PRIM_COUNT) {
            *ok = false;
            return NULL;
        }
        dst->u.prim = src->u.prim;
        break;

    default:
        *ok = false;
        return NULL;
    }

    return dst;
}

/* ---- Public API ---- */

hon_registry *hon_registry_new(hon_arena *a) {
    hon_registry *reg = hon_arena_alloc(a, sizeof(hon_registry),
                                         _Alignof(hon_registry));
    if (!reg) return NULL;
    reg->arena   = a;
    reg->cap     = REGISTRY_INITIAL_CAP;
    reg->count   = 0;
    reg->entries = hon_arena_alloc(a, sizeof(reg_entry) * reg->cap,
                                   _Alignof(reg_entry));
    if (!reg->entries) return NULL;
    return reg;
}

static bool registry_grow(hon_registry *reg) {
    if (reg->cap > UINT32_MAX / 2) return false;
    uint32_t new_cap = reg->cap * 2;
    size_t entries_size = 0;
    if (!array_size(new_cap, sizeof(reg_entry), &entries_size)) return false;
    reg_entry *new_entries = hon_arena_alloc(
        reg->arena, entries_size, _Alignof(reg_entry));
    if (!new_entries) return false;

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
    return true;
}

static bool registry_extend_checked(hon_registry *reg, hon_type *type) {
    if (!reg || !type) return false;
    if (reg->count >= reg->cap / 2 && !registry_grow(reg)) return false;

    /* Deep-copy type into registry's arena — registry owns its types */
    bool copy_ok = true;
    hon_type *owned = deep_copy_type(reg->arena, type, 0, &copy_ok);
    if (!copy_ok || !owned) return false;

    uint32_t idx = hash_str(owned->name) % reg->cap;
    while (reg->entries[idx].occupied) {
        if (hon_str_eq(reg->entries[idx].name, owned->name)) {
            /* Update existing entry */
            reg->entries[idx].type = owned;
            return true;
        }
        idx = (idx + 1) % reg->cap;
    }

    reg->entries[idx].name     = owned->name;
    reg->entries[idx].type     = owned;
    reg->entries[idx].occupied = true;
    reg->count++;
    return true;
}

void hon_registry_extend(hon_registry *reg, hon_type *type) {
    (void)registry_extend_checked(reg, type);
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

static bool diagnostics_have_errors(const hon_diag_list *diags) {
    if (!diags) return false;
    for (uint32_t i = 0; i < diags->count; i++) {
        if (diags->items[i].level == HON_DIAG_ERROR) return true;
    }
    return false;
}

/* Keep parser diagnostics alive after the document arena is released. */
static void detach_diagnostics(hon_diag_list *diags) {
    if (!diags || diags->count == 0 || diags->owns_memory) return;

    hon_diagnostic *items = calloc(diags->count, sizeof(*items));
    if (!items) {
        diags->items = NULL;
        diags->count = 0;
        diags->cap = 0;
        return;
    }

    for (uint32_t i = 0; i < diags->count; i++) {
        items[i] = diags->items[i];
        if (diags->items[i].message) {
            size_t message_len = strlen(diags->items[i].message);
            char *message = malloc(message_len + 1);
            if (!message) {
                for (uint32_t j = 0; j < i; j++) free((void *)items[j].message);
                free(items);
                diags->items = NULL;
                diags->count = 0;
                diags->cap = 0;
                return;
            }
            memcpy(message, diags->items[i].message, message_len + 1);
            items[i].message = message;
        }
    }
    diags->items = items;
    diags->cap = diags->count;
    diags->owns_memory = true;
}

int hon_registry_load(hon_registry *reg, const char *path, hon_diag_list *diags) {
    if (!reg || !path) return -1;
    hon_diag_list local_diags = {0};
    hon_diag_list *parse_diags = diags ? diags : &local_diags;
    hon_document *doc = hon_parse_rulebook_file(path, parse_diags);
    if (!doc) {
        if (!diags) hon_diag_free(&local_diags);
        return -1;
    }

    if (doc->var_count > 0) {
        hon_diag_push(parse_diags, doc->arena, HON_DIAG_ERROR, 0, 0,
                      "rulebook must not contain variable assignments");
    }
    if (diagnostics_have_errors(parse_diags)) {
        if (diags) detach_diagnostics(diags);
        hon_doc_free(doc);
        return -1;
    }

    /* Stage a replacement table so any allocation failure leaves the
     * caller-visible registry unchanged. Arena allocations need no rollback. */
    uint32_t stage_cap = reg->cap;
    uint64_t maximum_count = (uint64_t)reg->count + doc->type_count;
    while (maximum_count >= stage_cap / 2) {
        if (stage_cap > UINT32_MAX / 2) {
            if (diags) detach_diagnostics(diags);
            hon_doc_free(doc);
            return -1;
        }
        stage_cap *= 2;
    }

    size_t stage_size = 0;
    if (!array_size(stage_cap, sizeof(reg_entry), &stage_size)) {
        if (diags) detach_diagnostics(diags);
        hon_doc_free(doc);
        return -1;
    }
    reg_entry *stage_entries = hon_arena_alloc(
        reg->arena, stage_size, _Alignof(reg_entry));
    if (!stage_entries) {
        if (diags) detach_diagnostics(diags);
        hon_doc_free(doc);
        return -1;
    }

    for (uint32_t i = 0; i < reg->cap; i++) {
        if (!reg->entries[i].occupied) continue;
        uint32_t idx = hash_str(reg->entries[i].name) % stage_cap;
        while (stage_entries[idx].occupied) idx = (idx + 1) % stage_cap;
        stage_entries[idx] = reg->entries[i];
    }

    hon_registry stage = {
        .arena = reg->arena,
        .entries = stage_entries,
        .cap = stage_cap,
        .count = reg->count,
    };
    for (uint32_t i = 0; i < doc->type_count; i++) {
        if (!registry_extend_checked(&stage, doc->types[i])) {
            if (diags) detach_diagnostics(diags);
            hon_doc_free(doc);
            return -1;
        }
    }

    reg->entries = stage.entries;
    reg->cap = stage.cap;
    reg->count = stage.count;

    /* Safe to free — registry deep-copied all types */
    hon_doc_free(doc);
    return 0;
}

/* ---- SHA-256 ---- */

/* Minimal SHA-256 implementation — no external dependency */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define RR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x) (RR(x, 2) ^ RR(x, 13) ^ RR(x, 22))
#define S1(x) (RR(x, 6) ^ RR(x, 11) ^ RR(x, 25))
#define s0(x) (RR(x, 7) ^ RR(x, 18) ^ ((x) >> 3))
#define s1(x) (RR(x, 17) ^ RR(x, 19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];
    uint8_t  buf[64];
    uint64_t total;
} sha256_ctx;

static void sha256_init(sha256_ctx *c) {
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->total = 0;
}

static void sha256_transform(sha256_ctx *c, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        w[i] = s1(w[i-2]) + w[i-7] + s0(w[i-15]) + w[i-16];
    }

    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + S1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        uint32_t t2 = S0(a) + MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g;  c->state[7] += h;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len) {
    const uint8_t *p = data;
    size_t bufused = (size_t)(c->total % 64);
    c->total += len;

    if (bufused > 0) {
        size_t need = 64 - bufused;
        if (len < need) {
            memcpy(c->buf + bufused, p, len);
            return;
        }
        memcpy(c->buf + bufused, p, need);
        sha256_transform(c, c->buf);
        p += need;
        len -= need;
    }

    while (len >= 64) {
        sha256_transform(c, p);
        p += 64;
        len -= 64;
    }

    if (len > 0) memcpy(c->buf, p, len);
}

static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    size_t bufused = (size_t)(c->total % 64);
    c->buf[bufused++] = 0x80;

    if (bufused > 56) {
        memset(c->buf + bufused, 0, 64 - bufused);
        sha256_transform(c, c->buf);
        bufused = 0;
    }

    memset(c->buf + bufused, 0, 56 - bufused);
    uint64_t bits = c->total * 8;
    for (int i = 7; i >= 0; i--) {
        c->buf[56 + (7 - i)] = (uint8_t)(bits >> (i * 8));
    }
    sha256_transform(c, c->buf);

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(c->state[i] >> 24);
        out[i*4+1] = (uint8_t)(c->state[i] >> 16);
        out[i*4+2] = (uint8_t)(c->state[i] >> 8);
        out[i*4+3] = (uint8_t)(c->state[i]);
    }
}

#undef RR
#undef CH
#undef MAJ
#undef S0
#undef S1
#undef s0
#undef s1

/* ---- Canonical Type Serialisation (for hashing) ---- */

/* Append to a dynamically growing buffer */
typedef struct { char *data; size_t len; size_t cap; bool failed; } hashbuf;

static int hb_init(hashbuf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->len = 0;
    b->failed = b->data == NULL;
    return b->data ? 0 : -1;
}
static void hb_free(hashbuf *b) { free(b->data); }

static void hb_append(hashbuf *b, const char *s, size_t n) {
    if (b->failed || n > SIZE_MAX - b->len) {
        b->failed = true;
        return;
    }
    size_t needed = b->len + n;
    while (needed >= b->cap) {
        if (b->cap > SIZE_MAX / 2) {
            b->failed = true;
            return;
        }
        size_t new_cap = b->cap * 2;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) {
            b->failed = true;
            return;
        }
        b->data = new_data;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void hb_str(hashbuf *b, const char *s) { hb_append(b, s, strlen(s)); }

static void serialise_type_canonical(hashbuf *b, const hon_type *t) {
    hb_append(b, t->name.data, t->name.len);
    hb_str(b, " = ");

    switch (t->kind) {
    case HON_TYPE_ENUM:
        hb_str(b, "(");
        for (uint32_t i = 0; i < t->u.enumeration.value_count; i++) {
            if (i > 0) hb_str(b, ", ");
            hb_append(b, t->u.enumeration.values[i].data,
                       t->u.enumeration.values[i].len);
        }
        hb_str(b, ")");
        break;

    case HON_TYPE_RECORD:
        hb_str(b, "record ");
        for (uint32_t i = 0; i < t->u.record.field_count; i++) {
            if (i > 0) hb_str(b, "; ");
            hb_append(b, t->u.record.fields[i].name.data,
                       t->u.record.fields[i].name.len);
            hb_str(b, " : ");
            hb_append(b, t->u.record.fields[i].type->name.data,
                       t->u.record.fields[i].type->name.len);
        }
        if (t->u.record.has_variant) {
            if (t->u.record.field_count > 0) hb_str(b, "; ");
            hb_str(b, "case ");
            hb_append(b, t->u.record.tag_name.data, t->u.record.tag_name.len);
            hb_str(b, " : ");
            hb_append(b, t->u.record.tag_type->name.data,
                       t->u.record.tag_type->name.len);
            hb_str(b, " of ");
            for (uint32_t i = 0; i < t->u.record.arm_count; i++) {
                if (i > 0) hb_str(b, " ");
                hb_append(b, t->u.record.arms[i].label.data,
                           t->u.record.arms[i].label.len);
                hb_str(b, " : (");
                for (uint32_t j = 0; j < t->u.record.arms[i].field_count; j++) {
                    if (j > 0) hb_str(b, "; ");
                    hb_append(b, t->u.record.arms[i].fields[j].name.data,
                               t->u.record.arms[i].fields[j].name.len);
                    hb_str(b, " : ");
                    hb_append(b, t->u.record.arms[i].fields[j].type->name.data,
                               t->u.record.arms[i].fields[j].type->name.len);
                }
                hb_str(b, ");");
            }
        }
        hb_str(b, " end");
        break;

    case HON_TYPE_SEQUENCE:
        hb_str(b, "sequence of ");
        hb_append(b, t->u.sequence.element_type->name.data,
                   t->u.sequence.element_type->name.len);
        break;

    case HON_TYPE_ALIAS:
        hb_append(b, t->u.alias.target->name.data,
                   t->u.alias.target->name.len);
        break;

    case HON_TYPE_PRIMITIVE:
        hb_append(b, t->name.data, t->name.len);
        break;
    }
}

/* ---- Registry Hash (SHA-256 of canonical type definitions) ---- */

typedef struct { hon_str name; const hon_type *type; } hash_entry;

static int cmp_hash_entries(const void *a, const void *b) {
    const hash_entry *ea = a, *eb = b;
    size_t min_len = ea->name.len < eb->name.len ? ea->name.len : eb->name.len;
    int c = memcmp(ea->name.data, eb->name.data, min_len);
    if (c != 0) return c;
    return (ea->name.len > eb->name.len) - (ea->name.len < eb->name.len);
}

int hon_registry_hash(const hon_registry *reg, char out[65]) {
    if (!reg || reg->count == 0 || !out) return -1;

    /* Collect entries */
    size_t entries_size = 0;
    if (!array_size(reg->count, sizeof(hash_entry), &entries_size)) return -1;
    hash_entry *entries = malloc(entries_size);
    if (!entries) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < reg->cap; i++) {
        if (reg->entries[i].occupied) {
            entries[n].name = reg->entries[i].name;
            entries[n].type = reg->entries[i].type;
            n++;
        }
    }

    /* Sort alphabetically by name */
    qsort(entries, n, sizeof(hash_entry), cmp_hash_entries);

    /* Serialise each type in canonical form and hash */
    sha256_ctx ctx;
    sha256_init(&ctx);

    hashbuf buf;
    if (hb_init(&buf) < 0) {
        free(entries);
        return -1;
    }

    for (uint32_t i = 0; i < n; i++) {
        buf.len = 0; /* reset buffer for each type */
        serialise_type_canonical(&buf, entries[i].type);
        if (buf.failed) {
            hb_free(&buf);
            free(entries);
            return -1;
        }
        sha256_update(&ctx, buf.data, buf.len);
        sha256_update(&ctx, "\n", 1); /* separator */
    }

    hb_free(&buf);
    free(entries);

    uint8_t digest[32];
    sha256_final(&ctx, digest);

    /* Hex-encode into caller-provided buffer */
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[64] = '\0';
    return 0;
}
