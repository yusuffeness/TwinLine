/*
 * SPDX-License-Identifier: MIT
 *
 * Overview
 *
 * 1. Index: every line with words becomes a feature set. All sets live in one
 *    contiguous arena; the entries are sorted by set size so that, for a fixed
 *    row, the size-ratio bound only decreases along the row.
 * 2. Compare: worker threads take chunks of rows from a shared atomic cursor
 *    and score each row against every larger entry. Each worker keeps its own
 *    counters and best pair, so the hot loop takes no locks.
 * 3. Merge: the per-worker results are combined. The best pair is chosen by
 *    (similarity, then lowest line numbers), so the result does not depend on
 *    the number of threads or on scheduling.
 */
#include "twinline/analysis.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "analysis/index.h"
#include "twinline/featureset.h"
#include "twinline/similarity.h"
#include "twinline/sysinfo.h"

#define CHUNKS_PER_THREAD 64

/* ------------------------------------------------------------------------ */
/* Options and progress                                                      */
/* ------------------------------------------------------------------------ */

void tw_analysis_options_default(tw_analysis_options *options)
{
    options->threshold = TW_DEFAULT_THRESHOLD;
    options->fold = TW_FOLD_DEFAULT;
    options->threads = 0;
    options->exhaustive = false;
    options->strategy = TW_STRATEGY_JACCARD;
    options->ngram = TW_NGRAM_DEFAULT;
}

const char *tw_strategy_name(tw_strategy strategy)
{
    switch (strategy) {
    case TW_STRATEGY_EXACT:
        return "exact";
    case TW_STRATEGY_MINHASH:
        return "minhash";
    case TW_STRATEGY_JACCARD:
        break;
    }
    return "jaccard";
}

bool tw_strategy_parse(const char *name, tw_strategy *out)
{
    static const tw_strategy ALL[] = {TW_STRATEGY_JACCARD, TW_STRATEGY_EXACT, TW_STRATEGY_MINHASH};
    for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++) {
        if (strcmp(name, tw_strategy_name(ALL[i])) == 0) {
            *out = ALL[i];
            return true;
        }
    }
    return false;
}

size_t tw_analysis_thread_count(const tw_analysis_options *options)
{
    size_t threads = options->threads ? options->threads : tw_cpu_count();
    return threads > TW_MAX_THREADS ? TW_MAX_THREADS : threads;
}

void tw_analysis_progress_init(tw_analysis_progress *progress)
{
    atomic_init(&progress->pairs_done, 0);
    atomic_init(&progress->pairs_total, 0);
    atomic_init(&progress->start_ns, 0);
    atomic_init(&progress->threads, 0);
    for (size_t i = 0; i < TW_PROGRESS_SLOTS; i++)
        atomic_init(&progress->thread_done[i], 0);
}

void tw_analysis_progress_reset(tw_analysis_progress *progress)
{
    atomic_store(&progress->pairs_done, 0);
    atomic_store(&progress->pairs_total, 0);
    atomic_store(&progress->start_ns, 0);
    atomic_store(&progress->threads, 0);
    for (size_t i = 0; i < TW_PROGRESS_SLOTS; i++)
        atomic_store(&progress->thread_done[i], 0);
}

static unsigned long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}

double tw_analysis_progress_elapsed(tw_analysis_progress *progress)
{
    unsigned long long start = atomic_load_explicit(&progress->start_ns, memory_order_relaxed);
    if (start == 0)
        return 0.0;
    unsigned long long now = now_ns();
    return now > start ? (double)(now - start) / 1e9 : 0.0;
}

size_t tw_analysis_progress_threads(tw_analysis_progress *progress, double *fractions, size_t max)
{
    size_t threads = atomic_load_explicit(&progress->threads, memory_order_relaxed);
    unsigned long long total = atomic_load_explicit(&progress->pairs_total, memory_order_relaxed);
    if (threads > TW_PROGRESS_SLOTS)
        threads = TW_PROGRESS_SLOTS;
    if (threads > max)
        threads = max;
    double share = threads > 0 ? (double)total / (double)threads : 0.0;
    for (size_t t = 0; t < threads; t++) {
        double done = (double)atomic_load_explicit(&progress->thread_done[t], memory_order_relaxed);
        double f = share > 0.0 ? done / share : 0.0;
        fractions[t] = f > 1.0 ? 1.0 : f;
    }
    return threads;
}

double tw_analysis_progress_fraction(tw_analysis_progress *progress)
{
    unsigned long long total = atomic_load_explicit(&progress->pairs_total, memory_order_relaxed);
    unsigned long long done = atomic_load_explicit(&progress->pairs_done, memory_order_relaxed);
    if (total == 0)
        return 0.0;
    return (double)done / (double)total;
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool is_cancelled(atomic_bool *cancel)
{
    return cancel != NULL && atomic_load_explicit(cancel, memory_order_relaxed);
}

/* ------------------------------------------------------------------------ */
/* Step 2: parallel comparison                                               */
/* ------------------------------------------------------------------------ */

typedef struct shared_work {
    const tw_feature_index *index;
    double threshold;
    bool exhaustive;
    atomic_bool *cancel;
    tw_analysis_progress *progress;
    atomic_size_t next_row;
    size_t chunk;
} shared_work;

typedef struct worker_result {
    unsigned long long compared;
    unsigned long long identical;
    unsigned long long near;
    double sum;
    double best;
    size_t best_a; /* 0-based lines, best_a < best_b */
    size_t best_b;
    bool cancelled;
} worker_result;

typedef struct worker {
    pthread_t thread;
    size_t index;
    bool started;
    shared_work *work;
    worker_result result;
} worker;

/* True if (similarity, a, b) should replace the current best pair. */
static bool is_better(const worker_result *r, double similarity, size_t a, size_t b)
{
    if (similarity != r->best)
        return similarity > r->best;
    if (similarity <= 0.0)
        return false;
    return a < r->best_a || (a == r->best_a && b < r->best_b);
}

static void consider_best(worker_result *r, double similarity, size_t line_i, size_t line_j)
{
    size_t a = line_i < line_j ? line_i : line_j;
    size_t b = line_i < line_j ? line_j : line_i;
    if (is_better(r, similarity, a, b)) {
        r->best = similarity;
        r->best_a = a;
        r->best_b = b;
    }
}

static void compare_row(const shared_work *work, size_t i, worker_result *r)
{
    const tw_feature_index *index = work->index;
    const tw_line_entry *entries = index->entries;
    const uint64_t *a = index->arena + entries[i].offset;
    size_t na = entries[i].count;

    for (size_t j = i + 1; j < index->count; j++) {
        size_t nb = entries[j].count; /* nb >= na: entries are sorted by size */

        if (!work->exhaustive) {
            double bound = (double)na / (double)nb;
            if (bound < work->threshold && bound < r->best)
                break; /* every later entry is at least as large */
        }

        /* A pair only matters if J >= threshold (near duplicate) or J > best
         * (new best pair); below min(threshold, best) the exact value is not
         * needed. Exhaustive mode needs every value for the mean. */
        size_t needed = 0;
        if (!work->exhaustive) {
            double target = r->best < work->threshold ? r->best : work->threshold;
            needed = tw_min_common_for(target, na, nb);
        }

        size_t common = tw_intersect(a, na, index->arena + entries[j].offset, nb, needed);
        r->compared++;
        if (common == TW_NO_MATCH)
            continue;
        if (common == na && common == nb) {
            r->identical++;
            r->sum += 1.0;
            continue;
        }

        double similarity = tw_jaccard_of(common, na, nb);
        r->sum += similarity;
        if (similarity >= work->threshold)
            r->near++;
        consider_best(r, similarity, entries[i].line_index, entries[j].line_index);
    }
}

static void *worker_main(void *arg)
{
    worker *self = arg;
    shared_work *work = self->work;
    size_t rows = work->index->count;

    for (;;) {
        if (is_cancelled(work->cancel)) {
            self->result.cancelled = true;
            break;
        }

        size_t start =
            atomic_fetch_add_explicit(&work->next_row, work->chunk, memory_order_relaxed);
        if (start >= rows)
            break;
        size_t end = (rows - start > work->chunk) ? start + work->chunk : rows;

        unsigned long long done = 0;
        for (size_t i = start; i < end; i++) {
            compare_row(work, i, &self->result);
            done += (unsigned long long)(rows - i - 1);
        }
        if (work->progress != NULL) {
            atomic_fetch_add_explicit(&work->progress->pairs_done, done, memory_order_relaxed);
            atomic_fetch_add_explicit(&work->progress->thread_done[self->index % TW_PROGRESS_SLOTS],
                                      done, memory_order_relaxed);
        }
    }
    return NULL;
}

/* Runs `count` workers; the calling thread acts as the first one. Workers
 * that cannot be started are skipped: the others pick up their rows. */
static void run_workers(worker *workers, size_t count)
{
    for (size_t t = 1; t < count; t++)
        workers[t].started =
            pthread_create(&workers[t].thread, NULL, worker_main, &workers[t]) == 0;

    workers[0].started = true;
    worker_main(&workers[0]);

    for (size_t t = 1; t < count; t++) {
        if (workers[t].started)
            pthread_join(workers[t].thread, NULL);
    }
}

/* ------------------------------------------------------------------------ */
/* Step 3: merge                                                             */
/* ------------------------------------------------------------------------ */

static bool merge_results(const worker *workers, size_t count, tw_analysis_result *out,
                          size_t *best_a, size_t *best_b)
{
    worker_result total;
    memset(&total, 0, sizeof total);
    double sum = 0.0;

    for (size_t t = 0; t < count; t++) {
        const worker_result *r = &workers[t].result;
        if (r->cancelled)
            return false;
        total.compared += r->compared;
        total.identical += r->identical;
        total.near += r->near;
        sum += r->sum;
        if (is_better(&total, r->best, r->best_a, r->best_b)) {
            total.best = r->best;
            total.best_a = r->best_a;
            total.best_b = r->best_b;
        }
    }

    out->compared = total.compared;
    out->identical = total.identical;
    out->near = total.near;
    out->best_similarity = total.best;
    if (out->exhaustive && out->pairs > 0)
        out->mean_similarity = sum / (double)out->pairs;
    *best_a = total.best_a;
    *best_b = total.best_b;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public entry point                                                        */
/* ------------------------------------------------------------------------ */

tw_status tw_analysis_run(const tw_document *doc, const tw_analysis_options *options,
                          atomic_bool *cancel, tw_analysis_progress *progress,
                          tw_analysis_result *out)
{
    memset(out, 0, sizeof *out);
    if (!(options->threshold > 0.0 && options->threshold <= 1.0))
        return TW_ERR_INVALID;

    const double start = now_seconds();
    if (progress != NULL)
        atomic_store(&progress->start_ns, now_ns());

    if (options->strategy != TW_STRATEGY_JACCARD) {
        tw_similarity similarity;
        tw_status status = tw_similarity_run(doc, options, cancel, progress, &similarity);
        if (status != TW_OK)
            return status;
        tw_similarity_to_result(&similarity, doc, out);
        tw_similarity_free(&similarity);
        out->seconds = now_seconds() - start;
        return TW_OK;
    }
    out->threshold = options->threshold;
    out->exhaustive = options->exhaustive;
    out->lines_total = doc->count;

    tw_feature_index index;
    tw_status status =
        tw_index_build(doc, options->fold, options->ngram, tw_analysis_thread_count(options),
                       cancel, &index, &out->words_total);
    if (status != TW_OK)
        return status;

    size_t rows = index.count;
    out->lines_with_words = rows;
    out->pairs = rows > 1 ? (unsigned long long)rows * (unsigned long long)(rows - 1) / 2 : 0;
    if (progress != NULL)
        atomic_store(&progress->pairs_total, out->pairs);

    size_t threads = tw_analysis_thread_count(options);
    if (threads > rows && rows > 0)
        threads = rows;
    if (threads == 0)
        threads = 1;
    out->threads = threads;
    if (progress != NULL)
        atomic_store(&progress->threads, threads);

    worker *workers = calloc(threads, sizeof *workers);
    if (workers == NULL) {
        tw_index_free(&index);
        return TW_ERR_NOMEM;
    }

    shared_work work;
    work.index = &index;
    work.threshold = options->threshold;
    work.exhaustive = options->exhaustive;
    work.cancel = cancel;
    work.progress = progress;
    atomic_init(&work.next_row, 0);
    work.chunk = rows / (threads * CHUNKS_PER_THREAD);
    if (work.chunk == 0)
        work.chunk = 1;
    for (size_t t = 0; t < threads; t++) {
        workers[t].work = &work;
        workers[t].index = t;
    }

    run_workers(workers, threads);

    size_t best_a = 0;
    size_t best_b = 0;
    bool complete = merge_results(workers, threads, out, &best_a, &best_b);
    free(workers);
    tw_index_free(&index);

    if (!complete || is_cancelled(cancel))
        return TW_ERR_CANCELLED;

    if (out->best_similarity > 0.0) {
        out->best_a = best_a + 1;
        out->best_b = best_b + 1;
        tw_snippets(doc->lines[best_a], doc->lines[best_b], out->best_a_text, out->best_b_text);
    }

    out->seconds = now_seconds() - start;
    return TW_OK;
}
