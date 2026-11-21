/*
 * Unit tests for src/tui/lineedit.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "tui/lineedit.h"

TW_TEST(insert_and_move)
{
    tw_line line;
    TW_CHECK(tw_line_init(&line));
    TW_CHECK(tw_line_insert(&line, "hello"));
    tw_line_home(&line);
    TW_CHECK(tw_line_insert(&line, ">"));
    TW_CHECK_EQ_STR(line.buf, ">hello");
    TW_CHECK_EQ_SIZE(line.cursor, 1);
    tw_line_end(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 6);
    tw_line_free(&line);
}

TW_TEST(edits_respect_utf8_characters)
{
    tw_line line;
    TW_CHECK(tw_line_init(&line));
    TW_CHECK(tw_line_set(&line, "\xC3\xA7"
                                "ay")); /* çay */
    tw_line_home(&line);
    tw_line_right(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 2); /* moved over the whole two-byte letter */
    tw_line_backspace(&line);
    TW_CHECK_EQ_STR(line.buf, "ay");
    tw_line_end(&line);
    TW_CHECK(tw_line_insert(&line, "\xF0\x9F\x98\x80"));
    tw_line_left(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 2);
    tw_line_delete(&line);
    TW_CHECK_EQ_STR(line.buf, "ay");
    tw_line_free(&line);
}

TW_TEST(word_motion_and_kills)
{
    tw_line line;
    TW_CHECK(tw_line_init(&line));
    TW_CHECK(tw_line_set(&line, "open  my notes.txt"));
    tw_line_word_left(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 9);
    tw_line_word_left(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 6);
    tw_line_word_right(&line);
    TW_CHECK_EQ_SIZE(line.cursor, 8);

    tw_line_end(&line);
    tw_line_kill_word(&line);
    TW_CHECK_EQ_STR(line.buf, "open  my ");
    tw_line_home(&line);
    tw_line_word_right(&line);
    tw_line_kill_to_end(&line);
    TW_CHECK_EQ_STR(line.buf, "open");
    tw_line_kill_to_start(&line);
    TW_CHECK_EQ_STR(line.buf, "");
    TW_CHECK_EQ_SIZE(line.cursor, 0);
    tw_line_free(&line);
}

TW_TEST(edges_are_safe)
{
    tw_line line;
    TW_CHECK(tw_line_init(&line));
    tw_line_backspace(&line);
    tw_line_delete(&line);
    tw_line_left(&line);
    tw_line_right(&line);
    tw_line_word_left(&line);
    tw_line_kill_word(&line);
    TW_CHECK_EQ_SIZE(line.len, 0);
    TW_CHECK_EQ_STR(line.buf, "");
    tw_line_free(&line);
}

TW_TEST(history_skips_blanks_and_repeats)
{
    tw_history h;
    tw_history_init(&h, 3);
    TW_CHECK(tw_history_add(&h, "open a.txt"));
    TW_CHECK(tw_history_add(&h, "open a.txt"));
    TW_CHECK(tw_history_add(&h, "   "));
    TW_CHECK(tw_history_add(&h, "show"));
    TW_CHECK_EQ_SIZE(h.count, 2);
    TW_CHECK(tw_history_add(&h, "status"));
    TW_CHECK(tw_history_add(&h, "quit"));
    TW_CHECK_EQ_SIZE(h.count, 3); /* the oldest entry was dropped */
    TW_CHECK_EQ_STR(h.items[0], "show");
    tw_history_free(&h);
}

TW_TEST(history_search_goes_backwards)
{
    tw_history h;
    tw_history_init(&h, 10);
    TW_CHECK(tw_history_add(&h, "open first.txt"));
    TW_CHECK(tw_history_add(&h, "analyze"));
    TW_CHECK(tw_history_add(&h, "open second.txt"));

    size_t found = 99;
    TW_CHECK(tw_history_search(&h, "open", h.count, &found));
    TW_CHECK_EQ_SIZE(found, 2);
    TW_CHECK(tw_history_search(&h, "open", found, &found));
    TW_CHECK_EQ_SIZE(found, 0);
    TW_CHECK(!tw_history_search(&h, "open", found, &found));
    TW_CHECK(!tw_history_search(&h, "missing", h.count, &found));
    tw_history_free(&h);
}

int main(void)
{
    puts("tui line editing");
    TW_RUN(insert_and_move);
    TW_RUN(edits_respect_utf8_characters);
    TW_RUN(word_motion_and_kills);
    TW_RUN(edges_are_safe);
    TW_RUN(history_skips_blanks_and_repeats);
    TW_RUN(history_search_goes_backwards);
    return tw_test_summary();
}
