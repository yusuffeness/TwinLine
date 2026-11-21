/*
 * Decoding of terminal input bytes into key events.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_KEYS_H
#define TWINLINE_TUI_KEYS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum tw_key_type {
    TW_KEY_NONE = 0, /* recognised but ignored sequence */
    TW_KEY_TEXT,     /* one printable character in `text` */
    TW_KEY_ENTER,
    TW_KEY_TAB,
    TW_KEY_SHIFT_TAB,
    TW_KEY_BACKSPACE,
    TW_KEY_DELETE,
    TW_KEY_LEFT,
    TW_KEY_RIGHT,
    TW_KEY_UP,
    TW_KEY_DOWN,
    TW_KEY_HOME,
    TW_KEY_END,
    TW_KEY_WORD_LEFT,
    TW_KEY_WORD_RIGHT,
    TW_KEY_PAGE_UP,
    TW_KEY_PAGE_DOWN,
    TW_KEY_ESCAPE,
    TW_KEY_CTRL,        /* Ctrl + letter in `ctrl` ('a'..'z') */
    TW_KEY_PASTE_BEGIN, /* bracketed paste starts */
    TW_KEY_PASTE_END,   /* bracketed paste ends */
} tw_key_type;

typedef struct tw_key {
    tw_key_type type;
    char ctrl;    /* for TW_KEY_CTRL */
    char text[5]; /* for TW_KEY_TEXT: one UTF-8 character */
} tw_key;

/*
 * Decodes the first key in `buf`. Returns the number of bytes consumed, or 0
 * when `buf` holds only the start of a sequence and more bytes are needed.
 * When `more_may_follow` is false, a lone ESC byte is reported as Escape
 * instead of waiting for the rest of a sequence.
 */
size_t tw_key_decode(const unsigned char *buf, size_t len, bool more_may_follow, tw_key *key);

#endif /* TWINLINE_TUI_KEYS_H */
