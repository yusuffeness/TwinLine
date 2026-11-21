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

typedef struct tw_session {
    pthread_mutex_t lock;
    tw_document doc;
    char *path; /* file the document was loaded from or last saved to; may be NULL */
    bool dirty; /* true when there are unsaved changes */
} tw_session;

tw_status tw_session_init(tw_session *session);
void tw_session_destroy(tw_session *session);

/* Copies the current document into `out` (which must not own memory). */
tw_status tw_session_snapshot(tw_session *session, tw_document *out);

/* Replaces the stored path with a copy of `path`. Caller holds the lock. */
tw_status tw_session_set_path_locked(tw_session *session, const char *path);

#endif /* TWINLINE_SESSION_H */
