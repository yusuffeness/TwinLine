/*
 * Application state shared by the command layer and the REPL.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_APP_H
#define TWINLINE_CMD_APP_H

#include <stdatomic.h>
#include <stdbool.h>

#include "twinline/analysis.h"
#include "twinline/job.h"
#include "twinline/session.h"
#include "twinline/status.h"

#define TW_PROMPT "twinline> "

typedef struct tw_app {
    tw_session session;
    tw_analysis_job job;
    tw_analysis_options options;
    bool quit_armed;      /* "quit" was typed once with unsaved changes */
    atomic_bool quitting; /* suppresses the prompt redraw from the worker */
} tw_app;

tw_status tw_app_init(tw_app *app);
void tw_app_destroy(tw_app *app);

#endif /* TWINLINE_CMD_APP_H */
