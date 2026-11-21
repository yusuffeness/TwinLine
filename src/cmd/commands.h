/*
 * Command interpreter.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_COMMANDS_H
#define TWINLINE_CMD_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "cmd/app.h"

typedef enum tw_cmd_result {
    TW_CMD_CONTINUE = 0,
    TW_CMD_QUIT,
} tw_cmd_result;

/* A command line split into its parts. Pointers refer into the input line. */
typedef struct tw_parsed_command {
    char *name; /* command name without the trailing '!' */
    char *arg;  /* rest of the line, leading blanks removed; may be "" */
    bool force; /* the name ended with '!' */
} tw_parsed_command;

/*
 * Splits `line` in place into name, argument and force flag. Returns false
 * for a blank line. Never fails otherwise; unknown names are reported by
 * tw_command_execute().
 */
bool tw_command_parse(char *line, tw_parsed_command *out);

/* Parses and runs one command line. `line` is modified in place. */
tw_cmd_result tw_command_execute(tw_app *app, char *line);

/* Opens `path`; refuses if there are unsaved changes unless `force`. */
void tw_command_open(tw_app *app, const char *path, bool force);

/* Prints the command reference. */
void tw_command_print_help(FILE *out);

/* Read-only view of the command table, for menus and completion. */
typedef struct tw_command_info {
    const char *name;
    const char *usage;
    const char *summary;
    bool takes_file; /* the argument is a file name */
} tw_command_info;

size_t tw_command_count(void);
const tw_command_info *tw_command_at(size_t index);

#endif /* TWINLINE_CMD_COMMANDS_H */
