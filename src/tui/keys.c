/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/keys.h"

#include <stdlib.h>
#include <string.h>

static size_t simple(tw_key *key, tw_key_type type, size_t consumed)
{
    key->type = type;
    return consumed;
}

static tw_key_type arrow(unsigned char final, int modifier)
{
    bool word = modifier == 3 || modifier == 5; /* Alt or Ctrl */
    switch (final) {
    case 'A':
        return TW_KEY_UP;
    case 'B':
        return TW_KEY_DOWN;
    case 'C':
        return word ? TW_KEY_WORD_RIGHT : TW_KEY_RIGHT;
    case 'D':
        return word ? TW_KEY_WORD_LEFT : TW_KEY_LEFT;
    case 'H':
        return TW_KEY_HOME;
    case 'F':
        return TW_KEY_END;
    case 'Z':
        return TW_KEY_SHIFT_TAB;
    default:
        return TW_KEY_NONE;
    }
}

static tw_key_type tilde(int code)
{
    switch (code) {
    case 1:
    case 7:
        return TW_KEY_HOME;
    case 4:
    case 8:
        return TW_KEY_END;
    case 3:
        return TW_KEY_DELETE;
    case 5:
        return TW_KEY_PAGE_UP;
    case 6:
        return TW_KEY_PAGE_DOWN;
    case 200:
        return TW_KEY_PASTE_BEGIN;
    case 201:
        return TW_KEY_PASTE_END;
    default:
        return TW_KEY_NONE;
    }
}

/* ESC [ params final */
static size_t decode_csi(const unsigned char *buf, size_t len, tw_key *key)
{
    int params[4] = {0, 0, 0, 0};
    int count = 0;
    for (size_t i = 2; i < len; i++) {
        unsigned char c = buf[i];
        if (c >= '0' && c <= '9') {
            if (count == 0)
                count = 1;
            if (count <= 4 && params[count - 1] < 10000)
                params[count - 1] = params[count - 1] * 10 + (c - '0');
        } else if (c == ';') {
            count++;
        } else if (c >= 0x40 && c <= 0x7E) {
            key->type = (c == '~') ? tilde(params[0]) : arrow(c, count >= 2 ? params[1] : 0);
            return i + 1;
        } else if (c < 0x20 || c > 0x3F) {
            return simple(key, TW_KEY_NONE, i + 1); /* malformed: drop it */
        }
    }
    return 0; /* incomplete */
}

size_t tw_key_decode(const unsigned char *buf, size_t len, bool more_may_follow, tw_key *key)
{
    memset(key, 0, sizeof *key);
    if (len == 0)
        return 0;

    unsigned char c = buf[0];
    if (c == 0x1B) {
        if (len == 1)
            return more_may_follow ? 0 : simple(key, TW_KEY_ESCAPE, 1);
        if (buf[1] == '[')
            return decode_csi(buf, len, key);
        if (buf[1] == 'O') {
            if (len < 3)
                return more_may_follow ? 0 : simple(key, TW_KEY_ESCAPE, 1);
            key->type = arrow(buf[2], 0);
            return 3;
        }
        return simple(key, TW_KEY_ESCAPE, 1);
    }

    if (c == '\r' || c == '\n')
        return simple(key, TW_KEY_ENTER, 1);
    if (c == '\t')
        return simple(key, TW_KEY_TAB, 1);
    if (c == 0x7F || c == 0x08)
        return simple(key, TW_KEY_BACKSPACE, 1);
    if (c < 0x20) {
        if (c >= 1 && c <= 26) {
            key->type = TW_KEY_CTRL;
            key->ctrl = (char)('a' + c - 1);
        }
        return 1;
    }

    /* A printable character, possibly multi-byte. */
    size_t need = 1;
    if (c >= 0xF0)
        need = 4;
    else if (c >= 0xE0)
        need = 3;
    else if (c >= 0xC0)
        need = 2;
    else if (c >= 0x80)
        return simple(key, TW_KEY_NONE, 1); /* stray continuation byte */
    if (len < need)
        return more_may_follow ? 0 : simple(key, TW_KEY_NONE, len);
    for (size_t i = 1; i < need; i++) {
        if ((buf[i] & 0xC0) != 0x80)
            return simple(key, TW_KEY_NONE, i);
    }
    key->type = TW_KEY_TEXT;
    memcpy(key->text, buf, need);
    key->text[need] = '\0';
    return need;
}
