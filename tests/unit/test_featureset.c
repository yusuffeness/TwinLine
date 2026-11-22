/*
 * Unit tests for src/analysis/featureset.c
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "../tw_test.h"
#include "twinline/featureset.h"

static double similarity(const char *a, const char *b, tw_fold_mode fold)
{
    tw_featureset fa;
    tw_featureset fb;
    if (tw_featureset_build(a, fold, &fa, NULL) != TW_OK)
        return -1.0;
    if (tw_featureset_build(b, fold, &fb, NULL) != TW_OK) {
        tw_featureset_free(&fa);
        return -1.0;
    }
    double j = tw_featureset_jaccard(&fa, &fb);
    tw_featureset_free(&fa);
    tw_featureset_free(&fb);
    return j;
}

TW_TEST(empty_line_has_no_features)
{
    tw_featureset set;
    size_t words = 0;
    TW_CHECK_EQ_INT(tw_featureset_build("", TW_FOLD_DEFAULT, &set, &words), TW_OK);
    TW_CHECK_EQ_SIZE(set.count, 0);
    TW_CHECK(set.hashes == NULL);
    TW_CHECK_EQ_SIZE(words, 0);

    TW_CHECK_EQ_INT(tw_featureset_build(" ,;- \xE2\x80\x94 ", TW_FOLD_DEFAULT, &set, &words),
                    TW_OK);
    TW_CHECK_EQ_SIZE(set.count, 0);
    TW_CHECK_EQ_SIZE(words, 0);
}

TW_TEST(words_and_bigrams_are_counted)
{
    tw_featureset set;
    size_t words = 0;
    TW_CHECK_EQ_INT(tw_featureset_build("hello world", TW_FOLD_DEFAULT, &set, &words), TW_OK);
    TW_CHECK_EQ_SIZE(words, 2);
    TW_CHECK_EQ_SIZE(set.count, 3); /* hello, world, hello+world */
    tw_featureset_free(&set);

    words = 0;
    TW_CHECK_EQ_INT(tw_featureset_build("a a a", TW_FOLD_DEFAULT, &set, &words), TW_OK);
    TW_CHECK_EQ_SIZE(words, 3);
    TW_CHECK_EQ_SIZE(set.count, 2); /* a, a+a (duplicates removed) */
    tw_featureset_free(&set);
}

TW_TEST(hashes_are_sorted_and_unique)
{
    tw_featureset set;
    TW_CHECK_EQ_INT(
        tw_featureset_build("the cat and the dog and the bird", TW_FOLD_DEFAULT, &set, NULL),
        TW_OK);
    TW_CHECK(set.count > 0);
    for (size_t i = 1; i < set.count; i++)
        TW_CHECK(set.hashes[i - 1] < set.hashes[i]);
    tw_featureset_free(&set);
}

TW_TEST(case_and_punctuation_are_ignored)
{
    TW_CHECK_NEAR(similarity("Hello, World!", "hello world", TW_FOLD_DEFAULT), 1.0, 1e-12);
    TW_CHECK_NEAR(similarity("\xE2\x80\x9CQuoted\xE2\x80\x9D text", "quoted TEXT", TW_FOLD_DEFAULT),
                  1.0, 1e-12);
    TW_CHECK_NEAR(similarity("\xC3\x87"
                             "AY",
                             "\xC3\xA7"
                             "ay",
                             TW_FOLD_DEFAULT),
                  1.0, 1e-12);
}

TW_TEST(word_order_matters)
{
    /* Same words, different bigram: {a, b, a+b} vs {a, b, b+a} -> 2/4. */
    TW_CHECK_NEAR(similarity("a b", "b a", TW_FOLD_DEFAULT), 0.5, 1e-12);
}

TW_TEST(turkish_folding_mode)
{
    const char *upper = "I\xC5\x9EIK";               /* ISIK with S cedilla */
    const char *lower = "\xC4\xB1\xC5\x9F\xC4\xB1k"; /* isik with dotless i */
    TW_CHECK_NEAR(similarity(upper, lower, TW_FOLD_TURKISH), 1.0, 1e-12);
    TW_CHECK(similarity(upper, lower, TW_FOLD_DEFAULT) < 1.0);

    const char *istanbul_upper = "\xC4\xB0STANBUL";
    TW_CHECK_NEAR(similarity(istanbul_upper, "istanbul", TW_FOLD_DEFAULT), 1.0, 1e-12);
    TW_CHECK_NEAR(similarity(istanbul_upper, "istanbul", TW_FOLD_TURKISH), 1.0, 1e-12);
}

TW_TEST(jaccard_of_disjoint_and_empty_sets)
{
    TW_CHECK_NEAR(similarity("alpha beta", "gamma delta", TW_FOLD_DEFAULT), 0.0, 1e-12);
    TW_CHECK_NEAR(similarity("", "", TW_FOLD_DEFAULT), 0.0, 1e-12);
    TW_CHECK_NEAR(similarity("", "word", TW_FOLD_DEFAULT), 0.0, 1e-12);
}

TW_TEST(intersection_is_symmetric)
{
    tw_featureset a;
    tw_featureset b;
    TW_CHECK_EQ_INT(tw_featureset_build("one two three four", TW_FOLD_DEFAULT, &a, NULL), TW_OK);
    TW_CHECK_EQ_INT(tw_featureset_build("two three five", TW_FOLD_DEFAULT, &b, NULL), TW_OK);
    size_t ab = tw_featureset_intersection(&a, &b);
    size_t ba = tw_featureset_intersection(&b, &a);
    tw_featureset_free(&a);
    tw_featureset_free(&b);
    TW_CHECK_EQ_SIZE(ab, ba);
    TW_CHECK_EQ_SIZE(ab, 3); /* two, three, two+three */
}

TW_TEST(word_count_accumulates)
{
    tw_featureset set;
    size_t words = 10;
    TW_CHECK_EQ_INT(tw_featureset_build("x y", TW_FOLD_DEFAULT, &set, &words), TW_OK);
    tw_featureset_free(&set);
    TW_CHECK_EQ_SIZE(words, 12);
}

static size_t ngram_count(const char *line, int ngram)
{
    tw_featureset_builder builder;
    tw_featureset_builder_init(&builder);
    tw_featureset view;
    size_t count = 0;
    if (tw_featureset_builder_build_ngram(&builder, line, TW_FOLD_DEFAULT, ngram, &view, NULL) ==
        TW_OK)
        count = view.count;
    tw_featureset_builder_free(&builder);
    return count;
}

TW_TEST(ngram_sizes)
{
    /* 4 words: 4 singles, 3 pairs, 2 triples, 1 quadruple. */
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 1), 4);
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 2), 7);
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 3), 9);
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 4), 10);
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 9), 10); /* clamped */
    TW_CHECK_EQ_SIZE(ngram_count("one two three four", 0), 4);  /* clamped */
    TW_CHECK_EQ_SIZE(ngram_count("solo", 3), 1);

    /* n = 2 is the default feature set. */
    tw_featureset a;
    TW_CHECK_EQ_INT(tw_featureset_build("one two three four", TW_FOLD_DEFAULT, &a, NULL), TW_OK);
    tw_featureset_builder builder;
    tw_featureset_builder_init(&builder);
    tw_featureset b;
    TW_CHECK_EQ_INT(tw_featureset_builder_build_ngram(&builder, "one two three four",
                                                      TW_FOLD_DEFAULT, 2, &b, NULL),
                    TW_OK);
    TW_CHECK_EQ_SIZE(a.count, b.count);
    TW_CHECK(memcmp(a.hashes, b.hashes, a.count * sizeof *a.hashes) == 0);
    tw_featureset_builder_free(&builder);
    tw_featureset_free(&a);

    /* Triples tell "a b c d" from "b c d a" better than pairs do. */
    tw_featureset_builder_init(&builder);
    tw_featureset_builder bb;
    tw_featureset_builder_init(&bb);
    tw_featureset x;
    tw_featureset y;
    TW_CHECK_EQ_INT(
        tw_featureset_builder_build_ngram(&builder, "a b c d e", TW_FOLD_DEFAULT, 3, &x, NULL),
        TW_OK);
    TW_CHECK_EQ_INT(
        tw_featureset_builder_build_ngram(&bb, "a b c d f", TW_FOLD_DEFAULT, 3, &y, NULL), TW_OK);
    TW_CHECK_EQ_SIZE(tw_featureset_intersection(&x, &y), 4 + 3 + 2);
    tw_featureset_builder_free(&builder);
    tw_featureset_builder_free(&bb);
}

int main(void)
{
    puts("featureset");
    TW_RUN(empty_line_has_no_features);
    TW_RUN(words_and_bigrams_are_counted);
    TW_RUN(hashes_are_sorted_and_unique);
    TW_RUN(case_and_punctuation_are_ignored);
    TW_RUN(word_order_matters);
    TW_RUN(turkish_folding_mode);
    TW_RUN(jaccard_of_disjoint_and_empty_sets);
    TW_RUN(intersection_is_symmetric);
    TW_RUN(word_count_accumulates);
    TW_RUN(ngram_sizes);
    return tw_test_summary();
}
