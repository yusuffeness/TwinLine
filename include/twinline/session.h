/*
 * Editing session: the shared document and the state that goes with it.
 *
 * Every field below `lock` must only be accessed while holding `lock`.
 * Background workers never touch the session directly; they receive a
 * snapshot taken with tw_session_snapshot().
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_SESSION_H
#define TWINLINE_SESSION_H

#include <pthread.h>
#include <stdbool.h>

#include "twinline/document.h"
#include "twinline/status.h"
#include "twinline/undo.h"

/* Everything that belongs to one open document. */
typedef struct tw_buffer {
    tw_document doc;
    char *path; /* file the document was loaded from or last saved to; may be NULL */
    bool dirty; /* true when there are unsaved changes */
    tw_undo undo;
    unsigned long long saved_state; /* undo state when last opened or saved */
    size_t page;                    /* first line of the next "show" page, 0-based */
} tw_buffer;

void tw_buffer_init(tw_buffer *buffer);
void tw_buffer_free(tw_buffer *buffer);

typedef struct tw_session {
    pthread_mutex_t lock;
    /* The active buffer. Its fields are spelled out (rather than nesting a
     * tw_buffer) so that the rest of the code can keep writing session.doc. */
    tw_document doc;
    char *path;
    bool dirty;
    tw_undo undo;
    unsigned long long saved_state;
    size_t page;
} tw_session;

tw_status tw_session_init(tw_session *session);
void tw_session_destroy(tw_session *session);

/* Copies the current document into `out` (which must not own memory). */
tw_status tw_session_snapshot(tw_session *session, tw_document *out);

/* Replaces the stored path with a copy of `path`. Caller holds the lock. */
tw_status tw_session_set_path_locked(tw_session *session, const char *path);

/*
 * Edits the document through the undo history and updates `dirty`.
 * Replaces `remove` lines at index `at` with copies of `texts`.
 * Caller holds the lock.
 */
tw_status tw_session_edit_locked(tw_session *session, size_t at, size_t remove,
                                 const char *const *texts, size_t count);

/* Undo / redo; `*edit` (may be NULL) receives what was reversed or replayed.
 * Caller holds the lock. */
tw_status tw_session_undo_locked(tw_session *session, const tw_edit **edit);
tw_status tw_session_redo_locked(tw_session *session, const tw_edit **edit);

/* Installs `doc` (taken over) as a freshly opened document named `path`
 * (may be NULL); the previous document goes into `doc`. Caller holds the lock. */
tw_status tw_session_replace_locked(tw_session *session, tw_document *doc, const char *path);

/* Records that the document was just saved. Caller holds the lock. */
void tw_session_mark_saved_locked(tw_session *session);

/* Exchanges the active buffer with `buffer`. Caller holds the lock. */
void tw_session_exchange_locked(tw_session *session, tw_buffer *buffer);

#endif /* TWINLINE_SESSION_H */
