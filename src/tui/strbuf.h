/*
 * Growable string buffer used to build terminal output.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_STRBUF_H
#define TWINLINE_TUI_STRBUF_H

#include <stdbool.h>
#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define TW_STRBUF_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TW_STRBUF_PRINTF(fmt, args)
#endif

typedef struct tw_strbuf {
    char *data; /* always NUL-terminated once anything was appended */
    size_t len;
    size_t cap;
    bool failed; /* an allocation failed; further appends are ignored */
} tw_strbuf;

void tw_strbuf_init(tw_strbuf *sb);
void tw_strbuf_free(tw_strbuf *sb);
void tw_strbuf_clear(tw_strbuf *sb);
void tw_strbuf_append(tw_strbuf *sb, const char *text);
void tw_strbuf_append_n(tw_strbuf *sb, const char *text, size_t len);
void tw_strbuf_appendf(tw_strbuf *sb, const char *fmt, ...) TW_STRBUF_PRINTF(2, 3);

/* Contents as a C string ("" when empty). */
const char *tw_strbuf_str(const tw_strbuf *sb);

#endif /* TWINLINE_TUI_STRBUF_H */
