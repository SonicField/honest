#ifndef HONEST_CLI_COMMON_H
#define HONEST_CLI_COMMON_H

#include "honest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

char *hon_cli_read_all(FILE *input, size_t *out_len);
bool hon_cli_has_errors(const hon_diag_list *diags);
void hon_cli_print_diags(const hon_diag_list *diags);
int hon_cli_write_stdout(const char *data, size_t len);
int hon_cli_finish_stdout(void);

#endif
