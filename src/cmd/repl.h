/*
 * Read-eval-print loop on standard input.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_REPL_H
#define TWINLINE_CMD_REPL_H

#include "cmd/app.h"

/* Runs until "quit" or end of input. */
void tw_repl_run(tw_app *app);

#endif /* TWINLINE_CMD_REPL_H */
