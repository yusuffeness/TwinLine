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

#include "twinline/featureset.h"
#include "twinline/sysinfo.h"

#define CANCEL_CHECK_LINES 1024
#define CHUNKS_PER_THREAD  64

/* ------------------------------------------------------------------------ */
/* Options and progress                                                      */
/* ------------------------------------------------------------------------ */

void tw_analysis_options_default(tw_analysis_options *options)
{
    options->threshold = TW_DEFAULT_THRESHOLD;
    options->fold = TW_FOLD_DEFAULT;
    options->threads = 0;
    options->exhaustive = false;
}

size_t tw_analysis_thread_count(const tw_analysis_options *options)
{
    size_t threads = options->threads ? options->threads : tw_cpu_count();
    return threads > TW_MAX_THREADS ? TW_MAX_THREADS : threads;
}

void tw_analysis_progress_reset(tw_analysis_progress *progress)
{
    atomic_store(&progress->pairs_done, 0);
    atomic_store(&progress->pairs_total, 0);
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

static void copy_snippet(char *dst, const char *src)
{
    size_t len = tw_utf8_prefix_len(src, TW_SNIPPET_BYTES);
    memcpy(dst, src, len);
    if (src[len] != '\0')
        memcpy(dst + len, "...", 4);
    else
        dst[len] = '\0';
}

#define TW_NO_MATCH SIZE_MAX

static double jaccard_of(size_t common, size_t na, size_t nb)
{
    return (double)common / (double)(na + nb - common);
}

/*
 * Smallest intersection size c for which jaccard_of(c, na, nb) >= target,
 * computed with the same floating-point expression used for scoring so the
 * filter can never disagree with the score. Returns 0 when target <= 0.
 */
static size_t min_common_for(double target, size_t na, size_t nb)
{
    if (target <= 0.0)
        return 0;
    size_t c = (size_t)(target * (double)(na + nb) / (1.0 + target));
    if (c > na)
        c = na;
    while (c > 0 && jaccard_of(c - 1, na, nb) >= target)
        c--;
    while (c <= na && jaccard_of(c, na, nb) < target)
        c++;
    return c;
}

/*
 * Size of the intersection of two sorted, unique hash arrays. Stops early
 * and returns TW_NO_MATCH as soon as the remaining elements cannot lift the
 * intersection to `needed`; pass 0 to always get the exact size.
 */
static size_t intersect(const uint64_t *a, size_t na, const uint64_t *b, size_t nb, size_t needed)
{
    size_t i = 0;
    size_t j = 0;
    size_t common = 0;
    while (i < na && j < nb) {
        if (a[i] == b[j]) {
            common++;
            i++;
            j++;
            continue;
        }
        if (a[i] < b[j])
            i++;
        else
            j++;
        size_t left = (na - i < nb - j) ? na - i : nb - j;
        if (common + left < needed)
            return TW_NO_MATCH;
    }
    return common;
}

/* ------------------------------------------------------------------------ */
/* Step 1: feature index                                                     */
/* ------------------------------------------------------------------------ */

typedef struct line_entry {
    size_t offset;     /* first hash in the arena */
    size_t count;      /* number of hashes */
    size_t line_index; /* 0-based line in the document */
} line_entry;

typedef struct feature_index {
    uint64_t *arena;
    size_t arena_len;
    size_t arena_cap;
    line_entry *entries;
    size_t count;
} feature_index;

static void index_free(feature_index *index)
{
    free(index->arena);
    free(index->entries);
    memset(index, 0, sizeof *index);
}

static tw_status arena_append(feature_index *index, const uint64_t *hashes, size_t count)
{
    if (index->arena_len + count > index->arena_cap) {
        size_t new_cap = index->arena_cap ? index->arena_cap : 4096;
        while (new_cap < index->arena_len + count) {
            if (new_cap > SIZE_MAX / 2 / sizeof *index->arena)
                return TW_ERR_NOMEM;
            new_cap *= 2;
        }
        uint64_t *arena = realloc(index->arena, new_cap * sizeof *arena);
        if (arena == NULL)
            return TW_ERR_NOMEM;
        index->arena = arena;
        index->arena_cap = new_cap;
    }
    memcpy(index->arena + index->arena_len, hashes, count * sizeof *hashes);
    index->arena_len += count;
    return TW_OK;
}

static int compare_entries(const void *lhs, const void *rhs)
{
    const line_entry *a = lhs;
    const line_entry *b = rhs;
    if (a->count != b->count)
        return a->count < b->count ? -1 : 1;
    return (a->line_index > b->line_index) - (a->line_index < b->line_index);
}

static tw_status build_index(const tw_document *doc, tw_fold_mode fold, atomic_bool *cancel,
                             feature_index *index, size_t *word_count)
{
    memset(index, 0, sizeof *index);
    index->entries = calloc(doc->count ? doc->count : 1, sizeof *index->entries);
    if (index->entries == NULL)
        return TW_ERR_NOMEM;

    tw_featureset_builder builder;
    tw_featureset_builder_init(&builder);
    tw_status status = TW_OK;

    for (size_t i = 0; i < doc->count; i++) {
        if (i % CANCEL_CHECK_LINES == 0 && is_cancelled(cancel)) {
            status = TW_ERR_CANCELLED;
            break;
        }

        tw_featureset view;
        status = tw_featureset_builder_build(&builder, doc->lines[i], fold, &view, word_count);
        if (status != TW_OK)
            break;
        if (view.count == 0)
            continue;

        size_t offset = index->arena_len;
        status = arena_append(index, view.hashes, view.count);
        if (status != TW_OK)
            break;
        index->entries[index->count].offset = offset;
        index->entries[index->count].count = view.count;
        index->entries[index->count].line_index = i;
        index->count++;
    }

    tw_featureset_builder_free(&builder);
    if (status != TW_OK) {
        index_free(index);
        return status;
    }

    qsort(index->entries, index->count, sizeof *index->entries, compare_entries);
    return TW_OK;
}

/* ------------------------------------------------------------------------ */
/* Step 2: parallel comparison                                               */
/* ------------------------------------------------------------------------ */

typedef struct shared_work {
    const feature_index *index;
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
    const feature_index *index = work->index;
    const line_entry *entries = index->entries;
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
            needed = min_common_for(target, na, nb);
        }

        size_t common = intersect(a, na, index->arena + entries[j].offset, nb, needed);
        r->compared++;
        if (common == TW_NO_MATCH)
            continue;
        if (common == na && common == nb) {
            r->identical++;
            r->sum += 1.0;
            continue;
        }

        double similarity = jaccard_of(common, na, nb);
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
        if (work->progress != NULL)
            atomic_fetch_add_explicit(&work->progress->pairs_done, done, memory_order_relaxed);
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
    out->threshold = options->threshold;
    out->exhaustive = options->exhaustive;
    out->lines_total = doc->count;

    feature_index index;
    tw_status status = build_index(doc, options->fold, cancel, &index, &out->words_total);
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

    worker *workers = calloc(threads, sizeof *workers);
    if (workers == NULL) {
        index_free(&index);
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
    for (size_t t = 0; t < threads; t++)
        workers[t].work = &work;

    run_workers(workers, threads);

    size_t best_a = 0;
    size_t best_b = 0;
    bool complete = merge_results(workers, threads, out, &best_a, &best_b);
    free(workers);
    index_free(&index);

    if (!complete || is_cancelled(cancel))
        return TW_ERR_CANCELLED;

    if (out->best_similarity > 0.0) {
        out->best_a = best_a + 1;
        out->best_b = best_b + 1;
        copy_snippet(out->best_a_text, doc->lines[best_a]);
        copy_snippet(out->best_b_text, doc->lines[best_b]);
    }

    out->seconds = now_seconds() - start;
    return TW_OK;
}
