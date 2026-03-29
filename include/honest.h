/*
 * libhonest — Honest Interchange Format
 *
 * Public API for parsing, constructing, and serialising Honest documents.
 */

#ifndef HONEST_H
#define HONEST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Arena Allocator ---- */

typedef struct hon_arena hon_arena;

hon_arena *hon_arena_new(size_t initial_cap);
void      *hon_arena_alloc(hon_arena *a, size_t size, size_t align);
void       hon_arena_free(hon_arena *a);

/* ---- String View ---- */

typedef struct {
    const char *data;
    size_t      len;
} hon_str;

#define HON_STR_LIT(s) ((hon_str){(s), sizeof(s) - 1})

/* ---- Token Types ---- */

typedef enum {
    /* Structural */
    HON_TOK_LPAREN,     /* ( */
    HON_TOK_RPAREN,     /* ) */
    HON_TOK_COLON,      /* : */
    HON_TOK_SEMICOLON,  /* ; */
    HON_TOK_EQUALS,     /* = */
    HON_TOK_COMMA,      /* , */
    HON_TOK_DOT,        /* . */

    /* Keywords */
    HON_TOK_TYPE,       /* type */
    HON_TOK_RECORD,     /* record */
    HON_TOK_END,        /* end */
    HON_TOK_CASE,       /* case */
    HON_TOK_OF,         /* of */
    HON_TOK_SEQUENCE,   /* sequence */
    HON_TOK_VAR,        /* var */

    /* Literals */
    HON_TOK_IDENT,      /* identifier */
    HON_TOK_INT,        /* integer literal */
    HON_TOK_REAL,       /* real literal */
    HON_TOK_STRING,     /* string literal */
    HON_TOK_TRUE,       /* True */
    HON_TOK_FALSE,      /* False */

    /* Special */
    HON_TOK_COMMENT,    /* { ... } or (* ... *) */
    HON_TOK_EOF,
    HON_TOK_ERROR,
} hon_tok_kind;

typedef struct {
    hon_tok_kind kind;
    hon_str      text;      /* raw text of the token */
    uint32_t     line;
    uint32_t     col;
} hon_token;

/* ---- Lexer ---- */

typedef struct hon_lexer hon_lexer;

hon_lexer  *hon_lexer_new(hon_arena *a, const char *src, size_t len);
hon_token   hon_lexer_next(hon_lexer *lex);
hon_token   hon_lexer_peek(hon_lexer *lex);
const char *hon_tok_kind_name(hon_tok_kind kind);

/* ---- Type System ---- */

typedef enum {
    HON_PRIM_BOOLEAN,
    HON_PRIM_BYTE,
    HON_PRIM_WORD,
    HON_PRIM_INTEGER,
    HON_PRIM_LONGINT,
    HON_PRIM_REAL,
    HON_PRIM_CHAR,
    HON_PRIM_STRING,
    HON_PRIM_COUNT,     /* not a type — count of primitives */
} hon_primitive;

typedef enum {
    HON_TYPE_PRIMITIVE,
    HON_TYPE_ENUM,
    HON_TYPE_RECORD,
    HON_TYPE_SEQUENCE,
    HON_TYPE_ALIAS,
} hon_type_kind;

typedef struct hon_type    hon_type;
typedef struct hon_field   hon_field;
typedef struct hon_variant hon_variant;

struct hon_field {
    hon_str        name;
    hon_type      *type;
};

struct hon_variant {
    hon_str        label;       /* enum value name */
    hon_field     *fields;
    uint32_t       field_count;
};

struct hon_type {
    hon_type_kind  kind;
    hon_str        name;        /* declared type name */
    union {
        /* HON_TYPE_PRIMITIVE */
        hon_primitive prim;

        /* HON_TYPE_ENUM */
        struct {
            hon_str   *values;
            uint32_t   value_count;
        } enumeration;

        /* HON_TYPE_RECORD */
        struct {
            hon_field    *fields;
            uint32_t      field_count;
            /* variant part (optional) */
            bool          has_variant;
            hon_str       tag_name;
            hon_type     *tag_type;     /* must be enum or Boolean */
            hon_variant  *arms;
            uint32_t      arm_count;
        } record;

        /* HON_TYPE_SEQUENCE */
        struct {
            hon_type  *element_type;
        } sequence;

        /* HON_TYPE_ALIAS */
        struct {
            hon_type  *target;
        } alias;
    } u;
};

/* ---- Values ---- */

typedef enum {
    HON_VAL_BOOL,
    HON_VAL_INT,
    HON_VAL_REAL,
    HON_VAL_CHAR,
    HON_VAL_STRING,
    HON_VAL_ENUM,
    HON_VAL_RECORD,
    HON_VAL_SEQUENCE,
} hon_val_kind;

typedef struct hon_value hon_value;

typedef struct {
    hon_str     name;
    hon_value  *value;
} hon_field_val;

struct hon_value {
    hon_val_kind kind;
    hon_type    *type;
    union {
        bool        boolean;
        int64_t     integer;
        double      real;
        uint32_t    character;  /* Unicode code point */
        hon_str     string;     /* unescaped content */
        hon_str     enum_val;   /* enum value name */

        struct {
            hon_field_val  *fields;
            uint32_t        field_count;
            /* variant part */
            hon_str         tag_value;
            hon_field_val  *variant_fields;
            uint32_t        variant_field_count;
        } record;

        struct {
            hon_value  *elements;
            uint32_t    count;
        } sequence;
    } u;
};

/* ---- Variable (top-level data binding) ---- */

typedef struct {
    hon_str     name;
    hon_type   *type;
    hon_value  *value;
} hon_variable;

/* ---- Document ---- */

typedef struct {
    hon_type     **types;
    uint32_t       type_count;
    hon_variable  *vars;
    uint32_t       var_count;
    hon_arena     *arena;       /* owns all memory */
} hon_document;

/* ---- Diagnostics ---- */

typedef enum {
    HON_DIAG_ERROR,
    HON_DIAG_WARNING,
    HON_DIAG_INFO,
} hon_diag_level;

typedef struct {
    hon_diag_level  level;
    uint32_t        line;
    uint32_t        col;
    const char     *message;
} hon_diagnostic;

typedef struct {
    hon_diagnostic *items;
    uint32_t        count;
    uint32_t        cap;
    bool            owns_memory;  /* true when items are malloc'd (failed parse) */
} hon_diag_list;

void hon_diag_init(hon_diag_list *dl, hon_arena *a);
void hon_diag_push(hon_diag_list *dl, hon_arena *a,
                   hon_diag_level level, uint32_t line, uint32_t col,
                   const char *fmt, ...);
void hon_diag_free(hon_diag_list *dl);

/* ---- Forward Declarations ---- */

typedef struct hon_registry hon_registry;

/* ---- Parser ---- */

hon_document *hon_parse(const char *src, size_t len, hon_diag_list *diags);
hon_document *hon_parse_with_registry(const char *src, size_t len,
                                       const hon_registry *reg,
                                       hon_diag_list *diags);
hon_document *hon_parse_file(const char *path, hon_diag_list *diags);

/* ---- Serialiser ---- */

typedef enum {
    HON_MODE_DOCUMENT,  /* human-readable, preserves comments */
    HON_MODE_WIRE,      /* minimal whitespace, no comments */
} hon_serial_mode;

char *hon_serialise(const hon_document *doc, hon_serial_mode mode, size_t *out_len);
int   hon_serialise_to(const hon_document *doc, hon_serial_mode mode, FILE *out);

/* ---- Type Registry (Rulebook) ---- */

hon_registry *hon_registry_new(hon_arena *a);
int           hon_registry_load(hon_registry *reg, const char *path, hon_diag_list *diags);
hon_type     *hon_registry_lookup(const hon_registry *reg, hon_str name);
void          hon_registry_extend(hon_registry *reg, hon_type *type);
int           hon_registry_hash(const hon_registry *reg, char out[65]);

/* ---- Document Construction (Programmatic API) ---- */

hon_document *hon_doc_new(void);
hon_type     *hon_doc_add_enum(hon_document *doc, hon_str name,
                               const hon_str *values, uint32_t count);
hon_type     *hon_doc_add_record(hon_document *doc, hon_str name,
                                  const hon_field *fields, uint32_t count);
hon_type     *hon_doc_add_sequence(hon_document *doc, hon_str name,
                                    hon_type *elem_type);
hon_type     *hon_doc_add_alias(hon_document *doc, hon_str name,
                                 hon_type *target);
int           hon_doc_add_var(hon_document *doc, hon_str name,
                              hon_type *type, hon_value *val);

void          hon_doc_free(hon_document *doc);

/* ---- Field Accessors ---- */

int  hon_get_string(const hon_document *doc,
                    const char *var_name,
                    const char *field_name,
                    char *buf, size_t bufsz);

int  hon_get_long(const hon_document *doc,
                  const char *var_name,
                  const char *field_name,
                  long *out);

int  hon_get_bool(const hon_document *doc,
                  const char *var_name,
                  const char *field_name,
                  bool *out);

int  hon_get_real(const hon_document *doc,
                  const char *var_name,
                  const char *field_name,
                  double *out);

int  hon_get_char(const hon_document *doc,
                  const char *var_name,
                  const char *field_name,
                  uint32_t *out);

/* ---- Utility ---- */

bool          hon_str_eq(hon_str a, hon_str b);
hon_str       hon_str_dup(hon_arena *a, hon_str s);
const char   *hon_primitive_name(hon_primitive p);
hon_type     *hon_resolve_alias(hon_type *t);
bool          hon_types_equal(const hon_type *a, const hon_type *b);

#ifdef __cplusplus
}
#endif

#endif /* HONEST_H */
