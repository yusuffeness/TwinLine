/*
 * Unit tests for src/tui/keys.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "tui/keys.h"

static size_t decode(const char *bytes, bool more, tw_key *key)
{
    return tw_key_decode((const unsigned char *)bytes, strlen(bytes), more, key);
}

#define CHECK_KEY(bytes, expected_type, expected_len)                                              \
    do {                                                                                           \
        tw_key key_;                                                                               \
        TW_CHECK_EQ_SIZE(decode((bytes), false, &key_), (expected_len));                           \
        TW_CHECK_EQ_INT(key_.type, (expected_type));                                               \
    } while (0)

TW_TEST(plain_and_control_keys)
{
    CHECK_KEY("\r", TW_KEY_ENTER, 1);
    CHECK_KEY("\n", TW_KEY_ENTER, 1);
    CHECK_KEY("\t", TW_KEY_TAB, 1);
    CHECK_KEY("\x7f", TW_KEY_BACKSPACE, 1);
    CHECK_KEY("\x08", TW_KEY_BACKSPACE, 1);

    tw_key key;
    TW_CHECK_EQ_SIZE(decode("\x12", false, &key), 1);
    TW_CHECK_EQ_INT(key.type, TW_KEY_CTRL);
    TW_CHECK_EQ_INT(key.ctrl, 'r');
    TW_CHECK_EQ_SIZE(decode("\x03", false, &key), 1);
    TW_CHECK_EQ_INT(key.ctrl, 'c');
}

TW_TEST(text_including_multibyte)
{
    tw_key key;
    TW_CHECK_EQ_SIZE(decode("ab", false, &key), 1);
    TW_CHECK_EQ_INT(key.type, TW_KEY_TEXT);
    TW_CHECK_EQ_STR(key.text, "a");

    TW_CHECK_EQ_SIZE(decode("\xC3\xA7x", false, &key), 2);
    TW_CHECK_EQ_STR(key.text, "\xC3\xA7");
    TW_CHECK_EQ_SIZE(decode("\xF0\x9F\x98\x80", false, &key), 4);
    TW_CHECK_EQ_INT(key.type, TW_KEY_TEXT);
}

TW_TEST(incomplete_sequences_wait_for_more)
{
    tw_key key;
    TW_CHECK_EQ_SIZE(decode("\x1b", true, &key), 0);
    TW_CHECK_EQ_SIZE(decode("\x1b[", true, &key), 0);
    TW_CHECK_EQ_SIZE(decode("\x1b[1;5", true, &key), 0);
    TW_CHECK_EQ_SIZE(decode("\xC3", true, &key), 0);
    /* Without more input, a lone ESC is the Escape key. */
    CHECK_KEY("\x1b", TW_KEY_ESCAPE, 1);
}

TW_TEST(arrows_and_navigation)
{
    CHECK_KEY("\x1b[A", TW_KEY_UP, 3);
    CHECK_KEY("\x1b[B", TW_KEY_DOWN, 3);
    CHECK_KEY("\x1b[C", TW_KEY_RIGHT, 3);
    CHECK_KEY("\x1b[D", TW_KEY_LEFT, 3);
    CHECK_KEY("\x1bOA", TW_KEY_UP, 3);
    CHECK_KEY("\x1b[H", TW_KEY_HOME, 3);
    CHECK_KEY("\x1b[F", TW_KEY_END, 3);
    CHECK_KEY("\x1b[1~", TW_KEY_HOME, 4);
    CHECK_KEY("\x1b[4~", TW_KEY_END, 4);
    CHECK_KEY("\x1b[3~", TW_KEY_DELETE, 4);
    CHECK_KEY("\x1b[5~", TW_KEY_PAGE_UP, 4);
    CHECK_KEY("\x1b[Z", TW_KEY_SHIFT_TAB, 3);
    CHECK_KEY("\x1b[1;5C", TW_KEY_WORD_RIGHT, 6);
    CHECK_KEY("\x1b[1;5D", TW_KEY_WORD_LEFT, 6);
    CHECK_KEY("\x1b[1;3D", TW_KEY_WORD_LEFT, 6);
}

TW_TEST(bracketed_paste_markers)
{
    CHECK_KEY("\x1b[200~", TW_KEY_PASTE_BEGIN, 6);
    CHECK_KEY("\x1b[201~", TW_KEY_PASTE_END, 6);
}

TW_TEST(unknown_sequences_are_consumed)
{
    CHECK_KEY("\x1b[99~", TW_KEY_NONE, 5);
    CHECK_KEY("\x1b[15;2Q", TW_KEY_NONE, 7);
    CHECK_KEY("\x80", TW_KEY_NONE, 1);
}

int main(void)
{
    puts("tui keys");
    TW_RUN(plain_and_control_keys);
    TW_RUN(text_including_multibyte);
    TW_RUN(incomplete_sequences_wait_for_more);
    TW_RUN(arrows_and_navigation);
    TW_RUN(bracketed_paste_markers);
    TW_RUN(unknown_sequences_are_consumed);
    return tw_test_summary();
}
