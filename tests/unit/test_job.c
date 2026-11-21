/*
 * Unit tests for src/analysis/job.c (background analysis).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "../tw_test.h"
#include "twinline/job.h"

typedef struct done_record {
    atomic_int calls;
    atomic_int status;
    atomic_ullong identical;
} done_record;

static void record_done(tw_status status, const tw_analysis_result *result, void *user)
{
    done_record *rec = user;
    atomic_store(&rec->status, (int)status);
    if (status == TW_OK)
        atomic_store(&rec->identical, result->identical);
    atomic_fetch_add(&rec->calls, 1);
}

static void record_init(done_record *rec)
{
    atomic_init(&rec->calls, 0);
    atomic_init(&rec->status, -1);
    atomic_init(&rec->identical, 0);
}

/* A document large enough that analysing it takes far longer than the test. */
static bool make_large_document(tw_document *doc, int lines)
{
    tw_document_init(doc);
    char buf[64];
    for (int i = 0; i < lines; i++) {
        snprintf(buf, sizeof buf, "word%d alpha beta gamma %d delta", i % 97, i);
        if (tw_document_append(doc, buf) != TW_OK)
            return false;
    }
    return true;
}

TW_TEST(runs_to_completion_and_reports)
{
    tw_analysis_job job;
    tw_job_init(&job);
    done_record rec;
    record_init(&rec);

    tw_document snapshot;
    tw_document_init(&snapshot);
    TW_CHECK_EQ_INT(tw_document_append(&snapshot, "same words here"), TW_OK);
    TW_CHECK_EQ_INT(tw_document_append(&snapshot, "Same words, here!"), TW_OK);

    tw_analysis_options options;
    tw_analysis_options_default(&options);
    TW_CHECK_EQ_INT(tw_job_start(&job, &snapshot, &options, record_done, &rec), TW_OK);
    TW_CHECK_EQ_SIZE(snapshot.count, 0); /* ownership moved to the job */

    tw_job_wait(&job);
    TW_CHECK(!tw_job_is_running(&job));
    TW_CHECK_EQ_INT(atomic_load(&rec.calls), 1);
    TW_CHECK_EQ_INT(atomic_load(&rec.status), TW_OK);
    TW_CHECK_EQ_SIZE(atomic_load(&rec.identical), 1);
    tw_job_destroy(&job);
}

TW_TEST(can_be_cancelled)
{
    tw_analysis_job job;
    tw_job_init(&job);
    done_record rec;
    record_init(&rec);

    tw_document snapshot;
    TW_CHECK(make_large_document(&snapshot, 40000));

    tw_analysis_options options;
    tw_analysis_options_default(&options);
    TW_CHECK_EQ_INT(tw_job_start(&job, &snapshot, &options, record_done, &rec), TW_OK);
    tw_job_cancel(&job);
    tw_job_wait(&job);

    TW_CHECK_EQ_INT(atomic_load(&rec.calls), 1);
    TW_CHECK_EQ_INT(atomic_load(&rec.status), TW_ERR_CANCELLED);
    TW_CHECK(!tw_job_is_cancelling(&job));
    tw_job_destroy(&job);
}

TW_TEST(refuses_a_second_job_while_busy)
{
    tw_analysis_job job;
    tw_job_init(&job);
    done_record rec;
    record_init(&rec);

    tw_document first;
    TW_CHECK(make_large_document(&first, 40000));
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    TW_CHECK_EQ_INT(tw_job_start(&job, &first, &options, record_done, &rec), TW_OK);

    tw_document second;
    TW_CHECK(make_large_document(&second, 10));
    TW_CHECK_EQ_INT(tw_job_start(&job, &second, &options, record_done, &rec), TW_ERR_BUSY);
    TW_CHECK_EQ_SIZE(second.count, 0); /* the rejected snapshot is still released */

    tw_job_destroy(&job); /* cancels and joins the running worker */
    TW_CHECK_EQ_INT(atomic_load(&rec.calls), 1);
}

TW_TEST(can_run_again_after_finishing)
{
    tw_analysis_job job;
    tw_job_init(&job);
    done_record rec;
    record_init(&rec);
    tw_analysis_options options;
    tw_analysis_options_default(&options);

    for (int round = 0; round < 3; round++) {
        tw_document snapshot;
        TW_CHECK(make_large_document(&snapshot, 50));
        TW_CHECK_EQ_INT(tw_job_start(&job, &snapshot, &options, record_done, &rec), TW_OK);
        tw_job_wait(&job);
    }
    TW_CHECK_EQ_INT(atomic_load(&rec.calls), 3);
    TW_CHECK_EQ_INT(atomic_load(&rec.status), TW_OK);
    tw_job_destroy(&job);
}

int main(void)
{
    puts("job");
    TW_RUN(runs_to_completion_and_reports);
    TW_RUN(can_be_cancelled);
    TW_RUN(refuses_a_second_job_while_busy);
    TW_RUN(can_run_again_after_finishing);
    return tw_test_summary();
}
