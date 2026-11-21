/*
 * Unit tests for src/tui/complete.c (fuzzy matching and file completion).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../tw_test.h"
#include "tui/complete.h"

TW_TEST(fuzzy_matches_subsequences)
{
    int score = 0;
    uint64_t bits = 0;
    TW_CHECK(tw_fuzzy_match("sh", "show", &score, &bits));
    TW_CHECK_EQ_SIZE(bits, 0x3); /* s and h */
    TW_CHECK(tw_fuzzy_match("az", "analyze-sync", &score, &bits));
    TW_CHECK(tw_fuzzy_match("AN", "analyze", &score, &bits)); /* case-insensitive */
    TW_CHECK(!tw_fuzzy_match("sx", "show", &score, &bits));
    TW_CHECK(!tw_fuzzy_match("ba", "abc", &score, &bits)); /* order matters */
    TW_CHECK(tw_fuzzy_match("", "anything", &score, &bits));
    TW_CHECK_EQ_INT(score, 0);
}

TW_TEST(fuzzy_prefers_prefixes_and_runs)
{
    int prefix = 0;
    int scattered = 0;
    uint64_t bits;
    TW_CHECK(tw_fuzzy_match("sa", "save", &prefix, &bits));
    TW_CHECK(tw_fuzzy_match("sa", "status", &scattered, &bits));
    TW_CHECK(prefix > scattered);

    int word_start = 0;
    TW_CHECK(tw_fuzzy_match("s", "analyze-sync", &word_start, &bits));
    int mid_word = 0;
    TW_CHECK(tw_fuzzy_match("y", "analyze-sync", &mid_word, &bits));
    TW_CHECK(word_start > mid_word);
}

static char dir[256];

static void touch(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (f != NULL)
        fclose(f);
}

TW_TEST(completes_paths)
{
    char prefix[512];
    tw_completions c;

    snprintf(prefix, sizeof prefix, "%s/dr", dir);
    TW_CHECK_EQ_INT(tw_complete_path(prefix, &c), TW_OK);
    TW_CHECK_EQ_SIZE(c.count, 3); /* draft-one.txt, draft-two.txt, drafts/ */
    char expected[512];
    snprintf(expected, sizeof expected, "%s/drafts/", dir);
    TW_CHECK_EQ_STR(c.items[2], expected);
    snprintf(expected, sizeof expected, "%s/draft", dir);
    TW_CHECK_EQ_SIZE(tw_completions_common_prefix(&c), strlen(expected));
    tw_completions_free(&c);

    snprintf(prefix, sizeof prefix, "%s/fi", dir);
    TW_CHECK_EQ_INT(tw_complete_path(prefix, &c), TW_OK);
    TW_CHECK_EQ_SIZE(c.count, 1);
    tw_completions_free(&c);

    snprintf(prefix, sizeof prefix, "%s/", dir);
    TW_CHECK_EQ_INT(tw_complete_path(prefix, &c), TW_OK);
    TW_CHECK_EQ_SIZE(c.count, 4); /* the hidden file is skipped */
    tw_completions_free(&c);

    snprintf(prefix, sizeof prefix, "%s/.h", dir);
    TW_CHECK_EQ_INT(tw_complete_path(prefix, &c), TW_OK);
    TW_CHECK_EQ_SIZE(c.count, 1);
    tw_completions_free(&c);

    TW_CHECK_EQ_INT(tw_complete_path("/no/such/dir/x", &c), TW_OK);
    TW_CHECK_EQ_SIZE(c.count, 0);
    TW_CHECK_EQ_SIZE(tw_completions_common_prefix(&c), 0);
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/twinline-complete-XXXXXX", (base && *base) ? base : "/tmp");
    if (mkdtemp(dir) == NULL)
        return 1;
    touch("draft-one.txt");
    touch("draft-two.txt");
    touch("final.txt");
    touch(".hidden");
    char sub[512];
    snprintf(sub, sizeof sub, "%s/drafts", dir);
    mkdir(sub, 0700);

    puts("tui completion");
    TW_RUN(fuzzy_matches_subsequences);
    TW_RUN(fuzzy_prefers_prefixes_and_runs);
    TW_RUN(completes_paths);

    rmdir(sub);
    const char *names[] = {"draft-one.txt", "draft-two.txt", "final.txt", ".hidden"};
    for (size_t i = 0; i < 4; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);
    return tw_test_summary();
}
