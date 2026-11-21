/*
 * Correctness tests for the parallel, pruned analysis.
 *
 * The optimised engine (worker threads, size-bound pruning and early exit
 * from intersections) must give exactly the same answer as a plain
 * all-pairs reference implementation, for any number of threads.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "../tw_test.h"
#include "twinline/analysis.h"
#include "twinline/featureset.h"

/* Deterministic pseudo-random numbers (no dependence on rand()). */
static uint32_t next_random(uint64_t *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(*state >> 33);
}

static const char *const VOCABULARY[] = {
    "server", "client", "request", "reply",  "thread", "lock", "queue",  "file",
    "line",   "word",   "editor",  "report", "fast",   "slow", "big",    "small",
    "tea",    "coffee", "river",   "city",   "build",  "test", "commit", "merge",
    "alpha",  "beta",   "gamma",   "delta",  "one",    "two",  "three",  "four",
};
#define VOCABULARY_SIZE (sizeof VOCABULARY / sizeof VOCABULARY[0])

/*
 * Builds a document with random sentences plus planted exact copies, near
 * copies and case variants, so every branch of the engine is exercised.
 */
static bool make_corpus(tw_document *doc, size_t lines, uint64_t seed)
{
    tw_document_init(doc);
    uint64_t state = seed;
    char buf[256];

    for (size_t i = 0; i < lines; i++) {
        uint32_t roll = next_random(&state) % 100;
        if (i > 0 && roll < 8) {
            /* exact copy of an earlier line */
            const char *src = doc->lines[next_random(&state) % i];
            snprintf(buf, sizeof buf, "%s", src);
        } else if (i > 0 && roll < 20) {
            /* near copy: change the last word */
            const char *src = doc->lines[next_random(&state) % i];
            snprintf(buf, sizeof buf, "%s %s", src,
                     VOCABULARY[next_random(&state) % VOCABULARY_SIZE]);
        } else if (i > 0 && roll < 24) {
            /* case and punctuation variant */
            const char *src = doc->lines[next_random(&state) % i];
            size_t k = 0;
            for (; src[k] != '\0' && k < sizeof buf - 2; k++)
                buf[k] = (src[k] >= 'a' && src[k] <= 'z') ? (char)(src[k] - 'a' + 'A') : src[k];
            buf[k++] = '!';
            buf[k] = '\0';
        } else if (roll < 26) {
            buf[0] = '\0'; /* an empty line */
        } else {
            size_t words = 3 + next_random(&state) % 10;
            size_t len = 0;
            for (size_t w = 0; w < words; w++) {
                int n = snprintf(buf + len, sizeof buf - len, "%s%s", w ? " " : "",
                                 VOCABULARY[next_random(&state) % VOCABULARY_SIZE]);
                if (n < 0 || (size_t)n >= sizeof buf - len)
                    break;
                len += (size_t)n;
            }
        }
        if (tw_document_append(doc, buf) != TW_OK)
            return false;
    }
    return true;
}

typedef struct reference {
    unsigned long long identical;
    unsigned long long near;
    double best;
    size_t best_a; /* 1-based */
    size_t best_b;
    double mean;
} reference;

/* The obvious O(n^2) algorithm, written independently of the engine. */
static bool reference_analysis(const tw_document *doc, double threshold, reference *ref)
{
    memset(ref, 0, sizeof *ref);
    tw_featureset *sets = calloc(doc->count, sizeof *sets);
    if (sets == NULL)
        return false;
    for (size_t i = 0; i < doc->count; i++) {
        if (tw_featureset_build(doc->lines[i], TW_FOLD_DEFAULT, &sets[i], NULL) != TW_OK)
            return false;
    }

    double sum = 0.0;
    unsigned long long pairs = 0;
    for (size_t i = 0; i < doc->count; i++) {
        if (sets[i].count == 0)
            continue;
        for (size_t j = i + 1; j < doc->count; j++) {
            if (sets[j].count == 0)
                continue;
            pairs++;
            size_t common = tw_featureset_intersection(&sets[i], &sets[j]);
            if (common == sets[i].count && common == sets[j].count) {
                ref->identical++;
                sum += 1.0;
                continue;
            }
            double j_sim = tw_featureset_jaccard(&sets[i], &sets[j]);
            sum += j_sim;
            if (j_sim >= threshold)
                ref->near++;
            if (j_sim > ref->best) { /* first maximum in row-major order */
                ref->best = j_sim;
                ref->best_a = i + 1;
                ref->best_b = j + 1;
            }
        }
    }
    ref->mean = pairs ? sum / (double)pairs : 0.0;

    for (size_t i = 0; i < doc->count; i++)
        tw_featureset_free(&sets[i]);
    free(sets);
    return true;
}

static bool run_engine(const tw_document *doc, double threshold, size_t threads, bool exhaustive,
                       tw_analysis_result *out)
{
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    options.threshold = threshold;
    options.threads = threads;
    options.exhaustive = exhaustive;
    return tw_analysis_run(doc, &options, NULL, NULL, out) == TW_OK;
}

#define CHECK_MATCHES_REFERENCE(result, ref)                                                       \
    do {                                                                                           \
        TW_CHECK_EQ_SIZE((result).identical, (ref).identical);                                     \
        TW_CHECK_EQ_SIZE((result).near, (ref).near);                                               \
        TW_CHECK_NEAR((result).best_similarity, (ref).best, 0.0);                                  \
        TW_CHECK_EQ_SIZE((result).best_a, (ref).best_a);                                           \
        TW_CHECK_EQ_SIZE((result).best_b, (ref).best_b);                                           \
    } while (0)

TW_TEST(matches_reference_for_every_thread_count)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 600, 1));
    reference ref;
    TW_CHECK(reference_analysis(&doc, TW_DEFAULT_THRESHOLD, &ref));
    TW_CHECK(ref.identical > 0);
    TW_CHECK(ref.near > 0);

    const size_t thread_counts[] = {1, 2, 3, 4, 7, 16};
    for (size_t k = 0; k < sizeof thread_counts / sizeof thread_counts[0]; k++) {
        tw_analysis_result r;
        TW_CHECK(run_engine(&doc, TW_DEFAULT_THRESHOLD, thread_counts[k], false, &r));
        CHECK_MATCHES_REFERENCE(r, ref);
        TW_CHECK_EQ_SIZE(r.threads, thread_counts[k]);
        TW_CHECK(r.compared <= r.pairs);
    }
    tw_document_free(&doc);
}

TW_TEST(exhaustive_mode_matches_reference_including_mean)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 400, 2));
    reference ref;
    TW_CHECK(reference_analysis(&doc, TW_DEFAULT_THRESHOLD, &ref));

    for (size_t threads = 1; threads <= 4; threads++) {
        tw_analysis_result r;
        TW_CHECK(run_engine(&doc, TW_DEFAULT_THRESHOLD, threads, true, &r));
        CHECK_MATCHES_REFERENCE(r, ref);
        TW_CHECK(r.exhaustive);
        TW_CHECK_EQ_SIZE(r.compared, r.pairs);
        TW_CHECK_NEAR(r.mean_similarity, ref.mean, 1e-9);
    }
    tw_document_free(&doc);
}

TW_TEST(pruning_is_exact_for_many_thresholds)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 300, 3));
    const double thresholds[] = {0.05, 0.3, 0.5, 0.7, 0.9, 1.0};
    for (size_t k = 0; k < sizeof thresholds / sizeof thresholds[0]; k++) {
        reference ref;
        TW_CHECK(reference_analysis(&doc, thresholds[k], &ref));
        tw_analysis_result r;
        TW_CHECK(run_engine(&doc, thresholds[k], 3, false, &r));
        CHECK_MATCHES_REFERENCE(r, ref);
    }
    tw_document_free(&doc);
}

TW_TEST(pruning_skips_work)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 600, 4));
    tw_analysis_result pruned;
    TW_CHECK(run_engine(&doc, TW_DEFAULT_THRESHOLD, 2, false, &pruned));
    TW_CHECK(pruned.compared < pruned.pairs);
    tw_document_free(&doc);
}

TW_TEST(rejects_invalid_threshold)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 10, 5));
    tw_analysis_result r;
    TW_CHECK(!run_engine(&doc, 0.0, 1, false, &r));
    TW_CHECK(!run_engine(&doc, 1.5, 1, false, &r));
    tw_document_free(&doc);
}

TW_TEST(more_threads_than_lines)
{
    tw_document doc;
    TW_CHECK(make_corpus(&doc, 3, 6));
    tw_analysis_result r;
    TW_CHECK(run_engine(&doc, TW_DEFAULT_THRESHOLD, 64, false, &r));
    TW_CHECK(r.threads <= 3);
    tw_document_free(&doc);
}

TW_TEST(thread_count_resolution)
{
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    TW_CHECK(tw_analysis_thread_count(&options) >= 1); /* auto */
    options.threads = 5;
    TW_CHECK_EQ_SIZE(tw_analysis_thread_count(&options), 5);
    options.threads = 100000;
    TW_CHECK_EQ_SIZE(tw_analysis_thread_count(&options), TW_MAX_THREADS);
}

int main(void)
{
    puts("parallel analysis");
    TW_RUN(matches_reference_for_every_thread_count);
    TW_RUN(exhaustive_mode_matches_reference_including_mean);
    TW_RUN(pruning_is_exact_for_many_thresholds);
    TW_RUN(pruning_skips_work);
    TW_RUN(rejects_invalid_threshold);
    TW_RUN(more_threads_than_lines);
    TW_RUN(thread_count_resolution);
    return tw_test_summary();
}
