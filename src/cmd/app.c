/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/app.h"

tw_status tw_app_init(tw_app *app)
{
    app->out = stdout;
    app->err = stderr;
    app->emit = tw_app_emit_plain;
    app->ui = NULL;
    tw_status status = tw_session_init(&app->session);
    if (status != TW_OK)
        return status;
    tw_job_init(&app->job);
    tw_analysis_options_default(&app->options);
    app->quit_armed = false;
    atomic_init(&app->quitting, false);
    return TW_OK;
}

void tw_app_destroy(tw_app *app)
{
    atomic_store(&app->quitting, true);
    tw_job_destroy(&app->job);
    tw_session_destroy(&app->session);
}

void tw_app_emit_plain(tw_app *app, const char *text, bool is_error)
{
    (void)is_error;
    flockfile(stdout);
    fputc('\n', stdout);
    fputs(text, stdout);
    if (!atomic_load(&app->quitting))
        fputs(TW_PROMPT, stdout);
    fflush(stdout);
    funlockfile(stdout);
}
