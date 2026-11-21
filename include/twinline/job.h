/*
 * Background analysis job.
 *
 * A job owns one worker thread at a time. The caller hands it a snapshot of
 * the document, so the worker never touches shared state and needs no locks.
 * Only one thread (the one that owns the job, usually the main thread) may
 * call the functions below.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_JOB_H
#define TWINLINE_JOB_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "twinline/analysis.h"
#include "twinline/document.h"
#include "twinline/status.h"

/* Called on the worker thread when the analysis ends. `result` is only
 * meaningful when `status` is TW_OK. */
typedef void (*tw_job_done_fn)(tw_status status, const tw_analysis_result *result, void *user);

typedef struct tw_analysis_job {
    pthread_t thread;
    bool joinable;       /* a thread was started and not joined yet */
    atomic_bool running; /* the worker is still working */
    atomic_bool cancel;  /* set to request cancellation */
    tw_analysis_progress progress;
    tw_document snapshot; /* owned by the worker while running */
    tw_analysis_options options;
    tw_job_done_fn on_done;
    void *user;
} tw_analysis_job;

void tw_job_init(tw_analysis_job *job);

/* Cancels and joins a running worker, then releases resources. */
void tw_job_destroy(tw_analysis_job *job);

/*
 * Starts the analysis of `snapshot` on a new thread. Ownership of the
 * snapshot's memory always moves to the job, even on failure, and
 * `*snapshot` is reset to empty. Returns TW_ERR_BUSY if a job is running.
 */
tw_status tw_job_start(tw_analysis_job *job, tw_document *snapshot,
                       const tw_analysis_options *options, tw_job_done_fn on_done, void *user);

bool tw_job_is_running(tw_analysis_job *job);

/* True while a cancelled worker is still winding down. */
bool tw_job_is_cancelling(tw_analysis_job *job);

/* Requests cancellation; returns immediately. */
void tw_job_cancel(tw_analysis_job *job);

/* Waits for the worker (if any) to finish and reclaims the thread. */
void tw_job_wait(tw_analysis_job *job);

#endif /* TWINLINE_JOB_H */
