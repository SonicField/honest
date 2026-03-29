/*
 * Integration tests for CLI tools.
 *
 * Tests honest-extract, honest-build, and honest-get enhancements
 * by invoking the actual binaries and checking their output.
 * These are end-to-end tests — they verify the tools from the
 * user's perspective (stdin/stdout/exit code).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

/*
 * Run a command, feed it stdin_data, capture stdout, return exit code.
 * Caller must free *out_buf.
 */
static int run_cmd(const char *cmd, const char *stdin_data,
                   char **out_buf, size_t *out_len) {
    /* Use a temp file for output instead of pipes to avoid blocking */
    char tmpout[] = "/tmp/honest-test-XXXXXX";
    int tmpfd = mkstemp(tmpout);
    if (tmpfd < 0) return -1;
    close(tmpfd);

    char tmpin[] = "/tmp/honest-test-in-XXXXXX";
    if (stdin_data) {
        int infd = mkstemp(tmpin);
        if (infd < 0) { unlink(tmpout); return -1; }
        write(infd, stdin_data, strlen(stdin_data));
        close(infd);
    }

    char full_cmd[4096];
    if (stdin_data) {
        snprintf(full_cmd, sizeof(full_cmd), "%s < %s > %s 2>/dev/null",
                 cmd, tmpin, tmpout);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>/dev/null",
                 cmd, tmpout);
    }

    int status = system(full_cmd);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (stdin_data) unlink(tmpin);

    /* Read output */
    FILE *f = fopen(tmpout, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        *out_buf = malloc((size_t)flen + 1);
        *out_len = fread(*out_buf, 1, (size_t)flen, f);
        (*out_buf)[*out_len] = '\0';
        fclose(f);
    } else {
        *out_buf = strdup("");
        *out_len = 0;
    }
    unlink(tmpout);

    return exit_code;
}

/* Strip trailing newline from output for comparison. */
static void strip_trailing_newline(char *s, size_t *len) {
    while (*len > 0 && (s[*len - 1] == '\n' || s[*len - 1] == '\r')) {
        s[--(*len)] = '\0';
    }
}

/* ================================================================
 * honest-extract tests
 * ================================================================ */

static void test_extract_from_code_fence(void) {
    const char *input =
        "Here is my evaluation:\n"
        "\n"
        "```pascal\n"
        "type\n"
        "  V = (Pass, Fail);\n"
        "var result : V = Pass;\n"
        "```\n"
        "\n"
        "The test passes because...\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-extract -", input, &out, &out_len);
    ASSERT(rc == 0, "extract code fence: exit 0");
    ASSERT(out_len > 0, "extract code fence: produces output");
    ASSERT(strstr(out, "Pass") != NULL, "extract code fence: contains 'Pass'");

    free(out);
}

static void test_extract_from_bare_text(void) {
    const char *input =
        "Some preamble text here.\n"
        "\n"
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "var c : Color = Green;\n"
        "\n"
        "And some trailing text.\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-extract -", input, &out, &out_len);
    ASSERT(rc == 0, "extract bare text: exit 0");
    ASSERT(strstr(out, "Green") != NULL, "extract bare text: contains 'Green'");

    free(out);
}

static void test_extract_no_document(void) {
    const char *input =
        "This is just plain text.\n"
        "No honest document here.\n"
        "The type of data is irrelevant.\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-extract -", input, &out, &out_len);
    ASSERT(rc == 1, "extract no doc: exit 1");

    free(out);
}

static void test_extract_wire_mode_output(void) {
    const char *input =
        "```\n"
        "type\n"
        "  { This is a comment }\n"
        "  N = Integer;\n"
        "var x : N = 42;\n"
        "```\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-extract -", input, &out, &out_len);
    ASSERT(rc == 0, "extract wire mode: exit 0");
    /* Wire mode should strip comments */
    ASSERT(strstr(out, "This is a comment") == NULL,
           "extract wire mode: comments stripped");
    ASSERT(strstr(out, "42") != NULL, "extract wire mode: value present");

    free(out);
}

static void test_extract_complex_document(void) {
    const char *input =
        "I analyzed the results carefully.\n"
        "\n"
        "```pascal\n"
        "type\n"
        "  Verdict = (Pass, Fail, Unknown);\n"
        "  EvalResult = record\n"
        "    verdict   : Verdict;\n"
        "    reasoning : String;\n"
        "  end;\n"
        "var result : EvalResult = (\n"
        "  verdict   : Pass;\n"
        "  reasoning : 'All criteria met'\n"
        ");\n"
        "```\n"
        "\n"
        "In conclusion, the test passes.\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-extract -", input, &out, &out_len);
    ASSERT(rc == 0, "extract complex: exit 0");
    ASSERT(strstr(out, "Pass") != NULL, "extract complex: contains verdict");
    ASSERT(strstr(out, "All criteria met") != NULL,
           "extract complex: contains reasoning");

    free(out);
}

/* ================================================================
 * honest-get enhanced tests
 * ================================================================ */

static void test_get_stdin(void) {
    const char *input =
        "type\n"
        "  R = record\n"
        "    name : String;\n"
        "    age  : LongInt;\n"
        "  end;\n"
        "var p : R = (name : 'Bob'; age : 25);\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - p name", input, &out, &out_len);
    ASSERT(rc == 0, "get stdin: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "Bob") == 0, "get stdin: correct unquoted value 'Bob'");

    free(out);
}

static void test_get_raw_string(void) {
    /* Verify strings are output without quotes */
    const char *input =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'hello world');\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r s", input, &out, &out_len);
    ASSERT(rc == 0, "get raw string: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "hello world") == 0,
           "get raw string: unquoted output");
    /* Must not contain single quotes */
    ASSERT(strchr(out, '\'') == NULL, "get raw string: no quotes in output");

    free(out);
}

static void test_get_raw_enum(void) {
    const char *input =
        "type\n"
        "  V = (Pass, Fail);\n"
        "var r : V = Fail;\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r", input, &out, &out_len);
    /* honest-get needs at least one field arg; for top-level enum,
     * we need to test differently — checking current CLI contract */
    free(out);

    /* Use a record wrapper since honest-get requires field args */
    const char *input2 =
        "type\n"
        "  V = (Pass, Fail);\n"
        "  R = record\n"
        "    verdict : V;\n"
        "  end;\n"
        "var r : R = (verdict : Fail);\n";

    out = NULL;
    out_len = 0;
    rc = run_cmd("build/honest-get - r verdict", input2, &out, &out_len);
    ASSERT(rc == 0, "get raw enum: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "Fail") == 0, "get raw enum: unquoted enum value");

    free(out);
}

static void test_get_bool_lowercase(void) {
    const char *input =
        "type\n"
        "  R = record\n"
        "    active : Boolean;\n"
        "  end;\n"
        "var r : R = (active : True);\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r active", input, &out, &out_len);
    ASSERT(rc == 0, "get bool lowercase: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "true") == 0,
           "get bool lowercase: outputs 'true' not 'True'");

    free(out);
}

static void test_get_bool_false_lowercase(void) {
    const char *input =
        "type\n"
        "  R = record\n"
        "    flag : Boolean;\n"
        "  end;\n"
        "var r : R = (flag : False);\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r flag", input, &out, &out_len);
    ASSERT(rc == 0, "get bool false: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "false") == 0,
           "get bool false: outputs 'false' not 'False'");

    free(out);
}

static void test_get_multiple_fields(void) {
    const char *input =
        "type\n"
        "  V = (Pass, Fail);\n"
        "  R = record\n"
        "    verdict : V;\n"
        "    reason  : String;\n"
        "    score   : Integer;\n"
        "  end;\n"
        "var r : R = (verdict : Pass; reason : 'all good'; score : 100);\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r verdict reason score",
                     input, &out, &out_len);
    ASSERT(rc == 0, "get multi field: exit 0");

    /* Output should be three lines: Pass\nall good\n100\n */
    char *line1 = strtok(out, "\n");
    char *line2 = strtok(NULL, "\n");
    char *line3 = strtok(NULL, "\n");

    ASSERT(line1 && strcmp(line1, "Pass") == 0,
           "get multi field: first line is 'Pass'");
    ASSERT(line2 && strcmp(line2, "all good") == 0,
           "get multi field: second line is 'all good'");
    ASSERT(line3 && strcmp(line3, "100") == 0,
           "get multi field: third line is '100'");

    free(out);
}

static void test_get_missing_field_exit_code(void) {
    const char *input =
        "type\n"
        "  R = record\n"
        "    name : String;\n"
        "  end;\n"
        "var r : R = (name : 'Alice');\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r nonexistent", input, &out, &out_len);
    ASSERT(rc == 1, "get missing field: exit 1");

    free(out);
}

static void test_get_special_chars_in_string(void) {
    const char *input =
        "type\n"
        "  R = record\n"
        "    s : String;\n"
        "  end;\n"
        "var r : R = (s : 'it''s a test');\n";

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd("build/honest-get - r s", input, &out, &out_len);
    ASSERT(rc == 0, "get special chars: exit 0");
    strip_trailing_newline(out, &out_len);
    ASSERT(strcmp(out, "it's a test") == 0,
           "get special chars: embedded quote unescaped in output");

    free(out);
}

/* ================================================================
 * honest-build tests
 * ================================================================ */

static void test_build_simple_record(void) {
    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type R = record name : String; age : LongInt; end;' "
        "--type R "
        "name=Alice age=30",
        NULL, &out, &out_len);
    ASSERT(rc == 0, "build simple: exit 0");
    ASSERT(out_len > 0, "build simple: produces output");
    ASSERT(strstr(out, "Alice") != NULL, "build simple: contains name value");
    ASSERT(strstr(out, "30") != NULL, "build simple: contains age value");

    free(out);
}

static void test_build_output_parses(void) {
    /* Build a document, then parse it back with honest-parse */
    char *build_out = NULL;
    size_t build_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type R = record s : String; n : Integer; end;' "
        "--type R "
        "s=hello n=42",
        NULL, &build_out, &build_len);
    ASSERT(rc == 0, "build-then-parse: build exit 0");

    /* Feed the output to honest-parse */
    char *parse_out = NULL;
    size_t parse_len = 0;
    rc = run_cmd("build/honest-parse -", build_out, &parse_out, &parse_len);
    ASSERT(rc == 0, "build-then-parse: parse exit 0 (valid document)");

    free(build_out);
    free(parse_out);
}

static void test_build_with_single_quotes(void) {
    /* Value containing single quotes — must be properly escaped.
     * Use a temp file for the typedef to avoid shell quoting issues. */
    char tmptype[] = "/tmp/honest-test-type-XXXXXX";
    int tfd = mkstemp(tmptype);
    const char *typedef_content =
        "type\n  R = record\n    s : String;\n  end;\nvar _d : Boolean = True;\n";
    write(tfd, typedef_content, strlen(typedef_content));
    close(tfd);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "build/honest-build --rulebook %s --type R \"s=it'\\''s a test\"", tmptype);

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(cmd, NULL, &out, &out_len);
    ASSERT(rc == 0, "build quotes: exit 0");
    /* The output should contain the value with escaped quotes */
    ASSERT(out_len > 0, "build quotes: produces output");

    unlink(tmptype);
    free(out);
}

static void test_build_enum_validation(void) {
    /* Invalid enum value should produce an error */
    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type V = (Pass, Fail); type R = record v : V; end;' "
        "--type R "
        "v=Invalid",
        NULL, &out, &out_len);
    ASSERT(rc == 1, "build enum validation: exit 1 for invalid enum value");

    free(out);
}

static void test_build_missing_field(void) {
    /* Missing required field should produce an error */
    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type R = record a : String; b : String; end;' "
        "--type R "
        "a=hello",
        NULL, &out, &out_len);
    ASSERT(rc == 1, "build missing field: exit 1 for missing field 'b'");

    free(out);
}

static void test_build_boolean(void) {
    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type R = record flag : Boolean; end;' "
        "--type R "
        "flag=True",
        NULL, &out, &out_len);
    ASSERT(rc == 0, "build boolean: exit 0");
    ASSERT(strstr(out, "True") != NULL, "build boolean: contains True");

    free(out);
}

static void test_build_self_describing(void) {
    /* Output must contain type declarations (self-describing per R1.4).
     * Use a temp file for the typedef to avoid shell quoting issues. */
    char tmptype[] = "/tmp/honest-test-type-XXXXXX";
    int tfd = mkstemp(tmptype);
    const char *typedef_content =
        "type\n"
        "  Color = (Red, Green, Blue);\n"
        "  R = record\n"
        "    c : Color;\n"
        "  end;\n"
        "var _d : Boolean = True;\n";
    write(tfd, typedef_content, strlen(typedef_content));
    close(tfd);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "build/honest-build --rulebook %s --type R c=Blue", tmptype);

    char *out = NULL;
    size_t out_len = 0;
    int rc = run_cmd(cmd, NULL, &out, &out_len);
    ASSERT(rc == 0, "build self-describing: exit 0");
    /* Output must contain 'type' keyword — declarations are present */
    ASSERT(strstr(out, "type") != NULL,
           "build self-describing: output contains type declarations");
    ASSERT(strstr(out, "Color") != NULL,
           "build self-describing: output contains type name");

    unlink(tmptype);
    free(out);
}

/* ================================================================
 * Round-trip: honest-build | honest-get
 * ================================================================ */

static void test_build_then_get(void) {
    /* Build a document, extract a field — end-to-end pipeline */
    char *build_out = NULL;
    size_t build_len = 0;
    int rc = run_cmd(
        "build/honest-build "
        "--typedef 'type R = record name : String; score : Integer; end;' "
        "--type R "
        "name=Alice score=99",
        NULL, &build_out, &build_len);
    ASSERT(rc == 0, "build-then-get: build exit 0");

    /* Extract name via honest-get */
    char *get_out = NULL;
    size_t get_len = 0;
    rc = run_cmd("build/honest-get - result name", build_out, &get_out, &get_len);
    ASSERT(rc == 0, "build-then-get: get exit 0");
    strip_trailing_newline(get_out, &get_len);
    ASSERT(strcmp(get_out, "Alice") == 0,
           "build-then-get: round-trip value correct");

    free(build_out);
    free(get_out);
}

/* ================================================================
 * Round-trip: honest-extract | honest-get
 * ================================================================ */

static void test_extract_then_get(void) {
    const char *mixed_text =
        "Analysis complete.\n"
        "\n"
        "```pascal\n"
        "type\n"
        "  V = (Pass, Fail);\n"
        "  R = record\n"
        "    verdict : V;\n"
        "    note    : String;\n"
        "  end;\n"
        "var result : R = (verdict : Pass; note : 'looks good');\n"
        "```\n"
        "\n"
        "Done.\n";

    /* Extract the document */
    char *extract_out = NULL;
    size_t extract_len = 0;
    int rc = run_cmd("build/honest-extract -", mixed_text,
                     &extract_out, &extract_len);
    ASSERT(rc == 0, "extract-then-get: extract exit 0");

    /* Get the verdict field */
    char *get_out = NULL;
    size_t get_len = 0;
    rc = run_cmd("build/honest-get - result verdict",
                 extract_out, &get_out, &get_len);
    ASSERT(rc == 0, "extract-then-get: get exit 0");
    strip_trailing_newline(get_out, &get_len);
    ASSERT(strcmp(get_out, "Pass") == 0,
           "extract-then-get: verdict is 'Pass'");

    free(extract_out);
    free(get_out);
}

int main(void) {
    /* honest-extract */
    test_extract_from_code_fence();
    test_extract_from_bare_text();
    test_extract_no_document();
    test_extract_wire_mode_output();
    test_extract_complex_document();

    /* honest-get enhancements */
    test_get_stdin();
    test_get_raw_string();
    test_get_raw_enum();
    test_get_bool_lowercase();
    test_get_bool_false_lowercase();
    test_get_multiple_fields();
    test_get_missing_field_exit_code();
    test_get_special_chars_in_string();

    /* honest-build */
    test_build_simple_record();
    test_build_output_parses();
    test_build_with_single_quotes();
    test_build_enum_validation();
    test_build_missing_field();
    test_build_boolean();
    test_build_self_describing();

    /* Round-trip pipelines */
    test_build_then_get();
    test_extract_then_get();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
