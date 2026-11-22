/*
 * Similar-line search with clusters: the engine behind the exact and
 * minhash strategies, "clusters", "dedupe" and "export".
 *
 * Lines with identical feature sets are first collapsed into one distinct
 * text each (a hash table, linear time). Pairs of distinct texts with a
 * Jaccard similarity of at least the threshold are then found with the
 * chosen strategy:
 *
 *   jaccard  every pair, with the exact size-ratio pruning (exact result);
 *   minhash  candidates from MinHash signatures and LSH bands, each one
 *            scored exactly (no false positives, rare misses);
 *   exact    none: only identical lines are grouped.
 *
 * Union-find over those pairs and the identical groups gives the clusters:
 * sets of lines that are connected by similarity, directly or through
 * other lines.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_SIMILARITY_H
#define TWINLINE_SIMILARITY_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "twinline/analysis.h"
#include "twinline/document.h"
#include "twinline/status.h"

/* MinHash parameters: TW_MINHASH_BANDS bands of TW_MINHASH_ROWS values.
 * A pair with similarity J becomes a candidate with probability
 * 1 - (1 - J^rows)^bands: 99.98% at J = 0.7, 87% at J = 0.5. */
#define TW_MINHASH_BANDS 32
#define TW_MINHASH_ROWS  4

/* Two distinct texts that are similar. Lines are 0-based; for texts that
 * occur several times, the first occurrence stands for all. */
typedef struct tw_pair {
    size_t a; /* a < b */
    size_t b;
    double similarity;
} tw_pair;

typedef struct tw_similarity {
    tw_strategy strategy;
    double threshold;
    size_t threads;
    size_t lines_total;
    size_t lines_with_words;
    size_t words_total;
    size_t unique_sets; /* distinct texts (feature sets) */

    unsigned long long identical_pairs; /* line pairs with identical sets */
    unsigned long long near_pairs;      /* line pairs with threshold <= J < 1 that were found */
    unsigned long long candidates;      /* pairs of distinct texts that were scored */

    /* Pairs of distinct texts with J >= threshold, most similar first. */
    tw_pair *pairs;
    size_t pair_count;

    /* Most similar pair of distinct texts that was scored (J < 1); 0-based. */
    double best_similarity; /* 0 when none */
    size_t best_a;
    size_t best_b;

    /* Clusters, largest first: cluster k is the ascending 0-based lines
     * cluster_lines[cluster_start[k]] .. cluster_lines[cluster_start[k + 1] - 1]. */
    size_t cluster_count;
    size_t *cluster_start; /* cluster_count + 1 entries */
    size_t *cluster_lines;

    double seconds;
} tw_similarity;

/*
 * Finds the similar lines of `doc`. `options->exhaustive` is ignored.
 * `cancel` and `progress` (both may be NULL) work as for tw_analysis_run().
 * On success `out` owns memory; release it with tw_similarity_free().
 */
tw_status tw_similarity_run(const tw_document *doc, const tw_analysis_options *options,
                            atomic_bool *cancel, tw_analysis_progress *progress,
                            tw_similarity *out);

void tw_similarity_free(tw_similarity *similarity);

/* Number of lines in cluster `k`. */
size_t tw_similarity_cluster_size(const tw_similarity *similarity, size_t k);

/* Fills the summary fields of an analysis result (counts, best pair with
 * snippets) from a similarity search over `doc`. */
void tw_similarity_to_result(const tw_similarity *similarity, const tw_document *doc,
                             tw_analysis_result *out);

/* Jaccard similarity of two lines with the given features (1 when both have
 * no words); -1 when out of memory. */
double tw_line_similarity(const char *a, const char *b, tw_fold_mode fold, int ngram);

#endif /* TWINLINE_SIMILARITY_H */
