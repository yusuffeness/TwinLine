/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/lineedit.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ line */

static bool reserve(tw_line *line, size_t extra)
{
    if (line->len + extra + 1 <= line->cap)
        return true;
    size_t new_cap = line->cap ? line->cap : 64;
    while (new_cap < line->len + extra + 1)
        new_cap *= 2;
    char *buf = realloc(line->buf, new_cap);
    if (buf == NULL)
        return false;
    line->buf = buf;
    line->cap = new_cap;
    return true;
}

bool tw_line_init(tw_line *line)
{
    line->buf = NULL;
    line->len = line->cap = line->cursor = 0;
    if (!reserve(line, 0))
        return false;
    line->buf[0] = '\0';
    return true;
}

void tw_line_free(tw_line *line)
{
    free(line->buf);
    line->buf = NULL;
    line->len = line->cap = line->cursor = 0;
}

void tw_line_clear(tw_line *line)
{
    line->len = line->cursor = 0;
    line->buf[0] = '\0';
}

bool tw_line_set(tw_line *line, const char *text)
{
    tw_line_clear(line);
    return tw_line_insert(line, text);
}

bool tw_line_insert(tw_line *line, const char *text)
{
    size_t n = strlen(text);
    if (!reserve(line, n))
        return false;
    memmove(line->buf + line->cursor + n, line->buf + line->cursor, line->len - line->cursor + 1);
    memcpy(line->buf + line->cursor, text, n);
    line->len += n;
    line->cursor += n;
    return true;
}

static bool is_continuation(char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

static size_t prev_char(const tw_line *line, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && is_continuation(line->buf[pos]))
        pos--;
    return pos;
}

static size_t next_char(const tw_line *line, size_t pos)
{
    if (pos >= line->len)
        return line->len;
    pos++;
    while (pos < line->len && is_continuation(line->buf[pos]))
        pos++;
    return pos;
}

static void erase(tw_line *line, size_t from, size_t to)
{
    memmove(line->buf + from, line->buf + to, line->len - to + 1);
    line->len -= to - from;
    line->cursor = from;
}

void tw_line_backspace(tw_line *line)
{
    if (line->cursor > 0)
        erase(line, prev_char(line, line->cursor), line->cursor);
}

void tw_line_delete(tw_line *line)
{
    if (line->cursor < line->len) {
        size_t end = next_char(line, line->cursor);
        size_t start = line->cursor;
        erase(line, start, end);
    }
}

void tw_line_left(tw_line *line)
{
    line->cursor = prev_char(line, line->cursor);
}

void tw_line_right(tw_line *line)
{
    line->cursor = next_char(line, line->cursor);
}

void tw_line_home(tw_line *line)
{
    line->cursor = 0;
}

void tw_line_end(tw_line *line)
{
    line->cursor = line->len;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

void tw_line_word_left(tw_line *line)
{
    size_t pos = line->cursor;
    while (pos > 0 && is_space(line->buf[pos - 1]))
        pos--;
    while (pos > 0 && !is_space(line->buf[pos - 1]))
        pos--;
    line->cursor = pos;
}

void tw_line_word_right(tw_line *line)
{
    size_t pos = line->cursor;
    while (pos < line->len && is_space(line->buf[pos]))
        pos++;
    while (pos < line->len && !is_space(line->buf[pos]))
        pos++;
    line->cursor = pos;
}

void tw_line_kill_to_start(tw_line *line)
{
    erase(line, 0, line->cursor);
}

void tw_line_kill_to_end(tw_line *line)
{
    line->len = line->cursor;
    line->buf[line->len] = '\0';
}

void tw_line_kill_word(tw_line *line)
{
    size_t end = line->cursor;
    tw_line_word_left(line);
    erase(line, line->cursor, end);
}

/* --------------------------------------------------------------- history */

void tw_history_init(tw_history *history, size_t limit)
{
    history->items = NULL;
    history->count = history->cap = 0;
    history->limit = limit ? limit : 1000;
}

void tw_history_free(tw_history *history)
{
    for (size_t i = 0; i < history->count; i++)
        free(history->items[i]);
    free(history->items);
    tw_history_init(history, history->limit);
}

bool tw_history_add(tw_history *history, const char *text)
{
    if (strspn(text, " \t") == strlen(text))
        return true;
    if (history->count > 0 && strcmp(history->items[history->count - 1], text) == 0)
        return true;

    if (history->count == history->limit) {
        free(history->items[0]);
        memmove(history->items, history->items + 1, (history->count - 1) * sizeof *history->items);
        history->count--;
    }
    if (history->count == history->cap) {
        size_t new_cap = history->cap ? history->cap * 2 : 32;
        char **items = realloc(history->items, new_cap * sizeof *items);
        if (items == NULL)
            return false;
        history->items = items;
        history->cap = new_cap;
    }
    char *copy = strdup(text);
    if (copy == NULL)
        return false;
    history->items[history->count++] = copy;
    return true;
}

bool tw_history_search(const tw_history *history, const char *query, size_t before, size_t *found)
{
    if (before > history->count)
        before = history->count;
    while (before > 0) {
        before--;
        if (strstr(history->items[before], query) != NULL) {
            *found = before;
            return true;
        }
    }
    return false;
}
