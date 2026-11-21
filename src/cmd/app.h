/*
 * Application state shared by the command layer and the REPL.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_APP_H
#define TWINLINE_CMD_APP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "twinline/analysis.h"
#include "twinline/job.h"
#include "twinline/session.h"
#include "twinline/status.h"

#define TW_PROMPT "twinline> "

struct tw_app;

/*
 * Delivers text produced outside a command (for example the result of a
 * background analysis), possibly from a worker thread. The plain REPL prints
 * it and redraws the prompt; the terminal UI shows it above the input box.
 */
typedef void (*tw_emit_fn)(struct tw_app *app, const char *text, bool is_error);

typedef struct tw_app {
    FILE *out; /* where commands write results (stdout by default) */
    FILE *err; /* where commands write errors (stderr by default) */
    tw_emit_fn emit;
    void *ui; /* owned by the active front end, may be NULL */
    tw_session session;
    tw_analysis_job job;
    tw_analysis_options options;
    bool quit_armed;      /* "quit" was typed once with unsaved changes */
    atomic_bool quitting; /* suppresses the prompt redraw from the worker */
} tw_app;

tw_status tw_app_init(tw_app *app);
void tw_app_destroy(tw_app *app);

/* tw_emit_fn for the plain REPL: prints the text, then the prompt again. */
void tw_app_emit_plain(tw_app *app, const char *text, bool is_error);

#endif /* TWINLINE_CMD_APP_H */
