/*
 * bench_accuracy - how much MinHash + LSH finds, and how fast.
 *
 * For each file, the similar pairs of distinct texts are found twice: with
 * the exact jaccard strategy (the ground truth) and with minhash. The report
 * shows the time of each, the recall of minhash (the share of true pairs it
 * found) and its precision (the share of its pairs that are true; always 1,
 * since every candidate is scored exactly). The exact strategy and the
 * identical-line counts are checked as well.
 *
 * Usage:
 *   bench_accuracy [--threshold=0.7] [--threads=N] [--csv=FILE] [--skip-jaccard] FILE...
 *
 * --skip-jaccard times minhash alone, for files too large for the exact search.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twinline/fileio.h"
#include "twinline/similarity.h"

/* Number of pairs in both sorted lists (same order as tw_similarity.pairs
 * is not needed: both are re-sorted by line numbers). */
static int compare_lines(const void *lhs, const void *rhs)
{
    const tw_pair *a = lhs;
    const tw_pair *b = rhs;
    if (a->a != b->a)
        return a->a < b->a ? -1 : 1;
    return (a->b > b->b) - (a->b < b->b);
}

static size_t common_pairs(tw_pair *x, size_t nx, tw_pair *y, size_t ny)
{
    qsort(x, nx, sizeof *x, compare_lines);
    qsort(y, ny, sizeof *y, compare_lines);
    size_t i = 0;
    size_t j = 0;
    size_t common = 0;
    while (i < nx && j < ny) {
        int c = compare_lines(&x[i], &y[j]);
        if (c == 0) {
            common++;
            i++;
            j++;
        } else if (c < 0) {
            i++;
        } else {
            j++;
        }
    }
    return common;
}

static bool run(const tw_document *doc, tw_analysis_options options, tw_strategy strategy,
                tw_similarity *out)
{
    options.strategy = strategy;
    tw_status status = tw_similarity_run(doc, &options, NULL, NULL, out);
    if (status != TW_OK) {
        fprintf(stderr, "bench_accuracy: %s failed: %s\n", tw_strategy_name(strategy),
                tw_status_str(status));
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    const char *csv_path = NULL;
    bool skip_jaccard = false;
    int first = 1;
    for (; first < argc && strncmp(argv[first], "--", 2) == 0; first++) {
        const char *arg = argv[first];
        if (strncmp(arg, "--threshold=", 12) == 0)
            options.threshold = strtod(arg + 12, NULL);
        else if (strncmp(arg, "--threads=", 10) == 0)
            options.threads = (size_t)strtoul(arg + 10, NULL, 10);
        else if (strncmp(arg, "--csv=", 6) == 0)
            csv_path = arg + 6;
        else if (strcmp(arg, "--skip-jaccard") == 0)
            skip_jaccard = true;
        else
            first = argc; /* unknown option: print the usage */
    }
    if (first >= argc || !(options.threshold > 0.0 && options.threshold <= 1.0)) {
        fprintf(stderr, "usage: bench_accuracy [--threshold=0.7] [--threads=N] [--csv=FILE] "
                        "[--skip-jaccard] FILE...\n");
        return 2;
    }

    FILE *csv = NULL;
    if (csv_path != NULL) {
        csv = fopen(csv_path, "w");
        if (csv == NULL) {
            perror(csv_path);
            return 1;
        }
        fprintf(csv, "file,lines,unique,true_pairs,jaccard_s,minhash_s,minhash_pairs,recall,"
                     "precision,exact_s\n");
    }

    printf("threshold %.2f, %d bands x %d rows\n\n", options.threshold, TW_MINHASH_BANDS,
           TW_MINHASH_ROWS);
    printf("%-24s %9s %9s %10s %10s %10s %8s %9s %9s\n", "file", "lines", "unique", "pairs",
           "jaccard s", "minhash s", "recall", "precision", "exact s");

    int exit_code = 0;
    for (int f = first; f < argc; f++) {
        tw_document doc;
        int err_no = 0;
        if (tw_file_load(argv[f], &doc, &err_no) != TW_OK) {
            fprintf(stderr, "bench_accuracy: cannot load '%s'\n", argv[f]);
            return 1;
        }

        tw_similarity truth = {0};
        tw_similarity approx;
        tw_similarity exact;
        if ((!skip_jaccard && !run(&doc, options, TW_STRATEGY_JACCARD, &truth)) ||
            !run(&doc, options, TW_STRATEGY_MINHASH, &approx) ||
            !run(&doc, options, TW_STRATEGY_EXACT, &exact)) {
            tw_document_free(&doc);
            return 1;
        }

        /* Every strategy must agree on the identical lines. */
        if (exact.identical_pairs != approx.identical_pairs ||
            (!skip_jaccard && truth.identical_pairs != exact.identical_pairs)) {
            fprintf(stderr, "bench_accuracy: identical pair counts differ on '%s'\n", argv[f]);
            exit_code = 1;
        }

        size_t found = approx.pair_count;
        if (skip_jaccard) {
            printf("%-24s %9zu %9zu %10s %10s %10.3f %8s %9s %9.3f\n", argv[f], doc.count,
                   approx.unique_sets, "-", "-", approx.seconds, "-", "-", exact.seconds);
            if (csv != NULL)
                fprintf(csv, "%s,%zu,%zu,,,%.4f,%zu,,,%.4f\n", argv[f], doc.count,
                        approx.unique_sets, approx.seconds, found, exact.seconds);
        } else {
            size_t both = common_pairs(truth.pairs, truth.pair_count, approx.pairs, found);
            double recall = truth.pair_count ? (double)both / (double)truth.pair_count : 1.0;
            double precision = found ? (double)both / (double)found : 1.0;
            if (precision < 1.0) {
                fprintf(stderr, "bench_accuracy: minhash reported a false pair on '%s'\n", argv[f]);
                exit_code = 1;
            }
            printf("%-24s %9zu %9zu %10zu %10.3f %10.3f %7.2f%% %8.2f%% %9.3f\n", argv[f],
                   doc.count, truth.unique_sets, truth.pair_count, truth.seconds, approx.seconds,
                   100.0 * recall, 100.0 * precision, exact.seconds);
            if (csv != NULL)
                fprintf(csv, "%s,%zu,%zu,%zu,%.4f,%.4f,%zu,%.6f,%.6f,%.4f\n", argv[f], doc.count,
                        truth.unique_sets, truth.pair_count, truth.seconds, approx.seconds, found,
                        recall, precision, exact.seconds);
        }

        tw_similarity_free(&truth);
        tw_similarity_free(&approx);
        tw_similarity_free(&exact);
        tw_document_free(&doc);
    }
    if (csv != NULL)
        fclose(csv);
    return exit_code;
}
