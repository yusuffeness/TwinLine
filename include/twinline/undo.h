/*
 * Undo and redo for a tw_document.
 *
 * Every change to a document is a splice: some lines are removed and others
 * put in their place. tw_undo records each splice with copies of both sides,
 * so it can be reversed and replayed. Each recorded state has a unique id,
 * which lets a session tell whether the document is back at the state it
 * was saved in.
 *
 * Like tw_document, a tw_undo is not thread-safe on its own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_UNDO_H
#define TWINLINE_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#include "twinline/document.h"
#include "twinline/status.h"

#define TW_UNDO_DEFAULT_LIMIT 1000 /* edits kept before the oldest is dropped */

typedef struct tw_edit {
    size_t at;     /* index of the first changed line */
    char **before; /* the lines the edit removed */
    size_t before_count;
    char **after; /* the lines it put in their place */
    size_t after_count;
    unsigned long long id; /* state id after the edit */
} tw_edit;

typedef struct tw_undo {
    tw_edit *edits;
    size_t count;   /* recorded edits */
    size_t applied; /* edits[0 .. applied) are applied; the rest can be redone */
    size_t cap;
    size_t limit;
    unsigned long long base_id; /* state id before edits[0] */
    unsigned long long next_id;
} tw_undo;

void tw_undo_init(tw_undo *undo, size_t limit);
void tw_undo_free(tw_undo *undo);

/* Forgets every edit and starts a new state (used when a file is opened). */
void tw_undo_clear(tw_undo *undo);

/*
 * Replaces `remove` lines of `doc` at `at` with copies of `texts` and records
 * the change. Edits that could be redone are dropped. On failure neither the
 * document nor the history changes.
 */
tw_status tw_undo_apply(tw_undo *undo, tw_document *doc, size_t at, size_t remove,
                        const char *const *texts, size_t count);

bool tw_undo_can_undo(const tw_undo *undo);
bool tw_undo_can_redo(const tw_undo *undo);

/* Reverses the newest applied edit; `*edit` (may be NULL) receives it.
 * Returns TW_ERR_INVALID when there is nothing to undo. */
tw_status tw_undo_undo(tw_undo *undo, tw_document *doc, const tw_edit **edit);

/* Applies the next undone edit again. Returns TW_ERR_INVALID when there is
 * nothing to redo. */
tw_status tw_undo_redo(tw_undo *undo, tw_document *doc, const tw_edit **edit);

/* Id of the current state; equal ids mean equal documents. */
unsigned long long tw_undo_state(const tw_undo *undo);

/* "insert of 2 lines at line 4", "delete of line 7", "replace of lines 3-5" */
void tw_edit_describe(const tw_edit *edit, char *buf, size_t size);

#endif /* TWINLINE_UNDO_H */
