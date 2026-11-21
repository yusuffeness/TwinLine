/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tw_strbuf_init(tw_strbuf *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->failed = false;
}

void tw_strbuf_free(tw_strbuf *sb)
{
    free(sb->data);
    tw_strbuf_init(sb);
}

void tw_strbuf_clear(tw_strbuf *sb)
{
    sb->len = 0;
    if (sb->data != NULL)
        sb->data[0] = '\0';
}

static bool reserve(tw_strbuf *sb, size_t extra)
{
    if (sb->failed)
        return false;
    if (sb->len + extra + 1 <= sb->cap)
        return true;
    size_t new_cap = sb->cap ? sb->cap : 256;
    while (new_cap < sb->len + extra + 1)
        new_cap *= 2;
    char *data = realloc(sb->data, new_cap);
    if (data == NULL) {
        sb->failed = true;
        return false;
    }
    sb->data = data;
    sb->cap = new_cap;
    return true;
}

void tw_strbuf_append_n(tw_strbuf *sb, const char *text, size_t len)
{
    if (!reserve(sb, len))
        return;
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void tw_strbuf_append(tw_strbuf *sb, const char *text)
{
    tw_strbuf_append_n(sb, text, strlen(text));
}

void tw_strbuf_appendf(tw_strbuf *sb, const char *fmt, ...)
{
    char small[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(small, sizeof small, fmt, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t)n < sizeof small) {
        tw_strbuf_append_n(sb, small, (size_t)n);
        return;
    }
    if (!reserve(sb, (size_t)n))
        return;
    va_start(args, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, args);
    va_end(args);
    sb->len += (size_t)n;
}

const char *tw_strbuf_str(const tw_strbuf *sb)
{
    return sb->data != NULL ? sb->data : "";
}
