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

static hon_type *deep_copy_type(hon_arena *a, const hon_type *src);

static hon_field *deep_copy_fields(hon_arena *a, const hon_field *src, uint32_t count) {
    if (count == 0 || !src) return NULL;
    hon_field *dst = hon_arena_alloc(a, sizeof(hon_field) * count, _Alignof(hon_field));
    for (uint32_t i = 0; i < count; i++) {
        dst[i].name = hon_str_dup(a, src[i].name);
        dst[i].type = src[i].type ? deep_copy_type(a, src[i].type) : NULL;
    }
    return dst;
}

static hon_type *deep_copy_type(hon_arena *a, const hon_type *src) {
    if (!src) return NULL;

    /* Don't copy primitives — they're static singletons */
    if (src->kind == HON_TYPE_PRIMITIVE) return (hon_type *)src;

    hon_type *dst = hon_arena_alloc(a, sizeof(hon_type), _Alignof(hon_type));
    dst->kind = src->kind;
    dst->name = hon_str_dup(a, src->name);

    switch (src->kind) {
    case HON_TYPE_ENUM:
        dst->u.enumeration.value_count = src->u.enumeration.value_count;
        dst->u.enumeration.values = hon_arena_alloc(a,
            sizeof(hon_str) * src->u.enumeration.value_count, _Alignof(hon_str));
        for (uint32_t i = 0; i < src->u.enumeration.value_count; i++) {
            dst->u.enumeration.values[i] = hon_str_dup(a, src->u.enumeration.values[i]);
        }
        break;

    case HON_TYPE_RECORD:
        dst->u.record.field_count = src->u.record.field_count;
        dst->u.record.fields = deep_copy_fields(a, src->u.record.fields,
                                                 src->u.record.field_count);
        dst->u.record.has_variant = src->u.record.has_variant;
        if (src->u.record.has_variant) {
            dst->u.record.tag_name = hon_str_dup(a, src->u.record.tag_name);
            dst->u.record.tag_type = src->u.record.tag_type
                ? deep_copy_type(a, src->u.record.tag_type) : NULL;
            dst->u.record.arm_count = src->u.record.arm_count;
            dst->u.record.arms = hon_arena_alloc(a,
                sizeof(hon_variant) * src->u.record.arm_count,
                _Alignof(hon_variant));
            for (uint32_t i = 0; i < src->u.record.arm_count; i++) {
                dst->u.record.arms[i].label = hon_str_dup(a, src->u.record.arms[i].label);
                dst->u.record.arms[i].field_count = src->u.record.arms[i].field_count;
                dst->u.record.arms[i].fields = deep_copy_fields(a,
                    src->u.record.arms[i].fields,
                    src->u.record.arms[i].field_count);
            }
        }
        break;

    case HON_TYPE_SEQUENCE:
        dst->u.sequence.element_type = src->u.sequence.element_type
            ? deep_copy_type(a, src->u.sequence.element_type) : NULL;
        break;

    case HON_TYPE_ALIAS:
        dst->u.alias.target = src->u.alias.target
            ? deep_copy_type(a, src->u.alias.target) : NULL;
        break;

    case HON_TYPE_PRIMITIVE:
        break; /* handled above */
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

    /* Deep-copy type into registry's arena — registry owns its types */
    hon_type *owned = deep_copy_type(reg->arena, type);

    uint32_t idx = hash_str(owned->name) % reg->cap;
    while (reg->entries[idx].occupied) {
        if (hon_str_eq(reg->entries[idx].name, owned->name)) {
            /* Update existing entry */
            reg->entries[idx].type = owned;
            return;
        }
        idx = (idx + 1) % reg->cap;
    }

    reg->entries[idx].name     = owned->name;
    reg->entries[idx].type     = owned;
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

/* ---- Registry Hash (SHA-256) ---- */

char *hon_registry_hash(const hon_registry *reg) {
    if (!reg) return NULL;

    /* Collect type names */
    hon_str *names = malloc(sizeof(hon_str) * reg->count);
    if (!names) return NULL;
    uint32_t n = 0;
    for (uint32_t i = 0; i < reg->cap; i++) {
        if (reg->entries[i].occupied) {
            names[n++] = reg->entries[i].name;
        }
    }

    /* Sort alphabetically (insertion sort, small N) */
    for (uint32_t i = 1; i < n; i++) {
        hon_str key = names[i];
        int j = (int)i - 1;
        while (j >= 0) {
            size_t cmp_len = names[j].len < key.len ? names[j].len : key.len;
            int cmp = memcmp(names[j].data, key.data, cmp_len);
            if (cmp < 0 || (cmp == 0 && names[j].len <= key.len)) break;
            names[j + 1] = names[j];
            j--;
        }
        names[j + 1] = key;
    }

    /* SHA-256 over sorted type names, each followed by a null separator */
    sha256_ctx ctx;
    sha256_init(&ctx);
    for (uint32_t i = 0; i < n; i++) {
        sha256_update(&ctx, names[i].data, names[i].len);
        sha256_update(&ctx, "\0", 1);
    }

    uint8_t digest[32];
    sha256_final(&ctx, digest);
    free(names);

    /* Hex-encode */
    char *result = malloc(65);
    if (result) {
        for (int i = 0; i < 32; i++) {
            snprintf(result + i * 2, 3, "%02x", digest[i]);
        }
    }
    return result;
}
