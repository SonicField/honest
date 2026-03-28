/*
 * Lexer tests for libhonest.
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

#define ASSERT_TOK(lex, expected_kind) do { \
    hon_token t = hon_lexer_next(lex); \
    tests_run++; \
    if (t.kind != (expected_kind)) { \
        fprintf(stderr, "FAIL [%s:%d]: expected %s, got %s ('%.*s')\n", \
                __FILE__, __LINE__, \
                hon_tok_kind_name(expected_kind), \
                hon_tok_kind_name(t.kind), \
                (int)t.text.len, t.text.data); \
    } else { \
        tests_passed++; \
    } \
} while(0)

static void test_empty(void) {
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, "", 0);
    ASSERT_TOK(lex, HON_TOK_EOF);
    hon_arena_free(a);
}

static void test_keywords(void) {
    const char *src = "type record end case of sequence var True False";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_TYPE);
    ASSERT_TOK(lex, HON_TOK_RECORD);
    ASSERT_TOK(lex, HON_TOK_END);
    ASSERT_TOK(lex, HON_TOK_CASE);
    ASSERT_TOK(lex, HON_TOK_OF);
    ASSERT_TOK(lex, HON_TOK_SEQUENCE);
    ASSERT_TOK(lex, HON_TOK_VAR);
    ASSERT_TOK(lex, HON_TOK_TRUE);
    ASSERT_TOK(lex, HON_TOK_FALSE);
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_identifiers(void) {
    const char *src = "Confidence my_field Agent42";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    hon_token t1 = hon_lexer_next(lex);
    ASSERT(t1.kind == HON_TOK_IDENT, "Confidence is ident");
    ASSERT(t1.text.len == 10 && memcmp(t1.text.data, "Confidence", 10) == 0,
           "Confidence text");

    hon_token t2 = hon_lexer_next(lex);
    ASSERT(t2.kind == HON_TOK_IDENT, "my_field is ident");

    hon_token t3 = hon_lexer_next(lex);
    ASSERT(t3.kind == HON_TOK_IDENT, "Agent42 is ident");

    hon_arena_free(a);
}

static void test_punctuation(void) {
    const char *src = "():;=,.";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_LPAREN);
    ASSERT_TOK(lex, HON_TOK_RPAREN);
    ASSERT_TOK(lex, HON_TOK_COLON);
    ASSERT_TOK(lex, HON_TOK_SEMICOLON);
    ASSERT_TOK(lex, HON_TOK_EQUALS);
    ASSERT_TOK(lex, HON_TOK_COMMA);
    ASSERT_TOK(lex, HON_TOK_DOT);
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_numbers(void) {
    const char *src = "42 -7 3.14 -0.5 1.0e10 2.5E-3";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_INT);   /* 42 */
    ASSERT_TOK(lex, HON_TOK_INT);   /* -7 */
    ASSERT_TOK(lex, HON_TOK_REAL);  /* 3.14 */
    ASSERT_TOK(lex, HON_TOK_REAL);  /* -0.5 */
    ASSERT_TOK(lex, HON_TOK_REAL);  /* 1.0e10 */
    ASSERT_TOK(lex, HON_TOK_REAL);  /* 2.5E-3 */
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_strings(void) {
    const char *src = "'hello' 'it''s' 'line\\none'";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    hon_token t1 = hon_lexer_next(lex);
    ASSERT(t1.kind == HON_TOK_STRING, "hello is string");

    hon_token t2 = hon_lexer_next(lex);
    ASSERT(t2.kind == HON_TOK_STRING, "it''s is string");

    hon_token t3 = hon_lexer_next(lex);
    ASSERT(t3.kind == HON_TOK_STRING, "escaped string");

    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_comments(void) {
    const char *src = "{ this is a comment } type (* another comment *) record";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_COMMENT);
    ASSERT_TOK(lex, HON_TOK_TYPE);
    ASSERT_TOK(lex, HON_TOK_COMMENT);
    ASSERT_TOK(lex, HON_TOK_RECORD);
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_peek(void) {
    const char *src = "type record";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    hon_token p = hon_lexer_peek(lex);
    ASSERT(p.kind == HON_TOK_TYPE, "peek returns type");

    hon_token n = hon_lexer_next(lex);
    ASSERT(n.kind == HON_TOK_TYPE, "next returns same type");

    ASSERT_TOK(lex, HON_TOK_RECORD);
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_case_sensitive_keywords(void) {
    /* Keywords are case-sensitive: Type is an ident, not a keyword */
    const char *src = "Type Record END";
    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_IDENT); /* Type */
    ASSERT_TOK(lex, HON_TOK_IDENT); /* Record */
    ASSERT_TOK(lex, HON_TOK_IDENT); /* END */
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

static void test_full_document_tokens(void) {
    const char *src =
        "type\n"
        "  Confidence = (Certain, High, Moderate);\n"
        "\n"
        "var x : Confidence = High;\n";

    hon_arena *a = hon_arena_new(0);
    hon_lexer *lex = hon_lexer_new(a, src, strlen(src));

    ASSERT_TOK(lex, HON_TOK_TYPE);       /* type */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* Confidence */
    ASSERT_TOK(lex, HON_TOK_EQUALS);     /* = */
    ASSERT_TOK(lex, HON_TOK_LPAREN);     /* ( */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* Certain */
    ASSERT_TOK(lex, HON_TOK_COMMA);      /* , */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* High */
    ASSERT_TOK(lex, HON_TOK_COMMA);      /* , */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* Moderate */
    ASSERT_TOK(lex, HON_TOK_RPAREN);     /* ) */
    ASSERT_TOK(lex, HON_TOK_SEMICOLON);  /* ; */
    ASSERT_TOK(lex, HON_TOK_VAR);        /* var */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* x */
    ASSERT_TOK(lex, HON_TOK_COLON);      /* : */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* Confidence */
    ASSERT_TOK(lex, HON_TOK_EQUALS);     /* = */
    ASSERT_TOK(lex, HON_TOK_IDENT);      /* High */
    ASSERT_TOK(lex, HON_TOK_SEMICOLON);  /* ; */
    ASSERT_TOK(lex, HON_TOK_EOF);

    hon_arena_free(a);
}

int main(void) {
    test_empty();
    test_keywords();
    test_identifiers();
    test_punctuation();
    test_numbers();
    test_strings();
    test_comments();
    test_peek();
    test_case_sensitive_keywords();
    test_full_document_tokens();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
