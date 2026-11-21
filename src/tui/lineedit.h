/*
 * Single-line text editing with a cursor, and command history.
 *
 * Positions are byte offsets that always sit on UTF-8 character boundaries.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_LINEEDIT_H
#define TWINLINE_TUI_LINEEDIT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct tw_line {
    char *buf; /* NUL-terminated */
    size_t len;
    size_t cap;
    size_t cursor;
} tw_line;

bool tw_line_init(tw_line *line);
void tw_line_free(tw_line *line);
void tw_line_clear(tw_line *line);
bool tw_line_set(tw_line *line, const char *text); /* cursor goes to the end */
bool tw_line_insert(tw_line *line, const char *text);
void tw_line_backspace(tw_line *line);
void tw_line_delete(tw_line *line);
void tw_line_left(tw_line *line);
void tw_line_right(tw_line *line);
void tw_line_home(tw_line *line);
void tw_line_end(tw_line *line);
void tw_line_word_left(tw_line *line);
void tw_line_word_right(tw_line *line);
void tw_line_kill_to_start(tw_line *line); /* Ctrl+U */
void tw_line_kill_to_end(tw_line *line);   /* Ctrl+K */
void tw_line_kill_word(tw_line *line);     /* Ctrl+W */

typedef struct tw_history {
    char **items; /* oldest first */
    size_t count;
    size_t cap;
    size_t limit;
} tw_history;

void tw_history_init(tw_history *history, size_t limit);
void tw_history_free(tw_history *history);

/* Adds an entry unless it is blank or repeats the newest one. */
bool tw_history_add(tw_history *history, const char *text);

/*
 * Searches backwards for an entry containing `query`, starting just before
 * index `before`. Returns true and stores the index when found.
 */
bool tw_history_search(const tw_history *history, const char *query, size_t before, size_t *found);

#endif /* TWINLINE_TUI_LINEEDIT_H */
