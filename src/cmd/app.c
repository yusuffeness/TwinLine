/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/app.h"

#include <stdlib.h>

tw_status tw_app_init(tw_app *app)
{
    app->out = stdout;
    app->err = stderr;
    app->emit = tw_app_emit_plain;
    app->on_result = NULL;
    app->prefill = NULL;
    app->marks = false;
    app->ui = NULL;
    app->buffers = malloc(sizeof *app->buffers);
    if (app->buffers == NULL)
        return TW_ERR_NOMEM;
    tw_buffer_init(&app->buffers[0]);
    app->buffer_count = 1;
    app->current = 0;
    tw_status status = tw_session_init(&app->session);
    if (status != TW_OK) {
        free(app->buffers);
        return status;
    }
    tw_job_init(&app->job);
    tw_analysis_options_default(&app->options);
    app->quit_armed = false;
    atomic_init(&app->sync_running, false);
    tw_analysis_progress_init(&app->sync_progress);
    atomic_init(&app->quitting, false);
    return TW_OK;
}

void tw_app_destroy(tw_app *app)
{
    atomic_store(&app->quitting, true);
    tw_job_destroy(&app->job);
    tw_session_destroy(&app->session);
    for (size_t i = 0; i < app->buffer_count; i++)
        tw_buffer_free(&app->buffers[i]);
    free(app->buffers);
    app->buffers = NULL;
    app->buffer_count = 0;
}

size_t tw_app_dirty_buffers(tw_app *app)
{
    pthread_mutex_lock(&app->session.lock);
    size_t dirty = app->session.dirty ? 1 : 0;
    for (size_t i = 0; i < app->buffer_count; i++) {
        if (i != app->current && app->buffers[i].dirty)
            dirty++;
    }
    pthread_mutex_unlock(&app->session.lock);
    return dirty;
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
