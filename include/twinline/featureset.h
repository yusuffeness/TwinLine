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
 * Reusable scratch space for building feature sets without allocating for
 * every line. One builder must not be shared between threads.
 */
typedef struct tw_featureset_builder {
    uint64_t *buf;
    size_t cap;
} tw_featureset_builder;

void tw_featureset_builder_init(tw_featureset_builder *builder);
void tw_featureset_builder_free(tw_featureset_builder *builder);

#define TW_NGRAM_DEFAULT 2 /* words and word pairs */
#define TW_NGRAM_MAX     4

/*
 * Same as tw_featureset_builder_build(), with the longest word n-gram given:
 * 1 uses single words only, 2 adds adjacent word pairs (the default), 3 also
 * word triples, up to TW_NGRAM_MAX. Values out of range are clamped.
 */
tw_status tw_featureset_builder_build_ngram(tw_featureset_builder *builder, const char *line,
                                            tw_fold_mode fold, int ngram, tw_featureset *view,
                                            size_t *word_count);

/*
 * Builds the feature set of `line` into the builder's scratch space. `view`
 * points into that space and stays valid until the next call on the same
 * builder or until it is freed; it must not be passed to
 * tw_featureset_free(). Words are counted as in tw_featureset_build().
 */
tw_status tw_featureset_builder_build(tw_featureset_builder *builder, const char *line,
                                      tw_fold_mode fold, tw_featureset *view, size_t *word_count);

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
