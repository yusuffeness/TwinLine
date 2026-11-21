/*
 * Fuzz target: UTF-8 decoding, encoding, folding and prefix cutting.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fuzz.h"
#include "twinline/utf8.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = tw_fuzz_cstring(data, size);
    if (text == NULL)
        return 0;

    const unsigned char *p = (const unsigned char *)text;
    size_t total = strlen(text);
    size_t consumed = 0;

    while (*p != '\0') {
        uint32_t cp = 0;
        size_t len = tw_utf8_decode(p, &cp);
        TW_FUZZ_ASSERT(len >= 1 && len <= 4);
        TW_FUZZ_ASSERT(cp <= 0x10FFFF);
        TW_FUZZ_ASSERT(cp < 0xD800 || cp > 0xDFFF);

        /* Valid code points survive an encode/decode round trip. */
        unsigned char buf[5] = {0};
        size_t enc = tw_utf8_encode(cp, buf);
        uint32_t back = 0;
        TW_FUZZ_ASSERT(tw_utf8_decode(buf, &back) == enc);
        TW_FUZZ_ASSERT(back == cp);

        /* Folding is idempotent for both modes. */
        for (int mode = TW_FOLD_DEFAULT; mode <= TW_FOLD_TURKISH; mode++) {
            uint32_t once = tw_utf8_fold(cp, (tw_fold_mode)mode);
            TW_FUZZ_ASSERT(tw_utf8_fold(once, (tw_fold_mode)mode) == once);
        }
        (void)tw_utf8_is_word(cp);

        p += len;
        consumed += len;
    }
    TW_FUZZ_ASSERT(consumed == total);

    /* Prefixes never end inside a multi-byte sequence. */
    for (size_t max = 0; max <= total && max < 64; max++) {
        size_t cut = tw_utf8_prefix_len(text, max);
        TW_FUZZ_ASSERT(cut <= max);
        TW_FUZZ_ASSERT(cut == total || ((unsigned char)text[cut] & 0xC0) != 0x80 || cut == 0);
    }

    free(text);
    return 0;
}
