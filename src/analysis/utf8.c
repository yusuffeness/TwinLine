/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/utf8.h"

#include <string.h>

size_t tw_utf8_decode(const unsigned char *s, uint32_t *cp)
{
    unsigned char lead = s[0];
    if (lead < 0x80) {
        *cp = lead;
        return 1;
    }

    size_t extra;
    uint32_t value;
    uint32_t min_value;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        value = lead & 0x1Fu;
        min_value = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        value = lead & 0x0Fu;
        min_value = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        value = lead & 0x07u;
        min_value = 0x10000;
    } else {
        *cp = TW_UTF8_REPLACEMENT;
        return 1;
    }

    /* A NUL terminator is not a continuation byte, so this never reads past
     * the end of the string. */
    for (size_t i = 1; i <= extra; i++) {
        unsigned char next = s[i];
        if ((next & 0xC0) != 0x80) {
            *cp = TW_UTF8_REPLACEMENT;
            return 1;
        }
        value = (value << 6) | (next & 0x3Fu);
    }

    if (value < min_value || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        *cp = TW_UTF8_REPLACEMENT;
        return 1;
    }
    *cp = value;
    return extra + 1;
}

size_t tw_utf8_encode(uint32_t cp, unsigned char out[4])
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Latin Extended-A (U+0100..U+017F) is mostly upper/lower pairs. */
static uint32_t fold_latin_ext_a(uint32_t cp)
{
    switch (cp) {
    case 0x0130: /* dotted capital I: handled by the caller */
    case 0x0131: /* dotless small i */
    case 0x0138: /* kra */
    case 0x0149: /* n preceded by apostrophe */
        return cp;
    case 0x0178: /* capital Y with diaeresis */
        return 0x00FF;
    case 0x017F: /* long s */
        return 's';
    default:
        break;
    }
    /* In these two ranges the capital letter has the odd code point. */
    if ((cp >= 0x0139 && cp <= 0x0148) || (cp >= 0x0179 && cp <= 0x017E))
        return (cp & 1u) ? cp + 1 : cp;
    /* Everywhere else the capital letter has the even code point. */
    return (cp & 1u) ? cp : cp + 1;
}

uint32_t tw_utf8_fold(uint32_t cp, tw_fold_mode mode)
{
    if (cp < 0x80) {
        if (cp >= 'A' && cp <= 'Z') {
            if (cp == 'I' && mode == TW_FOLD_TURKISH)
                return 0x0131;
            return cp + ('a' - 'A');
        }
        return cp;
    }
    if (cp == 0x0130)
        return 'i';
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7)
        return cp + 0x20;
    if (cp >= 0x0100 && cp <= 0x017F)
        return fold_latin_ext_a(cp);
    if (cp == 0x03C2) /* final sigma */
        return 0x03C3;
    if (cp >= 0x0391 && cp <= 0x03AB && cp != 0x03A2)
        return cp + 0x20;
    if (cp >= 0x0410 && cp <= 0x042F)
        return cp + 0x20;
    if (cp >= 0x0400 && cp <= 0x040F)
        return cp + 0x50;
    return cp;
}

bool tw_utf8_is_word(uint32_t cp)
{
    if (cp < 0x80)
        return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');

    /* Latin-1 punctuation and symbols, except the letters ª, µ and º. */
    if (cp < 0xC0)
        return cp == 0xAA || cp == 0xB5 || cp == 0xBA;
    if (cp == 0xD7 || cp == 0xF7) /* multiplication and division signs */
        return false;

    if (cp >= 0x2000 && cp <= 0x2BFF) /* punctuation, symbols, arrows, maths, box drawing */
        return false;
    if (cp >= 0x3000 && cp <= 0x303F) /* CJK symbols and punctuation */
        return false;
    if (cp >= 0xFE10 && cp <= 0xFE6F) /* vertical, compatibility and small forms */
        return false;
    if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65))
        return false;                   /* full-width punctuation */
    if (cp >= 0x1F000 && cp <= 0x1FAFF) /* emoji and pictographs */
        return false;
    if (cp == 0xFEFF || cp == TW_UTF8_REPLACEMENT)
        return false;
    return true;
}

size_t tw_utf8_prefix_len(const char *s, size_t max_bytes)
{
    size_t len = strlen(s);
    if (len <= max_bytes)
        return len;

    size_t cut = max_bytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
        cut--;
    return cut;
}
