/*
 * Unit tests for src/analysis/similarity.c (strategies, clusters),
 * src/cmd/export.c and src/cmd/settings.c
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "../tw_test.h"
#include "cmd/export.h"
#include "cmd/settings.h"
#include "twinline/similarity.h"

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

/* Lines 0, 2 and 6 are the same text. 0-3 and 3-5 share 13 of 15 features
 * (J = 0.87), 0-5 shares 11 of 15 (J = 0.73). */
static const char *const SAMPLE[] = {
    "alpha beta gamma delta epsilon zeta eta",       /* 0 */
    "completely unrelated words in this line",       /* 1 */
    "Alpha beta gamma delta epsilon zeta eta!",      /* 2 */
    "alpha beta gamma delta epsilon zeta eta theta", /* 3 */
    "one two three four five six",                   /* 4 */
    "beta gamma delta epsilon zeta eta theta",       /* 5 */
    "alpha beta gamma delta epsilon zeta eta",       /* 6 */
    "",                                              /* 7: no words */
};
#define SAMPLE_COUNT (sizeof SAMPLE / sizeof SAMPLE[0])

static tw_analysis_options options_for(tw_strategy strategy, size_t threads)
{
    tw_analysis_options o;
    tw_analysis_options_default(&o);
    o.strategy = strategy;
    o.threads = threads;
    return o;
}

TW_TEST(jaccard_groups_and_clusters)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, SAMPLE_COUNT));
    tw_analysis_options o = options_for(TW_STRATEGY_JACCARD, 1);
    tw_similarity s;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_OK);

    TW_CHECK_EQ_SIZE(s.lines_total, 8);
    TW_CHECK_EQ_SIZE(s.lines_with_words, 7);
    TW_CHECK_EQ_SIZE(s.unique_sets, 5);
    TW_CHECK(s.identical_pairs == 3); /* lines 0, 2, 6 */
    /* Distinct-text pairs (0,3), (3,5), (0,5); as line pairs, the three
     * copies of line 0 count three times: 3 + 1 + 3. */
    TW_CHECK_EQ_SIZE(s.pair_count, 3);
    TW_CHECK(s.near_pairs == 7);
    TW_CHECK_EQ_SIZE(s.pairs[0].a, 0); /* most similar first, then by line */
    TW_CHECK_EQ_SIZE(s.pairs[0].b, 3);
    TW_CHECK_EQ_SIZE(s.pairs[1].a, 3);
    TW_CHECK_EQ_SIZE(s.pairs[1].b, 5);
    TW_CHECK(s.pairs[0].similarity == s.pairs[1].similarity);
    TW_CHECK_EQ_SIZE(s.pairs[2].a, 0);
    TW_CHECK_EQ_SIZE(s.pairs[2].b, 5);
    TW_CHECK(s.pairs[2].similarity < s.pairs[1].similarity);
    TW_CHECK_EQ_SIZE(s.best_a, 0);
    TW_CHECK_EQ_SIZE(s.best_b, 3);

    /* One cluster: 0, 2, 3, 5, 6. */
    TW_CHECK_EQ_SIZE(s.cluster_count, 1);
    TW_CHECK_EQ_SIZE(tw_similarity_cluster_size(&s, 0), 5);
    static const size_t expected[] = {0, 2, 3, 5, 6};
    for (size_t i = 0; i < 5; i++)
        TW_CHECK_EQ_SIZE(s.cluster_lines[s.cluster_start[0] + i], expected[i]);
    TW_CHECK_EQ_SIZE(tw_similarity_cluster_size(&s, 1), 0);

    tw_similarity_free(&s);
    tw_document_free(&doc);
}

TW_TEST(exact_finds_identical_lines_only)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, SAMPLE_COUNT));
    tw_analysis_options o = options_for(TW_STRATEGY_EXACT, 2);
    tw_similarity s;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_OK);
    TW_CHECK(s.identical_pairs == 3);
    TW_CHECK_EQ_SIZE(s.pair_count, 0);
    TW_CHECK(s.candidates == 0);
    TW_CHECK_EQ_SIZE(s.cluster_count, 1);
    TW_CHECK_EQ_SIZE(tw_similarity_cluster_size(&s, 0), 3);
    tw_similarity_free(&s);
    tw_document_free(&doc);
}

TW_TEST(minhash_matches_jaccard_on_clear_cases)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, SAMPLE_COUNT));
    tw_analysis_options o = options_for(TW_STRATEGY_MINHASH, 2);
    tw_similarity s;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_OK);
    TW_CHECK(s.identical_pairs == 3);
    TW_CHECK_EQ_SIZE(s.pair_count, 3); /* J >= 0.73: found with near certainty */
    TW_CHECK_EQ_SIZE(s.cluster_count, 1);
    TW_CHECK_EQ_SIZE(tw_similarity_cluster_size(&s, 0), 5);
    tw_similarity_free(&s);
    tw_document_free(&doc);
}

/* A larger document: the pairs and clusters do not depend on the number of
 * threads, minhash never reports a pair that jaccard does not, and the
 * summary agrees with tw_analysis_run(). */
TW_TEST(strategies_agree_on_a_generated_document)
{
    static const char *const WORDS[] = {"red",  "green", "blue", "cat",  "dog",   "bird",  "sun",
                                        "moon", "star",  "tree", "rock", "river", "cloud", "rain"};
    tw_document doc;
    tw_document_init(&doc);
    unsigned seed = 7;
    char line[256];
    for (int i = 0; i < 1500; i++) {
        size_t len = 0;
        int words = 4 + (int)(seed % 6);
        for (int w = 0; w < words; w++) {
            seed = seed * 1103515245u + 12345u;
            len += (size_t)snprintf(line + len, sizeof line - len, "%s%s", w ? " " : "",
                                    WORDS[(seed >> 16) % 14]);
        }
        TW_CHECK_EQ_INT(tw_document_append(&doc, line), TW_OK);
    }

    tw_analysis_options one = options_for(TW_STRATEGY_JACCARD, 1);
    tw_analysis_options four = options_for(TW_STRATEGY_JACCARD, 4);
    tw_analysis_options mh = options_for(TW_STRATEGY_MINHASH, 3);
    tw_similarity a;
    tw_similarity b;
    tw_similarity m;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &one, NULL, NULL, &a), TW_OK);
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &four, NULL, NULL, &b), TW_OK);
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &mh, NULL, NULL, &m), TW_OK);

    TW_CHECK(a.pair_count > 0);
    TW_CHECK_EQ_SIZE(a.pair_count, b.pair_count);
    TW_CHECK_EQ_SIZE(a.cluster_count, b.cluster_count);
    bool same = true;
    for (size_t i = 0; i < a.pair_count && i < b.pair_count; i++)
        same = same && a.pairs[i].a == b.pairs[i].a && a.pairs[i].b == b.pairs[i].b;
    TW_CHECK(same);
    TW_CHECK(memcmp(a.cluster_lines, b.cluster_lines,
                    a.cluster_start[a.cluster_count] * sizeof(size_t)) == 0);

    TW_CHECK(m.identical_pairs == a.identical_pairs);
    TW_CHECK(m.pair_count <= a.pair_count);
    TW_CHECK(m.pair_count * 10 >= a.pair_count * 9); /* recall of at least 90% */

    /* tw_analysis_run() (the original jaccard search) agrees. */
    tw_analysis_result r;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &one, NULL, NULL, &r), TW_OK);
    TW_CHECK(r.identical == a.identical_pairs);
    TW_CHECK(r.near == a.near_pairs);
    TW_CHECK_EQ_SIZE(r.best_a, a.best_a + 1);
    TW_CHECK_EQ_SIZE(r.best_b, a.best_b + 1);

    /* And so does the minhash path of tw_analysis_run(). */
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &mh, NULL, NULL, &r), TW_OK);
    TW_CHECK_EQ_INT(r.strategy, TW_STRATEGY_MINHASH);
    TW_CHECK(r.identical == m.identical_pairs);
    TW_CHECK(r.near == m.near_pairs);
    TW_CHECK_EQ_SIZE(r.unique_lines, m.unique_sets);

    tw_similarity_free(&a);
    tw_similarity_free(&b);
    tw_similarity_free(&m);
    tw_document_free(&doc);
}

/* Large enough for the index to be built by several threads: the result
 * must be the same as with one. */
TW_TEST(parallel_index_gives_the_same_result)
{
    tw_document doc;
    tw_document_init(&doc);
    char line[96];
    for (int i = 0; i < 9000; i++) {
        /* Every 7th line repeats an earlier one; every 5th differs by one word. */
        int base = i % 7 == 0 && i > 0 ? i - 7 : i;
        snprintf(line, sizeof line, "item %d alpha beta gamma %s delta %d", base / 5,
                 i % 5 == 0 ? "changed" : "kept", base / 5 + 3);
        TW_CHECK_EQ_INT(tw_document_append(&doc, line), TW_OK);
    }
    tw_analysis_options one = options_for(TW_STRATEGY_JACCARD, 1);
    tw_analysis_options many = options_for(TW_STRATEGY_JACCARD, 4);
    tw_similarity a;
    tw_similarity b;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &one, NULL, NULL, &a), TW_OK);
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &many, NULL, NULL, &b), TW_OK);
    TW_CHECK_EQ_SIZE(a.words_total, b.words_total);
    TW_CHECK_EQ_SIZE(a.unique_sets, b.unique_sets);
    TW_CHECK(a.identical_pairs == b.identical_pairs);
    TW_CHECK_EQ_SIZE(a.pair_count, b.pair_count);
    TW_CHECK_EQ_SIZE(a.cluster_count, b.cluster_count);
    TW_CHECK(a.cluster_count > 0);
    TW_CHECK(memcmp(a.cluster_lines, b.cluster_lines,
                    a.cluster_start[a.cluster_count] * sizeof(size_t)) == 0);

    tw_analysis_result ra;
    tw_analysis_result rb;
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &one, NULL, NULL, &ra), TW_OK);
    TW_CHECK_EQ_INT(tw_analysis_run(&doc, &many, NULL, NULL, &rb), TW_OK);
    TW_CHECK(ra.identical == rb.identical && ra.near == rb.near);
    TW_CHECK_EQ_SIZE(ra.words_total, rb.words_total);
    tw_similarity_free(&a);
    tw_similarity_free(&b);
    tw_document_free(&doc);
}

TW_TEST(cancel_and_bad_threshold)
{
    tw_document doc;
    TW_CHECK(make_document(&doc, SAMPLE, SAMPLE_COUNT));
    tw_analysis_options o = options_for(TW_STRATEGY_MINHASH, 1);
    tw_similarity s;
    atomic_bool cancel = true;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, &cancel, NULL, &s), TW_ERR_CANCELLED);
    o.threshold = 0.0;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_ERR_INVALID);

    /* An empty document has nothing to report. */
    tw_document empty;
    tw_document_init(&empty);
    o.threshold = 0.7;
    TW_CHECK_EQ_INT(tw_similarity_run(&empty, &o, NULL, NULL, &s), TW_OK);
    TW_CHECK_EQ_SIZE(s.cluster_count, 0);
    tw_similarity_free(&s);
    tw_document_free(&doc);
}

TW_TEST(line_similarity)
{
    TW_CHECK(tw_line_similarity("a b c", "A B C!", TW_FOLD_DEFAULT, 2) == 1.0);
    TW_CHECK(tw_line_similarity("", "", TW_FOLD_DEFAULT, 2) == 1.0);
    TW_CHECK(tw_line_similarity("a b", "c d", TW_FOLD_DEFAULT, 2) == 0.0);
    /* Word order only counts through the n-grams. */
    TW_CHECK(tw_line_similarity("a b c", "c b a", TW_FOLD_DEFAULT, 1) == 1.0);
    TW_CHECK(tw_line_similarity("a b c", "c b a", TW_FOLD_DEFAULT, 2) < 1.0);
}

/* ------------------------------------------------------------------ export */

static char *export_text(tw_export_format format, const tw_document *doc, const tw_similarity *s,
                         const tw_analysis_options *o, const char *source)
{
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    if (out == NULL)
        return NULL;
    bool ok = tw_export_write(out, format, doc, s, o, source);
    fclose(out);
    if (!ok) {
        free(text);
        return NULL;
    }
    return text;
}

TW_TEST(export_formats)
{
    tw_export_format f;
    TW_CHECK(tw_export_format_for("out.json", &f) && f == TW_EXPORT_JSON);
    TW_CHECK(tw_export_format_for("dir.x/REPORT.CSV", &f) && f == TW_EXPORT_CSV);
    TW_CHECK(tw_export_format_for("r.htm", &f) && f == TW_EXPORT_HTML);
    TW_CHECK(tw_export_format_for("r.html", &f) && f == TW_EXPORT_HTML);
    TW_CHECK(!tw_export_format_for("r.txt", &f));
    TW_CHECK(!tw_export_format_for("noextension", &f));
    TW_CHECK(!tw_export_format_for("dir.json/file", &f));
    TW_CHECK(!tw_export_format_for("r.", &f));
}

TW_TEST(export_escapes_text)
{
    static const char *const LINES[] = {
        "say \"hi\", <b>then</b> & go",
        "say \"hi\", <b>then</b> & go now",
        "tab\there",
    };
    tw_document doc;
    TW_CHECK(make_document(&doc, LINES, 3));
    tw_analysis_options o = options_for(TW_STRATEGY_JACCARD, 1);
    o.threshold = 0.5;
    tw_similarity s;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_OK);
    TW_CHECK_EQ_SIZE(s.cluster_count, 1);

    char *json = export_text(TW_EXPORT_JSON, &doc, &s, &o, "a \"b\".txt");
    TW_CHECK(json != NULL);
    TW_CHECK(strstr(json, "\"file\": \"a \\\"b\\\".txt\"") != NULL);
    TW_CHECK(strstr(json, "\"text\": \"say \\\"hi\\\", <b>then</b> & go\"") != NULL);
    TW_CHECK(strstr(json, "\"clusters\": 1") != NULL);
    TW_CHECK(strstr(json, "\"pairs_truncated\": false") != NULL);
    free(json);

    char *csv = export_text(TW_EXPORT_CSV, &doc, &s, &o, NULL);
    TW_CHECK(csv != NULL);
    TW_CHECK(strncmp(csv, "cluster,size,line,similarity,text\r\n", 35) == 0);
    TW_CHECK(strstr(csv, "1,2,1,1.0000,\"say \"\"hi\"\", <b>then</b> & go\"\r\n") != NULL);
    free(csv);

    char *html = export_text(TW_EXPORT_HTML, &doc, &s, &o, "<notes>");
    TW_CHECK(html != NULL);
    TW_CHECK(strncmp(html, "<!DOCTYPE html>", 15) == 0);
    TW_CHECK(strstr(html, "&lt;notes&gt;") != NULL);
    TW_CHECK(strstr(html, "say &quot;hi&quot;, &lt;b&gt;then&lt;/b&gt; &amp; go") != NULL);
    TW_CHECK(strstr(html, "<mark class=\"add\">now</mark>") != NULL);
    TW_CHECK(strstr(html, "<b>then") == NULL); /* never unescaped */
    free(html);

    /* No clusters: valid, empty lists. */
    tw_similarity_free(&s);
    o.strategy = TW_STRATEGY_EXACT;
    TW_CHECK_EQ_INT(tw_similarity_run(&doc, &o, NULL, NULL, &s), TW_OK);
    json = export_text(TW_EXPORT_JSON, &doc, &s, &o, NULL);
    TW_CHECK(json != NULL);
    TW_CHECK(strstr(json, "\"file\": null") != NULL);
    TW_CHECK(strstr(json, "\"clusters\": [],") != NULL);
    TW_CHECK(strstr(json, "\"pairs\": [],") != NULL);
    free(json);
    html = export_text(TW_EXPORT_HTML, &doc, &s, &o, NULL);
    TW_CHECK(html != NULL);
    TW_CHECK(strstr(html, "No similar lines were found.") != NULL);
    free(html);

    tw_similarity_free(&s);
    tw_document_free(&doc);
}

/* ---------------------------------------------------------------- settings */

TW_TEST(settings_by_name)
{
    tw_analysis_options o;
    tw_analysis_options_default(&o);
    char msg[200];
    TW_CHECK(tw_setting_set(&o, "threshold", "0.85", msg, sizeof msg));
    TW_CHECK(o.threshold == 0.85);
    TW_CHECK(tw_setting_set(&o, "threads", "8", msg, sizeof msg));
    TW_CHECK_EQ_SIZE(o.threads, 8);
    TW_CHECK(tw_setting_set(&o, "threads", "auto", msg, sizeof msg));
    TW_CHECK_EQ_SIZE(o.threads, 0);
    TW_CHECK(tw_setting_set(&o, "strategy", "minhash", msg, sizeof msg));
    TW_CHECK_EQ_INT(o.strategy, TW_STRATEGY_MINHASH);
    TW_CHECK(tw_setting_set(&o, "exhaustive", "yes", msg, sizeof msg));
    TW_CHECK(o.exhaustive);
    TW_CHECK(tw_setting_set(&o, "fold", "turkish", msg, sizeof msg));
    TW_CHECK_EQ_INT(o.fold, TW_FOLD_TURKISH);
    TW_CHECK(tw_setting_set(&o, "ngram", "3", msg, sizeof msg));
    TW_CHECK_EQ_INT(o.ngram, 3);

    TW_CHECK(!tw_setting_set(&o, "threshold", "0", msg, sizeof msg));
    TW_CHECK(!tw_setting_set(&o, "threshold", "0.5x", msg, sizeof msg));
    TW_CHECK(!tw_setting_set(&o, "threads", "257", msg, sizeof msg));
    TW_CHECK(!tw_setting_set(&o, "ngram", "0", msg, sizeof msg));
    TW_CHECK(!tw_setting_set(&o, "colour", "red", msg, sizeof msg));
    TW_CHECK(strstr(msg, "unknown setting 'colour'") != NULL);
    TW_CHECK(o.threshold == 0.85); /* failures change nothing */

    tw_strategy st;
    TW_CHECK(tw_strategy_parse("exact", &st) && st == TW_STRATEGY_EXACT);
    TW_CHECK(!tw_strategy_parse("EXACT", &st));
    TW_CHECK_EQ_STR(tw_strategy_name(TW_STRATEGY_JACCARD), "jaccard");
}

int main(void)
{
    puts("similarity");
    TW_RUN(jaccard_groups_and_clusters);
    TW_RUN(exact_finds_identical_lines_only);
    TW_RUN(minhash_matches_jaccard_on_clear_cases);
    TW_RUN(strategies_agree_on_a_generated_document);
    TW_RUN(parallel_index_gives_the_same_result);
    TW_RUN(cancel_and_bad_threshold);
    TW_RUN(line_similarity);
    TW_RUN(export_formats);
    TW_RUN(export_escapes_text);
    TW_RUN(settings_by_name);
    return tw_test_summary();
}
