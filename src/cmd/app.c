/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/app.h"

tw_status tw_app_init(tw_app *app)
{
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
