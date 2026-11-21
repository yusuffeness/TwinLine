/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/analysis.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "twinline/featureset.h"

#define CANCEL_CHECK_LINES 1024

typedef struct line_entry {
    tw_featureset set;
    size_t line_index; /* 0-based index in the document */
} line_entry;

void tw_analysis_options_default(tw_analysis_options *options)
{
    options->threshold = TW_DEFAULT_THRESHOLD;
    options->fold = TW_FOLD_DEFAULT;
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

static void free_entries(line_entry *entries, size_t count)
{
    for (size_t i = 0; i < count; i++)
        tw_featureset_free(&entries[i].set);
    free(entries);
}

/* Step 1: turn every line into a feature set. Lines without words are skipped. */
static tw_status build_entries(const tw_document *doc, tw_fold_mode fold, atomic_bool *cancel,
                               line_entry **out_entries, size_t *out_count, size_t *word_count)
{
    line_entry *entries = calloc(doc->count ? doc->count : 1, sizeof *entries);
    if (entries == NULL)
        return TW_ERR_NOMEM;

    size_t count = 0;
    for (size_t i = 0; i < doc->count; i++) {
        if (i % CANCEL_CHECK_LINES == 0 && is_cancelled(cancel)) {
            free_entries(entries, count);
            return TW_ERR_CANCELLED;
        }

        tw_featureset set;
        tw_status status = tw_featureset_build(doc->lines[i], fold, &set, word_count);
        if (status != TW_OK) {
            free_entries(entries, count);
            return status;
        }
        if (set.count == 0)
            continue;

        entries[count].set = set;
        entries[count].line_index = i;
        count++;
    }

    *out_entries = entries;
    *out_count = count;
    return TW_OK;
}

tw_status tw_analysis_run(const tw_document *doc, const tw_analysis_options *options,
                          atomic_bool *cancel, tw_analysis_progress *progress,
                          tw_analysis_result *out)
{
    memset(out, 0, sizeof *out);
    const double start = now_seconds();
    out->threshold = options->threshold;
    out->lines_total = doc->count;

    line_entry *entries = NULL;
    size_t count = 0;
    tw_status status =
        build_entries(doc, options->fold, cancel, &entries, &count, &out->words_total);
    if (status != TW_OK)
        return status;
    out->lines_with_words = count;

    unsigned long long total_pairs =
        count > 1 ? (unsigned long long)count * (unsigned long long)(count - 1) / 2 : 0;
    if (progress != NULL)
        atomic_store(&progress->pairs_total, total_pairs);

    /* Step 2: compare every pair. */
    double sum = 0.0;
    size_t best_i = 0;
    size_t best_j = 0;
    for (size_t i = 0; i < count; i++) {
        if (is_cancelled(cancel)) {
            free_entries(entries, count);
            return TW_ERR_CANCELLED;
        }

        const tw_featureset *a = &entries[i].set;
        for (size_t j = i + 1; j < count; j++) {
            const tw_featureset *b = &entries[j].set;
            size_t common = tw_featureset_intersection(a, b);
            double similarity = (double)common / (double)(a->count + b->count - common);
            sum += similarity;

            if (common == a->count && common == b->count) {
                out->identical++;
                continue;
            }
            if (similarity >= options->threshold)
                out->near++;
            if (similarity > out->best_similarity) {
                out->best_similarity = similarity;
                best_i = i;
                best_j = j;
            }
        }

        if (progress != NULL)
            atomic_fetch_add_explicit(&progress->pairs_done, (unsigned long long)(count - i - 1),
                                      memory_order_relaxed);
    }

    out->pairs = total_pairs;
    out->mean_similarity = total_pairs ? sum / (double)total_pairs : 0.0;

    if (out->best_similarity > 0.0) {
        out->best_a = entries[best_i].line_index + 1;
        out->best_b = entries[best_j].line_index + 1;
        copy_snippet(out->best_a_text, doc->lines[entries[best_i].line_index]);
        copy_snippet(out->best_b_text, doc->lines[entries[best_j].line_index]);
    }

    free_entries(entries, count);
    out->seconds = now_seconds() - start;
    return TW_OK;
}
