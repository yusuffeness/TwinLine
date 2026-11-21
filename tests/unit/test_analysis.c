/*
 * Unit tests for src/analysis/analysis.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "twinline/analysis.h"

static bool make_document(tw_document *doc, const char *const *lines, size_t count)
{
    tw_document_init(doc);
    for (size_t i = 0; i < count; i++) {
        if (tw_document_append(doc, lines[i]) != TW_OK) {
            tw_document_free(doc);
            return false;
        }
    }
    return true;
}

/*
 * Line 0 and 1 have identical feature sets (case and punctuation differ).
 * Line 2 changes one word: 13 shared features out of 19 -> J = 13/19.
 * Line 3 shares nothing; line 4 has no words at all.
 */
static const char *const SAMPLE[] = {
    "the quick brown fox jumps over the lazy dog",
    "The quick brown fox jumps over the lazy dog!",
    "the quick brown fox jumped over the lazy dog",
    "completely different words here",
    "",
};

TW_TEST(counts_identical_and_near_pairs)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, 5));

    tw_analysis_options options;
    tw_analysis_options_default(&options);
    options.threshold = 0.6;

    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, NULL, NULL, &r), TW_OK);
    tw_document_free(&doc);

    TW_CHECK_EQ_SIZE(r.lines_total, 5);
    TW_CHECK_EQ_SIZE(r.lines_with_words, 4);
    TW_CHECK_EQ_SIZE(r.words_total, 31);
    TW_CHECK_EQ_SIZE(r.pairs, 6);
    TW_CHECK_EQ_SIZE(r.identical, 1);
    TW_CHECK_EQ_SIZE(r.near, 2);
    TW_CHECK_NEAR(r.best_similarity, 13.0 / 19.0, 1e-12);
    TW_CHECK_EQ_SIZE(r.best_a, 1);
    TW_CHECK_EQ_SIZE(r.best_b, 3);
    TW_CHECK_EQ_STR(r.best_a_text, SAMPLE[0]);
    TW_CHECK_NEAR(r.threshold, 0.6, 1e-12);
}

TW_TEST(default_threshold_excludes_weaker_pairs)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, 5));

    tw_analysis_options options;
    tw_analysis_options_default(&options);
    TW_CHECK_NEAR(options.threshold, TW_DEFAULT_THRESHOLD, 1e-12);

    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, NULL, NULL, &r), TW_OK);
    tw_document_free(&doc);
    TW_CHECK_EQ_SIZE(r.near, 0); /* 13/19 = 0.684 < 0.70 */
}

TW_TEST(empty_document)
{
    tw_document doc;
    tw_document_init(&doc);
    tw_analysis_options options;
    tw_analysis_options_default(&options);

    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, NULL, NULL, &r), TW_OK);
    TW_CHECK_EQ_SIZE(r.pairs, 0);
    TW_CHECK_NEAR(r.mean_similarity, 0.0, 1e-12);
    TW_CHECK_EQ_SIZE(r.best_a, 0);
}

TW_TEST(reports_progress)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, 5));
    tw_analysis_options options;
    tw_analysis_options_default(&options);

    tw_analysis_progress progress;
    atomic_init(&progress.pairs_done, 0);
    atomic_init(&progress.pairs_total, 0);

    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, NULL, &progress, &r), TW_OK);
    tw_document_free(&doc);
    TW_CHECK_EQ_SIZE(atomic_load(&progress.pairs_total), 6);
    TW_CHECK_EQ_SIZE(atomic_load(&progress.pairs_done), 6);
    TW_CHECK_NEAR(tw_analysis_progress_fraction(&progress), 1.0, 1e-12);

    tw_analysis_progress_reset(&progress);
    TW_CHECK_NEAR(tw_analysis_progress_fraction(&progress), 0.0, 1e-12);
}

TW_TEST(honours_cancellation)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, 5));
    tw_analysis_options options;
    tw_analysis_options_default(&options);

    atomic_bool cancel;
    atomic_init(&cancel, true);
    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, &cancel, NULL, &r), TW_ERR_CANCELLED);
    tw_document_free(&doc);
}

TW_TEST(long_lines_are_shortened_in_the_report)
{
    /* 80 bytes: 30 x "a " then a 2-byte letter straddling the 70-byte cut. */
    char line[128];
    memset(line, 0, sizeof line);
    for (int i = 0; i < 34; i++)
        memcpy(line + 2 * i, "a ", 2);
    memcpy(line + 68,
           "b\xC3\xA7"
           "cdefgh",
           9);

    const char *const lines[] = {line, line};
    char other[128];
    memcpy(other, line, sizeof other);
    other[0] = 'z'; /* make the pair distinct but similar */
    const char *const pair[] = {lines[0], other};

    tw_document doc;
    TW_CHECK(make_document(&doc, pair, 2));
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &options, NULL, NULL, &r), TW_OK);
    tw_document_free(&doc);

    size_t len = strlen(r.best_a_text);
    TW_CHECK(len <= TW_SNIPPET_BYTES + 3);
    TW_CHECK(strcmp(r.best_a_text + len - 3, "...") == 0);
    /* The cut falls inside the two-byte letter, so it must be dropped whole. */
    TW_CHECK_EQ_SIZE(len - 3, 69);
}

int main(void)
{
    puts("analysis");
    TW_RUN(counts_identical_and_near_pairs);
    TW_RUN(default_threshold_excludes_weaker_pairs);
    TW_RUN(empty_document);
    TW_RUN(reports_progress);
    TW_RUN(honours_cancellation);
    TW_RUN(long_lines_are_shortened_in_the_report);
    return tw_test_summary();
}
