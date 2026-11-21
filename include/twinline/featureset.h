/*
 * Feature sets: the representation of a line used for similarity.
 *
 * A line becomes the set of its words and of its adjacent word pairs
 * (bigrams), each stored as a 64-bit hash. Bigrams make word order count.
 * The hashes are sorted and unique, so two sets can be intersected in
 * linear time.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_FEATURESET_H
#define TWINLINE_FEATURESET_H

#include <stddef.h>
#include <stdint.h>

#include "twinline/status.h"
#include "twinline/utf8.h"

typedef struct tw_featureset {
    uint64_t *hashes; /* sorted, unique */
    size_t count;
} tw_featureset;

/*
 * Builds the feature set of `line`. Words are case-folded according to
 * `fold`. If `word_count` is not NULL, the number of words found is added
 * to it. An empty line produces an empty set (hashes == NULL).
 */
tw_status tw_featureset_build(const char *line, tw_fold_mode fold, tw_featureset *out,
                              size_t *word_count);

void tw_featureset_free(tw_featureset *set);

/* Size of the intersection of two feature sets. */
size_t tw_featureset_intersection(const tw_featureset *a, const tw_featureset *b);

/* Jaccard similarity |A n B| / |A u B|; 0 when both sets are empty. */
double tw_featureset_jaccard(const tw_featureset *a, const tw_featureset *b);

#endif /* TWINLINE_FEATURESET_H */
