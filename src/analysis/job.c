/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/job.h"

static void *job_main(void *arg)
{
    tw_analysis_job *job = arg;
    tw_analysis_result result;

    tw_status status =
        tw_analysis_run(&job->snapshot, &job->options, &job->cancel, &job->progress, &result);
    tw_document_free(&job->snapshot);

    if (job->on_done != NULL)
        job->on_done(status, &result, job->user);

    atomic_store_explicit(&job->running, false, memory_order_release);
    return NULL;
}

void tw_job_init(tw_analysis_job *job)
{
    job->joinable = false;
    atomic_init(&job->running, false);
    atomic_init(&job->cancel, false);
    tw_analysis_progress_init(&job->progress);
    tw_document_init(&job->snapshot);
    tw_analysis_options_default(&job->options);
    job->on_done = NULL;
    job->user = NULL;
}

void tw_job_destroy(tw_analysis_job *job)
{
    tw_job_cancel(job);
    tw_job_wait(job);
    tw_document_free(&job->snapshot);
}

bool tw_job_is_running(tw_analysis_job *job)
{
    return atomic_load_explicit(&job->running, memory_order_acquire);
}

bool tw_job_is_cancelling(tw_analysis_job *job)
{
    return tw_job_is_running(job) && atomic_load_explicit(&job->cancel, memory_order_relaxed);
}

void tw_job_cancel(tw_analysis_job *job)
{
    atomic_store_explicit(&job->cancel, true, memory_order_relaxed);
}

void tw_job_wait(tw_analysis_job *job)
{
    if (!job->joinable)
        return;
    pthread_join(job->thread, NULL);
    job->joinable = false;
}

tw_status tw_job_start(tw_analysis_job *job, tw_document *snapshot,
                       const tw_analysis_options *options, tw_job_done_fn on_done, void *user)
{
    if (tw_job_is_running(job)) {
        tw_document_free(snapshot);
        return TW_ERR_BUSY;
    }
    tw_job_wait(job); /* reclaim a previous, finished worker */

    job->snapshot = *snapshot;
    tw_document_init(snapshot);
    job->options = *options;
    job->on_done = on_done;
    job->user = user;
    atomic_store(&job->cancel, false);
    tw_analysis_progress_reset(&job->progress);
    atomic_store(&job->running, true);

    if (pthread_create(&job->thread, NULL, job_main, job) != 0) {
        atomic_store(&job->running, false);
        tw_document_free(&job->snapshot);
        return TW_ERR_SYSTEM;
    }
    job->joinable = true;
    return TW_OK;
}
