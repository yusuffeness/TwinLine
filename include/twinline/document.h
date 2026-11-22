/*
 * In-memory document: an ordered list of lines.
 *
 * A tw_document is a plain value type and is NOT thread-safe on its own.
 * Shared documents are protected by the lock in tw_session.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_DOCUMENT_H
#define TWINLINE_DOCUMENT_H

#include <stddef.h>

#include "twinline/status.h"

typedef struct tw_document {
    char **lines; /* each line is NUL-terminated and contains no '\n' */
    size_t count; /* number of lines */
    size_t cap;   /* allocated slots in `lines` */

    /* Large-file mode: a private memory mapping of the file the document was
     * loaded from. Lines inside it are not separately allocated; every other
     * line is. NULL when the document has no mapping. */
    char *map;
    size_t map_len;
} tw_document;

/* Initialises an empty document. */
void tw_document_init(tw_document *doc);

/* Releases every line and resets the document to empty. */
void tw_document_free(tw_document *doc);

/* Appends `line`. On success the document takes ownership of `line`;
 * on failure the caller still owns it. */
tw_status tw_document_push(tw_document *doc, char *line);

/* Appends a copy of `text`. */
tw_status tw_document_append(tw_document *doc, const char *text);

/*
 * Replaces `remove` lines starting at index `at` with the `count` lines in
 * `insert`, which the document takes over on success (on failure the caller
 * still owns them). The removed lines are released. Requires
 * `at <= doc->count` and `remove <= doc->count - at`; returns TW_ERR_INVALID
 * otherwise.
 */
tw_status tw_document_splice(tw_document *doc, size_t at, size_t remove, char **insert,
                             size_t count);

/* Same as tw_document_splice(), but inserts copies of `texts`. */
tw_status tw_document_splice_copy(tw_document *doc, size_t at, size_t remove,
                                  const char *const *texts, size_t count);

/* Makes `dst` a deep copy of `src`. `dst` must not own any memory. */
tw_status tw_document_clone(const tw_document *src, tw_document *dst);

/* Exchanges the contents of two documents in O(1). */
void tw_document_swap(tw_document *a, tw_document *b);

#endif /* TWINLINE_DOCUMENT_H */
