/*
 * Command interpreter.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_COMMANDS_H
#define TWINLINE_CMD_COMMANDS_H

#include <stdbool.h>

#include "cmd/app.h"

typedef enum tw_cmd_result {
    TW_CMD_CONTINUE = 0,
    TW_CMD_QUIT,
} tw_cmd_result;

/* Parses and runs one command line. `line` is modified in place. */
tw_cmd_result tw_command_execute(tw_app *app, char *line);

/* Opens `path`; refuses if there are unsaved changes unless `force`. */
void tw_command_open(tw_app *app, const char *path, bool force);

/* Prints the command reference. */
void tw_command_print_help(void);

#endif /* TWINLINE_CMD_COMMANDS_H */
