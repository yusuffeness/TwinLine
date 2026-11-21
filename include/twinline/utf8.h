/*
 * Minimal UTF-8 and Unicode helpers used by the tokenizer.
 *
 * Case folding covers ASCII, Latin-1, Latin Extended-A (which includes the
 * Turkish letters), basic Greek and basic Cyrillic. Other scripts are
 * compared as-is.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_UTF8_H
#define TWINLINE_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TW_UTF8_REPLACEMENT 0xFFFDu

typedef enum tw_fold_mode {
    /* Language-neutral folding: I -> i, U+0130 (dotted capital I) -> i,
     * U+0131 (dotless i) is kept as-is. */
    TW_FOLD_DEFAULT = 0,
    /* Turkish and Azerbaijani rules: I -> U+0131 (dotless i), U+0130 -> i. */
    TW_FOLD_TURKISH,
} tw_fold_mode;

/*
 * Decodes one code point from the NUL-terminated string `s` (which must not
 * point at the terminator). Returns the number of bytes consumed (at least 1).
 * Malformed sequences, overlong forms and surrogates decode to
 * TW_UTF8_REPLACEMENT and consume a single byte.
 */
size_t tw_utf8_decode(const unsigned char *s, uint32_t *cp);

/* Encodes `cp` into `out` and returns the number of bytes written (1-4). */
size_t tw_utf8_encode(uint32_t cp, unsigned char out[4]);

/* Returns the lower-case (folded) form of `cp`. */
uint32_t tw_utf8_fold(uint32_t cp, tw_fold_mode mode);

/* True if `cp` belongs to a word: letters, digits and combining marks.
 * Punctuation, symbols, spaces and emoji are separators. */
bool tw_utf8_is_word(uint32_t cp);

/* Length in bytes of the longest prefix of `s` that is at most `max_bytes`
 * long and does not split a multi-byte character. */
size_t tw_utf8_prefix_len(const char *s, size_t max_bytes);

#endif /* TWINLINE_UTF8_H */
