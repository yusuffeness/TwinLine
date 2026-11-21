/*
 * Near-duplicate line analysis.
 *
 * Every pair of non-empty lines is a candidate, scored with the Jaccard
 * similarity of their feature sets (see featureset.h). The work is split
 * across worker threads.
 *
 * Unless `exhaustive` is set, pairs that provably cannot matter are skipped:
 * since J(A, B) <= min(|A|, |B|) / max(|A|, |B|), a pair whose size ratio is
 * below both the threshold and the best similarity found so far can neither
 * be a near duplicate nor the most similar pair. All reported counts and the
 * most similar pair are therefore exact; only the mean similarity, which
 * needs every pair, is left out.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_ANALYSIS_H
#define TWINLINE_ANALYSIS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "twinline/document.h"
#include "twinline/status.h"
#include "twinline/utf8.h"

#define TW_DEFAULT_THRESHOLD 0.70 /* pairs at or above this are near duplicates */
#define TW_SNIPPET_BYTES     70   /* bytes of a line shown in reports */
#define TW_MAX_THREADS       256

typedef struct tw_analysis_options {
    double threshold; /* in (0, 1] */
    tw_fold_mode fold;
    size_t threads;  /* worker threads; 0 means one per online processor */
    bool exhaustive; /* compare every pair (needed for the mean similarity) */
} tw_analysis_options;

typedef struct tw_analysis_result {
    double threshold;             /* threshold used for `near` */
    size_t threads;               /* worker threads used */
    bool exhaustive;              /* every pair was scored */
    size_t lines_total;           /* lines in the document */
    size_t lines_with_words;      /* lines that contain at least one word */
    size_t words_total;           /* words in the whole document */
    unsigned long long pairs;     /* candidate pairs: n(n-1)/2 */
    unsigned long long compared;  /* pairs actually scored */
    unsigned long long identical; /* pairs with identical feature sets */
    unsigned long long near;      /* pairs with threshold <= J < 1 */
    double mean_similarity;       /* average J over all pairs; exhaustive only */
    double best_similarity;       /* highest J below 1, 0 if none */
    size_t best_a;                /* 1-based line numbers of that pair */
    size_t best_b;
    char best_a_text[TW_SNIPPET_BYTES + 4];
    char best_b_text[TW_SNIPPET_BYTES + 4];
    double seconds; /* wall-clock duration */
} tw_analysis_result;

/* Live progress, readable from any thread while an analysis runs. */
typedef struct tw_analysis_progress {
    atomic_ullong pairs_done;
    atomic_ullong pairs_total;
} tw_analysis_progress;

void tw_analysis_options_default(tw_analysis_options *options);
void tw_analysis_progress_reset(tw_analysis_progress *progress);

/* Number of worker threads `options` resolves to on this machine. */
size_t tw_analysis_thread_count(const tw_analysis_options *options);

/* Fraction of work done in [0, 1]. */
double tw_analysis_progress_fraction(tw_analysis_progress *progress);

/*
 * Analyses `doc` and fills `out`.
 *
 * `cancel` (may be NULL) is polled regularly; when it becomes true the
 * function stops and returns TW_ERR_CANCELLED. `progress` (may be NULL) is
 * updated as rows are processed.
 */
tw_status tw_analysis_run(const tw_document *doc, const tw_analysis_options *options,
                          atomic_bool *cancel, tw_analysis_progress *progress,
                          tw_analysis_result *out);

#endif /* TWINLINE_ANALYSIS_H */
