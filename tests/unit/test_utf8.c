/*
 * Unit tests for src/analysis/utf8.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "twinline/utf8.h"

/* Decodes the first code point of `bytes` and checks value and length. */
#define CHECK_DECODE(bytes, expected_cp, expected_len)                                             \
    do {                                                                                           \
        uint32_t cp_ = 0;                                                                          \
        size_t len_ = tw_utf8_decode((const unsigned char *)(bytes), &cp_);                        \
        TW_CHECK_EQ_SIZE(cp_, expected_cp);                                                        \
        TW_CHECK_EQ_SIZE(len_, expected_len);                                                      \
    } while (0)

TW_TEST(decode_ascii)
{
    CHECK_DECODE("A", 'A', 1);
    CHECK_DECODE("z!", 'z', 1);
}

TW_TEST(decode_multibyte)
{
    CHECK_DECODE("\xC3\xA7", 0x00E7, 2);          /* c with cedilla */
    CHECK_DECODE("\xC4\xB0", 0x0130, 2);          /* capital I with dot */
    CHECK_DECODE("\xE2\x80\x9C", 0x201C, 3);      /* left double quotation mark */
    CHECK_DECODE("\xF0\x9F\x98\x80", 0x1F600, 4); /* emoji */
}

TW_TEST(decode_rejects_malformed_input)
{
    CHECK_DECODE("\x80", TW_UTF8_REPLACEMENT, 1);             /* lone continuation byte */
    CHECK_DECODE("\xC0\x80", TW_UTF8_REPLACEMENT, 1);         /* overlong NUL */
    CHECK_DECODE("\xE0\x80\x80", TW_UTF8_REPLACEMENT, 1);     /* overlong 3-byte form */
    CHECK_DECODE("\xED\xA0\x80", TW_UTF8_REPLACEMENT, 1);     /* UTF-16 surrogate */
    CHECK_DECODE("\xF4\x90\x80\x80", TW_UTF8_REPLACEMENT, 1); /* above U+10FFFF */
    CHECK_DECODE("\xC3", TW_UTF8_REPLACEMENT, 1);             /* truncated sequence */
    CHECK_DECODE("\xE2\x80", TW_UTF8_REPLACEMENT, 1);         /* truncated sequence */
    CHECK_DECODE("\xFF", TW_UTF8_REPLACEMENT, 1);             /* invalid lead byte */
}

TW_TEST(encode_round_trips)
{
    const uint32_t samples[] = {0x24, 0xE7, 0x131, 0x416, 0x20AC, 0xFFFD, 0x1F600, 0x10FFFF};
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        unsigned char buf[5] = {0};
        size_t len = tw_utf8_encode(samples[i], buf);
        uint32_t back = 0;
        TW_CHECK_EQ_SIZE(tw_utf8_decode(buf, &back), len);
        TW_CHECK_EQ_SIZE(back, samples[i]);
    }
}

TW_TEST(fold_ascii_and_latin)
{
    TW_CHECK_EQ_SIZE(tw_utf8_fold('A', TW_FOLD_DEFAULT), 'a');
    TW_CHECK_EQ_SIZE(tw_utf8_fold('z', TW_FOLD_DEFAULT), 'z');
    TW_CHECK_EQ_SIZE(tw_utf8_fold('7', TW_FOLD_DEFAULT), '7');
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x00C7, TW_FOLD_DEFAULT), 0x00E7); /* C cedilla */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x00D6, TW_FOLD_DEFAULT), 0x00F6); /* O diaeresis */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x00D7, TW_FOLD_DEFAULT), 0x00D7); /* multiplication sign */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x00DF, TW_FOLD_DEFAULT), 0x00DF); /* sharp s */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x015E, TW_FOLD_DEFAULT), 0x015F); /* S cedilla */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x011E, TW_FOLD_DEFAULT), 0x011F); /* G breve */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0141, TW_FOLD_DEFAULT), 0x0142); /* L stroke (odd range) */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x017D, TW_FOLD_DEFAULT), 0x017E); /* Z caron (odd range) */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0178, TW_FOLD_DEFAULT), 0x00FF); /* Y diaeresis */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x017F, TW_FOLD_DEFAULT), 's');    /* long s */
}

TW_TEST(fold_turkish_i)
{
    /* Default mode: I -> i, dotted capital I -> i, dotless i unchanged. */
    TW_CHECK_EQ_SIZE(tw_utf8_fold('I', TW_FOLD_DEFAULT), 'i');
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0130, TW_FOLD_DEFAULT), 'i');
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0131, TW_FOLD_DEFAULT), 0x0131);

    /* Turkish mode: I -> dotless i. */
    TW_CHECK_EQ_SIZE(tw_utf8_fold('I', TW_FOLD_TURKISH), 0x0131);
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0130, TW_FOLD_TURKISH), 'i');
    TW_CHECK_EQ_SIZE(tw_utf8_fold('i', TW_FOLD_TURKISH), 'i');
}

TW_TEST(fold_greek_and_cyrillic)
{
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x03A3, TW_FOLD_DEFAULT), 0x03C3); /* capital sigma */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x03C2, TW_FOLD_DEFAULT), 0x03C3); /* final sigma */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0414, TW_FOLD_DEFAULT), 0x0434); /* De */
    TW_CHECK_EQ_SIZE(tw_utf8_fold(0x0401, TW_FOLD_DEFAULT), 0x0451); /* Io */
}

TW_TEST(word_characters)
{
    TW_CHECK(tw_utf8_is_word('a'));
    TW_CHECK(tw_utf8_is_word('Z'));
    TW_CHECK(tw_utf8_is_word('5'));
    TW_CHECK(tw_utf8_is_word(0x00E7)); /* c cedilla */
    TW_CHECK(tw_utf8_is_word(0x0131)); /* dotless i */
    TW_CHECK(tw_utf8_is_word(0x00AA)); /* feminine ordinal */
    TW_CHECK(tw_utf8_is_word(0x4E2D)); /* CJK ideograph */
    TW_CHECK(!tw_utf8_is_word(' '));
    TW_CHECK(!tw_utf8_is_word(','));
    TW_CHECK(!tw_utf8_is_word('_'));
    TW_CHECK(!tw_utf8_is_word(0x00A0));  /* no-break space */
    TW_CHECK(!tw_utf8_is_word(0x00D7));  /* multiplication sign */
    TW_CHECK(!tw_utf8_is_word(0x201C));  /* curly quote */
    TW_CHECK(!tw_utf8_is_word(0x2014));  /* em dash */
    TW_CHECK(!tw_utf8_is_word(0x1F600)); /* emoji */
    TW_CHECK(!tw_utf8_is_word(TW_UTF8_REPLACEMENT));
}

TW_TEST(prefix_never_splits_a_character)
{
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("hello", 10), 5);
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("hello", 3), 3);
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("h\xC3\xA9llo", 2), 1); /* would cut e acute */
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("h\xC3\xA9llo", 3), 3);
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("\xF0\x9F\x98\x80!", 3), 0);
    TW_CHECK_EQ_SIZE(tw_utf8_prefix_len("", 0), 0);
}

int main(void)
{
    puts("utf8");
    TW_RUN(decode_ascii);
    TW_RUN(decode_multibyte);
    TW_RUN(decode_rejects_malformed_input);
    TW_RUN(encode_round_trips);
    TW_RUN(fold_ascii_and_latin);
    TW_RUN(fold_turkish_i);
    TW_RUN(fold_greek_and_cyrillic);
    TW_RUN(word_characters);
    TW_RUN(prefix_never_splits_a_character);
    return tw_test_summary();
}
