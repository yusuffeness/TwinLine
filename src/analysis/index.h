/*
 * Internal: the feature index shared by the analysis strategies.
 *
 * Every line with words becomes a sorted, unique array of feature hashes.
 * All arrays live in one contiguous arena; the entries are sorted by set
 * size (then line), which the size-ratio bound of the pairwise strategies
 * relies on.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_ANALYSIS_INDEX_H
#define TWINLINE_ANALYSIS_INDEX_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "twinline/document.h"
#include "twinline/status.h"
#include "twinline/utf8.h"

#define TW_NO_MATCH           SIZE_MAX
#define TW_CANCEL_CHECK_LINES 1024

typedef struct tw_line_entry {
    size_t offset;     /* first hash in the arena */
    size_t count;      /* number of hashes */
    size_t line_index; /* 0-based line in the document */
} tw_line_entry;

typedef struct tw_feature_index {
    uint64_t *arena;
    size_t arena_len;
    size_t arena_cap;
    tw_line_entry *entries;
    size_t count;
} tw_feature_index;

void tw_index_free(tw_feature_index *index);

/* Builds the index of `doc` with up to `threads` threads; adds the number
 * of words to `*word_count` (may be NULL). The result does not depend on
 * the number of threads. */
tw_status tw_index_build(const tw_document *doc, tw_fold_mode fold, int ngram, size_t threads,
                         atomic_bool *cancel, tw_feature_index *index, size_t *word_count);

/* Jaccard similarity of two sets of sizes na and nb sharing `common` hashes. */
double tw_jaccard_of(size_t common, size_t na, size_t nb);

/*
 * Smallest intersection size c for which tw_jaccard_of(c, na, nb) >= target,
 * computed with the same floating-point expression used for scoring so the
 * filter can never disagree with the score. Returns 0 when target <= 0.
 */
size_t tw_min_common_for(double target, size_t na, size_t nb);

/*
 * Size of the intersection of two sorted, unique hash arrays. Stops early
 * and returns TW_NO_MATCH as soon as the remaining elements cannot lift the
 * intersection to `needed`; pass 0 to always get the exact size.
 */
size_t tw_intersect(const uint64_t *a, size_t na, const uint64_t *b, size_t nb, size_t needed);

/*
 * Report snippets of two similar lines: at most TW_SNIPPET_BYTES of each,
 * starting just before their first difference when it would be cut off.
 * `out_a` and `out_b` hold TW_SNIPPET_BYTES + 7 bytes.
 */
void tw_snippets(const char *a, const char *b, char *out_a, char *out_b);

#endif /* TWINLINE_ANALYSIS_INDEX_H */
