/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/report.h"

void tw_report_print(FILE *out, const char *title, const tw_analysis_result *r)
{
    fprintf(out, "%s\n", title);
    fprintf(out, "  Lines                       : %zu (%zu with words)\n", r->lines_total,
            r->lines_with_words);
    fprintf(out, "  Words                       : %zu\n", r->words_total);
    fprintf(out, "  Line pairs                  : %llu\n", r->pairs);
    switch (r->strategy) {
    case TW_STRATEGY_EXACT:
        fprintf(out, "  Strategy                    : exact (identical lines only)\n");
        fprintf(out, "  Distinct lines              : %zu\n", r->unique_lines);
        fprintf(out, "  Identical pairs             : %llu\n", r->identical);
        break;
    case TW_STRATEGY_MINHASH:
        fprintf(out, "  Strategy                    : minhash (near pairs may be missed)\n");
        fprintf(out, "  Distinct lines              : %zu\n", r->unique_lines);
        fprintf(out, "  Candidates scored           : %llu\n", r->compared);
        fprintf(out, "  Identical pairs             : %llu\n", r->identical);
        fprintf(out, "  Near-duplicate pairs (>=%.2f): %llu found\n", r->threshold, r->near);
        break;
    case TW_STRATEGY_JACCARD:
        if (r->exhaustive)
            fprintf(out, "  Pairs compared              : %llu (exhaustive)\n", r->compared);
        else
            fprintf(out, "  Pairs compared              : %llu (%llu skipped by the size bound)\n",
                    r->compared, r->pairs - r->compared);
        fprintf(out, "  Identical pairs             : %llu\n", r->identical);
        fprintf(out, "  Near-duplicate pairs (>=%.2f): %llu\n", r->threshold, r->near);
        break;
    }
    if (r->exhaustive)
        fprintf(out, "  Mean Jaccard similarity     : %.4f\n", r->mean_similarity);
    if (r->best_similarity > 0.0) {
        fprintf(out, "  Most similar distinct pair  : lines %zu and %zu (J = %.3f)\n", r->best_a,
                r->best_b, r->best_similarity);
        fprintf(out, "    %zu: %s\n", r->best_a, r->best_a_text);
        fprintf(out, "    %zu: %s\n", r->best_b, r->best_b_text);
    }
    fprintf(out, "Finished in %.3f s with %zu thread%s\n", r->seconds, r->threads,
            r->threads == 1 ? "" : "s");
}
